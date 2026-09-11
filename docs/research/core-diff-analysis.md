# Core diff analysis: mod-playerbots fork vs vanilla AzerothCore vs CoA fork

Recorded 2026-09-11. This is the primary evidence behind the project's whole
approach — re-derive it (see "Reproducing this analysis" below) rather than
trusting stale numbers if either upstream moves significantly.

## Method

Three shallow clones, compared with `diff -rq` (file-level) then `diff3 -m`
(real three-way merge, not just "the file differs") for every file where CoA's
copy differs from vanilla:

- `vanilla-core` = `azerothcore/azerothcore-wotlk` @ `master` (shallow, current HEAD at time of analysis)
- `playerbots-fork` = `mod-playerbots/azerothcore-wotlk` @ `Playerbot` branch (shallow, current HEAD)
- CoA = `azerothcore-wotlk-coa` (jealous-sound's fork, the actual core this project targets — full clone, already present in this workspace)

Scope: `src/server/game/` only (that's where every file mod-playerbots'
core fork touches actually lives — confirmed by a full-repo `diff -rq` finding
zero new top-level directories in the fork; the *bot AI itself* lives entirely
in the separate `mod-playerbots/mod-playerbots` module, not in the core fork).

## Headline numbers

- mod-playerbots' core fork touches **77 files**, **~1491 lines added / ~1223
  removed** (~2700 changed lines total) in `src/server/game/`. Not a rewrite —
  a moderate, bounded patch.
- Of those 77, **36 are byte-identical between CoA and vanilla** — the
  playerbots patch applies to them with zero conflict, automatically.
- Of the remaining 41 (where CoA's copy differs from vanilla for its own
  reasons), a real `diff3` three-way merge shows **7 auto-merge cleanly**
  (CoA's and playerbots' edits sit in different parts of the same file) and
  **34 have at least one genuine conflict marker** — **104 conflict markers
  total**, heavily concentrated: `Unit.cpp` (14) and `Creature.cpp` (10) alone
  account for 24 of the 104; every other file has 1–5.
- **Net: 43 of 77 files (56%) need zero manual work.** The other 34 need a
  merge decision, but after actually reading every hunk (categorization
  below), only **3 files** carry logic that must be preserved verbatim
  because it's bot-specific — everything else is a mechanical take-CoA's-side
  or keep-both-additions resolution.

## What the conflicts actually are, categorized

Read every one of the 104 conflict blocks by hand (not just counted them).
They fall into four real categories, not one undifferentiated pile:

### A. Pure version drift — CoA and playerbots-fork just diverged from upstream
at different points; the "conflict" is two versions of unrelated code, not a
real clash. **Resolution: take CoA's side, verified safe.**

The single biggest pattern: CoA carries a combat-math fix (resist chance /
absorb calc now accounts for the *caster's* level via a `casterLevel`
parameter added to `Unit::GetEffectiveResistChance` and
`Unit::CalcAbsorbResist`; `GetRageWeaponSpeedHitFactor` replaces a cruder
inline calc) that playerbots-fork's older baseline never received. This one
fix is why `Unit.cpp` (14), `Unit.h` (3), and `SpellAuraEffects.cpp` (2 of 4)
all show conflicts — it's the *same* fix rippling through every caller.
Similarly `SpellInfo.cpp/h` (`CanBeRedirectedBySpellMagnet()`) and one
`SpellAuraEffects.cpp` block are a spell-reflection/immunity fix CoA has that
playerbots-fork's baseline predates.

Files: `Unit.cpp`, `Unit.h`, `SpellAuraEffects.cpp` (3 of 4 blocks),
`SpellInfo.cpp`, `SpellInfo.h`, `Spell.cpp` (pathfinding refactor, same
pattern), `Vehicle.cpp` (CoA-only Pyrite power-display constant, unrelated to
bots), `Mail.cpp` (CoA-only online-receiver delivery fix), `Trainer.cpp/h`
(CoA-only new hooks), `ArenaTeamMgr.cpp`, `UpdateTime.h`.

One genuine amusing case in `Unit.cpp`: the last conflict is CoA and
playerbots-fork independently writing **the identical fix** for the same
crash (`m_Events.KillAllEvents(false)` ordering vs.
`Player::RestoreSpellMods`) — same code, different comment. Not a conflict at
all in substance.

### B. Shared hook-interface files — both projects added *different* new
`ScriptMgr` hooks near the same insertion point. **Resolution: keep both
additions, this is textbook diamond-merge, not a real clash.**

`ScriptMgr.h`, `PlayerScript.h/.cpp`, `ArenaScript.h/.cpp`, `GlobalScript.h/.cpp`.
CoA added hooks like `OnPlayerAfterMoveItemToInventory`,
`OnPlayerGetAmmoDisplay`, `OnPlayerBeforeReceiveSpellListFromTrainer`,
`OnArenaWeekReset`, `OnAddMember(ArenaTeam*, ArenaTeamMember&)`.
playerbots-fork added its own, e.g. `OnGetStartPersonalRating`. `diff3` flags
these because both insertions land at the same line in an enum/method list,
not because the *hooks* conflict semantically. Also
`SpellInfoCorrections.cpp`: CoA and playerbots-fork each added their own,
unrelated spell-data fix (CoA: Ulduar Vezax's Shadow Crash; playerbots-fork:
"Hurl Pyrite") — trivially both-keep.

### C. Genuine playerbots-specific compatibility fixes — code that exists
*because* the bot is a Player with a dead session, and must be preserved
verbatim, not discarded as "just their older version."

- **`Item.cpp`** — playerbots-fork's comment says it outright: *"we have to
  check the owner for mod_playerbots since bots programically call methods
  like DestroyItem, MoveItemToMail, DestroyItemCount which do not handle
  soulboundTradeable clearing."* A bot calling these methods synchronously
  (no real client round-trip) can hit an owner-null path a real client
  interaction never would.
- **`Group.cpp`** — playerbots-fork's comment: *"double invite hack
  workaround."* A concrete bug playerbots hit with group invites and a
  bot-driven accept flow.
- **`PointMovementGenerator.h`** — playerbots-fork *adds* two new parameters
  (`reverseOrientation`, `facingTargetGuid`) that CoA doesn't have at all.
  This is the one clear case of playerbots-fork having a capability CoA's
  baseline lacks outright (a bot needing to face a target while moving into
  position) — not a version-drift artifact, an actual addition to bring
  forward.

### D. Real overlapping changes to the same feature — RESOLVED 2026-09-11

Read every one of these 7 files' actual conflict hunks against CoA's *live*
checkout (not just the scratch `merge_test/*.merged` output) to settle each
one. **Verdict: none of the 7 contain bot-specific logic that needs
preserving.** Every hunk is CoA making an unrelated improvement that
playerbots-fork's older baseline predates — mechanically the same as
category A, just missed by the file-level `diff3` conflict count because the
changes happen to land on the same lines. One file (`Creature.cpp`) has a
single genuinely-missing line worth carrying over from playerbots-fork, for
an unrelated reason (see below). **Decision for all 7: take CoA's side**,
plus the one small addition noted under `Creature.cpp`.

- **`Creature.cpp`** (10 conflict markers, the file's actual conflict count
  is now effectively 1) — **important methodology finding**: 9 of the 10
  markers, all in `Creature::AddSpellCooldown` (the charmed-creature
  cooldown-notification path), turned out to be a **false conflict**. Direct
  diff of CoA's live `src/server/game/Entities/Creature/Creature.cpp` against
  `playerbots-fork`'s version of the same function shows **byte-identical
  code** — CoA simply hasn't pulled the newer upstream (vanilla AzerothCore)
  batching refactor that the `vanilla-core` scratch clone already had at
  research time. The `merge_test/*.merged` diff3 output is correct as far as
  it goes, but its "CoA" side there is actually reproducing `vanilla-core`'s
  content for this hunk, not CoA's real content — a reminder that
  `vanilla-core` in this research is a snapshot, not a live source, and can
  drift further from CoA than CoA was from it at the moment of cloning. No
  merge work needed here at all.
  The 10th marker is real and tiny: CoA's `Creature::SaveToDB(uint32, uint8,
  uint32)` never sets `data.spawnId = m_spawnId;` after
  `sObjectMgr->NewOrExistCreatureData(m_spawnId)` — confirmed by reading
  `ObjectMgr::NewOrExistCreatureData` (`ObjectMgr.h:1244`, a bare
  `_creatureDataStore[spawnId]`) and `SpawnData`'s default member
  (`SpawnData.h:68`, `spawnId{0}`): a brand-new creature's first `SaveToDB()`
  call leaves the cached `CreatureData::spawnId` at `0` instead of the real
  value. This is a **pre-existing CoA gap, unrelated to bots** — playerbots-
  fork's `// mod_playerbots`-commented line just happens to fix it as a side
  effect. **Take the fix**: add `data.spawnId = m_spawnId;` back in.
- **`Pet.cpp`/`Pet.h`** — CoA extracted the inline "cast a pending spell
  once in range/off cooldown" logic out of `Pet::Update()` into its own
  `Pet::CastPendingSpell()` method, and along the way fixed a real
  interrupt-order bug (calling `AttackStop()` instead of `PetStopAttack()`
  so the spell just cast isn't immediately interrupted; see the method's
  in-code comment) and added a `REACT_PASSIVE` + `COMMAND_FOLLOW` branch
  playerbots-fork's inline version lacks. Playerbots-fork's shape is simply
  the pre-refactor version — same triggers (`m_tempspell` set/cleared the
  same way), no bot-specific behavior in either. **Take CoA's side**
  (keep `CastPendingSpell()` as its own method, called from `Update()`).
- **`PlayerUpdates.cpp`** (5 conflicts, all resolved) —
  1. **Unread-mail bookkeeping**: CoA's `UpdateNextMailTimeAndUnreads()`
     (`PlayerUpdates.cpp:441`) fully recomputes both `unReadMails` and
     `m_nextMailDelivereTime` by re-scanning `GetMails()`. Playerbots-fork's
     inline `++unReadMails; m_nextMailDelivereTime = time_t(0);` only
     increments the counter and **unconditionally zeroes the next-delivery
     timer even if other mail is still pending delivery** — a real
     correctness gap in playerbots-fork's simpler version, not a feature to
     preserve. Take CoA's side.
  2. **`CraftSkillGainChance` vs `SkillGainChance`** (the formula flagged as
     needing verification) — confirmed by reading both: this **is** a real,
     deliberate formula difference. CoA's `CraftSkillGainChance`
     (`PlayerUpdates.cpp:813`) linearly interpolates skill-up chance between
     the yellow and gray skill thresholds; playerbots-fork reuses the
     coarser 4-tier `SkillGainChance` (the same function used for gathering
     skills) with a computed midpoint. This is CoA's own shipped crafting
     tuning, unrelated to bots — playerbots-fork's baseline simply predates
     it. **Take CoA's side** (`CraftSkillGainChance`); the "verify these
     produce the same numbers" question from the original write-up is moot,
     they're intentionally different by design and there's no bot-specific
     reason to keep the older one.
  3. **Weather on zone change**: CoA additionally sends
     `Weather::SendFineWeatherUpdateToPlayer(this)` when
     `GetOrGenerateZoneDefaultWeather()` returns false; playerbots-fork
     doesn't check the return value at all. CoA's is a real fix, unrelated
     to bots. Take CoA's side.
- **`Player.h`/`PlayerStorage.cpp`** — confirmed: `MoveItemToInventory`'s
  `void`→`Item*` return-type change exists solely to support CoA's own
  `OnPlayerAfterMoveItemToInventory` hook (category B). A wider return type
  is source-compatible with any caller that discards it, including
  playerbots-fork's own call sites. **Take CoA's side** — not even really a
  judgment call, just a compatible signature widening.
- **`MailHandler.cpp`** (4 conflicts, all resolved) — the `stored`/hook pair
  (mail-item-take) is the same `MoveItemToInventory`-return-type decision
  applied at a call site: take CoA's side, capture `stored`, fire
  `OnPlayerAfterTakeItemFromMail`. The mail-list packet-size precalculation
  turned out to have a clean explanation once read end-to-end: CoA sanitizes
  `subject`/`body` (the "prevent client crash" `\| \|`-replacement) **before**
  computing `next_mail_size`, so the declared packet size and the bytes
  actually written are guaranteed to match exactly. Playerbots-fork
  estimates `next_mail_size` from the **raw, unsanitized** strings and only
  sanitizes afterward in a second, separately-placed block — safe (sanitizing
  only ever shortens the string, so its size estimate is a conservative
  overestimate) but redundant and less precise than CoA's single-pass
  version. **Take CoA's side entirely**; playerbots-fork's second
  sanitization block is now fully subsumed and can be dropped, no bot-
  specific need lost.
- **`PetHandler.cpp`** (5 conflicts, all resolved) — all five are CoA adding
  `charmInfo->HasCommandState(COMMAND_STAY)` checks around channeled-spell
  casts/failures (don't let a stay-commanded pet's channel get interrupted by
  the default movement/AI-reset path; explicit forced-spell state clearing
  when stay is active) plus one motion fix (stop the current spline before a
  movement update can interrupt a just-started stationary channel). None of
  it touches session/socket state — it's pure pet-command-state logic, same
  for a bot's pet as a real player's. **Take CoA's side for all 5.** Still
  worth a live in-game check once bots exist (does a bot correctly issue
  "stay" to its pet through this path) — a testing note, not a reason to
  withhold the code.
- **`SmartScript.cpp`** (2 conflicts, coupled — must be resolved together) —
  CoA generalized `SMART_ACTION_MOVE_FORWARD` (`SmartScript.cpp:1516`) to
  move any `WorldObject` in the event's `targets` list instead of hardcoding
  `me`. The second conflict, ~2000 lines away, is `InstallTemplate`'s
  `SMARTAI_TEMPLATE_CAGED_NPC_PART` case (`SmartScript.cpp:3507`), which
  constructs the `SMART_ACTION_MOVE_FORWARD` event and must pass a target
  type consistent with whichever handler shape is in effect: CoA passes
  `SMART_TARGET_SELF` (correct for the generalized, targets-list handler);
  playerbots-fork passes `SMART_TARGET_NONE` (correct only for the old,
  hardcoded-`me` handler). **These two hunks are a matched pair — taking
  CoA's generalized handler but playerbots-fork's `SMART_TARGET_NONE` call
  site would silently break the caged-NPC SmartAI template** (empty targets
  list → the move-forward action does nothing). **Take CoA's side on both,
  never just one.** Purely a generic SmartAI engine change, no bot-specific
  logic anywhere in it.

## What this means for the patch plan

Category A + B (roughly 20 of the 34 conflicted files) are mechanical: take
CoA's side for A, keep both additions for B. **Category D turned out to
collapse into the same "take CoA's side" bucket once actually read** — the
only line of playerbots-fork code worth carrying over from the whole
7-file/21-conflict set is the one-line `Creature.cpp` `spawnId` fix (and
that's for an unrelated pre-existing CoA gap, not a bot need). That leaves
**category C (3 files: `Item.cpp`, `Group.cpp`, `PointMovementGenerator.h`)
as the only must-preserve-verbatim bot-specific fixes** in the entire 77-file
patch. Net effect: of 77 files, 43 apply with zero conflict, ~31 more are a
mechanical take-CoA's-side or keep-both resolution, and only **3 files**
carry logic that exists specifically because the bot is a session-less
`Player`.

## Files with zero conflict (safe to apply mechanically)

The 36 byte-identical-to-vanilla files plus the 7 that `diff3` auto-merges
cleanly (43 total) are listed in `three_way_comparison.txt` /
`conflict_results.txt` in the scratch research folder (not committed here —
regenerate per "Reproducing this analysis").

## Reproducing this analysis

```bash
# Shallow-clone all three
git clone --depth 1 --branch master https://github.com/azerothcore/azerothcore-wotlk.git vanilla-core
git clone --depth 1 --branch Playerbot https://github.com/mod-playerbots/azerothcore-wotlk.git playerbots-fork
# CoA core: already a full clone at C:\games\source\server\azerothcore-wotlk-coa

# File-level comparison (vanilla vs playerbots-fork)
diff -rq vanilla-core/src/server/game playerbots-fork/src/server/game

# For each differing file, check whether CoA also differs from vanilla,
# then diff3 -m <coa-file> <vanilla-file> <playerbots-file> for a real
# three-way merge attempt; count "^<<<<<<<" lines for conflict markers.
```

Re-run this before starting real patch work if either upstream (vanilla
AzerothCore or mod-playerbots) has moved significantly since 2026-09-11 —
the categorization above is tied to the exact commits compared, not a
permanent property of these projects.
