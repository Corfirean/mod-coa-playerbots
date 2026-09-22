# Combat Engine v2 -- Phase 1 & 2 (2026-09-22)

Audit + architectural rework of the Data-Driven Combat AI framework
(`BotAI.cpp` + `engine/*`), requested to make bots noticeably more effective
in PvE/PvP by fixing the combat *engine*, not by hand-tuning individual
class profiles. This file records Phase 1 and Phase 2 (see "Phased plan"
below) -- Phase 3 (per-profile tuning across all 21 classes) is not started.

## Audit findings (pre-existing bugs, confirmed by reading the code)

1. **Internal throttle bug** (`DpsEngine.cpp`/`HealerEngine.cpp`/`TankEngine.cpp`):
   after picking one action, all three engines looped over *every* ability in
   the profile and set `internalThrottleMs` on any with a nonzero value --
   regardless of whether that ability was the one cast, and regardless of
   whether the cast even succeeded.
2. **DataDrivenAI swallowed all fallback logic**: `Execute()` returned `bool`,
   and returning `true` only meant "a profile exists for this class/spec/
   role" -- once a profile existed, taunt/interrupt/AoE/burst/old rotation/
   generic fallback in `BotAI.cpp` became permanently unreachable, even on a
   tick where the profile found nothing castable.
3. **No generic Utility Layer**: zero profiles (checked Guardian, SunCleric)
   used `AbilityTag::Interrupt`/`Cleanse`. Generic interrupt logic existed in
   `BotAI.cpp` but was unreachable per bug #2 above for any class with a
   profile.
4. **No interrupt coordination** -- multiple bots with interrupts could all
   spend them on the same cast.
5. **No stop-to-cast gate** -- a cast-time Data-Driven ability could be
   attempted while `MoveChase` was still actively repositioning the bot,
   guaranteeing `SPELL_FAILED_MOVING`.
6. **Fixed 1500ms GCD approximation** (`APPROXIMATE_GCD_MS`), with a comment
   claiming this AzerothCore fork has no real per-spell GCD query API. That
   comment is **stale**: `Player::GetGlobalCooldownMgr()` (`HasGlobalCooldown`/
   `GetGlobalCooldown`) is real, already populated by every genuine
   `CastSpell` call via `Spell::TriggerGlobalCooldown`, and is haste-aware.
7. **`CombatContext::nearbyEnemyCount` was declared but never populated** in
   `Build()` -- always read as 0. `TargetType::AreaHostile` in
   `ActionEvaluator::ResolveTarget` is literally identical to `CurrentTarget`
   (not changed in Phase 1 -- see Known limitations).
8. **No cooldown fight-value gating** -- `AbilityTag::OffensiveCD` had no
   scoring modifier tied to whether the fight actually warranted it.
9. **`ActionEvaluator::ScoreAbility` used an `else if` chain** across tags --
   an ability tagged `EmergencyHeal | DirectHeal` only ever scored as
   `EmergencyHeal`, silently losing the `DirectHeal` contribution.

## What changed

### New files (`module/src/engine/`)

- **`SpellPredicates.h/.cpp`** -- the "is this known spell shaped like an
  offensive/taunt/heal/interrupt/dispel ability" predicates and the generic
  spellbook scanners (`SelectTauntSpell`, `SelectInterruptSpell`, etc.),
  moved out of `BotAI.cpp`'s anonymous namespace so `CombatUtility` can reuse
  the exact same shape checks instead of re-deriving its own copy. Added
  `IsUsableDispelSpell`/`SelectDispelSpell` (new) and `IsOffGlobalCooldown`
  (new, wraps the real `GlobalCooldownMgr`). `BotAI.cpp` now pulls these back
  into its own anonymous namespace via `using` declarations -- behavior
  unchanged, just de-duplicated.
- **`CombatResult.h`** -- `enum class CombatResult { Cast, Busy, NoAction }`,
  replacing the old `bool` return from `DpsEngine`/`HealerEngine`/
  `TankEngine::Execute`. `NoAction` is the new, meaningful case: the profile
  (or lack of one) found nothing to cast, so the caller falls through to the
  legacy chain instead of eating the tick.
- **`CombatUtility.h/.cpp`** -- the Global Combat Utility Layer. Runs before
  RoleEngine (Tank/Dps/Healer) on every combat tick: taunt (Tank, not
  holding aggro) -> interrupt (reservation-gated) -> cleanse (first
  dispellable ally). Built entirely off the bot's real spellbook via
  `SpellPredicates`, so it covers every class regardless of whether its
  profile bothers to tag `Interrupt`/`Cleanse` -- it does **not** replace a
  profile's own hand-authored Taunt/Interrupt entries where they exist, both
  just agree on the outcome.
- **`CombatReservations.h/.cpp`** -- `InterruptReservation` + a static
  `[enemyGuid] -> reservation` map. First bot to see an interruptible cast
  reserves it; other bots skip that enemy's cast until the reservation is
  cleared (interrupt landed, cast resolved, or a short timeout). Deliberately
  scoped to "coordinate bots targeting the same enemy's cast," not a full
  encounter database (see Known limitations).
- **`CombatMovement.h/.cpp`** -- `ReadyToCast(bot, spellInfo)`: instant
  spells always ready; a cast-time spell while moving calls `StopMoving()`
  and returns `false` for that tick, `true` once actually stationary. Reuses
  the exact "stop this tick, cast next tick" shape this codebase already
  uses for the same problem in `BotAI.cpp`'s `CastGatherAt`.

### Modified files

- **`AbilityDescriptor.h`**: unchanged (no new fields needed for Phase 1).
- **`ActionEvaluator.h/.cpp`**:
  - `BotAction` gained `rootSpellId` and `internalThrottleMs`, copied from
    the winning `AbilityDescriptor` -- lets the caller throttle exactly the
    ability that was cast, keyed on its root spell id.
  - `CanCast` now also checks `IsOffGlobalCooldown` (real GCD, see #6 above).
  - `ScoreAbility`'s tag block is now independent `if`s instead of
    `else if` (bug #9) -- `EmergencyHeal`/`DirectHeal`/`PeriodicHeal`/
    `AoEHeal` bonuses now all apply when combined; `Interrupt`/`Taunt`/
    `Execute` stay hard disqualifiers when their condition fails (every
    profile observed tags them alone, purpose-built for that one situation).
  - New `AoEDamage` scoring: `+= nearbyEnemyCount * 20` (was previously
    untouched by `nearbyEnemyCount`, which was always 0 anyway -- bug #7).
  - New `OffensiveCD` gating: `score *= 0.15` when `!ctx.worthOffensiveCooldown`
    (deprioritize, not hard-disqualify, so a class whose only usable ability
    happens to be `OffensiveCD`-tagged still eventually fires it).
- **`CombatContext.h/.cpp`**: added `victimCastingSpellId`/
  `victimCastFinishTimeMs` (for interrupt reservations), `targetIsBossOrElite`,
  `targetHpPct`, `worthOffensiveCooldown` (fight-value gate), and fixed
  `nearbyEnemyCount` to actually populate via the shared
  `SpellPredicates::CountNearbyEnemies` (10yd around the victim).
- **`DpsEngine.cpp`/`TankEngine.cpp`/`HealerEngine.cpp`**: `CombatResult`
  return type; throttle now applies only to `action.rootSpellId` and only
  after `SPELL_CAST_OK`; a failed cast returns `Busy` (tick handled, no
  throttle penalty) instead of silently eating the profile's internal
  cooldown; `CombatMovement::ReadyToCast` gates the actual `CastSpell` call;
  `AI_REACTION_GATE_MS` (150ms, real per-spell GCD is enforced inside
  `ActionEvaluator::CanCast` now) replaces the fixed 1500ms `APPROX_GCD_MS`.
- **`BotAI.cpp`**:
  - `APPROXIMATE_GCD_MS` (1500) replaced with `AI_REACTION_GATE_MS` (150) at
    both legacy-fallback cast sites (`UpdateOffensive`, `UpdateHealer`).
  - `UpdateOffensive`/`UpdateHealer` now call `CombatUtility::Execute` before
    `TankEngine`/`DpsEngine`/`HealerEngine::Execute`, and treat
    `CombatResult::NoAction` as "fall through to legacy chain" instead of
    always stopping.
  - The legacy chain's own cast dispatch (both the offensive fallback and
    the heal fallback) now gates through `CombatMovement::ReadyToCast`
    before calling `LogCastAttempt`/`CastSpell`.
  - The legacy interrupt-priority step now also checks
    `CombatReservations::IsInterruptReserved` so it doesn't duplicate a
    reservation the Utility Layer already holds.
  - `nearbyEnemies` in the legacy AoE-priority step now reads
    `utilityCtx.nearbyEnemyCount` (already built once per tick) instead of
    re-scanning.
  - `Forget()` now also calls `CombatReservations::ForgetBot`.

## Old functions reused vs. now-fallback-only

Reused as-is (moved, not rewritten): `IsUsableOffensiveSpell`,
`IsUsableTauntSpell`, `IsUsableHealSpell`, `IsUsableBuffSpell`,
`IsUsableInterruptSpell`, `IsUsableAoeSpell`, `IsUsableBurstSpell`,
`SelectKnownSpell` and all its `Select*Spell` wrappers,
`IsTargetCastingInterruptibleSpell`, `IsBossOrEliteTarget`,
`CountNearbyEnemies` -- all now live in `SpellPredicates` and are shared by
both `BotAI.cpp`'s legacy chain and the new `CombatUtility` layer, instead of
each maintaining its own copy.

Now-fallback-only (per item 23 of the rework -- kept working, not deleted,
reachable again now that bug #2 is fixed): generic taunt/interrupt/AoE/burst
selection, `BotAI::SelectClassRotationSpell` and the per-class
`Select*RotationSpell` functions (Reaper/Felsworn/Bloodmage/Stormbringer/
Primalist/Runemaster/Xoroth/Chronomancer/Necromancer), the generic
single-target/offensive spell fallback. These now only run on a tick where
either no profile exists, or the profile's `ActionEvaluator` genuinely found
nothing castable -- previously most of them were dead code for any class
with a profile.

## Bugs fixed (numbered list above) vs. behavior before/after

| # | Before | After |
|---|--------|-------|
| 1 | Any successful cast throttled every `internalThrottleMs`-bearing ability in the whole profile, success or failure. | Only the cast ability is throttled, keyed on its root spell id, only after `SPELL_CAST_OK`. |
| 2 | A profile existing made taunt/interrupt/AoE/burst/rotation/fallback permanently unreachable. | `CombatResult::NoAction` falls through to the legacy chain; `Cast`/`Busy` consume the tick as before. |
| 3 | Interrupt/Cleanse only worked for classes whose profile happened to tag them (none did). | Every class gets a generic taunt/interrupt/cleanse floor via `CombatUtility`, independent of profile tags. |
| 4 | N bots with interrupts could all kick the same cast. | First bot reserves it; others skip that enemy's cast while the reservation is live. |
| 5 | A cast-time ability could be attempted mid-reposition -> `SPELL_FAILED_MOVING`. | `CombatMovement::ReadyToCast` stops the bot first, defers the cast one tick. |
| 6 | Every successful cast waited a flat 1500ms regardless of haste or off-GCD status. | Real per-spell GCD via `GlobalCooldownMgr` (haste-aware, off-GCD spells never gated); AI re-evaluates every 150ms. |
| 7 | `nearbyEnemyCount` always read 0; `AoEDamage` tag had no count-based bonus. | Populated every tick (10yd around victim); `AoEDamage` scores `+= count * 20`. |
| 8/9 | `OffensiveCD` abilities scored the same on a boss and a nearly-dead trash mob. | Score multiplied by 0.15 when the fight doesn't justify it (not boss/elite/PvP/pack, or target already <15% HP). |
| (bonus) | `EmergencyHeal \| DirectHeal` only ever scored as `EmergencyHeal`. | Both bonuses apply independently. |

## Phase 2: Group Combat Intelligence (2026-09-22)

Target/threat/heal scoring plus the reservation and resource-management
plumbing item 25's Phase 2 checklist asks for, built on top of Phase 1's now-
sound engine. Interrupt reservations were already delivered in Phase 1 (they
were tightly coupled to the Utility Layer) -- this phase extends the
reservation idea to heals and adds the scoring layers around targeting.

### New files (`module/src/engine/`)

- **`TargetEvaluator.h/.cpp`** (item 1/#8) -- `ScoreTarget` ranks nearby
  hostiles by leader/tank's-target proxy, attacking-the-healer, mid-cast,
  low-HP execute range, distance, and a hard penalty for a target under a
  breakable crowd-control effect (item 17's "don't break a groupmate's CC" is
  folded in here, not a separate system). `RefineTarget` re-evaluates the
  bot's already-acquired baseline target with a 1.25x stickiness margin so it
  doesn't ping-pong between similarly-threatening enemies every tick.
- **`ThreatEvaluator.h/.cpp`** (item 9/#3) -- `ScoreThreatTarget` ranks
  enemies currently attacking a groupmate (healer > low-HP ally > ordinary
  DPS), with a boss/elite bonus and a penalty when a different Tank-role
  groupmate already has that exact enemy as its victim (avoids a pointless
  re-taunt war between two tank bots). `SelectThreatTarget` replaces the call
  site of the old first-match `FindAllyThreatenedTarget` for Tank role.
- **`HealEvaluator.h/.cpp`** (item 10/12) -- `ScoreHealUrgency` factors in
  missing HP (net of other healers' already-reserved incoming heals),
  incoming-damage trend (`DamageTracker`), estimated time-to-die, role (tank
  bonus), whether an enemy is actively engaged with the candidate, and a real
  damage-over-time debuff. `SelectBestHealTarget` replaces `FindHealTarget`'s
  plain lowest-HP% scan. `BestKnownHealRange` (item 12) walks the healer's own
  spellbook for the real max range of its known heals (cached ~15s) instead
  of a fixed 25/35yd constant.
- **`DamageTracker.h/.cpp`** -- the "cheap rolling estimate" item 10
  explicitly asked for: one hash-map entry per tracked guid (`lastHp`,
  `lastSampleTimeMs`, a smoothed `incomingDamagePerSecond`), updated at most
  once per ~200ms per guid. Shared by `HealEvaluator` (ally urgency) and
  `CombatContext::Build` (the bot's own defensive-CD gating, item 19) so
  there's one incoming-damage signal, not two independently-drifting ones.

### Modified files

- **`CombatReservations.h/.cpp`**: added `HealReservation` (`healerGuid`,
  `targetGuid`, `spellId`, a rough `expectedHeal`, `expiresAt`) and
  `ReserveHeal`/`GetReservedIncomingHeal`/`ClearHealReservation`. Unlike the
  single-slot interrupt reservation, heal reservations are additive per
  target (several simultaneous reservations on one guid are legal -- a
  genuinely critical target may need two heals at once); `HealEvaluator`
  subtracts the sum from a candidate's missing health before scoring it.
- **`AbilityDescriptor.h`** (item 13): added `minPowerPct` (hard eligibility
  floor), `reservePowerPct` (soft floor -- deprioritize, don't disqualify),
  and `resourceEfficiency` (relative value-per-resource, 1.0 = neutral). All
  default to unrestricted (0/0/1) so existing profiles are unaffected until
  Phase 3 tunes real values per ability.
- **`ActionEvaluator.cpp`**: `CanCast` disqualifies below `minPowerPct`;
  `ScoreAbility` penalizes an inefficient (`resourceEfficiency < 1`) ability
  once power drops below its `reservePowerPct`, scaled by how deep into the
  reserve the bot already is -- an efficient option takes no penalty, so it
  naturally outscores a penalized wasteful one. Also added `DefensiveCD`
  contextual scoring (item 19): `+= 200` when `ctx.worthDefensiveCooldown`,
  `*= 0.5` when not, on top of the existing HP-threshold eligibility gate.
- **`CombatContext.h/.cpp`** (item 19): added `botIncomingDps` and
  `worthDefensiveCooldown` (true on an emergency HP floor, a real
  incoming-damage trend implying death within ~6s, or a boss/elite mid-cast
  actually targeting the bot below 60% HP). The ally-triage snapshot's
  `lowestAlly` selection now uses `HealEvaluator::ScoreHealUrgency` instead of
  plain lowest-HP% -- this also upgrades every Data-Driven healer profile's
  `TargetType::LowestHealthAlly`/`AnyInjuredAlly` abilities, not just the
  legacy healer fallback.
- **`HealerEngine.cpp`**: reserves an estimated heal (sized off the winning
  action's tags -- `EmergencyHeal` 35%, `AoEHeal` 15%, else 20% of the
  target's max health) before casting, cleared only on a failed cast (a
  cast-time heal's real effect doesn't land until the cast finishes, so the
  reservation is left to expire on its own ~6s TTL rather than cleared right
  after `CastSpell` returns).
- **`BotAI.cpp`**:
  - `UpdateOffensive`: Tank's ally-threat check now calls
    `ThreatEvaluator::SelectThreatTarget` (falling back to the old
    `FindAllyThreatenedTarget` if it finds nothing, item 23); every role then
    runs `TargetEvaluator::RefineTarget` against the acquired baseline target
    (skipped on the same tick a Tank's threat check already picked one).
  - `UpdateHealer`: `FindHealTarget` replaced with
    `HealEvaluator::SelectBestHealTarget` (falling back to the old function if
    it finds nobody, item 23); positioning now uses
    `HealEvaluator::BestKnownHealRange` instead of the fixed `HEAL_ENGAGE_RANGE`;
    the legacy heal-cast fallback reserves a rough 20%-of-max-health estimate
    before casting, same pattern as `HealerEngine.cpp`.
  - `Forget()` now also calls `DamageTracker::Forget` and
    `HealEvaluator::ForgetBot`.

## Old functions reused vs. now-fallback-only (Phase 2 additions)

`FindAllyThreatenedTarget` and `FindHealTarget` are both kept verbatim (item
23) and are still real code paths -- each is now the fallback when its
scored replacement (`ThreatEvaluator`/`HealEvaluator`) finds nothing, not
dead code.

## Phased plan

- **Phase 1: done, builds clean, live-tested.** Throttle fix, CombatResult
  semantics, Utility Layer (taunt/interrupt/cleanse), interrupt reservations,
  stop-to-cast, real GCD, AoE count, cooldown fight value.
- **Phase 2: done, builds clean.** `TargetEvaluator` + stickiness,
  `ThreatEvaluator`, `HealEvaluator` + heal reservations, resource-aware
  scoring (`minPowerPct`/`reservePowerPct`/`resourceEfficiency`),
  defensive-CD contextual scoring, real heal-spell-range positioning.
- **Phase 3 (not started)**: per-profile tuning pass across all 21 classes
  now that the shared engine is sound.

## Known limitations (Phase 1 scope decisions)

- **Utility Layer covers Taunt/Interrupt/Cleanse only.** "Emergency survival"
  and "Critical heal" (the top two tiers of the original priority sketch)
  need `HealUrgencyScore` (Phase 2) and Defensive Intelligence (Phase 2) --
  there's no reliable generic way to detect "a defensive cooldown" from raw
  `SpellInfo` shape the way taunt/interrupt/heal/dispel can be (too varied,
  easy to false-positive on unrelated buffs).
- **Interrupt reservation coordinates bots sharing the *same* target's cast**
  (the common case: a group fighting one enemy). Ranking danger *across*
  several simultaneously-casting enemies needs the Target/Threat evaluators
  from Phase 2 -- explicitly out of scope for "no encounter DB yet."
- **Cleanse doesn't match `DispelType`.** `IsUsableDispelSpell` detects the
  real `SPELL_EFFECT_DISPEL` shape but doesn't check the ally's specific
  debuff type against what the bot's spell actually removes -- the real
  `Player::CastSpell` cast validation rejects a genuine mismatch on its own
  (wasted GCD, not a bad cast landing).
- **`TargetType::AreaHostile` is still identical to `CurrentTarget`** in
  `ActionEvaluator::ResolveTarget` -- picking a real cluster center for AoE
  is item 15's full AoE decision layer, Phase 3 scope. Phase 1 only fixed
  the *count* feeding into `AoEDamage` scoring, not target selection.
- **`CombatContext` is built twice per tick** when the Utility Layer declines
  to act and RoleEngine proceeds (once for the Utility Layer, once inside
  `DpsEngine`/`TankEngine`/`HealerEngine::Execute`). Both scans are cheap
  (bounded group size, 10yd enemy radius), but sharing one snapshot across
  both layers is a real Phase 2 performance cleanup, not done here to avoid
  threading a `CombatContext*` through every engine's public signature in
  the same pass as the correctness fixes.
- **A discovered-but-unrelated MSVC gotcha, fixed along the way**: two new
  headers forward-declared `struct SpellInfo;` where the real type is
  `class SpellInfo` (`SpellInfo.h:340`). Both spellings are legal C++ and
  compile silently, but MSVC mangles member-function parameters of that type
  differently depending on which spelling was in scope at the call site --
  produced `LNK2019`/`LNK2001` unresolved-symbol errors against genuinely
  unrelated, untouched core functions (`Unit::IsValidAttackTarget`,
  `Unit::HasAuraState`, `GameObject::IsAtInteractDistance`). Fixed by using
  `class SpellInfo;` consistently. Worth remembering for any future header
  in this module that forward-declares a core engine type instead of
  including its real header.

## Known limitations (Phase 2 scope decisions)

- **CC Intelligence (item 17)** is only partially addressed: `TargetEvaluator`
  avoids *selecting* a target under breakable CC, but `AbilityTag::CrowdControl`
  itself still has no contextual scoring in `ActionEvaluator` (a stun/fear tag
  still scores on `baseScore` alone, same as Phase 1 left it) -- wasn't on
  Phase 2's explicit checklist, left for Phase 3.
- **Heal reservation sizing is a flat guess** (20%/35%/15% of max health by
  tag), not derived from the spell's real heal-per-cast -- WotLK-era heal
  amount depends on caster spellpower/crit/talents in ways too involved for a
  cheap estimate, and the spec explicitly allows a rough number here.
- **`DamageTracker`'s map has no periodic sweep**, only per-guid cleanup on a
  *bot's* own despawn (`BotAI::Forget`) -- a real player or a creature the bot
  never "forgets" leaves a small (~24 byte) entry for the rest of the server's
  uptime. Bounded by "however many distinct units this server ever damages
  bots or gets healed near," not unbounded per-tick growth, but a periodic
  sweep is worth adding if this ever shows up in a memory profile.
- **`ThreatEvaluator`/`TargetEvaluator` both re-scan nearby hostiles
  independently** (via `SpellPredicates::GetNearbyEnemies`) rather than
  sharing one scan with `CombatContext`'s own `nearbyEnemyCount` count -- same
  category of "known, not yet worth the signature churn to fix" perf note as
  Phase 1's double `CombatContext::Build`, see Performance notes below.

## Verification done

### Phase 1

- Full `worldserver` rebuild (`build_ws.bat` against
  `azerothcore-wotlk-coa/build`) compiles and links clean with these changes
  synced into `azerothcore-wotlk-coa/modules/mod-coa-playerbots/`.
- **Live-tested against `CoA-Repack`** (2026-09-22): deployed the new
  `worldserver.exe`, confirmed 0 connected real players first (safe to
  restart), restarted, and observed the live ambient bot population (~260+
  bots) plus a directly-controlled test bot (`Shaniel`, guid 2, Ranger/
  Farstrider) over several minutes via RA console + `Server.log`:
  - **No crashes, no new errors** attributable to this rework (`Errors.log`/
    `Crashes/` clean; the pre-existing DBC/script-validation warnings at
    boot are unrelated).
  - **`CombatResult::NoAction` fallback confirmed live**: `Shaniel` (Support
    role, spec with no registered Dps profile) fought and killed a 'Young
    Nightsaber' entirely through the legacy `BotAI: bot 'X' cast spell N`
    chain -- exactly the path bug #2 made unreachable before.
  - **`CombatUtility` confirmed live** across the ambient population:
    `CombatAI: bot 'Triomaewyn' reserved and used interrupt (spell 814287)
    on 'Scorpid Hunter'.` (3 distinct interrupts, 3 distinct bots/enemies)
    and `CombatAI: bot 'Valeigaronax' reflexively taunted (spell 804412) on
    'Cracked Golem'.` -- both fired off raw spellbook shape, no profile tags
    involved. No cleanse observed this session (rare trigger condition:
    needs a dispellable debuff, uncommon against ordinary world mobs).
  - **`DpsEngine`/`TankEngine`/`HealerEngine` all confirmed active at scale**:
    8510 `DataDrivenAI [DPS]`, 1050 `DataDrivenAI [Healer]`, and multiple
    `DataDrivenAI [Tank]` log lines across the population, with varied spell
    names/scores (not a fixed rotation stuck on one ability).
  - **One real, concrete regression-adjacent finding**: two Cultist tank
    bots (`Dryanirodor`, `Valyanothul`) each recast `Cultist_Dreadnought_Tank`'s
    "Dreadnought" ability (spell 680750, `DefensiveCD | Shield`,
    `maxSelfHpPct=55`) 100-200+ times in a few minutes (310 total) --
    because that `AbilityDescriptor` sets neither `internalThrottleMs` nor a
    recast guard (`missingAuraOnCaster`), and apparently the spell itself has
    no meaningful native recovery time either, so nothing gated it except
    the AI's own decision cadence. **This is a pre-existing profile gap
    (`ProfileCultist.cpp`), not a bug in this rework's code** -- but
    shortening the reaction gate from the old fixed 1500ms to 150ms (item 6)
    made it ~10x more frequent and therefore much more visible. Checked
    across the whole population: this pattern is isolated to Cultist's
    `Dreadnought`/`Abyssal Ward` entries specifically -- every other tank
    ability observed was cast a normal, bounded number of times. Grepping
    the profiles confirms only `ProfileWitchDoctor.cpp` ever sets
    `internalThrottleMs` at all (20-45s range) -- every other profile's
    `DefensiveCD`/`OffensiveCD` self-buffs are equally exposed to this same
    risk *if* their underlying spell also lacks a real native cooldown.
    **Left unfixed deliberately** (profile edits are explicitly Phase 3
    scope per this rework's own instructions), but flagged here as a
    concrete, prioritized Phase 3 checklist item: audit every
    `DefensiveCD`/`OffensiveCD`-tagged, no-`internalThrottleMs` entry across
    all 21 profiles against its spell's real `RecoveryTime`/
    `CategoryRecoveryTime`, and add `internalThrottleMs` (or a recast guard)
    to any that have neither.
  - **Not exercised this session** (ambient world combat didn't happen to
    produce the right conditions, and constructing them via RA text commands
    alone is impractical without a real client): multi-tank aggro handoff,
    simultaneous multi-bot interrupt contention on the *same* cast (to watch
    the reservation actually block a second bot, not just fire once),
    healer-triage-under-pressure, and the dungeon/boss-shaped scenarios from
    the live-test checklist below. These need either a real client in a
    group/dungeon or more elaborate RA scripting than this pass did.

### Phase 2

Deployed and observed the live ambient population (~260-600 bots) for
several minutes across two builds -- the second after fixing a real bug the
first one surfaced (see below). Diagnostic logging (item 24) was added
during this pass specifically so these decisions were actually observable in
`Server.log`, not just "the server didn't crash."

- **No crashes, no new errors** across the whole session.
- **`TargetEvaluator` confirmed live and a real bug found+fixed**: the first
  build showed a bot (`Vrouderax`) flipping its target every single tick
  between two enemies (`Murkgill Hunter` <-> `Murkgill Warrior`) -- the 1.25x
  score-margin stickiness alone wasn't enough, since the margin check has no
  memory of the previous switch and two similarly-dangerous enemies trading
  which one is mid-cast this exact tick can flip the winner every recompute.
  **Fixed** with a per-bot `MIN_TARGET_LOCK_MS` (3000ms) that blocks another
  *voluntary* switch until it expires (a *forced* switch -- current target
  now dead/unreachable/CC'd -- still bypasses it immediately). Re-deployed
  and re-observed: switches dropped to a normal cadence (seconds to tens of
  seconds apart, tracking genuine changes in the fight), 93 switches total
  across the whole population in 90s versus dozens from one single bot
  before. (Some log lines read `'Thundering Exile' -> 'Thundering Exile'` --
  that's two different mobs sharing a display name, not a same-target
  no-op; the log format doesn't include GUIDs, worth adding if this needs
  debugging again.)
- **`HealEvaluator`/heal reservations confirmed live**: 1209 `reserved ~N
  heal on 'X'` log lines over 90s across the population, sizes varying by
  the tag-based estimate (e.g. ~321 and ~554 on different bots' self-heals,
  ~114 on an `AoEHeal`-shaped one) -- confirms both the urgency-based target
  selection and the reservation sizing logic are executing correctly at
  scale.
- **`ThreatEvaluator` code path executes without error but its actual
  pickup trigger was never observed this session**: `TankEngine`/Tank-role
  ticks ran constantly and without incident (thousands of ticks, no crash,
  no null-deref), confirming `ScoreThreatTarget`/`SelectThreatTarget` run
  safely every tick for every tank bot -- but the specific *trigger*
  condition (a groupmate other than the tank is under attack by a different
  enemy than the tank's own target) never fired, because this server's
  ambient bot population fights mostly solo rather than in tank+healer+DPS
  groups. Not a regression signal (the equivalent DPS/general target-scoring
  logic against the same kind of scan clearly works, see above) but genuine
  threat-handoff behavior still needs a real grouped multi-bot encounter to
  verify end-to-end, same caveat as Phase 1's interrupt-contention scenario.
- **Resource management (`minPowerPct`/`reservePowerPct`/`resourceEfficiency`)
  and defensive-CD contextual scoring were not independently observable**:
  both are real engine changes exercised on every `ScoreAbility` call (no
  crashes), but neither produces its own distinct log line, and no existing
  profile sets non-default values for the new resource fields yet (that's
  Phase 3), so there's nothing behaviorally different to see in the log
  until a profile actually uses them.

## Performance notes

- Per item 21's constraint (thousands of bots): nothing added in Phase 1 or
  Phase 2 scans the full spellbook or full zone every tick beyond what the
  pre-existing code already did. `CombatUtility`'s cleanse scan and
  `HealEvaluator`'s candidate scan are both bounded by group size (<=5-10
  members); `TargetEvaluator`/`ThreatEvaluator`'s hostile scans reuse the
  same bounded-radius `GetNearbyEnemies` as `CombatContext`'s own enemy
  count. `CombatReservations`/`ActionEvaluator`'s throttle map,
  `DamageTracker`'s sample map, `TargetEvaluator`'s lock map, and
  `HealEvaluator`'s range cache are all simple guid-keyed hash maps, cleared
  on despawn (except `DamageTracker`, see Known limitations above).
- The double `CombatContext::Build` per tick (Phase 1's own finding) and
  `TargetEvaluator`/`ThreatEvaluator` each re-scanning nearby hostiles
  independently instead of sharing one scan (Phase 2) are both known,
  measured-safe-so-far perf notes -- worth profiling under real sustained
  bot load before Phase 3, not fixed here to avoid threading a shared
  snapshot through every call site in the same pass as the correctness work.
