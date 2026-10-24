# Combat Intelligence - baseline audit

Branch `combat-intelligence`, base commit `966e05498542c5c9dfb3f0cf230c0d4ad7c7bbc2` (bots-module master at the start of the work).
Method: read the engine code listed below, cross-check with logs from live dungeon sessions (2026-10-21/22) and with training-arena runs.
Every finding says whether it is a **confirmed bug**, a **missing mechanic**, an **architectural limitation**, a **performance issue** or an
**unverified hypothesis**. A finding is only "confirmed" when a run or a code path proves it; the evidence column says which.

Files read for this pass: `engine/ActionEvaluator.cpp`, `CombatUtility.cpp`, `CombatContext.*`, `ThreatEvaluator.cpp`, `TankEngine.cpp`,
`HealEvaluator.cpp`, `HealerEngine.cpp`, `DpsEngine.cpp`, `DamageTracker.cpp`, `CombatReservations.cpp`, `TargetEvaluator.cpp` (first half),
`BotAI.cpp` (`UpdateOffensive`, `UpdateHealer`, `TryAutoPullInInstance`, `TryRestIfNeeded`), `BotMovement.cpp` (`IssueLeg`, `MoveToInternal`).
Not yet read in depth: `SpecStrategyRegistry.cpp` (916 lines), `SpellPredicates.cpp`, `CombatResource.cpp`, `CastGuard.cpp`, `CombatMovement.cpp`,
`BotAvoidance.cpp`, `BotBossKnowledge.cpp`, the 45 profiles. Those are the next audit passes.

## Architecture as found

* Bots are ticked from `WorldScript::OnUpdate` (world thread) through `BotMgr` -> `BotAI::Update`. All engine state kept in file-level
  `unordered_map`s (`s_internalThrottles`, `s_noActionRetryAt`, `s_targetLocks`, `s_samples`, reservations) is therefore single-threaded
  with respect to the map update threads (`MapUpdate.Threads = 4` in Server 2's config). Core hooks that run on map threads
  (`CoaTraining::*`) take their own mutex. **Latent risk** F-012: anything that starts reading this state from a map-thread hook breaks that guarantee.
* Decision flow per role: `CombatUtility::Execute` (taunt, interrupt, cleanse, area resurrection - fixed order) -> role engine
  (`TankEngine`/`HealerEngine`/`DpsEngine`: `ActionEvaluator::EvaluateBestAction` over the profile's `AbilityDescriptor`s) -> legacy
  class rotation as fallback when the engine returns `NoAction`.
* `EvaluateBestAction`: form/state machine first, phase detection, then for each descriptor `ResolveCastSpell` -> `ResolveTarget` -> `CanCast` (hard
  gates) -> `ScoreAbility` -> strategy phase modifiers; the strictly highest score wins, the first descriptor wins a tie (deterministic).

## Findings

| ID | Severity | Class | Subsystem | Source | Observed / reasoned behaviour | Evidence | Proposed fix | Regression risk | Test scenario | Status |
|----|----------|-------|-----------|--------|-------------------------------|----------|--------------|-----------------|---------------|--------|
| F-001 | High | Confirmed bug (code path) | Healing | `ActionEvaluator.cpp` `SelectBestReachableAlly` (l.106-137) + `ScoreAbility` DirectHeal bonus (l.482) | A profile heal with the default `maxTargetHpPct = 100` can be aimed at an ally who needs no healing: the ally pick only filters on `maxTargetHpPct` and aura-due, never on `HealEvaluator::ShouldConsiderHealing`, and every `DirectHeal` keeps a positive base score. `ScoreHealUrgency` even adds +40 for any ally with an attacker, so a full-health tank in combat is a valid, top-ranked target. Result: mana spent on overheal. | Code read; run-time proof pending (new `targetHpPct` / `botManaPct` fields in `cast_result` events, Package 0) | Gate Direct/EmergencyHeal on `ShouldConsiderHealing` and on expected overheal; keep HoT upkeep and pre-heal paths exempt | Medium: healers must still pre-heal a tank before a tankbuster (exempt when incoming dps / boss cast says so) | HEAL_RAMP, HEAL_POOL, THREAT_STRESS with a healer candidate | Hypothesis partly refuted (see note) |
| F-002 | High | Confirmed (code) | Utility priority | `CombatUtility.cpp` `Execute` order | Fixed order taunt -> interrupt -> cleanse -> resurrection, always before the role engine. A healer with a critical ally still casts a non-lethal cleanse first (it is instant, but it costs a GCD and mana, and the heal is delayed by `UTILITY_REACTION_GATE_MS`). The healer path in `UpdateHealer` calls `CombatUtility::Execute` before `HealerEngine`. | Code read; `UpdateHealer` call order (BotAI.cpp) | Contextual gating: no non-lethal cleanse while `criticalAllyCount > 0` or an ally has a predicted TTD below the cast time | Low | DISPEL_MAGIC + HEAL_RAMP together | Open |
| F-003 | Medium | Missing mechanic | Interrupts | `CombatContext.cpp` (`victimIsCastingInterruptible` is computed for `ctx.victim` only), `CombatUtility.cpp` step 2 | Only the cast of the bot's own current target is ever considered. A dangerous cast from another enemy is invisible to every bot not targeting it; every interruptible cast of the own target is kicked regardless of whether it matters; there is no backup interrupter. | Code read | Group-level cast watcher with primary/backup assignment (Package 3) | Medium | `interruptible_cast` mechanic | Open |
| F-004 | Medium | Architectural limitation | Threat | `ThreatEvaluator.cpp` | Never reads `ThreatManager`. A "threat candidate" is any enemy whose current victim is a grouped non-tank player. No threat margin, no aggro-loss prediction, no per-enemy threat rate; the tank reacts after the loss, not before. | Code read | Read real threat values through the core API for the nearest enemies; margin-based pre-emptive taunt/threat burst (Package 2) | Medium | THREAT_STRESS | Open |
| F-005 | Medium | Architectural limitation | Damage prediction | `DamageTracker.cpp` | "Incoming dps" is the smoothed *net* health loss between two samples (alpha 0.5, min interval 200 ms). Healing between samples hides damage, so a healed tank reads as taking nothing; it is poll-driven (only updates when some bot asks), and has no burst/absorb/max-HP-change handling. Used for TTD in `HealEvaluator` and `worthDefensiveCooldown`. | Code read | Feed it from the real damage hook (`UnitScript::OnDamage`) with a bounded per-unit ring buffer; expose gross damage separately from heals | Medium | HEAL_RAMP | Open |
| F-006 | Medium | Missing mechanic | Healing | `HealEvaluator::ScoreHealUrgency` | No absorb/shield awareness and no incoming-HoT credit; reservations cover direct heals only (`HealerEngine` reserves only when `expectedHeal > 0`). Two healers can both start a HoT or shield on the same target. | Code read | Credit active absorbs and own/other HoT remaining ticks in missing-health calculation | Low | HEAL_POOL with two healers | Open |
| F-007 | Medium | Missing mechanic | Resource | `ActionEvaluator::ScoreAbility`, `ResourcePolicy` | Mana is never paced: `reservePowerPct`/`resourceEfficiency` default to 0/1.0 so only a few hand-tuned descriptors ever feel a mana floor. A caster spends its pool in one pull (observed live: Pyromancer at 3.6% mana mid-pull, tank waited 43 times). No opportunity cost for a spender that starves a later tankbuster. | Live dungeon log 2026-10-23, `listauras`: `mana 44/1215` | General mana pacing and opportunity cost in the evaluator (Package 1) | Medium | RESOURCE_ENDURANCE | Open |
| F-008 | Low | Confirmed (code) | Tank | `CombatContext.cpp` `victimTargetingNonTank` | A mob attacking a pet/guardian (`GetRole` of a non-player guid) counts as "targeting a non-tank" and can trigger a taunt. | Code read | Treat owned units of a tank as tank-held | Low | pet class tank | Open |
| F-009 | Medium | Confirmed bug (fixed 2026-10-21, commit 27fe318) | Movement | `PathGenerator` bot cap | The core refuses a bot's path of 74+ smooth points (NOPATH). Long dungeon routes need legs; fixed with navmesh waypoints. Keep as regression test. | Live run, `botcmd pathtest` | done | - | `botcmd pathtest` on route points | Fixed |
| F-010 | Medium | Confirmed bug (fixed 2026-10-23, 966e054) | Group | `TryAutoPullInInstance` | Tank pulled/walked while the group was out of mana / dead / far. Added `AutoDungeonWaitReason`. | Live log "waiting: ... low on mana" | done | - | auto-dungeon run | Fixed |
| F-011 | Low | Unverified hypothesis | Tank | live Templar tank only auto-attacked (Oct 22) | Not reproducible in `ST_STATIC` (98 dps from abilities) nor in a group probe; a second Templar fought normally. Cause unknown. | Training runs + probe | Capture a decision trace during a live pull (Package 6) | - | live | Open |
| F-012 | Low | Latent risk | Concurrency | file-level `unordered_map` state (see Architecture) | Safe today because bots tick on the world thread; unsafe if a map-thread hook ever touches it. | Code read | Document; add an assertion in debug builds | Low | - | Open |
| F-013 | Low | Missing mechanic | Telemetry | training `CastResult` | No per-cast target HP / mana, so overheal and mana burn cannot be measured; heal events record only the effective amount. | `events.jsonl` inspection | `cast_result` now carries `targetHpPct`, `targetFriendly`, `botManaPct` (Package 0) | None | any | Done |
| F-014 | High | Confirmed bug (data) | Tank profile | `profiles/ProfileVenomancer.cpp` Fortitude, Spell.dbc 704235 | `Pinch` is tagged `Taunt | MeleeAttack` at score 440, but the spell is `MOD_DISARM` + `MOD_DISARM_OFFHAND`, 150 rage, 60 s cooldown. The engine treated it as a taunt and spent 150 rage whenever a mob left the tank. | `tools/combat-intelligence/spell_dbc.py 704235` (auras 67, 254) | Retag as defensive, only against an enemy that hits the tank | Low | `SURVIVAL`, `THREAT_STRESS` with Fortitude | Fixed in Package 4 (pending test) |
| F-015 | High | Confirmed bug (data) | Tank profile | same file, Spell.dbc 705966 / 800894 | `Reinforced Shell` and `Fury of Anub'Rekhan` are `[Passive]` in Spell.dbc; as castable defensives they were rejected ("passive ability") on every decision, so Fortitude had no active mitigation at all. The real buttons were missing: `Regrow Exoskeleton` (803197, -21% damage taken, 60 s), `Lifeblood` (804963, -21%, 90 s). | spell_dbc.py output; `CanCast` passive rejection | Remove the passives, add the two buttons | Low | `SURVIVAL` | Fixed in Package 4 (pending test) |
| F-016 | Medium | Confirmed (data) | Profiles | `tools/combat-intelligence/profile_audit.py` over 66 profiles / 899 descriptors (`profile_audit_raw.json`) | 10 descriptors name a spell id that is not in Spell.dbc (Putrefy 804558 x3, Raise: Skeletal Archer 500332, Unholy Command 802123, Shudder Scythe 801321, Rifle (Gunsling) 801648, Repeater 805903 x3), 5 are passive (Aortic Assault 806212, Ripple 806296, Ignis Ultimatus 680369, Sunslam 805629, Gatling Gun 500213): dead on every decision. 6 Taunt-tagged descriptors are not taunts in the data (Grasp of Zek'voz 573028, Protective Roar 802782, Tormentor 92147, Glare 805583 = a stun, Courage 707754, Gaze of the Black Knight 802138) - each ranks at 440 whenever a mob is on a non-tank and burns a GCD while the real taunt is on its 8 s cooldown. Seraphim's only Taunt-tagged descriptor is the stun (its real taunt is not in the profile; the spellbook scan in `CombatUtility` covers it). Heal/Shield/HoT tag mismatches (60+) are mostly script-implemented spells and need per-spec reading. | profile_audit output | Remove or retag per spec after reading the spell's script; track in `SPEC_VALIDATION_MATRIX.md` | Low-Medium (a dead descriptor costs nothing; a retagged one changes rotation) | all-spec smoke (ST_STATIC) before/after | Open |

## Baseline measurements

Package 0 re-measures on the current build (core 12f459cff + all modules except stock Zyth playerbots). Results go to `TRAINING_RESULTS.md`.
Earlier measurements that remain valid as history: the 70-spec sweeps in `.agents/training-live/reports/iteration-catalog.json`
(I93-I122, fixture gear plan v5; DPS roles +28% over I109, tanks/healers flat).

## Next audit passes

1. `SpecStrategyRegistry` / form state machine / `SpellPredicates` - deadlock and starvation check.
2. `CastGuard`, `CombatMovement`, `BotAvoidance` - move/cast conflict inventory.
3. Profiles: per-spec descriptors against Sidekick + Spell.dbc (pilot: Venomancer Fortitude, Tinker Mechanics, a healer, a melee, a support).

## Measured notes

* **F-001 (heal on full-health allies)** - pilot run, Vizier healer, `HEAL_POOL` and `HEAL_RAMP` x3 seeds, baseline build: 25-26% of accepted friendly casts
  land on allies at 95% health or more. Broken down by spell, they are all heal-over-time upkeep (`Shadra's Prayer` 502947, `Mending Mist` 502957,
  `Green Salve`-type pre-heals), not the direct heals (`Alkahest`, `Emerald Veil` are gated by their own `maxTargetHpPct`). So for this healer the
  waste is deliberate HoT upkeep, not a mana drain; the new direct-heal gate changed nothing measurable here (-3.8% HPS, inside the +-9% noise).
  Kept as a defensive rule for descriptors that leave `maxTargetHpPct` at 100, but its value is **unproven**; mana pacing (F-007) is the lead
  for healer sustain.
* **Training damage fidelity** - `tankbuster`, `party_aoe`, `targeted_damage` and `pool_refill` call `Unit::DealDamage` directly with raw numbers, so
  damage-taken auras, armor and absorbs never apply: a tank's `-21%` defensive and a healer's shield cannot show in `damageTaken` (identical 7200 in
  baseline and candidate). Only the `melee` mechanic (a real swing) is mitigated. Fixed in Package 0 with an opt-in modifier path.
