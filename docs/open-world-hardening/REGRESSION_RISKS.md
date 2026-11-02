# Open-World Autonomous Intelligence & Navigation Hardening — Regression Risks & Monitoring

## 1. Remaining Technical Risks & Mitigations

### 1.1 Switchback Detour Circuit Breaker (`maxDetourLegMs = 27,000 ms`)
- **Risk:** An unusually long, single-leg switchback where Euclidean distance to the final goal does not decrease by at least `2.5 yd` or `1.5 yd` Z for more than 27 consecutive seconds could hit `maxDetourLegMs` and trigger a `Repath`.
- **Why Mitigated:** `BotMovement::IssueLeg` caps each individual leg at `MAX_LEG = 120.0f` yards. At normal unmounted run speed (`7.0 yd/s`), a `120 yd` leg takes `~17.1 seconds` (and `~8.5 seconds` mounted), well within the `27,000 ms` circuit breaker. Furthermore, even if `Repath` triggers at 27 seconds, it simply refreshes the corridor from the bot's new position without discarding the task.

### 1.2 Vertical Arrival Tolerance (`MaxArrivalZ`) on Steep Terrain
- **Risk:** In `NavigateTransaction::MaxArrivalZ(acceptRadius)`, vertical tolerance is `min(12.0f, max(4.0f, acceptRadius * 1.5f))`. If an objective area anchor `(task.x, task.y, task.z)` was computed from the average Z of spawns on a steep hillside where the bot enters the perimeter 15+ yards above or below the anchor Z, `UpdateTravel` or `InArea` (`dz <= 18.0f`) could require the bot to walk closer to the anchor elevation before transitioning from `TravelToArea` to `Search`.
- **Why Mitigated:** Requiring the bot to walk closer to the anchor elevation on steep hills is strictly safer than prematurely switching to `Search` on a cliff face 25 yards below the camp. `SpawnClustering::Cluster` already splits clusters whose Z gap exceeds `14.0f` yards (`TestClustering`), so spawns within a single cluster share a consistent elevation band.

### 1.3 Cross-Bot `SharedNoPathCache` Cell Granularity
- **Risk:** `SharedNoPathCache` quantizes start positions into `8 x 8 x 8 yd` cells and destination positions into `4 x 4 x 4 yd` cells (`DEFAULT_TTL_MS = 20,000 ms`, `STRIKE_THRESHOLD = 2`). If two bots in the same `8 x 8 x 8 yd` start cell fail to path to a destination cell, a third bot standing 6 yards away inside the same `8 x 8 x 8 yd` cell that is on the valid side of a thin wall could be blocked from pathing to that destination for up to 20 seconds.
- **Why Mitigated:** The cache TTL is short (`20 seconds`), requires 2 independent failures before activating, and includes the quantized Z coordinate (`sz / 8.0f`, `dz / 4.0f`) so different floors never collide.

### 1.4 Commitment Hysteresis Tuning (`ActiveQuestHysteresis = 18`, `CurrentAreaHysteresis = 15`)
- **Risk:** Setting hysteresis too high could make bots reluctant to leave a depleted camp; setting it too low could allow crowd noise to re-introduce ping-ponging.
- **Why Mitigated:** Both weights are exposed in `mod_coa_playerbots.conf.dist` (`CoaBots.WorldBrain.Weight.ActiveQuestHysteresis` and `CoaBots.WorldBrain.Weight.CurrentAreaHysteresis`) and backed up by `BehavioralLoopDetector` (`AreaPingPong` and `TaskOscillation`), which catches and breaks any residual oscillation even if hysteresis is tuned down.

---

## 2. Recommended Production Monitoring

Operators and developers validating live server soak runs should monitor the new telemetry counters via `.bot world` and `.bot move debug`:

1. **Global Movement Telemetry (`.bot world`):**
   - `PathGenerator queries` vs. `MovePoints issued`: ratio should remain near `1.0–1.5` in open-world zones.
   - `shared no-path cache hits`: high values indicate broken spawn coordinates or unpathable mob placements in the database.
   - `switchbacks`: counts how many times `NavProgress::UpdateRoute` prevented a false stall while a bot traversed a legitimate detour/switchback leg.
   - `Z-rejections`: counts how many times 3D arrival validation prevented a bot from falsely arriving on the wrong vertical floor (e.g., caves, towers, bridges).
2. **Global Behavioral Loop Telemetry (`.bot world`):**
   - `Loops broken`: counts total `TargetApproachLoop`, `AreaPingPong`, and `TaskOscillation` interventions across all bots.
   - `Danger waypoints skipped`: counts coarse `DangerMap` waypoints bypassed due to navmesh unreachability (`FailKind::NavCorridor`).
3. **Per-Bot Inspection (`.bot world` & `.bot move debug` with bot selected):**
   - Displays `legDist`, `distZ`, `hold=<NavHoldReason>`, `fail=<NavFailureKind>`, `switchbacks`, `noPathStrikes`, and `loops broken (detector: N, last: <LoopKind>)`.
