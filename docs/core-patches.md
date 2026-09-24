# Core patches & where everything goes

This module needs two small, deliberately minimal patches to AzerothCore's
core (not to any module) to exist at all, plus a few files that live in
specific places outside this repo. This doc is the map: what's patched, why,
and what needs to go where when setting this up from scratch or updating an
existing checkout.

## 1. Core engine patches (in `azerothcore-wotlk-coa`, not this repo)

A bot is a real `Player` behind a real `WorldSession` whose socket is null
instead of a live client connection — no custom "fake player" class, no
alternate code path through combat/movement/etc. That needs exactly two
small enabling changes to core files that a normal module cannot make on its
own (private members, module-local statics). Nothing else in the core is
touched — every actual behavior (AI, roles, talents, chat) lives entirely in
this module.

### Patch 1 — the bot login chassis

Commit: `227a371b2` ("Add minimal fake-session bot chassis support").

| File | Change |
| :--- | :--- |
| `src/server/game/Handlers/CharacterHandler.cpp` | `LoginQueryHolder` used to be a private class defined inline in this file. Split into a declaration (moved out, see next row) and its `Initialize()` body left here. |
| `src/server/game/Server/WorldSession.h` | `LoginQueryHolder` (declaration) moved here as a public class, so an external module can subclass/construct it. Mirrors the same patch `mod-playerbots` carries for the same reason. |
| `src/server/game/World/IWorld.h` | Added a pure-virtual `AddQueryHolderCallback(SQLQueryHolderCallback&&)` to the `IWorld` interface. |
| `src/server/game/World/World.cpp` | Implemented `World::AddQueryHolderCallback`, and made `World::ProcessQueryCallbacks()` (already called unconditionally every tick) also drain the new `_queryHolderProcessor`. |
| `src/server/game/World/World.h` | Declared the override and the new `AsyncCallbackProcessor<SQLQueryHolderCallback> _queryHolderProcessor` member. |

**Why the `World`-level plumbing was needed, specifically:** `WorldSession`
already has its own per-session `AddQueryHolderCallback`/`_queryHolderProcessor`
pair, and a normal client session's async login-query callback drains through
that. But `WorldSession::ProcessQueryCallbacks()` is `private` and
`friend`-only to `World` — it only ever runs for sessions that
`WorldSessionMgr` calls it on, i.e. sessions that went through the normal
socket-accept path. A bot's session is never registered with
`WorldSessionMgr` that way, so it has **no path at all** to get its login
query callback processed at the session level. Routing it through
`World::Update()`'s own unconditional per-tick `ProcessQueryCallbacks()` call
instead (which every session already relies on for the *other* query
processor) was the fix — found empirically, not planned up front: an earlier
version of the pilot used the session-level `AddQueryHolderCallback` and the
bot's login silently never completed.

### Patch 2 — read-only access to pending loot rolls

Commit: `26aec4cdb` ("Expose Group::RollId via a public GetRolls() accessor").

| File | Change |
| :--- | :--- |
| `src/server/game/Groups/Group.h` | Added `Rolls GetRolls() const { return RollId; }`. `RollId` itself stays `protected` — this is a read-only accessor, not a visibility change to the underlying data. |

Needed so a bot can see the group's pending loot rolls at all (to auto-Greed
them in `BotMgr::DoRollGreed`) — `Group` had no existing way to read `RollId`
from outside the class.

### Patch 3 — spending Ascension talent points through the real budget-checked path

Not committed yet (2026-09-24) — apply against `src/server/coa/` in
`azerothcore-wotlk-coa`, build/test locally, then commit following the same
pattern as Patch 1/2.

**The bug this fixes**: `BotMgr::LearnSpecialization` and
`BotTalentBuilds::ApplyBuildForLevel` (this repo, `module/src/BotMgr.cpp` and
`module/src/BotTalentBuilds.cpp`) grant paid talent spells to bots via a plain
`bot->learnSpell(spellId, false)`, and set the active spec via a raw
`bot->UpdatePlayerSetting("core.ascension_active_spec", 0, specId)`. Neither
ever calls the real `AscensionClassService::SetTalentRank()` /
`SwitchSpecialization()` — because that class is defined entirely inside a
file-local anonymous namespace in `AscensionCompat.cpp` (lines ~590–2145 as of
this fork's current `main`), with no header and no external linkage, so no
other module can reach it at all. This was a known, deliberate tradeoff (see
AGENTS.md's 2026-09-13 "Healer/Tank casting live-validated" entry), but it has
two real consequences, not just one:

1. **No talent-point budget enforcement.** `SetTalentRank` checks a real
   per-level class/spec `AE`/`TE` point budget
   (`AscensionCompatData::GetCoATalentBudget`) computed live from the
   spellbook before granting a rank. The bot module's direct `learnSpell`
   path enforces none of this, so a bot can end up over- or
   under-invested relative to what a real player at the same level could
   actually afford.
2. **`GetActiveSpecialization()` never sees a bot's spec at all.**
   `AscensionClassService::GetActiveSpecialization()` reads an **in-memory**
   `_activeSpecializations` map keyed by player guid, populated only by the
   real `SwitchSpecialization()` (and by login, from the same PlayerSetting
   this module also writes). A raw `UpdatePlayerSetting` write never touches
   that map, so from the core's own point of view every bot's active spec is
   stuck at `0` — which silently blocks every spec-gated *automatic* talent
   grant (`SynchronizeAutomaticTalents`) and any tuning that keys off active
   spec (`AscensionClassTuning::Synchronize`), independent of the paid-talent
   problem above.

**The fix**: expose `SwitchSpecialization()` and `SetTalentRank()` as two
thin free-function forwarders with real external linkage, so
`mod-coa-playerbots` can call the actual gated logic instead of reimplementing
a subset of it.

New file: `src/server/coa/AscensionClassServiceBridge.h`

```cpp
#pragma once

#include "AscensionCoATalentData.h"
#include <string>

class Player;

// Minimal, deliberately narrow bridge into AscensionClassService, which is entirely
// private to AscensionCompat.cpp (defined inside a file-local anonymous namespace, no
// other public header). mod-coa-playerbots needs to actually switch spec and spend a
// bot's paid (AECost/TECost > 0) Ascension talent points through the same
// budget-checked, mutually-exclusive-group-aware path a real player's
// `.localspec`/`.localtalent` commands use, instead of writing PlayerSettings and
// learnSpell()-ing spells directly and silently bypassing the budget check and the
// in-memory active-spec bookkeeping GetActiveSpecialization() depends on.
namespace AscensionClassServiceBridge
{
    // Mirrors AscensionClassService::SwitchSpecialization(player, specializationId)
    // exactly. Returns false if specializationId isn't valid for player's class.
    bool SwitchSpecialization(Player* player, uint32 specializationId);

    // Mirrors AscensionClassService::SetTalentRank(player, entry, rank, error) exactly --
    // same validation order, same budget accounting, same free-choice-group mutual
    // exclusion. Returns false and fills `error` with a human-readable reason on failure
    // (wrong class, spec mismatch, level too low, over budget, etc.); on success the
    // player's spellbook and specialization bookkeeping are already updated, no further
    // action needed by the caller.
    bool SetTalentRank(Player* player, AscensionCompatData::CoATalentEntry const& entry, uint32 rank, std::string& error);
}
```

In `src/server/coa/AscensionCompat.cpp`:

- Add `#include "AscensionClassServiceBridge.h"` to the top of the file's
  include block.
- Append these two forwarders just before `void AddAscensionCompatScripts()`
  (they must be textually *after* the anonymous namespace containing
  `AscensionClassService`, but do not need to be inside it — an anonymous
  namespace's names stay visible via unqualified lookup for the rest of the
  translation unit):

```cpp
bool AscensionClassServiceBridge::SwitchSpecialization(Player* player, uint32 specializationId)
{
    return AscensionClassService::Instance().SwitchSpecialization(player, specializationId);
}

bool AscensionClassServiceBridge::SetTalentRank(Player* player, AscensionCompatData::CoATalentEntry const& entry, uint32 rank, std::string& error)
{
    return AscensionClassService::Instance().SetTalentRank(player, entry, rank, error);
}
```

Both `SwitchSpecialization` and `SetTalentRank` are `public` members of
`AscensionClassService` already (confirmed by reading the class body directly
— its one `private:` label comes much later, at the data-member block), so
no visibility change inside that class is needed at all — this patch only
adds two new file-scope functions with external linkage next to it.

**Module-side changes this enables** (already committed in this repo):
`BotMgr::LearnSpecialization` now calls `AscensionClassServiceBridge::
SwitchSpecialization()` once per spec change and `AscensionClassServiceBridge::
SetTalentRank()` per paid entry instead of `learnSpell`/`removeSpell`/
`UpdatePlayerSetting` directly; `BotTalentBuilds::ApplyBuildForLevel()` does
the same per curated pick. A `SetTalentRank` failure (most commonly: the
class/spec point budget is already spent at the bot's level) is logged and
skipped rather than forced through.

**Not yet built or tested** — this cloud session has no access to the
Windows dev box or a live CoA-Repack instance. Apply this patch to the real
`azerothcore-wotlk-coa` checkout, mirror the already-committed module changes
in this repo per the "Building after a change" section below, rebuild, and
verify live (a `.botcmd learnspec`/`.botcmd checkrole` round-trip, checking
that a bot at a low level now correctly gets *fewer* paid talents than the
full curated build lists, and that `SynchronizeAutomaticTalents`-granted
spec-specific automatic entries actually appear post-fix where they didn't
before) before trusting it the way Patch 1/2 are trusted.

### Applying these to a fresh core checkout

These are ordinary commits against `azerothcore-wotlk-coa` — `git log --oneline -- src/server/game/Handlers/CharacterHandler.cpp src/server/game/Server/WorldSession.h` (etc.) will find them by hash if you need to `git cherry-pick` them onto a different branch, or diff them out as a standalone patch file. They're small and mechanical enough (29 + 4 lines) to reapply by hand if a merge conflict ever makes cherry-picking painful — see the "surviving an upstream merge" note below.

**Surviving an upstream merge:** these files are also touched by unrelated
upstream `azerothcore-wotlk-coa` changes from time to time. When merging
upstream, `git merge` handles this fine as long as upstream's own edits to
the same file don't overlap the exact lines above — confirmed in practice on
a 24-commit upstream merge (2026-09-14) where `CharacterHandler.cpp` had
also been touched upstream and still auto-merged cleanly, leaving both sides'
changes intact. If a real conflict ever does land on one of these five
files, re-diff against the table above to re-apply just this module's half
by hand rather than guessing.

## 2. Repository layout — what lives where

This repo (`mod-coa-playerbots`) is the **source of truth**. None of it is
inside the `azerothcore-wotlk-coa` checkout's own git history — the build
tree's copy is deliberately git-ignored there (`modules/mod-coa-playerbots/`
has no whitelist entry in `azerothcore-wotlk-coa/.gitignore`, unlike
`mod-ascension-compat` which does).

| This repo | Goes to (manually copied, not a symlink) | Purpose |
| :--- | :--- | :--- |
| `module/src/*.cpp`, `module/src/*.h` | `<azerothcore-wotlk-coa checkout>/modules/mod-coa-playerbots/src/` | The actual module AzerothCore's build system compiles. No `CMakeLists.txt` needed in either location — AzerothCore auto-discovers any `modules/<name>/` directory containing a `src/` folder. |
| `addon/CoABotUI/` | The WoW 3.3.5a client's `Interface/AddOns/CoABotUI/` | Client-side addon (`CoABotUI.lua` + `CoABotUI.toc`) — the in-game panel a real player uses to control their bots. Copy the whole `CoABotUI` folder as-is; the `.toc`'s `## Interface: 30300` and `SavedVariables: CoABotUIDB` lines don't need editing. |
| `tools/ra_client.py` | Wherever you run it from — talks to the running server over the network (RA console, port 3443 by default) | Dev/debug helper, not deployed anywhere; no core or client-side placement needed. |
| `docs/`, `AGENTS.md`, `README.md` | Nowhere — reference only | Not deployed; these are for people (or Claude/Gemini) working on the module itself. |

**There is no symlink between this repo and the build tree, intentionally**
— every edit to a file in `module/src/` must be manually re-copied to
`azerothcore-wotlk-coa/modules/mod-coa-playerbots/src/` before rebuilding.
Forgetting this step is the single most common way to "fix a bug" that then
doesn't show up live — always mirror before `ninja worldserver`.

## 3. Building after a change

1. Mirror any edited/new `module/src/*` file(s) into
   `azerothcore-wotlk-coa/modules/mod-coa-playerbots/src/`.
2. **If any file is brand-new** (not just edited) or the module gained/lost a
   source file for any other reason: re-run CMake's configure step first —
   `cmake .` from the `azerothcore-wotlk-coa/build` directory. AzerothCore's
   module discovery globs each module's `src/` directory, but that glob is
   only re-evaluated on a reconfigure; a plain `ninja worldserver` after only
   adding a file will silently keep building the old file list and the new
   file's symbols simply won't exist in the binary. Confirmed twice
   independently (once mid-module-development, once after a 24-commit
   upstream merge added several new `mod-ascension-compat` source files) —
   both times the symptom was `LNK2019: unresolved external symbol` at the
   final link step, not a compile error, which makes it easy to misdiagnose
   as a code problem instead of a stale build-file problem.
3. Build: from a Visual Studio dev environment (`vcvarsall.bat x64`), run
   `ninja worldserver` from `azerothcore-wotlk-coa/build`.
4. Deploy: **check player count first** (`.server info` over RA, or ask
   whoever's online) — restarting kicks everyone. Then, from `CoA-Repack`:
   `Runtime/python/python.exe -B Scripts/manage.py stop-all`, copy the new
   `build/bin/worldserver.exe` over `CoA-Repack/Core/worldserver.exe`, then
   `start-auth` and `start-world`.
5. Verify: `.server info` over RA should report the new build's revision
   hash and boot with no crash; a quick `.botcmd spawnbot <guid>` /
   `.botcmd despawn <guid>` cycle on a non-protected test character
   confirms the binary is actually live and the bot chassis still works.

## 3.5 Uncommitted working-tree patches (as of 2026-09-17) — needs attention

Beyond the two committed patches above, `git status` against the current
`azerothcore-wotlk-coa` checkout shows **10 modified core files with no
commit at all** — they exist only in this checkout's working tree, on top
of `6b7ccb5` (`Merge remote-tracking branch 'origin/main'`, 2026-09-17
06:48). Discovered while answering "did we patch the core for bots?" — the
answer was yes, more than this doc said, and none of it is safe from being
silently lost by a `git reset --hard`, `git stash` left unapplied, or a bad
merge conflict resolution. **These should be committed** (as their own
commits, following the pattern of the two above) before anyone relies on
this checkout surviving a routine git operation.

| File | What changed | Bot-related? |
| :--- | :--- | :--- |
| `src/server/game/Guilds/Guild.h` | Added `friend class BotMgr;` | Yes — `BotMgr`'s guild deposit/withdraw/gather commands need private `Guild` access. |
| `src/server/game/DungeonFinding/LFGMgr.h` + `.cpp` | Added `LFGMgr::GetProposalIdForPlayer(ObjectGuid)` | Yes — comment explicitly says "needed by anything that has to call `UpdateProposal()` without already knowing the id a real client would have learned from its own `SMSG_LFG_PROPOSAL_UPDATE` packet (mod-coa-playerbots)". Used by the LFG dungeon-fill feature. |
| `src/server/game/Scripting/ScriptDefines/PlayerScript.h` + `.cpp`, `src/server/game/Scripting/ScriptMgr.h` | Added a new `PLAYERHOOK_ON_PETITION_OFFERED` hook (`ScriptMgr::OnPetitionOffered`) | Yes — lets a module react when a specific player (bot) is offered a guild/arena petition to sign, needed for bot guild-invite auto-accept. |
| `src/server/game/Handlers/PetitionsHandler.cpp` | Wires the new hook into `HandleOfferPetitionOpcode`, **plus a large number of `LOG_ERROR` debug lines added throughout `HandlePetitionSignOpcode`** | The hook wiring is bot-related; the extra `LOG_ERROR` instrumentation looks like active debugging of a guild-petition-signing issue that was never cleaned up — worth reviewing and either removing or downgrading to `LOG_DEBUG` before committing. |
| `src/server/game/Combat/CombatManager.cpp` | `CombatManager::PutReference` no longer `ASSERT`-crashes the whole process on a duplicate combat-reference slot; force-ends the stale reference and logs an error instead. | Indirectly — the comment says this was "observed under heavy concurrent-bot load," so it's a robustness fix the bot population's scale surfaced, not bot-specific logic. |
| `src/server/game/Handlers/QueryHandler.cpp` | Added a diagnostic `LOG_ERROR` when `SendNameQueryOpcode` finds no `CharacterCache` entry for a guid | Likely bot-related (debugging a name-lookup issue), but generic/diagnostic — safe either way. |
| `src/server/game/Entities/Player/Player.cpp` | `Player::ApplySpellMod`: a `SPELLMOD_COST`/`SPELLMOD_CASTING_TIME` early-return now also checks `mod->value <= 0`, so a modifier that *increases* cost/casting time from a free/instant baseline is no longer skipped. | Unclear — no bot-specific comment; may be an unrelated gameplay fix found via bot-scale testing rather than something bots specifically need. Verify before assuming it's safe to drop. |

**Action needed**: review the `PetitionsHandler.cpp` debug logging (looks
unfinished), then commit each logical group separately with a message
matching the style of the two committed patches above, so this table can be
updated with real commit hashes and this checkout stops depending on
uncommitted working-tree state for core bot functionality.

## 4. What this module depends on but does not patch

`mod-ascension-compat` (also in `azerothcore-wotlk-coa/modules/`, but *that*
one's build-tree copy **is** tracked directly in that repo's own git history
— it's not mirrored from anywhere external) owns all of the actual Ascension
class/talent/spec data and logic this module reads: `AscensionCoATalentData.h`
(the talent catalog bots use to pick abilities and detect roles) and
`AscensionClassService` (the real player-facing spec-switch logic bots try to
mirror). This module never edits `mod-ascension-compat`'s files — any bug
found there gets reported upstream instead (see the CoA bug-report workflow
in the main server checkout), not patched locally.
