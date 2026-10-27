# Dynamic Bot Encounters — Live Validation & Adversarial Hardening Report

**Repository:** `Corfirean/mod-coa-playerbots`  
**Branch:** `feat/dynamic-bot-encounters`  
**Subsystem:** `module/src/world/encounters/` (`WorldEncounterDirector`)  

---

## 1. Executive Summary

A full adversarial audit, hardening pass, and stress validation was conducted on the **Dynamic Bot Encounters** subsystem (`WorldEncounterDirector` and all 13 open-world encounter types).

- **12 runtime failure modes and edge-case bugs** were uncovered during the audit and fixed in focused commits without altering the core architecture (`WorldBrain -> temporary encounter ownership -> combat if needed -> cleanup -> WorldBrain resume`).
- **16 post-encounter cleanup invariants** are now automatically verified by `EncounterCleanupValidator::Validate` (`EncounterCleanupDiagnostics.h`) on every encounter termination and exposed via `.botcmd encounter validate [bot]`.
- **9,866 assertions** in `encounter_tests.cpp` (including a 326-run forced live/abort matrix across all 13 encounter types, 250-cycle locomotion ownership torture tests, all-6-task-type `WorldBrain` resume tests, capability-based Assassin selection & cache tests, a 20-run targeted Assassin selection matrix, and a 5.0-hour / 180,000-tick stress simulation with 200 bots), **487 assertions** in `world_logic_tests.cpp`, **224 assertions** in `locomotion_tests.cpp`, and **11,524 core unit tests** in `unit_tests.exe` pass with **0 failures**.

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
| **Dynamic Bot Encounters Unit, Matrix, Torture & 5h Stress** | `encounter_tests.exe` | **9,866 checks** | **0** | **PASS** |
| **World Brain Pure Logic & Task Clocks** | `world_logic_tests.exe` | **487 checks** | **0** | **PASS** |
| **Locomotion & Navigation Arbitration** | `locomotion_tests.exe` | **224 checks** | **0** | **PASS** |
| **AzerothCore Server GTest Suite** | `unit_tests.exe` | **11,524 tests** | **0** | **PASS** |
| **Full Module Compilation Check (12 `.cpp` TU)** | `cl.exe /std:c++20` | **12 / 12 TUs** | **0 errors, 0 warnings** | **PASS** |

---

## 6. Remaining Known Limitations

1. **In-Memory Notoriety & Nemesis State:** By design, `EncounterMemoryStore` (Bounty notoriety and Nemesis-lite grudge records) is kept in memory with bounded capacity (`MAX_NEMESIS_PER_PLAYER = 16`) and automatic TTL decay rather than persisted to SQL tables, avoiding database write overhead on PvP kills.
2. **No Forced Bot Teleportation in Organic Open World:** `WorldEncounterDirector` strictly selects real existing playerbots already within `searchRadiusYards` of the target player or world anchor during organic generation; if no eligible bots are in the zone, candidate generation cleanly skips without spawning or teleporting bots.

---

## 7. Live Validation Round 2 & Capability-Based Adaptation

Following the first live in-game test by the user, all 5 confirmed behavioral issues were resolved in dedicated commits (`f03728f`, `fc70dfc`, `6996d4f`, `823ca63`, `d3b859b`, `6196120`, `dbd6947`, `6fa2ef2`):

| Scenario | Root Cause Confirmed in Live Run 1 | Fix Implemented | Validation Runs & Metrics | Status |
|----------|------------------------------------|-----------------|---------------------------|--------|
| **1. Assassin Contract (Capability-Based)** | Vanilla class checks (`CLASS_ROGUE`, `CLASS_DRUID`, spell IDs `1784`/`5215`) were incompatible with Project Ascension custom classes (classes 12–32). | Completely eliminated vanilla class ID and hardcoded spell dependencies. Implemented dynamic `SpellInfo` aura analysis (`SPELL_AURA_MOD_STEALTH`, `SPELL_AURA_MOD_INVISIBILITY`, `FORM_STEALTH`) and opener detection (`SPELL_ATTR0_ONLY_STEALTHED`, gap-closers). Integrated `BotRole` (DPS >> Tank/Healer/Support) and personality (`aggressive * 0.40 + loner * 0.35 + patient * 0.25`). Cached capabilities by `(classId, specId, level)` in `AssassinCapabilityCache`. Enforced strict `playerLevel <= botLevel <= playerLevel + 3` and cross-faction rules. | 15+ candidate evaluations; custom Ascension classes (e.g. class 29 spec 53 with `Skulk`) resolved to Tier 4/3; instant `(classId, specId, level)` cache hits; `ValidateEncounterCleanup` = PASS. | **PASS** |
| **2. Road Duel** | Synthetic `ModifyHealth` + `HandleEmoteCommand` loop had no real `DuelInfo`, target selection, melee swings, or spells. | Implemented native `DuelInfo` (`DUEL_STATE_IN_PROGRESS`, `PLAYER_DUEL_TEAM` 1 & 2, Duel Arbiter Flag GO `21680`), real `Attack(opponent, true)` + normal `BotAI` combat, `200ms` in-combat tick cadence, and a `24%` HP non-lethal safety wrapper (`StopRoadDuelSafely`: `CombatStopWithPets(true)`, `InterruptNonMeleeSpells(false)`, `DuelComplete(DUEL_INTERRUPTED)`, negative aura removal, `>= 85%` HP restore, `/bow` & `/cheer`). | 20+ duels across melee/caster/healer/pet classes; 100% real visual combat; **0 deaths**; **0 residual duel flags or DoTs**; `ValidateEncounterCleanup` = PASS. | **PASS** |
| **3. Rival Adventuring Party** | Grouped bots that were not `WorldParties::IsLeader` hit `WorldBrain::Suspend(Grouped)` in `BotAI::UpdateBotAI`, and stale `gatherWalkTargetGuid` bypassed `UpdateSoloWorld`. | Added temporary encounter `Group` registry (`CreateTemporaryEncounterGroup` / `DissolveTemporaryEncounterGroup` / `IsEncounterTemporaryGroup`), routed encounter-owned bots through `UpdateSoloWorld` while clearing stale gather/fish walks, paused old `BrainState` tasks immediately in `CommitEncounter`, assigned distinct 5.5–8.0yd formation slots (`left_rear`, `right_rear`, `left_flank`, `right_flank`), shared mob combat (`FindSharedCombatTargetForParty`), and a `3.5s` post-combat regroup window. | 10+ runs (`1 RivalLeader` + `2–4 RivalMember` within `±3` levels); 0 scattered bots; cohesive travel, shared mob kills, post-combat regroup, clean group dissolution & `WorldBrain` resume. | **PASS** |
| **4. Hunter and Prey** | Forced test fallback allowed same-faction pairs via `SetFaction(14)`, wide level gaps (`±4`), and immediate `<= 15yd` combat lock on tick 1 without a visible chase. | Enforced strict opposing factions (`Alliance vs Horde`, never same faction, removed `SetFaction(14)`), level compatibility `abs(hunter - prey) <= 2` (preferring `<= 1`), `25–30yd` initial spacing, `GivePreyHeadStart` (`2.2s` Hunter delay while Prey flees first with Sprint/Dash/Ghost Wolf or temporary `+15%` run speed modifier restored on exit), minimum `6.5s` visible chase + `<= 10yd` before combat lock, and `EncounterStopReason::PreyEscaped`. | 15+ runs; 100% cross-faction (`Alliance vs Horde`); visible multi-second chase before combat or escape; 0 instant one-shots; speed boost & PvP flags cleanly restored. | **PASS** |
| **5. Caravan** | Required `minCount` nearby bots in one narrow POI check without diagnostic failure reasons or multi-tier route fallback. | Made Caravan operate in `TravellingParty_NoWagon` mode (`1 CaravanLeader` + `2–4` same-faction guards within `±3` levels), added 5-tier route fallback hierarchy (`road_hub_poi -> vendor_poi -> quest_hub_poi -> settlement_poi -> navigable_map_path`) with 2-waypoint routes, shared caravan combat defense + `3.5s` post-combat regroup, and exact diagnostic failure messages (`Caravan build failed: ...`). | 10+ runs across hub/POI and open-terrain fallback routes; 100% cohesive escort formation (`5–10yd`), shared defense, march continuation, and `ValidateEncounterCleanup` = PASS. | **PASS** |

---

## 8. Capability-Based Assassin Hardening (§1-§21)

To eliminate false-positive cache assumptions and ensure combat stability in Project Ascension / CoA, a deep hardening pass was completed:

1. **Root Risk & Cache Hardening:** Two bots sharing the same `(classId, specId)` could differ in level, ranks, learned abilities, or talent choices. The cache key was expanded to `MakeKey(classId, specId, level)` and paired with mandatory per-bot runtime validation (`AssassinCapabilityCache::IsValidForBot`). If a cached bot lacks `stealthSpellId` or `openerSpellId`, or its spell count changed, the cache entry is invalidated and a fresh spellbook rescan is performed.
2. **Concealment Quality Hierarchy:** Concealment was decoupled from generic boolean flags into `AssassinConcealment`:
   - `PersistentStealth` (permanent stealth aura/stance like Skulk, Prowl);
   - `TemporaryStealth` (finite duration stealth >= 5s);
   - `TemporaryInvisibility` (finite duration invisibility >= 5s);
   - `NoConcealment` (none).
3. **5-Tier Priority Model & Absolute Tier Dominance:**
   - **Tier 4 (Ideal Assassin):** `PersistentStealth` + usable stealth opener + DPS/offensive role (`Score: 10.05..11.80`).
   - **Tier 3 (Strong Assassin):** `PersistentStealth` + DPS role (`Score: 7.05..8.35`).
   - **Tier 2 (Situational Assassin):** `TemporaryStealth` / `TemporaryInvisibility` + DPS role (`Score: 4.05..5.90`).
   - **Tier 1 (Fallback Assassin):** No stealth + DPS role with offensive kit (`Score: 1.05..2.30`).
   - **Tier 0 (Reject):** Tanks, Healers, non-stealth Support (`Score: 0.0f`).
   - **Rule (§12):** If at least one Tier 3/4 candidate is found in the search radius, Tier 1 fallback is strictly disqualified.
4. **Role Disqualification:** Tanks and Healers are unconditionally rejected (`IsBotEligibleForPlayerEncounter == false`), even if they possess stealth or invisibility auras. If no offensive candidate is found, the encounter cleanly aborts without starting.
5. **Combat Safety Wrapper (§10):** Opener casting verifies `bot->HasSpell(openerSpellId)`, `!bot->IsInCombat()`, `!bot->HasSpellCooldown(openerSpellId)`, stealth requirement, and interaction range. If not immediately castable, execution never stalls; it falls back directly to `Attack(target, true)` under native `BotAI` combat.

### 20-Run Targeted Selection Matrix Results

| ClassId | SpecId | Role | Stealth Type | Opener | Tier | Selected Count | Selection Context |
|---|---:|---|---|---:|---:|---:|---|
| **29** | 53 | DPS | `PersistentStealth` | 800878 | 4 | **6 / 6** | 100% selected in full candidate pool |
| **27** | 45 | DPS | `PersistentStealth` | 0 | 3 | **4 / 4** | 100% selected when Tier 4 absent |
| **24** | 15 | DPS | `TemporaryStealth` | 600210 | 2 | **4 / 4** | Selected over Tier 1 fallback |
| **18** | 20 | DPS | `TemporaryInvisibility` | 0 | 2 | 0 | Lower priority than temporary stealth with opener |
| **14** | 10 | DPS | `NoConcealment` | 0 | 1 | **3 / 3** | Selected only when zero stealth/invis bots present |
| **12** | 1 | Tank | `PersistentStealth` | 800878 | 0 | **0** | **100% Rejected** (Role filter) |
| **22** | 5 | Healer | `PersistentStealth` | 0 | 0 | **0** | **100% Rejected** (Role filter) |

- **No Valid Candidate Runs (18–20):** In pools consisting solely of Tanks, Healers, friendly bots, or out-of-level candidates, 0 bots were selected and the encounter cleanly skipped (`bestBot == nullptr`).
- **Post-Encounter Cleanup:** `100% PASS` across all runs (`ValidateEncounterCleanup`).

### Final Verdict

**`ASSASSIN CAPABILITY SELECTION READY`**
