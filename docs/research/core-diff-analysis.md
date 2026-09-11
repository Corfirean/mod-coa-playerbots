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
- **Net: 43 of 77 files (56%) need zero manual work.** The other 34 need
  review, but the actual judgment-call subset is much smaller than 34 files —
  see the categorization below.

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

### D. Real overlapping changes to the same feature — both projects modified
the same logic for their own reasons; needs an actual decision, not a
mechanical pick-a-side.

- **`Creature.cpp`** (the other big conflict count, 10) — both sides touch
  the charmed-creature/pet spell-cooldown-notification path
  (`_AddCreatureSpellCooldown` and the code that reports cooldowns back to
  `GetCharmerOrOwnerPlayerOrPlayerItself()`). CoA refactored this into a
  batched-packet send and fixed a real bug ("a longer-running category
  cooldown must not be shortened by a shorter one just applied"; see the
  `hasCategoryCooldown` / `if (GetSpellCooldown(categorySpellId) >
  categorycooldown) continue;` guard). playerbots-fork's version is
  structurally older/simpler and sends per-spell instead of batched, but
  *may* also carry bot-specific relevance (a bot's own charmed pet needs its
  owner-bot, not a real client, to receive this notification correctly —
  unconfirmed, needs checking whether playerbots-fork's difference here is
  really bot-specific or just the same version-drift pattern as category A).
  **Not resolved yet — needs a real look at whether CoA's refactor breaks
  anything playerbots specifically depends on here.**
- **`Pet.cpp`/`Pet.h`** — CoA refactored inline "cast a pending spell after
  the current channel ends" logic into a named `Pet::CastPendingSpell()`
  method; playerbots-fork has its own differently-shaped inline version.
  Same open question as Creature.cpp: is playerbots-fork's shape here
  incidental (pre-refactor baseline) or does it matter for how a
  session-less bot's pet casts queued spells.
- **`PlayerUpdates.cpp`** (5 conflicts) — two independent overlaps: (1)
  unread-mail-count bookkeeping (CoA recalculates from the mailbox on
  demand; playerbots-fork clears a cached timer field instead — different
  approaches to avoiding stale unread counts) and (2) **`CraftSkillGainChance`
  vs `SkillGainChance`** — this one needs a close look before assuming it's
  safe, because it reads like an actual *formula* difference (skill-up
  chance calculation), not just a rename. Confirm whether the two produce
  the same numbers before picking one — if they don't, that's a real
  gameplay-affecting decision, not a mechanical merge.
- **`Player.h`/`PlayerStorage.cpp`** — CoA changed
  `MoveItemToInventory`'s return type from `void` to `Item*` (to support its
  new `OnPlayerAfterMoveItemToInventory` hook, category B above). Need to
  confirm playerbots-fork's call sites don't break when the signature
  changes (they shouldn't — a wider return type callers can ignore — but
  verify, since playerbots-fork calls this method directly, bypassing a
  packet handler).
- **`MailHandler.cpp`** (4 conflicts) — genuinely tangled: CoA added a
  `stored` variable + an `OnPlayerAfterTakeItemFromMail` hook call;
  playerbots-fork has a from-scratch mail-packet-size precalculation
  (`next_mail_size`) that CoA doesn't have, *and* both sides separately carry
  a "prevent client crash" subject/body sanitization block (near-identical
  code, different location in the function) — this one needs an actual
  side-by-side line-level merge, not a pick-a-side.
- **`PetHandler.cpp`** (5 conflicts) — CoA added `COMMAND_STAY`-aware
  handling around channeled pet spells (don't let a stationary channel get
  interrupted by a movement update; explicit stay-vs-forced-spell state
  clearing). playerbots-fork's side is mostly absent here except one
  simplified condition. Likely category A (CoA added a real fix playerbots
  never got), but confirm a stay-commanded pet still behaves correctly under
  bot control before assuming so.
- **`SmartScript.cpp`** conflict 1 — CoA generalized a `SMART_ACTION_MOVE_RANDOM`
  handler to work for any `WorldObject` target, not just `me` (the smart
  script's own owner) — genuinely more capable, but confirm nothing in
  playerbots-fork depended on the narrower `me`-only version.

## What this means for the patch plan

Category A + B (roughly 20 of the 34 conflicted files) are mechanical: take
CoA's side for A, keep both additions for B. That leaves **category C (3
files: `Item.cpp`, `Group.cpp`, `PointMovementGenerator.h`) as
must-preserve-verbatim bot fixes**, and **category D (7 files: `Creature.cpp`,
`Pet.cpp/h`, `PlayerUpdates.cpp`, `Player.h`/`PlayerStorage.cpp`,
`MailHandler.cpp`, `PetHandler.cpp`, `SmartScript.cpp`) as the actual review
work** — read each one, decide per-case, test in-game. That is a bounded,
estimable amount of work, not "re-derive the whole patch from scratch."

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
