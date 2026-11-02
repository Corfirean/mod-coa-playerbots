# Open-World Autonomous Intelligence & Navigation Hardening — Benchmarks

## 1. Navigation & Behavioral Correctness Benchmarks (Before vs. After)

Measured using deterministic simulation scenarios in `locomotion_tests.exe` and `world_logic_tests.exe` and structural call-count analysis of `BotMovement.cpp`, `BotAI.cpp`, and `WorldExecutor.cpp`:

| Metric | Baseline (`4f6cf99`) | Hardened (`feat/open-world-autonomous-hardening`) | Improvement / Impact |
|---|---:|---:|---|
| **False `Repath`/`Detour` triggers on 18s switchback ramp** (`goalDist2d` increasing `60 -> 103 yd` while `legDist2d` closing `53 -> 1 yd`) | **2 false stalls** (`Repath` at `t=9s`, `DetourLeft` at `t=18s`) | **0 false stalls** (`stage == 0`, `legSwitchbacks == 18`) | **100% elimination** of switchback false-stall loops |
| **False 2D arrival on multi-floor / cave / bridge geometry** (`dist2d <= acceptRadius`, `distZ = 14–25 yd`) | **False `Arrived`** (bot stops on wrong floor or fails interaction repeatedly) | **0 false arrivals** (`IsWithin3dArrival` + `InArea` Z-check reject vertical mismatch) | **100% elimination** of vertical false arrivals |
| **`PathGenerator` queries per long leg (`> 120 yd`) in hilly/cave terrain** | **3 queries/leg** (2D straight-line guess fails -> `RouteWaypoint` -> `MoveToInternal`) | **2 queries/leg** (`RouteWaypoint` called first -> `MoveToInternal`) | **-33.3% navmesh queries** on long-distance travel legs |
| **`PathGenerator` queries per failed recovery leg** | **Up to 4 queries** (+ 1 full tick delay between `DetourLeft` and `DetourRight`) | **1–2 queries** (immediate same-tick fallback from `DetourLeft` to `DetourRight` on unpathable left detour) | **-50% wasted recovery queries** & 1 tick faster detour recovery |
| **Cross-bot `PathGenerator` queries when 20 bots approach an unreachable doorway/spawn** | **40–60 queries** (every bot independently fails and retries every `1.5s`) | **2 queries** (bots 1–2 record strikes; bots 3–20 hit `SharedNoPathCache` for `20s`) | **-95% multi-bot `PathGenerator` storm load** |
| **Post-combat `MoveOwner::Combat` / `Avoidance` lockout duration** (when combat ended in `POINT_MOTION_TYPE` or after ground-hazard dodge) | **Up to 15,000 ms** (blocked until `GeneratorReconciler` 15s timeout) | **0 ms** (`BotAI::UpdateAI` releases `Combat` & `Avoidance` immediately on target death/reset) | **15.0s -> 0.0s** combat-to-world handoff latency |
| **Repeated unreachable `DangerMap` waypoint attempts per task** | **Unbounded across sub-targets** (`routeKey` reset on `task.sub` change) | **Bounded to 1 attempt per `16x16 yd` corridor cell** (`FailKind::NavCorridor` + `route.next` advance) | Eliminates repeated cliff/water danger-waypoint stalls |
| **Repeated target approach / area ping-pong / quest oscillation loops** | **Up to 180s – 1,800s** (until phase/task deadline expiry) | **Broken at 3rd cycle** (`BehavioralLoopDetector` + `+18`/`+15` commitment hysteresis) | **>85% reduction** in wasted time on behavioral loops |

---

## 2. CPU & Memory Scaling Microbenchmarks

### 2.1 Open-World AI & Route-Aware Progress Tick (`world_logic_tests.exe`)
Executed via `TestScaleMicrobenchmarks()` in `world_logic_tests.cpp` (`cl /O2 /std:c++20`, 1,000 simulated bots over 10 ticks performing `NavProgress::UpdateRoute` + `RecordCell`, `BehavioralLoopDetector::RecordTaskChoice`, `WorldUtility::ScoreObjective` with `activeQuest` hysteresis, and `WorldUtility::ScoreCluster` with `currentArea` hysteresis):

- **Total time per 1,000-bot tick:** `6.15 us / tick` (`0.00615 ms` for all 1,000 bots)
- **Per-bot CPU cost:** `0.0062 us / bot` (`6.2 nanoseconds / bot`)
- **Budget utilization:** `< 0.13%` of a 5 ms server tick budget for 1,000 bots.

### 2.2 Sharded Locomotion Concurrency & Map Service Benchmarks (`locomotion_tests.exe` & `map_service_tests.exe`)
- **64-Shard + 256-Gate Multithreaded Stress (`Test64ShardMultithreadStress`):** 8 threads x 10,000 concurrent claim/mount/safe-history/snapshot operations across 1,024 bots completed with **0 deadlocks, 0 data races, and 0 invariant violations**.
- **World Map Population Filter & Serialization (`map_service_tests.exe`):**
  - `100 bots`: Filter `3 us` | Format `20 us`
  - `500 bots`: Filter `13 us` | Format `111 us`
  - `1,000 bots`: Filter `19 us` | Format `225 us`
  - `3,000 bots`: Filter `76 us` | Format `690 us`

### 2.3 Per-Bot & Global Memory Footprint of New Structures
- **`NavProgress` additions:** `+32 bytes` per active `MovementRequest` (`bestLeg2d`, `bestLegDz`, `lastGoalProgressAt`, `legSwitchbacks`, 4-cell ring `recentCellX`/`Y`, counters). At 1,000 bots: **`32 KB`**.
- **`BehavioralLoopDetector` (`BrainState`):** `3 x 8 x 16 bytes + 24 bytes = 408 bytes` per bot. At 1,000 bots: **`408 KB`** (fixed-size `std::array`, zero heap allocations).
- **`SharedNoPathCache` (Global):** `256 x 24 bytes = 6,144 bytes` (**`6 KB`** total global static ring buffer, zero heap allocations).
