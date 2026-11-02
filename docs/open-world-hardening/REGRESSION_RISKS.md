# Open-World Autonomous Intelligence & Navigation Hardening — Regression Risks & Monitoring

## 1. Remaining Technical Risks & Mitigations

### 1.1 Switchback Detour Circuit Breaker (`maxDetourLegMs = 27,000 ms`)
- **Risk:** An unusually long, single-leg switchback where Euclidean distance to the final goal does not decrease by at least `2.5 yd` or `1.5 yd` Z for more than 27 consecutive seconds could hit `maxDetourLegMs` and trigger a `Repath`.
- **Why Mitigated:** `BotMovement::IssueLeg` caps each individual leg at `MAX_LEG = 120.0f` yards. At normal unmounted run speed (`7.0 yd/s`), a `120 yd` leg takes `~17.1 seconds` (and `~8.5 seconds` mounted), well within the `27,000 ms` circuit breaker. Furthermore, even if `Repath` triggers at 27 seconds, it simply refreshes the corridor from the bot's new position without discarding the task.

### 1.2 Vertical & Topological Area Arrival (`AreaArrivalRule` & `MaxArrivalZ`) on Steep Terrain
- **Risk:** In `AreaArrivalRule::Evaluate` (`SpawnClustering.h`), vertical separation from the nearest area reference point above `SAME_FLOOR_Z_YARDS = 6.0f` triggers `BotMovement::QueryDirectPathLength` to verify a complete same-floor navmesh path (`<= MaxDirectPathLength(refDist2d, refDz)`). On steep hillsides or multi-elevation camps, a naive check against only the area anchor `(task.x, task.y, task.z)` could reject legitimate perimeter entries.
- **Why Mitigated:** `AreaArrivalRule::SelectReferencePoint` evaluates all member spawn points in the `ObjectiveArea` and picks the member point minimizing `|botZ - pz| + d2d * 0.25f`, while `MaxAllowedZ` scales with horizontal slope distance (`min(22.0f, 10.0f + refDist2d * 0.40f)`). Flat terrain (`refDz <= 6.0f`) incurs zero extra navmesh queries.

### 1.3 Cross-Bot `SharedNoPathCache` Cell Granularity & Context Isolation
- **Risk:** `SharedNoPathCache` quantizes start and destination positions into `4 x 4 x 4 yd` cells (`DEFAULT_TTL_MS = 20,000 ms`, `STRIKE_THRESHOLD = 2`). Without topological and context isolation, two bots on opposite sides of a thin wall or in different phases/capabilities could poison each other's path queries.
- **Why Mitigated:** `SharedNoPathCache::MakeCorridorKey` hashes `(mapId, instanceId, phaseMask, capabilityFlags, startPolyRef, sx/4, sy/4, sz/4, dx/4, dy/4, dz/4)`, requires 2 distinct `botGuid` reporters before activating, conservatively bypasses the cache when `startPolyRef == 0` (`ambiguous = true`), and immediately invalidates any negative entry via `RecordSuccess` when a valid path is found.

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
