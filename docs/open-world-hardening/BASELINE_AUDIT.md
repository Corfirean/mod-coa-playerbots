# Phase 0 Baseline Audit — CoA Playerbots Open-World Autonomous Intelligence & Navigation Hardening

**Repository:** `mod-coa-playerbots`  
**Branch:** `feat/open-world-autonomous-hardening` (based on `origin/master` @ `4f6cf99`)  
**Audit Date:** 2026-11-02

---

## 1. End-to-End Execution Flow Trace

### 1.1 Primary Autonomous Decision & Locomotion Chain

```
BotAI::Update (module/src/BotAI.cpp:5381)
 └─► BotMovement::Update (module/src/BotMovement.cpp:1335)
 │    └─► Samples stationary/swimming state, updates safe-position history ring (every 2000 ms / 4 yd),
 │        prunes expired no-path cache entries (every 10000 ms).
 └─► Role Dispatch: UpdateOffensive / UpdateHealer / UpdateSupport
      └─► If dead/ghost: UpdateDeathHandling (BotAI.cpp:5115) -> WorldBrain::OnDeath -> MoveOwner::Corpse
      └─► If in combat / valid target:
      │    ├─► StuckDetector::Update (BotAI.cpp:4275) -> 90 s target ignore on 9 s zero-progress non-attacker
      │    ├─► BotFlee::Evaluate / Drive (BotFlee.cpp:164) -> MoveOwner::Avoidance
      │    ├─► CastGuard::IsCurrentlyCasting / EvaluatePreemption (BotAI.cpp:4316)
      │    ├─► BotAvoidance::TryAvoidGroundHazards / TryAvoidBossTelegraphedAttacks -> MoveOwner::Avoidance
      │    ├─► Positioning: BotMovement::Chase / MoveTo (MoveOwner::Combat)
      │    └─► CombatUtility -> DpsEngine / TankEngine / HealerEngine -> Legacy rotation fallback
      └─► If out of combat & no target:
           ├─► Release CHASE_MOTION_TYPE (BotAI.cpp:4187) [DEFECT: misses POINT_MOTION_TYPE & Avoidance]
           ├─► TryProcessPendingLoot (BotAI.cpp:1796) -> MoveOwner::Loot
           ├─► TryRestIfNeeded (BotAI.cpp:1903) -> ForceStopOwner(MoveOwner::Travel)
           └─► Solo / WorldParty Leader / Encounter Bot:
                ├─► In-flight gather/fish completion (TryFinishGathering, TryContinueGatherWalk, TryFinishFishing)
                └─► UpdateSoloWorld (BotAI.cpp:3841)
                     ├─► GuildGatherOrder override -> WorldBrain::Suspend(ManualCommand)
                     ├─► BotEconomy::UpdateTrip -> WorldBrain::NotifyAmbientBusy
                     ├─► BotWorldBehavior::UpdateBeforeSolo -> WorldBrain::NotifyAmbientBusy
                     └─► WorldBrain::Update (module/src/world/WorldBrain.cpp:477)
                          ├─► WorldEncounterDirector::UpdateBot -> PauseTask / MoveOwner::Encounter
                          ├─► Social cooperation: TryResurrectNearby / TryHelpNearby -> PauseTask
                          ├─► Opportunistic gather detour: UpdateOpportunity -> PauseTask / MoveOwner::Gather
                          ├─► Active Task Execution: WorldExecutor::UpdateTask (WorldExecutor.cpp:200)
                          │    ├─► QuestAccept / QuestTurnIn -> QuestInteraction::UpdateNpcTask
                          │    ├─► QuestObjective -> QuestExecutor::UpdateObjective
                          │    │    ├─► KillObjectiveHandler / LootItemObjectiveHandler / CastObjectiveHandler / TalkObjectiveHandler
                          │    │    │    └─► CreatureTargetHandler::Update (CreatureTargetHandler.cpp:203)
                          │    │    ├─► UseGameObjectObjectiveHandler / LootGameObjectObjectiveHandler / CastOnObjectObjectiveHandler
                          │    │    │    └─► ObjectTargetHandler::Update (ObjectTargetHandler.cpp:94)
                          │    │    └─► ExploreObjectiveHandler::Update (ExploreObjectiveHandler.cpp:40)
                          │    └─► Travel -> UpdateTravel (WorldExecutor.cpp:25)
                          └─► Planner Re-evaluation (when idle or nextPlanMs elapsed):
                               └─► WorldPlanner::ChooseTask (WorldPlanner.cpp:620)
                                    ├─► PlanTurnIns / PlanObjectives / PlanAccepts / PlanRoute
                                    └─► Fallback directive: Gather / Fish / Grind / Ambient
```

### 1.2 Locomotion & Recovery Chain (`WorldExecutor::TravelTo` -> `BotMovement::Navigate`)

```
WorldExecutor::TravelTo (module/src/world/WorldExecutor.cpp:89)
 ├─► DangerMap::Plan (module/src/world/DangerMap.cpp:198) -> up to 3 grid waypoints (80 yd cells)
 └─► BotMovement::Navigate (module/src/BotMovement.cpp:623)
      ├─► NavigateTransaction::ValidatePreconditions (owner priority, planningEpoch, requestGeneration)
      ├─► External control / MountCasting hold -> req.progress.Hold(now)
      ├─► Arrival check: Dist2d(bx, by, x, y) <= acceptRadius [DEFECT: 2D-only, ignores Z]
      ├─► New or changed goal:
      │    ├─► RequestMount if dist >= mountDist
      │    └─► IssueLeg (BotMovement.cpp:315)
      │         ├─► If dist > 120 yd: straight-line interpolated MoveToInternal [DEFECT: blind 2D guess]
      │         ├─► Fallback 1: full-goal MoveToInternal [DEFECT: fails >250 yd]
      │         └─► Fallback 2: RouteWaypoint + MoveToInternal
      └─► Ongoing leg monitoring:
           ├─► Leg arrival -> IssueLeg for next leg
           └─► NavProgress::Update(distToGoal2d, distZ, ...) (module/src/BotNavProgress.h:83)
                └─► If stalled >= 9000 ms -> AdvanceRecovery (None -> Repath -> DetourLeft -> DetourRight -> Backtrack -> Stuck)
```

---

## 2. Verification of the 7 Known Audit Targets

| # | Audit Target | Verdict | Summary of Evidence |
|---|---|---|---|
| 1 | `NavProgress` planar/vertical distance progress misclassification when moving away from final destination | **CONFIRMED DEFECT (P0)** | `NavProgress::Update` (`BotNavProgress.h:83-114`) called at `BotMovement.cpp:881` only passes `distToGoal = Dist2d(bx, by, x, y)` and `distZ = std::fabs(bz - z)` to the **final destination**. Legitimate movement along a navmesh corridor (`req.legX, req.legY`) or detour that temporarily moves perpendicular to or away from `(x, y)` records 0 progress for 9 s and triggers false `Repath` -> `DetourLeft` -> `DetourRight` -> `Backtrack`. |
| 2 | Recovery ladder (`Repath`, `DetourLeft`, `DetourRight`, `Backtrack`, `Stuck`) strategy diversity & validity | **CONFIRMED DEFECT (P0)** | `DetourLeft` (+60° 15 yd) and `DetourRight` (-60° 15 yd) (`BotMovement.cpp:898-935`) compute unvalidated 2D offsets relative to the goal direction without checking navmesh reachability first, and while walking toward the detour point `Navigate` still measures progress by reduction of Euclidean distance to the *final goal* (which a 60° lateral detour rarely reduces by >= 2 yd), guaranteeing escalation to `Backtrack` / `Stuck`. |
| 3 | `DangerMap::Plan` coarse risk grid waypoint reachability & Z-coordinate validity | **CONFIRMED DEFECT (P0)** | `DangerMap::Plan` (`DangerMap.cpp:198-325`) returns 2D `(x, y)` cell centers (80 yd cells) with no Z coordinate or navmesh check. `WorldExecutor::TravelTo` (`WorldExecutor.cpp:118`) passes `tz = viaWaypoint ? bot->GetPositionZ() : z`. On sloped/mountainous/ravine terrain, `ResolveGroundZ` at `bot->GetPositionZ()` fails or snaps to the wrong floor. |
| 4 | `WorldExecutor::TravelTo` disabling danger route after waypoint failure and falling back toward original destination | **CONFIRMED DEFECT (P0)** | When a waypoint returns `NavStatus::Blocked` for >4 s or `NavStatus::Stuck` (`WorldExecutor.cpp:131-147`), `TravelTo` sets `route.disabled = true` (abandoning remaining valid waypoints instead of advancing `++route.next`) and walks straight through the high-danger camp (`AVOID_RISK`). Worse, if `sub` changes (e.g. `Area` -> `Approach` -> `Area`), `route.key` changes and `TravelTo` re-plans the exact same unreachable waypoint. |
| 5 | `BotMovement::MoveToInternal` path validation, failure classification, and cross-bot no-path queries | **CONFIRMED DEFECT (P0)** | `MoveToInternal` (`BotMovement.cpp:494-614`) has a per-bot 8-slot `NoPathEntry` cache (4 s TTL), but zero cross-bot static unreachable-endpoint caching. Moreover, `IssueLeg` (`BotMovement.cpp:315-372`) can trigger up to **3 `PathGenerator` calls + 1 `RouteWaypoint` call on a single tick** for long routes because it tries a blind 120 yd straight-line point first, then full destination, then `RouteWaypoint`. |
| 6 | Distinction between `Blocked`, `Superseded`, `Stuck`, temporary external control, normal movement pause, and unreachable destinations | **CONFIRMED DEFECT (P0)** | `NavStatus` (`BotMovement.h:94-101`) conflates higher-priority `MoveOwner` preemption (`Blocked`), temporary cast/root pause (`Blocked`), and initial pathfinding failure (`IssueLeg` failure -> `Blocked`, `BotMovement.cpp:790`). Furthermore, `UNIT_STATE_ROOT` and active non-melee casts (`IsNonMeleeSpellCast`) are **not** held via `req.progress.Hold(now)` in `Navigate` (`BotMovement.cpp:657-668`), causing rooted or casting bots to burn their stall timer and trigger false recovery! |
| 7 | Preservation of movement ownership, request generations, planning epochs, command IDs, and transactional dispatch | **CONFIRMED DEFECT (P0)** | While `LocomotionStateStore` and `NavigateTransaction` preserve epochs/generations inside `BotMovement`, two critical ownership leaks exist in callers: (1) `BotAvoidance` claims `MoveOwner::Avoidance` (priority 120) and **never releases it**, locking out `MoveOwner::Combat` (110) and all world owners for `STALE_CLAIM_MS` (15 s); (2) `BotAI::UpdateOffensive` (`BotAI.cpp:4187`) only releases `MoveOwner::Combat` if `GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE`, leaking `MoveOwner::Combat` for 15 s whenever combat ends during a `POINT_MOTION_TYPE` reposition. |

---

## 3. Detailed Catalogue of Confirmed Defects

### Defect #1 (P0): Route-Blind Progress & Switchback Misclassification in `NavProgress` and `BotMovement::Navigate`
- **Locations:**
  - `module/src/BotNavProgress.h:83-114` (`NavProgress::Update`)
  - `module/src/BotMovement.cpp:881-965` (`BotMovement::Navigate`)
- **Root Cause:**
  `NavProgress::Update(float dist2d, float distZ, float bx, float by, uint32 nowMs)` only considers a bot to have made progress if `dist2d <= bestDist2d - minProgressYards` (2.0 yd) or `distZ <= bestDistZ - minProgressZ` (1.5 yd) relative to the **final destination** `(req.x, req.y, req.z)`. When a bot follows a valid navmesh route around a mountain, wall, building, or switchback ramp (via `RouteWaypoint` leg `(req.legX, req.legY, req.legZ)` or multi-point spline), its Euclidean distance to the final goal temporarily increases or stays flat for >9 seconds (`stallMs = 9000`). `NavProgress::Update` returns `true` (stalled), and `Navigate` yanks the bot off its valid path into `Repath`, `DetourLeft`, `DetourRight`, and `Backtrack`.
- **Severity:** **P0 (Critical)**
- **Proposed Correction:**
  Extend `NavProgress` (`BotNavProgress.h`) to be **route- and leg-aware**: track progress toward the active corridor waypoint/leg `(legDist2d, legDistZ)` as well as actual displacement along the planned leg vector, while bounding total leg time and cumulative path distance so genuine loops between waypoints are still detected and escalated.
- **Reproducible Test:**
  `TestRouteAwareSwitchbackProgress` in `locomotion_tests.cpp` / `open_world_hardening_tests.cpp`: simulate a bot walking a 120-yard switchback leg around a ridge where `distToGoal2d` increases from 80 yd to 110 yd over 12 seconds while `legDist2d` decreases monotonically from 60 yd to 5 yd; verify zero false stall/recovery triggers.

---

### Defect #2 (P0): 2D-Only Arrival Validation Causing Multi-Level False Arrival & Approach Loops
- **Locations:**
  - `module/src/BotMovementPrimitives.h:168-174` (`NavigateTransaction::ValidateArrival`)
  - `module/src/BotMovement.cpp:678, 868` (`BotMovement::Navigate`)
  - `module/src/world/WorldExecutor.cpp:32-33` (`UpdateTravel`)
  - `module/src/world/QuestInteraction.cpp:353-355` (`UpdateNpcTask`)
  - `module/src/world/objectives/ObjectiveCommon.cpp:176-179` (`InArea`)
  - `module/src/world/objectives/ObjectTargetHandler.cpp:134-149` (`ObjectTargetHandler::Update`)
- **Root Cause:**
  `BotMovement::Navigate` checks `if (distToGoal <= acceptRadius)` where `distToGoal = Dist2d(bx, by, x, y)`, completely ignoring `distZ = std::fabs(bz - z)`. In multi-story buildings, caves beneath surface terrain, bridges, towers, or mines where the bot is horizontally within `acceptRadius` of `(x, y)` but 15–40 yards above or below `z`, `Navigate` immediately returns `NavStatus::Arrived`.
  In `ObjectTargetHandler::Update` (`ObjectTargetHandler.cpp:134-149`), `InUseRange` uses 3D `go->IsWithinDistInMap(ctx.bot, UseRange)` (which is `false` due to Z separation), so it calls `TravelTo(..., UseRange - 1.5f)`, which calls `Navigate` — and `Navigate` immediately returns `NavStatus::Arrived` every tick until the 25-second `approachTimeoutMs` expires!
- **Severity:** **P0 (Critical)**
- **Proposed Correction:**
  Require bounded vertical proximity (`distZ <= MaxArrivalZDelta(acceptRadius)`) in `NavigateTransaction::ValidateArrival`, `BotMovement::Navigate` goal and leg arrival checks, `QuestInteraction::UpdateNpcTask`, and `ObjectiveCommon::InArea`. In `ObjectTargetHandler` and `CreatureTargetHandler`, if `TravelTo` reports `Arrived` while 3D interaction range / LOS is still unsatisfied, tighten the approach radius down to melee/interaction tolerance and, if still vertically separated or unreachable, fail the target into `FailureMemory` immediately rather than spinning until timeout.
- **Reproducible Test:**
  `TestMultilevelVerticalArrivalRejection` in `locomotion_tests.cpp`: place bot at `(100, 100, 10)` and target at `(101, 100, 32)` with `acceptRadius = 4.0f`; verify `ValidateArrival` and `Navigate` return `false` / `Moving` instead of `Arrived`.

---

### Defect #3 (P0): Redundant Multi-Query Pathfinding in `IssueLeg` & Conflation of `Blocked` vs `Unreachable`
- **Locations:**
  - `module/src/BotMovement.cpp:315-372` (`IssueLeg`)
  - `module/src/BotMovement.cpp:782-791` (`BotMovement::Navigate`)
  - `module/src/world/objectives/ObjectiveCommon.cpp:198-206, 421-428` (`TravelToArea`, `Approach`)
- **Root Cause:**
  1. When `dist > LEG_YARDS` (120 yd), `IssueLeg` first guesses a straight-line 2D point `(tx, ty, bot->GetPositionZ())` 120 yards toward the goal and runs `MoveToInternal` (`PathGenerator::CalculatePath`). In caves, mountains, indoor complexes, or across rivers/cliffs, that straight-line point frequently lies in unwalkable terrain or at the wrong elevation. When it fails, `IssueLeg` runs a *second* `MoveToInternal` to the full destination `(req.x, req.y, req.z)` (which always fails with `PATHFIND_NOPATH` when `dist > ~250 yd`), and only *then* calls `RouteWaypoint` (a *third* navmesh query) followed by a *fourth* `MoveToInternal`!
  2. When `IssueLeg` fails on a new request (`BotMovement.cpp:782-791`), `Navigate` erases the request and returns `NavStatus::Blocked` — the exact same enum value returned when a higher-priority `MoveOwner` holds the movement slot or when the bot is stunned/casting. Callers (`TravelToArea`, `Approach`, `ObjectTargetHandler`, `QuestInteraction`) treat `NavStatus::Blocked` as "still running" and retry `Navigate` endlessly until phase timeout (25 s–360 s).
- **Severity:** **P0 (Critical)**
- **Proposed Correction:**
  1. In `IssueLeg`, when `dist > LEG_YARDS`, query `RouteWaypoint` first to obtain a navmesh-corridor-aligned waypoint with a valid navmesh Z height before falling back to geometric interpolation, cutting per-leg `PathGenerator` calls from up to 4 down to 1–2.
  2. Track consecutive `IssueLeg` path failures on the persistent `MovementRequest` (or via a bounded failure counter per goal) and escalate genuine repeated path generation failures to `NavStatus::Stuck` (with failure classification `PathStatus::NoPath` / `Unreachable`) after a bounded retry budget, while reserving `NavStatus::Blocked` strictly for legitimate preemption/holds.
  3. Add a bounded cross-bot static unreachable-endpoint cache so multiple bots do not repeatedly run expensive `PathGenerator` queries to the exact same unreachable coordinate cell.
- **Reproducible Test:**
  `TestLongRouteLegQueryCount` and `TestUnreachableGoalEscalation` in `locomotion_tests.cpp`.

---

### Defect #4 (P0): `UNIT_STATE_ROOT` and Active Non-Melee Casts Advance Stall Timer in `BotMovement::Navigate`
- **Locations:**
  - `module/src/BotMovement.cpp:395-398` (`IsExternallyControlled`)
  - `module/src/BotMovement.cpp:504-508` (`MoveToInternal`)
  - `module/src/BotMovement.cpp:657-668` (`BotMovement::Navigate`)
- **Root Cause:**
  `IsExternallyControlled` checks `UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING | UNIT_STATE_IN_FLIGHT | UNIT_STATE_STUNNED` but omits `UNIT_STATE_ROOT`. Similarly, `MoveToInternal` refuses to move while `bot->IsNonMeleeSpellCast(false, false, true)` is true (`BotMovement.cpp:504`), but `Navigate` (`BotMovement.cpp:657-668`) only calls `req.progress.Hold(now)` for `IsExternallyControlled` and `MountState::MountCasting`.
  Therefore, if a bot is rooted by a frost nova / entangling roots / net or pauses to cast/channel a spell during `Navigate`, `MoveToInternal` refuses to move while `Navigate` continues ticking `req.progress.Update(...)`, triggering false stall detection and recovery stages!
- **Severity:** **P0 (High)**
- **Proposed Correction:**
  Include `UNIT_STATE_ROOT` and active non-melee spell casting (`bot->IsNonMeleeSpellCast(false, false, true)`) in `Navigate`'s hold condition (`req.progress.Hold(now)`), and expose the exact hold/wait reason in `BotMovement::Describe`.
- **Reproducible Test:**
  `TestRootAndCastHoldProgressClock` in `locomotion_tests.cpp`: hold a bot in `UNIT_STATE_ROOT` or casting for 15 seconds mid-route; verify `NavProgress` does not trigger stall/recovery and resumes cleanly afterward.

---

### Defect #5 (P0): `MoveOwner::Avoidance` and `MoveOwner::Combat` Ownership Leaks Blocking Post-Combat Locomotion for 15 Seconds
- **Locations:**
  - `module/src/BotAvoidance.cpp:415, 443, 471, 485` (`TryAvoidGroundHazards`, `TryAvoidBossTelegraphedAttacks`)
  - `module/src/BotAI.cpp:4187-4188` (`UpdateOffensive`)
  - `module/src/BotAI.cpp:4950-4951` (`UpdateHealer`)
  - `module/src/BotMovement.cpp:173-179` (`CanTakeOver`)
- **Root Cause:**
  1. `BotAvoidance` issues `BotMovement::MoveTo(bot, MoveOwner::Avoidance, ...)` (`OwnerPriority = 120`, highest in the hierarchy). Unlike `BotFlee::Drive` (which calls `ForceReleaseOwner(bot, MoveOwner::Avoidance)` on exit), `BotAvoidance` **never** releases `MoveOwner::Avoidance` once the bot steps out of the hazard or the boss cast finishes! For the next `STALE_CLAIM_MS = 15000` (15 seconds), `CanTakeOver` rejects `MoveOwner::Combat` (110), `Loot` (80), `Quest` (60), `Travel` (50), etc.
  2. In `BotAI::UpdateOffensive` (`BotAI.cpp:4187`), when combat ends (`!target || !target->IsAlive()`), it only calls `BotMovement::ForceStopOwner(bot, MoveOwner::Combat)` if `bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE`. Whenever combat used `MoveTo(bot, MoveOwner::Combat, ...)` (ranged dead-zone backoff at `BotAI.cpp:4495`, or `HandleSpellCastFailure` `MoveForwards`/`MoveBackwards` at `BotClassRotations.cpp:1571, 1600, 1620`), the generator is `POINT_MOTION_TYPE` (or `IDLE_MOTION_TYPE` once finished), so `s.active.owner` remains `MoveOwner::Combat` (110) and blocks `Loot` (80), `Quest` (60), `Travel` (50), `Gather` (30), `Grind` (20), and `Ambient` (10) for up to 15 seconds!
- **Severity:** **P0 (Critical)**
- **Proposed Correction:**
  1. When `BotAvoidance` is not actively avoiding a ground hazard or boss telegraph (and `!state.flee.fleeing`), release `MoveOwner::Avoidance` once the avoidance point move finishes or hazard severity is `None`.
  2. When combat ends (`!target || !target->IsAlive() || !bot->IsValidAttackTarget(target)`), always release `MoveOwner::Combat` (and `MoveOwner::Avoidance` if `!state.flee.fleeing`) if `BotMovement::CurrentOwner(bot)` is `Combat` or `Avoidance`, regardless of the underlying `MotionMaster` generator type.
- **Reproducible Test:**
  `TestAvoidanceAndCombatOwnershipRelease` in `locomotion_tests.cpp`: verify that after a `MoveOwner::Avoidance` hazard step or a `MoveOwner::Combat` `MoveMode::Point` reposition, `MoveOwner::Combat` / `MoveOwner::Loot` / `MoveOwner::Quest` can immediately acquire locomotion without waiting 15 seconds.

---

### Defect #6 (P0): Ranged/Caster Combat LOS Deadlock (`ReadyToCast` Halts LOS Repositioning & `Chase` Overwrites `MoveForwards`)
- **Locations:**
  - `module/src/BotAI.cpp:4483-4505` (`UpdateOffensive`)
  - `module/src/engine/CombatMovement.cpp:14-33` (`CombatMovement::ReadyToCast`)
  - `module/src/BotClassRotations.cpp:1609-1624` (`HandleSpellCastFailure`)
  - `module/src/engine/ActionEvaluator.cpp:492-500` (`ActionEvaluator::CanCast`)
- **Root Cause:**
  When a ranged/caster bot (`preferredDist = 22 yd`) has a target at 18 yards behind a wall, tree, or ridge (`!bot->IsWithinLOSInMap(target)`):
  1. `UpdateOffensive` (`BotAI.cpp:4500`) calls `BotMovement::Chase(bot, MoveOwner::Combat, target, ChaseRange(15.4f, 22.0f))` without checking `bot->IsWithinLOSInMap(target)` (unlike `UpdateHealer` at `BotAI.cpp:4935`, which explicitly checks `!bot->IsWithinLOSInMap(healTarget)`).
  2. When a spell fails with `SPELL_FAILED_LINE_OF_SIGHT`, `HandleSpellCastFailure` (`BotClassRotations.cpp:1620`) calls `BotMovement::MoveForwards(bot, MoveOwner::Combat, target, 8.0f)` (`MoveMode::Point`).
  3. On the very next tick (100 ms later), `UpdateOffensive` calls `BotMovement::Chase` at line 4500, which overwrites `MoveMode::Point` with `MoveMode::Chase(15.4f, 22.0f)`!
  4. Even worse: while the bot is moving to clear LOS, `DpsEngine::Execute` selects another cast-time spell (because `ActionEvaluator::CanCast` does not check `IsWithinLOSInMap`) and calls `CombatMovement::ReadyToCast` (`CombatMovement.cpp:31`), which calls `BotMovement::ForceStopOwner(bot, MoveOwner::Combat)` — stopping the bot before it reaches line of sight!
- **Severity:** **P0 (Critical)**
- **Proposed Correction:**
  1. In `UpdateOffensive`, check `!bot->IsWithinLOSInMap(target)`: when out of LOS, chase into close LOS range (`ChaseRange(0.0f, MELEE_ENGAGE_RANGE)`) rather than holding at `[0.7 * preferredDist, preferredDist]`.
  2. In `CombatMovement::ReadyToCast` (and/or `UpdateOffensive`), never call `ForceStopOwner(bot, MoveOwner::Combat)` to stop for a cast-time spell if the bot lacks line of sight to its target!
- **Reproducible Test:**
  `TestRangedCasterLosRepositionNotHalted` in `world_logic_tests.cpp` / `locomotion_tests.cpp`.

---

### Defect #7 (P0): `DangerMap::Plan` & `WorldExecutor::TravelTo` Waypoint Height Mismatch and Premature Route Abandonment
- **Locations:**
  - `module/src/world/DangerMap.cpp:198-325` (`DangerMap::Plan`)
  - `module/src/world/WorldExecutor.cpp:89-166` (`WorldExecutor::TravelTo`)
- **Root Cause:**
  1. `DangerMap::Plan` computes waypoints in 2D grid coordinates (80-yard cells). `WorldExecutor::TravelTo` (`WorldExecutor.cpp:118`) passes `tz = viaWaypoint ? bot->GetPositionZ() : z` to `BotMovement::Navigate`. When a waypoint is 150–250 yards away across hilly terrain, `bot->GetPositionZ()` can be 30–80 yards off the terrain height at `(tx, ty)`, causing `ResolveGroundZ` (`MAX_GROUND_SNAP_Z = 50.0f`) to reject the height or snap to an underground cave.
  2. When a single waypoint fails (`status == NavStatus::Stuck` or `Blocked` for >4000 ms), `WorldExecutor::TravelTo` (`WorldExecutor.cpp:139`) sets `route.disabled = true`, discarding all remaining waypoints and walking straight through the high-risk danger zone.
  3. `route.key` hashes `sub`, so transitioning between `WorldGoalSub::Area` and `WorldGoalSub::Approach` resets `state.travelRoute` and re-attempts the same failed waypoint.
- **Severity:** **P0 (High)**
- **Proposed Correction:**
  1. Interpolate waypoint Z height between `bot->GetPositionZ()` and destination `z` (and resolve via `Map::GetHeight` / navmesh in `WorldExecutor::TravelTo` before dispatching).
  2. When a waypoint fails, record that waypoint cell in `FailureMemory` (`FailKind::NavCorridor`) and advance `++route.next` to try the next waypoint before disabling the entire danger route.
  3. Key `TravelRoute` on quantized destination coordinates and mapId (independent of `sub`) so `disabled` / waypoint progress persists across minor sub-goal changes toward the same destination.
- **Reproducible Test:**
  `TestDangerMapWaypointAdvanceAndHeightInterpolation` in `world_logic_tests.cpp`.

---

### Defect #8 (P0): Missing Decision Hysteresis & Behavioral Loop / Deadlock Detection in `WorldPlanner` and `WorldBrain`
- **Locations:**
  - `module/src/world/WorldPlanner.cpp:620-666` (`ChooseTask`)
  - `module/src/world/WorldPlanner.cpp:166-215` (`PickArea`)
  - `module/src/world/WorldBrainState.h:155-205` (`BrainState`)
- **Root Cause:**
  1. `WorldPlanner::ChooseTask` scores all candidate tasks from scratch on every planner pass without giving a commitment/hysteresis bonus to the quest chain or area the bot just worked on. When two objectives or areas have utility scores within jitter/distance noise (~5–15 points) of each other, a bot that pauses or finishes one step can oscillate between regions.
  2. There is no bounded **Behavioral Loop Detector** monitoring repeated unproductive cycles (e.g., `TravelToArea -> Search -> Approach -> TravelToArea` repeating on the same target/area without advancing objective counters, or repeated task switches between two quest IDs without completion).
- **Severity:** **P0 (High)**
- **Proposed Correction:**
  1. Add a configurable **Decision Hysteresis** bonus (`hysteresisSameQuestBonus`, `hysteresisSameAreaBonus`) in `WorldPlanner::ChooseTask` and `PickArea` so bots maintain commitment to their active quest/zone unless an alternative exceeds the current context by a meaningful margin.
  2. Implement a pure, header-testable `BehavioralLoopDetector` inside `BrainState` that tracks a bounded ring of recent action/target/area transitions and detects:
     - Approach-without-execute loops on the same target/object
     - Area/task ping-pong oscillation (`A -> B -> A -> B` without progress)
     - Repeated recovery/stall cycles in the same 3D grid cell
     When triggered, it records a strike in `FailureMemory` (`FailKind::Target`, `FailKind::Cluster`, or `FailKind::NavCorridor`), increments `WorldMetrics::loopBreakEvents`, and forces an alternative selection.
- **Reproducible Test:**
  `TestDecisionHysteresisPreventsOscillation` and `TestBehavioralLoopDetectorBreaksPingPong` in `world_logic_tests.cpp`.

---

### Defect #9 (P1): Unbounded Wait on Scripted/Unspawned Quest NPCs & Unreachable Elevated Targets
- **Locations:**
  - `module/src/world/QuestInteraction.cpp:367-379` (`UpdateNpcTask`)
  - `module/src/world/objectives/CreatureTargetHandler.cpp:259-297` (`CreatureTargetHandler::Update`)
  - `module/src/world/objectives/ObjectTargetHandler.cpp:134-169` (`ObjectTargetHandler::Update`)
- **Root Cause:**
  1. In `QuestInteraction::UpdateNpcTask` (`QuestInteraction.cpp:367-379`), when a bot arrives at a quest giver/ender coordinate and `FindLiveNpc` returns `nullptr` (e.g., an NPC spawned only by an event or phased/dead), the bot enters `TaskPhase::Approach` with `SetPhase(..., "waiting for the npc to spawn")` and stands still for the entire `interactTimeoutMs = 30000` (30 s) before failing, and only records a single strike in `FailureMemory` (`FailKind::NpcSpot`, 300 s TTL).
  2. In `CreatureTargetHandler::Update` (`CreatureTargetHandler.cpp:265`), `ready` checks `ctx.bot->GetDistance(target) <= range && ctx.bot->IsWithinLOSInMap(target)`. When `target` is within `range` (e.g. 28 yd) but behind an obstacle or on a ledge out of LOS, `TravelTo` is called with `acceptRadius = std::max(2.0f, range - 2.0f) = 26.0f`. Since the bot is already at 15 yd (`<= 26.0f`), `TravelTo` -> `Navigate` immediately returns `NavStatus::Arrived` without moving the bot a single inch! The bot stands stationary out of LOS for the entire 25-second `approachTimeoutMs`!
- **Severity:** **P1 (High)**
- **Proposed Correction:**
  When `ctx.bot->GetDistance(target) <= range` but `!ctx.bot->IsWithinLOSInMap(target)` (or in `ObjectTargetHandler` when 2D distance is within `UseRange` but 3D `InUseRange` is false), dynamically tighten `TravelTo`'s `acceptRadius` to `INTERACTION_DISTANCE * 0.5f` (2.0 yd) so `Navigate` actually paths around the obstacle/ramp to the target's position instead of returning `Arrived` at 26 yards!
- **Reproducible Test:**
  `TestApproachTightensRadiusWhenOutOfLos` in `world_logic_tests.cpp`.

---

### Defect #10 (P1): Observability Gaps in `.botcmd brain` and `.botcmd worldstats`
- **Locations:**
  - `module/src/world/WorldBrain.cpp:879-1085` (`Describe`, `DescribeGlobal`)
  - `module/src/BotMovement.cpp:1357-1402` (`BotMovement::Describe`)
  - `module/src/world/WorldMetrics.h:15-42` (`WorldMetrics`)
  - `module/src/BotMovement.h:103-112` (`MovementStats`)
- **Root Cause:**
  While `.botcmd brain` and `.botcmd worldstats` provide task/phase/counter summaries, they currently omit:
  - Explicit navigation progress state (goal distance vs active leg distance, Z delta, corridor progress, stall timer, hold reason such as casting/rooted/mounted).
  - Recovery stage history and failure classification (`NoPath`, `Unreachable`, `VerticallyDisconnected`, `BlockedByOwner`).
  - Behavioral loop detection counters (`loopBreakEvents`, `hysteresisKept`).
  - Cross-bot static no-path cache hit/miss counters and pathfinding query counts per leg.
- **Severity:** **P1 (Medium)**
- **Proposed Correction:**
  Extend `MovementStats`, `WorldMetrics`, `BotMovement::Describe`, `WorldBrain::Describe`, and `WorldBrain::DescribeGlobal` to report all required diagnostics concisely.

---

## 4. Baseline Test Suite Inventory & Metrics

All existing standalone test suites were compiled and executed against the unmodified baseline (`4f6cf99`) using MSVC (`cl /std:c++20 /EHsc`):

| Test Suite | Target / Scope | Checks / Assertions | Baseline Status |
|---|---|---:|---|
| `world_logic_tests.cpp` | `FailureMemory`, `WorldReservations`, `PopulationHeatmap`, `WorldUtility`, `ObjectiveCommon`, `DangerMap` | 487 checks | **PASS** (0 failures) |
| `locomotion_tests.cpp` | `LocomotionStateStore`, `MoveOwner` priorities, `NavProgress`, `NavigateTransaction`, mount state machine | 224 checks | **PASS** (0 failures) |
| `combat_decisions_tests.cpp` | Combat utility, heal forecasting, defensive stacking, threat decisions | 87 checks | **PASS** (0 failures) |
| `training_data_tests.cpp` | Training arena data & metrics | All assertions | **PASS** (0 failures) |
| `spell_resolver_tests.cpp` | Rank chain & effective spell resolution | All assertions | **PASS** (0 failures) |
| `encounter_tests.cpp` | `WorldEncounterDirector` state machine & dialogue pools | 9,916 checks | **PASS** (0 failures) |
| `gear_policy_tests.cpp` | Weapon kit fit & spec stat scoring | All assertions | **PASS** (0 failures) |
| `map_service_tests.cpp` | `BotMapProtocol` & `BotMapFilter` | All assertions | **PASS** (0 failures) |
| `test_training_reports.py` | Python training report generator | 5 tests | **PASS** |
| `test_ci_compare.py` | Python CI regression comparator | 20 tests | **PASS** |
