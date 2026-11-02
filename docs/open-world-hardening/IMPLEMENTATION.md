# Open-World Autonomous Intelligence & Navigation Hardening — Implementation

## 1. Summary of Modified & Added Files

| File | Role | Summary of Changes |
|---|---|---|
| `module/src/BotNavProgress.h` | Route-aware progress & oscillation detector | Added `bestLeg2d`, `bestLegDz`, `lastGoalProgressAt`, `legSwitchbacks`, `oscillatingArea`, 4-cell spatial ring (`recentCellX`/`Y`, `cellRevisits`), `StartLeg()`, and `UpdateRoute()`. Updated `Hold()` to advance both `lastProgressAt` and `lastGoalProgressAt`. |
| `module/src/BotMovementPrimitives.h` | Locomotion primitives, 3D arrival & shared no-path cache | Added `NavFailureKind`, `NavHoldReason`, and `SharedNoPathCache` (256-entry ring buffer, `STRIKE_THRESHOLD = 2`, `DEFAULT_TTL_MS = 20000`). Extended `MovementRequest` with `legZ`, `consecutiveNoPath`, `lastFailureKind`, `holdReason`. Added `NavigateTransaction::MaxArrivalZ`, `IsWithin3dArrival`, 3D-aware `ValidateArrival`, and 3D-aware `CommitLeg`. |
| `module/src/BotMovementStateStore.h` | Sharded state store | Added `IsSharedCorridorBlocked` and `RecordSharedCorridorFailure` backed by `_sharedNoPathCache` and `_sharedNoPathMutex`, cleared in `ResetAllForTest()`. |
| `module/src/BotMovement.h` | Movement API & telemetry counters | Extended `MovementStats` with `pathQueries`, `sharedNoPathHits`, `legSwitchbacksHandled`, and `verticalRejections`. |
| `module/src/BotMovement.cpp` | Navigation engine & recovery execution | Optimized `IssueLeg` to call `RouteWaypoint` first for long legs (`> 120 yd`); integrated `IsSharedCorridorBlocked`/`RecordSharedCorridorFailure` in `MoveToInternal`; added context-aware `Hold(now)` for `UNIT_STATE_ROOT` and `IsNonMeleeSpellCast`; enforced 3D arrival (`IsWithin3dArrival`); escalated repeated first-leg no-path failures to `NavStatus::Stuck`; switched `Navigate` to `UpdateRoute`; oriented recovery detours relative to goal bearing with immediate same-tick `DetourLeft -> DetourRight` fallback; enhanced `Describe`. |
| `module/src/BotAI.cpp` | Bot AI main loop & combat-to-world handoff | Fixed post-combat `MoveOwner::Combat` and `MoveOwner::Avoidance` ownership leaks (`BotAI.cpp:4185-4191`, `4973-4975`); released finished mid-combat `MoveOwner::Avoidance` claims (`BotAI.cpp:4447-4449`, `4883-4884`); fixed ranged/caster LOS deadlock by chasing with `MELEE_ENGAGE_RANGE` when `!bot->IsWithinLOSInMap(target)` (`BotAI.cpp:4490-4499`). |
| `module/src/engine/CombatMovement.cpp` | Combat cast-vs-move arbiter | Updated `CombatMovement::ReadyToCast` so a moving bot whose victim is out of LOS (`!bot->IsWithinLOSInMap(victim)`) does not call `ForceStopOwner(bot, MoveOwner::Combat)` prematurely. |
| `module/src/world/FailureMemory.h` | Per-bot failure memory | Added `FailKind::NavCorridor` and `FailureMemory::CorridorId(mapId, x, y)` (16x16 yd spatial cell key). |
| `module/src/world/BehavioralLoopDetector.h` | **NEW** Behavioral loop & deadlock detector | Implemented `BehavioralLoopDetector` (`LoopKind::TargetApproachLoop`, `AreaPingPong`, `TaskOscillation`), reset on real progress (`OnProgress`). |
| `module/src/world/WorldMetrics.h` | Open-world metrics | Added `loopsBroken` and `dangerWaypointsSkipped`. |
| `module/src/world/WorldUtility.h` | Utility scoring | Added `activeQuestHysteresis = 18.0f` (`ObjectiveUtilityInput::activeQuest`) and `currentAreaHysteresis = 15.0f` (`ClusterScoreInput::currentArea`). |
| `module/src/world/WorldBrainState.h` | Per-bot open-world state | Added `BehavioralLoopDetector loopDetector` and `uint32 lastCommittedQuestId = 0`. |
| `module/src/world/WorldBrainConfig.cpp` | Configuration loader | Loaded `CoaBots.WorldBrain.Weight.ActiveQuestHysteresis` and `CoaBots.WorldBrain.Weight.CurrentAreaHysteresis`. |
| `module/src/world/WorldPlanner.cpp` | Task & area planner | Wired `currentArea` hysteresis into `PickArea`, `activeQuest` hysteresis into `ChooseTask`, and `loopDetector.RecordTaskChoice` to break `TaskOscillation` loops. |
| `module/src/world/WorldExecutor.cpp` | Task phase executor & danger routing | Added 3D arrival check in `UpdateTravel`; removed `task.sub` from `DangerRouteState::routeKey`; filtered `DangerMap` waypoints against `FailKind::NavCorridor`; interpolated waypoint Z; advanced `++route.next` and recorded `FailKind::NavCorridor` when a danger waypoint stalls. |
| `module/src/world/objectives/ObjectiveCommon.cpp` | Shared objective helpers | Added vertical Z check (`<= 18.0f`) to `InArea`; recorded area switches and target attempts in `BehavioralLoopDetector`; tightened `Approach` `acceptRadius` (`2.0f`) when horizontally within range but out of 3D range or LOS. |
| `module/src/world/objectives/CreatureTargetHandler.cpp` | Kill/loot/talk creature handler | Called `state.loopDetector.OnProgress` when objective counters advance or combat/loot succeeds. |
| `module/src/world/objectives/ObjectTargetHandler.cpp` | GameObject interaction handler | Tightened `Navigate` `acceptRadius` (`1.5f`) when horizontally close (`dist2d <= USE_RANGE`) but `dist3d > USE_RANGE` (`GetDistance` 3D check); called `loopDetector.OnProgress` after use. |
| `module/src/world/QuestInteraction.cpp` | Quest giver/ender NPC handler | Added vertical Z check (`distZ <= 6.0f`) to early arrival in `UpdateNpcTask`; called `loopDetector.OnProgress` on quest accept and turn-in. |
| `module/src/world/WorldBrain.cpp` | `.bot world` diagnostics | Surfaced `loopsBroken`, `dangerWaypointsSkipped`, `loopDetector` counters, and extended `MovementStats` (`pathQueries`, `sharedNoPathHits`, `legSwitchbacksHandled`, `verticalRejections`, `backtracks`) in `Describe` and `DescribeGlobal`. |
| `module/conf/mod_coa_playerbots.conf.dist` | Default module configuration | Documented `CoaBots.WorldBrain.Weight.ActiveQuestHysteresis = 18` and `CoaBots.WorldBrain.Weight.CurrentAreaHysteresis = 15`. |
| `module/tests/locomotion_tests.cpp` | Locomotion regression test suite | Added Tests 43–48 covering route-aware switchbacks, detour circuit-breaker & cell oscillation, 3D multi-floor arrival rejection, cross-bot `SharedNoPathCache`, hold clock preservation, and all 20 geometric scenarios + fault injection. |
| `module/tests/world_logic_tests.cpp` | Open-world pure logic test suite | Added `TestCorridorFailureMemory`, `TestBehavioralLoopDetector`, `TestDecisionHysteresis`, and `TestScaleMicrobenchmarks` (1,000-bot AI & route-progress benchmark). |

---

## 2. Key Algorithmic Decisions & Tradeoffs

### 2.1 Corridor-Aware Progress vs. Per-Tick `PathGenerator` Re-query
- **Decision:** Measure progress against both the final destination `(x, y, z)` and the committed corridor leg endpoint `(legX, legY, legZ)` using `NavProgress::UpdateRoute`, combined with a 4-slot `6 x 6 yd` spatial cell ring buffer and a `27,000 ms` maximum detour circuit breaker (`maxDetourLegMs`).
- **Why:** Re-running `PathGenerator::CalculatePath` on every `Navigate` tick to compute exact remaining poly-corridor arc length would multiply navmesh CPU usage by 5–10x at 1,000 bots. By contrast, `legX, legY, legZ` is already produced by `RouteWaypoint` when each leg is issued, so `UpdateRoute` is O(1) (taking ~6 ns per bot) while still accurately recognizing switchback ramps and ridge detours that temporarily increase Euclidean distance to the final goal.

### 2.2 Long-Leg `RouteWaypoint` Ordering in `IssueLeg`
- **Decision:** When `dist > MAX_LEG` (`120 yd`), call `RouteWaypoint(bot, goalX, goalY, goalZ, MAX_LEG, legX, legY, legZ)` **before** attempting a straight-line 2D interpolated point on `GetMapHeight`.
- **Why:** Previously, `IssueLeg` guessed a straight-line 2D point `(bx + dx*t, by + dy*t)` and queried `MoveToInternal` (1st `PathGenerator` call); in hilly, indoor, or cave terrain that straight-line point frequently landed inside rock or off-mesh, triggering a `RouteWaypoint` call (2nd `PathGenerator` call) followed by another `MoveToInternal` (3rd `PathGenerator` call). Querying `RouteWaypoint` first guarantees the intermediate waypoint lies on the actual Detour corridor and cuts worst-case `PathGenerator` invocations per leg in half.

### 2.3 Two-Strike Cross-Bot `SharedNoPathCache`
- **Decision:** Require `STRIKE_THRESHOLD = 2` failures within `DEFAULT_TTL_MS = 20000` inside quantized `8 x 8 x 8 yd` start and `4 x 4 x 4 yd` destination corridor cells before blocking cross-bot path queries.
- **Why:** A single bot's path failure can occasionally be caused by a transient local condition (e.g., standing on a steep rock edge). Requiring 2 strikes before suppressing cross-bot `PathGenerator` queries avoids false positives while still stopping 20–50 bots in a crowded hub or camp from each running 3–4 expensive navmesh searches toward the same unreachable spawn.

### 2.4 Commitment Hysteresis vs. Responsiveness
- **Decision:** Set `ActiveQuestHysteresis = 18.0f` and `CurrentAreaHysteresis = 15.0f`.
- **Why:** In `WorldUtility`, `travelPer100Yards = 7.0f`, `crowdPerBot = 6.0f`, and `areaOccupancy = 12.0f`. A hysteresis bonus of 15–18 points prevents a bot from abandoning its current quest or camp when 1–2 other bots walk through the cell or when distances shift by 100–200 yards, while still allowing genuinely higher-value transitions (such as a ready quest turn-in worth `+120.0f` or a multi-quest overlap worth `+35.0f` per overlapping objective) to preempt immediately.
