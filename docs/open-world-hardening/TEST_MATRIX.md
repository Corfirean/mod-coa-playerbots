# Open-World Autonomous Intelligence & Navigation Hardening — Test Matrix

## 1. Automated Unit, Component, Regression & Full-Core Compilation Suites

All standalone C++ test suites, Python test suites, and full AzerothCore module compilation checks were executed on Windows using MSVC (`Visual Studio 18 / C++20`).

| Suite / Target | Command / Binary | Checks / Tests | Failures | Status |
|---|---|---:|---:|---|
| **Locomotion & Navigation Overhaul + Open-World Hardening** | `locomotion_tests.exe` (`module/tests/locomotion_tests.cpp`) | 398 checks (48 test functions) | 0 | **PASS** |
| **Open-World Pure Logic, Hysteresis, Loop Detector & Benchmarks** | `world_logic_tests.exe` (`module/tests/world_logic_tests.cpp`) | 519 checks (16 test functions) | 0 | **PASS** |
| **Combat Movement & Decision Logic** | `combat_decisions_tests.exe` (`module/tests/combat_decisions_tests.cpp`) | 87 checks | 0 | **PASS** |
| **Open-World Encounters & Choreography** | `encounter_tests.exe` (`module/tests/encounter_tests.cpp`) | 9,916 checks | 0 | **PASS** |
| **World Map Protocol, Viewport Filtering & Scale Benchmarks** | `map_service_tests.exe` (`module/tests/map_service_tests.cpp`) | 1,776 checks | 0 | **PASS** |
| **Gear Progression & Level Boundary Policy** | `gear_policy_tests.exe` (`module/tests/gear_policy_tests.cpp`) | 80 level boundaries + regressions | 0 | **PASS** |
| **Training Arena Scenarios & Deterministic Timelines** | `training_data_tests.exe` (`module/tests/training_data_tests.cpp`) | 17 scenarios | 0 | **PASS** |
| **Live Spellbook & Spell Resolver** | `spell_resolver_tests.exe` (`module/tests/spell_resolver_tests.cpp`) | Live mutation + rank resolution | 0 | **PASS** |
| **Python Regression & Statistical Verification Suites** | `python -m unittest discover -s module/tests -p "test_*.py"` | 43 tests (10 test modules) | 0 | **PASS** |
| **Full AzerothCore Server Module Compilation (`RelWithDebInfo`)** | `cl /c` with AzerothCore `RelWithDebInfo` flags & headers across all 11 modified `.cpp` files | 11 translation units | 0 errors, 0 warnings | **PASS** |

---

## 2. Section 9.2 Geometric & Navigation Scenario Matrix (20 Scenarios)

Verified in `locomotion_tests.cpp` (`TestRouteAwareProgressSwitchbacks`, `TestRouteAwareProgressDetourCircuitBreakerAndOscillation`, `Test3DArrivalValidationMultiFloorRejection`, `TestSharedNoPathCorridorCacheCrossBotAndExpiry`, `TestHoldPreservesGoalAndLegProgressClocks`, `TestTwentyGeometricNavigationScenariosAndRecoveryFaultInjection`) and `world_logic_tests.cpp`:

| # | Geometric / Behavioral Scenario | Test Coverage & Expected Behavior | Actual Result | Status |
|---:|---|---|---|---|
| 1 | Flat open terrain | Steady 2D progress (`7 yd/s`), zero recovery stages (`stage == 0`). | `stage == 0`, `NavRecovery::None` | **PASS** |
| 2 | Steep hills and ramps | Slow 2D progress (`0.5 yd/s`) with steady vertical climb (`2.0 yd/s`) recognized as progress. | `stage == 0`, `NavRecovery::None` | **PASS** |
| 3 | Switchbacks where initial path moves away from destination | `goalDist2d` increases `60 -> 103 yd` while `legDist2d` decreases `53 -> 1 yd`; credited via `UpdateRoute` (`legSwitchbacks > 0`) without false `Repath`. | `stage == 0`, `legSwitchbacks == 18`, `NavRecovery::None` | **PASS** |
| 4 | Target behind wall with nearby 2D distance | `goalDist2d = 5 yd`, corridor leg routes `35 yd` around wall; `Approach` tightens `acceptRadius = 2.0f` when `!IsWithinLOSInMap`. | No false stall; wraps wall | **PASS** |
| 5 | Target on cliff above or below bot | `dist2d = 2.5 yd`, `distZ = 22.0 yd`; `IsWithin3dArrival` rejects false arrival (`MaxArrivalZ = 6.0 yd`). | False arrival rejected | **PASS** |
| 6 | Cave entrance and interior objective | Overland position directly above cave (`dist2d = 0.5 yd`, `distZ = 18.0 yd`) rejected by `IsWithin3dArrival` and `ObjectiveCommon::InArea` (`distZ <= 18.0f`). | False arrival rejected; enters cave | **PASS** |
| 7 | Spiral tower or multi-level mine | Constant 2D radius (`6–7 yd`) with steady vertical climb (`1.6 yd/s`) never stalls; floor separation enforced. | `stage == 0`, `NavRecovery::None` | **PASS** |
| 8 | Indoor multi-room building | Multi-leg corridor transitions (`StartLeg`) credit room-to-room waypoint progress. | `stage == 0`, `NavRecovery::None` | **PASS** |
| 9 | Bridge or narrow causeway | Under-bridge position (`dist2d = 1.0 yd`, `distZ = 14.0 yd`) rejected; bridge deck (`distZ = 0.8 yd`) accepted. | Under-bridge rejected; deck accepted | **PASS** |
| 10 | Water-to-shore transition | `MountPolicyResolver` blocks mounting in water (`isInWater = true`) and permits mounting once on shore. | Water mount blocked; shore mount allowed | **PASS** |
| 11 | Fenced enclosure with single gate | Leg toward gate opposite target (`goalDist2d` increases `8 -> 22 yd`, `legDist2d` decreases `25 -> 4 yd`) credited without stall. | `stage == 0`, `NavRecovery::None` | **PASS** |
| 12 | Dense trees, rocks or pillars | Stall triggers bounded recovery ladder (`Repath -> DetourLeft -> DetourRight -> Backtrack -> GiveUp`). | Deterministic stage escalation | **PASS** |
| 13 | Dynamic blocked corridor (fault injection) | Fault injection verifies `Repath` (9s) -> `DetourLeft` (18s) -> `DetourRight` (27s) -> `SafePositionHistory` `Backtrack` -> `GiveUp` (36s). | Full ladder + backtrack verified | **PASS** |
| 14 | Multi-bot convergence on narrow doorway | `SharedNoPathCache` blocks repeated `PathGenerator` calls across 20 bots after 2 corridor strikes (`STRIKE_THRESHOLD = 2`). | Cross-bot cache hit on bots 3..20 | **PASS** |
| 15 | Moving target pursuit | `NavProgress::Rebase` updates baseline when target moves without losing current recovery stage or triggering false stall. | Smooth pursuit without false stall | **PASS** |
| 16 | Combat transition on uneven terrain | `MoveOwner::Combat` preempts `MoveOwner::Quest`; ranged out-of-LOS chases with `MELEE_ENGAGE_RANGE` until LOS restored. | Preemption & LOS chase verified | **PASS** |
| 17 | Post-combat resumption of prior task | `BotAI` releases `MoveOwner::Combat` and `MoveOwner::Avoidance` immediately on target death/reset (`0 ms` delay vs. `15,000 ms` baseline). | Immediate handoff to `Quest`/`Travel` | **PASS** |
| 18 | Quest NPC inside building | `UpdateNpcTask` and `Navigate` enforce `distZ <= 6.0f` and `IsWithin3dArrival` before interacting. | Roof/cellar false arrival rejected | **PASS** |
| 19 | Loot corpse on slope | `IsWithin3dArrival(2.5f, 3.2f, 2.8f)` accepts legitimate sloped corpse within `MaxArrivalZ(3.2f) = 4.8f`. | Sloped corpse loot accepted | **PASS** |
| 20 | Travel across zone boundary | Multi-leg route (`600 yd` across 5 x `120 yd` legs) queries `RouteWaypoint` first per leg and completes with `stage == 0`. | `stage == 0`, 1–2 path queries/leg | **PASS** |

---

## 3. Section 9.3 Behavioral Loop & Hysteresis Matrix

| Scenario | Test Function (`world_logic_tests.cpp`) | Expected Outcome | Actual Outcome | Status |
|---|---|---|---|---|
| **Repeated Target Approach Loop** | `TestBehavioralLoopDetector` | 3rd attempt on same target GUID within `60s` without progress triggers `LoopKind::TargetApproachLoop`, blacklists target (`2x` TTL), and increments `loopsBroken`. | Triggered on attempt 3; reset on `OnProgress` | **PASS** |
| **Area Ping-Pong (`A -> B -> A -> B -> A`)** | `TestBehavioralLoopDetector` | 3rd visit to Area A within `120s` without objective progress triggers `LoopKind::AreaPingPong`, records `FailKind::Cluster`, and increments `loopsBroken`. | Triggered on 5th switch (`A->B->A->B->A`) | **PASS** |
| **Quest/Task Oscillation (`Q1 -> Q2 -> Q1 -> Q2 -> Q1`)** | `TestBehavioralLoopDetector` | 3rd selection of `(questId, objectiveIndex)` within `90s` without progress triggers `LoopKind::TaskOscillation` and penalizes oscillating candidate. | Triggered on 5th switch (`Q1->Q2->Q1->Q2->Q1`) | **PASS** |
| **Active Quest Commitment Hysteresis** | `TestDecisionHysteresis` | Active quest (`+18.0f`) resists switching to another quest that is `80 yd` closer (`+5.6f`), but yields to strong multi-quest synergy (`+70.0f`). | Active quest preserved; yields to high-value synergy | **PASS** |
| **Current Area Commitment Hysteresis** | `TestDecisionHysteresis` | Current cluster (`+15.0f`) resists switching to a camp with 1 fewer bot (`+6.0f`). | Current cluster preserved | **PASS** |
| **Danger Route Waypoint Failure Memory** | `TestCorridorFailureMemory` | Failed `DangerMap` waypoint cell is remembered under `FailKind::NavCorridor` (`CorridorId(mapId, x, y)`) and skipped on subsequent plans. | Same `16x16 yd` cell skipped; expires after TTL | **PASS** |

---

## 4. Section 9.4 Live Multi-Bot In-Game Soak Trials (`NOT RUN`)

In accordance with Section 12 ("Do not fabricate test results, benchmark numbers, logs, or live-trial outcomes. If a live trial cannot be executed in full, mark it `NOT RUN` and explain why"), the 20–60 minute live `worldserver` + MySQL + full mmaps/vmaps multi-bot soak trials are marked **`NOT RUN`** because this execution environment does not have an active MySQL database instance and running `worldserver` realm session attached for 30–60 minute wall-clock soak capture.

| Trial | Description | Status | Reproducible Execution Command (on Live Realm Host) |
|---|---|---|---|
| **Trial A** | Single-bot deep trace (30–60 min across 2+ zones) | **NOT RUN** | `.bot world reset` -> `.bot world trace on` (with target bot selected) -> observe for 30 min -> `.bot world` & `.bot move debug` |
| **Trial B** | 25-bot local zone density (20–30 min in shared questing zone) | **NOT RUN** | Spawn 25 bots in Elwynn/Westfall or Durotar/Barrens -> `.bot world reset` -> soak 25 min -> `.bot world` (inspect `Reservations`, `Heatmap`, `loops broken`, `shared no-path cache hits`) |
| **Trial C** | 100–300 bot multi-zone soak (30+ min) | **NOT RUN** | Configure `CoaBots.SpawnRandom.MaxBots = 300` -> soak 30 min -> `.bot world` & `.server info` (inspect world update diff time, `PathGenerator queries`, `stalls`, `quests completed`) |
| **Trial D** | Stress & recovery fault injection (unreachable spawns, multi-floor caves, combat interrupts) | **NOT RUN** | Select bots near multi-level caves (e.g., Jasperlode Mine / Skull Rock) -> `.bot world trace on` -> verify `Z-rejections`, `switchbacks`, and `loops broken` via `.bot world` |
