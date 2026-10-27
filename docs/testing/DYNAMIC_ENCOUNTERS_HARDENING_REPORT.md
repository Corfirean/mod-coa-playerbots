# Dynamic Bot Encounters — Live Validation & Adversarial Hardening Report

**Repository:** `Corfirean/mod-coa-playerbots`  
**Branch:** `feat/dynamic-bot-encounters`  
**Subsystem:** `module/src/world/encounters/` (`WorldEncounterDirector`)  

---

## 1. Executive Summary

A full adversarial audit, hardening pass, and stress validation was conducted on the **Dynamic Bot Encounters** subsystem (`WorldEncounterDirector` and all 13 open-world encounter types).

- **12 runtime failure modes and edge-case bugs** were uncovered during the audit and fixed in focused commits without altering the core architecture (`WorldBrain -> temporary encounter ownership -> combat if needed -> cleanup -> WorldBrain resume`).
- **16 post-encounter cleanup invariants** are now automatically verified by `EncounterCleanupValidator::Validate` (`EncounterCleanupDiagnostics.h`) on every encounter termination and exposed via `.botcmd encounter validate [bot]`.
- **9,621 assertions** in `encounter_tests.cpp` (including a 326-run forced live/abort matrix across all 13 encounter types, 250-cycle locomotion ownership torture tests, all-6-task-type `WorldBrain` resume tests, and a 5.0-hour / 180,000-tick stress simulation with 200 bots), **487 assertions** in `world_logic_tests.cpp`, **224 assertions** in `locomotion_tests.cpp`, and **11,524 core unit tests** in `unit_tests.exe` pass with **0 failures**.

---

## 2. Bugs Found & Fixed During Adversarial Audit

| # | Severity | Component | Failure Mode Discovered | Fix Implemented |
|---|----------|-----------|-------------------------|-----------------|
| 1 | **P0** | `WorldBrain.cpp` (`OnDeath`) | Dying while `state.pausedByEncounter == true` cleared the flag without calling `WorldExecutor::ResumeTask`, leaving `state.task.paused = true` permanently after resurrection. | `WorldBrain::OnDeath` now calls `WorldExecutor::ResumeTask(bot, state)` whenever `pausedByEncounter`, `pausedBySocial`, or `pausedByAmbient` is active before entering `TaskPhase::Recover`, plus a safety unpause guard in `WorldBrain::Update`. |
| 2 | **P0** | `BotAI.cpp` (`UpdateSoloWorld`) | `BotEconomy::UpdateTrip` and `BotWorldBehavior::UpdateBeforeSolo` ran before `WorldBrain::Update` and could hijack an encounter-owned bot for a vendor errand (`pausedByAmbient`). | Gated `BotEconomy::UpdateTrip` and `BotWorldBehavior::UpdateBeforeSolo` with `!WorldEncounterDirector::IsBotInEncounter(bot->GetGUID())`. |
| 3 | **P0** | `WorldEncounterDirector.cpp` (`Update`) | `UpdateSoloWorld` is bypassed while `bot->IsInCombat()` is true, so `UpdateBot` was not called during combat, delaying `TryCowardlyRetreat` and skirmish resolution. | Added `TickInCombatEncounterParticipants` (500ms cadence inside `WorldEncounterDirector::Update`) to tick active encounter handlers for in-combat participants. |
| 4 | **P0** | `EncounterHandlersAmbient.cpp` (`RestoreParticipantState`) | Hostile encounters could leave stale `SetSelection`, active pet PvP attacks, or lingering PvP combat state (`CombatManager`) after termination. | `RestoreParticipantState` now clears `assignedTargetGuid` selection, stops pet attacks, and calls `bot->AttackStop()` + `bot->GetCombatManager().EndAllPvPCombat()` when `engagedInCombat` was set. |
| 5 | **P0** | `EncounterHandlersAmbient.cpp` (`UpdateRoadDuel`) | Aborting a `RoadDuel` mid-fight (via timeout, external aggro, or `.botcmd encounter stop`) skipped the `Resolve` health restore and left duelists at ~20% HP. | Enforced a 22% HP floor at the top of `Active`, added pet `AttackStop()` and external-attacker checks (`!bot->getAttackers().empty()`), and moved health restoration (`max(originalHealthPct, 80%)`) into `RestoreParticipantState` so all exit paths restore HP. |
| 6 | **P1** | `EncounterHandlersMystery.cpp` & `WorldEncounterDirector.cpp` | If a `MysteryEncounter` bot logged out or changed map before `FinalizeEncounter`, `ObjectAccessor::GetGameObject(*bot, guid)` could not find and delete the temporary rune GameObject. | Stored `temporaryGobMapId`, added `sMapMgr->FindMap(temporaryGobMapId, 0)->GetGameObject(...)` fallback in both `RestoreParticipantState` and offline `FinalizeEncounter`, and tracked `activeTemporaryGameObjects` in metrics. |
| 7 | **P1** | `EncounterHandlersHostile.cpp` (`UpdateAssassinContract`) | Stalking point used `target->GetOrientation()` (jittering whenever the player spun their camera) and reset `goalId = (inst.id << 8) + (nowMs / 2500u)` every 2.5s, resetting stuck detection. | Replaced with a 6-stage humanized stalking state machine (`Observe -> MoveFlank -> WaitDwell -> Reposition -> PreStrikeObserve -> ApproachAttack`) using vector-anchored flanking points (`ComputeVectorOffsetPoint`) and stable per-leg `navGoalId`. |
| 8 | **P1** | `EncounterHandlersHostile.cpp` (`UpdateTheWatcher`) | Had a 15–28yd dead zone where a player charging from 30yd to 16yd would not trigger evasive retreat until `< 15yd`. | Triggered immediate evasive stealth + transition to `Resolve` when the player closes within `< 24.0f` yards, and alternated 7s stationary observation pauses with lateral vantage shifts. |
| 9 | **P1** | `EncounterHandlersHostile.cpp` (`UpdatePatrolAmbush`) & `WorldEncounterDirector.cpp` | All ambushers navigated to the exact same `(destX, destY, destZ)` point without terrain height snapping, causing stacking. | Snapped `centerZ` and `destZ` to terrain via `Map::GetHeight` and assigned each ambusher a deterministic lateral/forward fan offset (`sideSign * 5.5f`, `forwardDist * 3.5f`). |
| 10 | **P1** | `EncounterHandlersAmbient.cpp` (`UpdateCaravan`, `UpdateRivalParty`) | Followers used `Navigate` with a 2.5s `goalId` reset (`nowMs / 2500u`) and no leader pace control, causing accordion stretching across the zone. | Switched followers within 45yd to `BotMovement::Follow` with staggered formation slots, added leader pace waiting (`> 35yd` behind holds leader up to 6s), and cleanly dropped stragglers (`> 130yd` or `NavStatus::Stuck`). |
| 11 | **P1** | `EncounterHandlersHostile.cpp` (`UpdateHunterAndPrey`) | Prey fled along `hunter->GetOrientation()` and reset `goalId` every 2s, causing circular paths and defeating stuck detection. | Computed escape destination along the `hunter -> prey` vector (`ComputeVectorOffsetPoint`) with terrain height clamping and stable per-leg `navGoalId`. |
| 12 | **P1** | `EncounterNarrator.cpp` & `WorldEncounterDirector.cpp` | Narrator did not check `IsWithinLOSInMap`, could repeat consecutive line variants, and `RareHunter`/`BountyHunter`/`Nemesis` needed stronger deduplication/cooldown gates. | Added `IsWithinLOSInMap` and living source/target gates in `EncounterNarrator`, duplicate variant avoidance (`AvoidDuplicateVariant`), immediate bounty cooldown on start, 35% probability gate for `Nemesis`, and per-rare deduplication (`IsRareAlreadyTargeted` + `_rareCreatureCooldownUntilMs`). |

---

## 3. Post-Encounter Cleanup Invariant Validator (§2 & §3)

`EncounterCleanupDiagnostics.h` defines `EncounterCleanupSnapshot`, `EncounterCleanupVerdict`, and `EncounterCleanupValidator::Validate`, which verifies all 16 post-encounter invariants for every participant inside `FinalizeEncounter`:

1. `hasEncounterOwnership == false`
2. `inAnyEncounterParticipantList == false`
3. `hasStaleSelectionTarget == false`
4. `hasStalePvPCombatTarget == false`
5. `hasTemporaryEncounterGroup == false`
6. `currentMoveOwner != MoveOwner::Encounter`
7. `moveTokenStillValid == false`
8. `hasEncounterNavRequest == false`
9. `hasResidualEncounterStealth == false`
10. `hasResidualEncounterMorph == false`
11. `hasResidualSitOrKneelState == false`
12. `hasResidualSpellVisual == false`
13. `hasResidualTemporaryGameObject == false`
14. `hasResidualFactionOverride == false`
15. `pausedByEncounterFlag == false`
16. `worldTaskStuckPaused == false`

Upon completion of any encounter, `FinalizeEncounter` logs:
```text
Encounter #<id> <type> END reason=<reason> duration=<N>s cleanup=PASS
```
or, if any invariant is violated, logs an `ERROR` with the exact residual fields (`cleanup=FAIL details=...`) and increments `_metrics.cleanupFailures`.

---

## 4. Runtime Diagnostics & GM Commands (§3)

The `.botcmd encounter` command suite in `BotCommand.cpp` provides full live inspection and control:

- `.botcmd encounter status` — global metrics, update duration (`last/avg/peak` in microseconds), candidate scan count, active temporary GO count, cleanup pass/fail counters, and per-type breakdown.
- `.botcmd encounter list` — concise table of all currently active encounters and their participants.
- `.botcmd encounter bot [name]` — detailed per-bot diagnostic line (`encounterId`, `type`, `phase`, `role`, `target`, `map/zone`, `dist`, `moveOwner`, `combatTarget`, `timeoutLeft`, `prevTask`, `brainPaused`, `narrator`, `tempGroup`, `tempGO`, `stopReason`).
- `.botcmd encounter validate [name]` — runs `ValidateEncounterCleanup` on a specific bot or all owned bots.
- `.botcmd encounter start <type> [player]` — forces immediate start of any of the 13 encounter types.
- `.botcmd encounter stop <id|all>` — immediately terminates one or all active encounters and runs full cleanup validation.
- `.botcmd encounter cooldown reset` — clears all encounter, bot, player, rare-creature, and narrator cooldowns.
- `.botcmd encounter debug <on|off>` — toggles lifecycle completion logging.
- `.botcmd encounter reload` — hot-reloads `BotAI.DynamicEncounters.*` settings from `mod_coa_playerbots.conf`.

---

## 5. Verification & Test Suite Results (§25)

| Test Suite | Executable | Checks / Tests | Failures | Status |
|------------|------------|----------------|----------|--------|
| **Dynamic Bot Encounters Unit, Matrix, Torture & 5h Stress** | `encounter_tests.exe` | **9,621 checks** | **0** | **PASS** |
| **World Brain Pure Logic & Task Clocks** | `world_logic_tests.exe` | **487 checks** | **0** | **PASS** |
| **Locomotion & Navigation Arbitration** | `locomotion_tests.exe` | **224 checks** | **0** | **PASS** |
| **AzerothCore Server GTest Suite** | `unit_tests.exe` | **11,524 tests** | **0** | **PASS** |
| **Full Module Compilation Check (12 `.cpp` TU)** | `cl.exe /std:c++20` | **12 / 12 TUs** | **0 errors, 0 warnings** | **PASS** |

---

## 6. Remaining Known Limitations

1. **In-Memory Notoriety & Nemesis State:** By design, `EncounterMemoryStore` (Bounty notoriety and Nemesis-lite grudge records) is kept in memory with bounded capacity (`MAX_NEMESIS_PER_PLAYER = 16`) and automatic TTL decay rather than persisted to SQL tables, avoiding database write overhead on PvP kills.
2. **No Forced Bot Teleportation:** `WorldEncounterDirector` strictly selects real existing playerbots already within `searchRadiusYards` of the target player or world anchor; if no eligible bots are in the zone, candidate generation cleanly skips without spawning or teleporting bots.
