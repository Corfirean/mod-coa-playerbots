# Open-World Autonomous Intelligence & Navigation Hardening — Architecture

## 1. Architectural Principles & Non-Negotiable Invariants

The hardened open-world autonomy and navigation architecture in `mod-coa-playerbots` enforces five core engineering invariants:

1. **Zero Artificial Movement Cheats:** Bots obey the same Detour/Recast navigation mesh (`PathGenerator`), terrain heightmaps, line-of-sight (`IsWithinLOSInMap`), movement speeds, and interaction ranges as real players. No obstacle teleportation, wall clipping, or unrestricted Z-snapping is used.
2. **One Authoritative Locomotion System:** All movement requests across combat, avoidance, corpse recovery, looting, encounters, questing, travel, gathering, grinding, and ambient activity flow through `BotMovement` and `LocomotionStateStore`, governed by a strict `MoveOwner` priority hierarchy, monotonic `commandId` tokens (`LocomotionToken`), `requestGeneration` counters, and `planningEpoch` tickets.
3. **Context-Aware Stationary Classification:** Stationary states caused by spell casting, channeling, mount casting, roots/stuns, external control (`UNIT_STATE_CONFUSED` / `UNIT_STATE_FLEEING` / `UNIT_STATE_CHARMED`), eating/drinking, NPC reading pauses, or group waiting call `NavProgress::Hold(now)` to shift both leg and goal progress timestamps forward rather than burning the stall timer.
4. **Bounded Retries & Multi-Layer Loop Prevention:** Every movement leg, recovery stage, danger-route waypoint, target approach, cluster switch, and quest task has a measurable progress criterion, a finite retry budget, and a deterministic escalation path.
5. **Predictable Scaling to 1,000+ Bots:** Hot-path state is partitioned across 64 `LocomotionShard` instances and 256 per-bot dispatch gates, with a bounded 256-entry cross-bot `SharedNoPathCache` and O(1) ring-buffer loop detectors (`NavProgress` cell ring and `BehavioralLoopDetector`).

---

## 2. Subsystem Boundaries & Control Flow

```
+-----------------------------------------------------------------------------------+
|                                   BotAI::UpdateAI                                 |
|  - Releases expired/post-combat MoveOwner::Combat & MoveOwner::Avoidance claims   |
|  - Suspends WorldBrain during combat/corpse/BG/dungeon; ticks WorldBrain when free|
+-----------------------------------------+-----------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                                  WorldBrain::Update                               |
|  - Updates PopulationHeatmap presence & sweeps expired WorldReservations          |
|  - Executes opportunistic side-activities (gather, social assist, resurrect)      |
|  - Invokes WorldPlanner when idle or replan interval expires                      |
|  - Invokes WorldExecutor::Update to advance the active WorldTask                  |
+--------------------+-----------------------------------------+--------------------+
                     |                                         |
                     v                                         v
+----------------------------------------+   +--------------------------------------+
|              WorldPlanner              |   |             WorldExecutor            |
| - Evaluates QuestKB & ObjectiveAreas   |   | - Phase state machine:               |
| - Applies ActiveQuestHysteresis (+18)  |   |   TravelToArea -> Search -> Approach |
|   & CurrentAreaHysteresis (+15)        |   |   -> Execute -> Combat -> Loot       |
| - Consults FailureMemory &             |   |   -> Verify -> Recover               |
|   BehavioralLoopDetector               |   | - DangerMap::Plan corridor routing   |
|   (TaskOscillation / AreaPingPong)     |   |   with FailKind::NavCorridor filter  |
+----------------------------------------+   +-----------------+--------------------+
                                                               |
                                                               v
+-----------------------------------------------------------------------------------+
|                               BotMovement::Navigate                               |
| - 64-shard LocomotionStateStore + 256 per-bot physical dispatch gates             |
| - 3D arrival validation (NavigateTransaction::IsWithin3dArrival)                  |
| - Context-aware hold (Rooted, Casting, MountCasting, ExternalControl)             |
| - Route-aware progress (NavProgress::UpdateRoute: goal + active leg + cell ring)  |
| - Recovery ladder: Repath -> DetourLeft -> DetourRight -> Backtrack -> GiveUp     |
| - SharedNoPathCache (cross-bot corridor failure suppression)                      |
+-----------------------------------------------------------------------------------+
```

---

## 3. Authoritative Ownership & Concurrency Model

### 3.1 `MoveOwner` Priority Hierarchy
`LocomotionArbiter::PriorityOf(MoveOwner)` defines a strict total order:

| Priority | `MoveOwner` | Responsibility |
|---:|---|---|
| 120 | `Avoidance` | Ground hazard step-out, boss telegraph dodge, low-HP flee |
| 110 | `Combat` | Melee/ranged chase, LOS wrap-around, kite/reposition |
| 100 | `Battleground` | Battleground objective movement |
| 90 | `AutoDungeon` | Dungeon group travel and boss room positioning |
| 80 | `Corpse` | Ghost walk to corpse / spirit healer |
| 70 | `Loot` | Walking to lootable corpse |
| 65 | `Encounter` | Open-world multi-bot encounter choreography |
| 60 | `Quest` | Quest objective travel, target approach, NPC interaction |
| 50 | `Travel` | Zone progression, hub travel, flight master approach |
| 40 | `Fish` | Fishing spot positioning |
| 35 | `Gather` | Herb/mineral/chest node approach |
| 30 | `Grind` | Grind spot positioning and mob pull |
| 10 | `Ambient` | Town errands (vendor, repair, train, mail, auction, bank) |
| 0 | `None` | Unowned / idle |

### 3.2 Ownership Release & Combat-to-World Handoff
To prevent priority inversion where `WorldExecutor` (`MoveOwner::Quest` / `MoveOwner::Travel`) is permanently locked out after combat or hazard avoidance:
- **Post-Combat Release (`BotAI.cpp`):** Whenever `!target || !target->IsAlive() || !bot->IsValidAttackTarget(target)` (and on `ResetCombatState`), `BotMovement::ReleaseOwner(bot, MoveOwner::Combat)` is unconditionally invoked regardless of whether `MotionMaster` was in `CHASE_MOTION_TYPE` or `POINT_MOTION_TYPE`, and `MoveOwner::Avoidance` is released whenever `!state.flee.fleeing`.
- **Mid-Combat Hazard Release (`BotAI.cpp`):** Once `TryAvoidGroundHazards` and `TryAvoidBossTelegraphedAttacks` return `false` and the avoidance step-out point finishes (`!bot->isMoving()`), `MoveOwner::Avoidance` is immediately released so `MoveOwner::Combat` can resume chasing.
- **Ranged LOS Restoration (`BotAI.cpp` & `CombatMovement.cpp`):** When a ranged/caster bot (`preferredDist > MELEE_ENGAGE_RANGE`) loses line of sight (`!bot->IsWithinLOSInMap(target)`), `CombatMovement::ReadyToCast` refuses to call `ForceStopOwner`, and `BotAI` issues `Chase(bot, target, MoveOwner::Combat, ChaseRange(0.0f, MELEE_ENGAGE_RANGE))` so Detour/MotionMaster routes around the pillar, tree, or doorway until LOS is restored.

### 3.3 Token, Generation & Epoch Safety
Every physical movement dispatch in `BotMovement` is protected by three orthogonal concurrency guards inside `LocomotionShard`:
1. **`LocomotionToken` (`commandId`):** Monotonically allocated per claim. Stale `Release(token)` calls with an older `commandId` are rejected without mutating active state.
2. **`requestGeneration`:** Monotonically incremented whenever a `MovementRequest` is created or reset (`AllocateRequestGenerationLocked`). Before any physical leg or recovery leg is committed, `NavigateTransaction::IsRequestCurrent` verifies `req.requestGeneration == expectedGen && req.goalId == goalId`.
3. **`PlanningEpoch` (`planTicket` / `ownerEpoch`):** Prevents an older asynchronous/in-flight `MoveToInternal` path calculation from overwriting a newer `HoldPosition`, `Follow`, `Chase`, or `ForceStopOwner` issued by the same or higher owner while `PathGenerator` was running outside the shard lock.

---

## 4. Navigation Progress, 3D Reachability & Recovery State Machine

### 4.1 Route-Aware Progress (`NavProgress::UpdateRoute`)
Euclidean distance to the final goal (`goalDist2d`) often increases temporarily when a bot follows a valid navmesh corridor around a mountain ridge, switchback ramp, fenced enclosure, or multi-room building. `NavProgress::UpdateRoute` tracks two simultaneous progress vectors plus a 4-cell spatial ring buffer:

1. **Goal Progress (`best2d`, `bestDz`):** Net planar reduction (`>= 2.5 yd`) or vertical reduction (`>= 1.5 yd`) toward the final destination `(x, y, z)`. Resets `lastProgressAt` and `lastGoalProgressAt`, and clears the recovery ladder (`stage = 0`) once total recovery improvement reaches `recoveryResetYards` (`20 yd` planar or `8 yd` vertical).
2. **Active Leg Progress (`bestLeg2d`, `bestLegDz`):** Net planar or vertical reduction toward the current corridor leg endpoint `(legX, legY, legZ)` initialized via `StartLeg`. Credited as legitimate movement (`lastProgressAt = now`, incrementing `legSwitchbacks`) provided:
   - Time since `lastGoalProgressAt` has not exceeded `maxDetourLegMs` (`27,000 ms` circuit breaker), and
   - The bot is not oscillating across the same quantized `12 x 12 yd` cells (`!oscillatingArea`).
3. **4-Slot Spatial Cell Revisitation Ring (`recentCellX`, `recentCellY`, `cellRevisits`):** Detects when a bot visits cell `A -> B -> A -> B` without goal progress (`cellRevisits >= 2`), setting `oscillatingArea = true` and denying pure-leg credit so recovery triggers on schedule.

### 4.2 3D Geometric Reachability & Vertical Floor Separation
All arrival and area-membership checks validate both horizontal (`dist2d`) and vertical (`distZ = |botZ - targetZ|`) coordinates, plus navmesh topological reachability where vertical separation exists:
- `NavigateTransaction::MaxArrivalZ(acceptRadius)` computes `min(12.0f, max(4.0f, acceptRadius * 1.5f))`.
- `NavigateTransaction::IsWithin3dArrival(dist2d, acceptRadius, distZ)` requires `dist2d <= acceptRadius && distZ <= MaxArrivalZ(acceptRadius)`.
- `ObjectiveCommon::InArea(ctx, slack)` and `AreaArrivalRule::Evaluate` (`module/src/world/SpawnClustering.h`) select the nearest area member spawn point `(refX, refY, refZ)` (minimizing `|botZ - pz| + d2d * 0.25f` so large hillside clusters with varied spawn elevations evaluate against the local spawn band rather than only the area centroid). It enforces `refDz <= MaxAllowedZ(refDist2d, slack)` (`min(22.0f, 10.0f + refDist2d * 0.40f)`), and whenever `refDz > 6.0f` (`NeedsTopologicalCheck`), invokes `BotMovement::QueryDirectPathLength` to verify that a complete, same-floor navmesh path exists with length `<= MaxDirectPathLength(refDist2d, refDz)` (`max(24.0f, d3d * 1.85f + 10.0f)`). Disconnected cave floors, upper building floors, bridges overhead, and long unclimbed switchback ramps are rejected before `TravelToArea` can prematurely transition to `Search`.
- Early scans during `TaskPhase::TravelToArea` in `CreatureTargetHandler::Update` and `ObjectTargetHandler::Update` are gated by `ObjectiveCommon::InArea(ctx, EARLY_SCAN_SLACK)`, and `FindCreature`/`FindObject` reject out-of-LOS targets with `|targetZ - botZ| > 12.0f` while in `TravelToArea`.
- When `dist2d <= acceptRadius` but `distZ > MaxArrivalZ(acceptRadius)` (e.g., bot is in a cave tunnel beneath an overland NPC, under a bridge, or on the wrong floor of a tower/mine), `BotMovement::Navigate` increments `MovementStats::verticalRejections`, sets `lastFailureKind = NavFailureKind::VerticallyDisconnected`, and continues routing along the navmesh ramp/stairs rather than declaring a false arrival.
- When `ObjectiveCommon::Approach` or `ObjectTargetHandler::Update` sees a bot horizontally within interaction range (`dist2d <= range`) but out of 3D interaction range or out of line of sight (`!bot->IsWithinLOSInMap(target)`), it tightens `acceptRadius` to `1.5f–2.0f` so `Navigate` walks around the wall/obstacle or up the slope until true 3D + LOS interaction is satisfied.

### 4.3 Navigation Recovery Ladder
When `NavProgress::UpdateRoute` detects a genuine stall (`now - lastProgressAt > stallMs`), `BotMovement::Navigate` executes a 5-stage bounded recovery ladder:

```
[Normal Corridor Leg]
        |
        | (stall > 9s)
        v
Stage 1: NavRecovery::Repath
        | Re-queries RouteWaypoint + MoveToInternal from current position
        | (stall > 9s)
        v
Stage 2: NavRecovery::DetourLeft (+70 deg off goal bearing, 14 yd)
        | If DetourLeft fails IssueLeg on navmesh, falls through IMMEDIATELY
        | in the same tick to DetourRight
        v
Stage 3: NavRecovery::DetourRight (-70 deg off goal bearing, 14 yd)
        | Upon reaching within 4 yd of detour point, resets RecoveryMode::None
        | and resumes toward final goal
        | (stall > 9s or both detours blocked)
        v
Stage 4: RecoveryMode::Backtrack (up to 3 SafePositionHistory candidates, 3-60 yd)
        | (if all backtrack candidates fail or stall)
        v
Stage 5: NavRecovery::GiveUp -> NavStatus::Stuck
        | Clears request, releases LocomotionToken, reports NavStatus::Stuck to WorldExecutor
```

Additionally, if the very first leg to a goal fails `IssueLeg` twice consecutively (`consecutiveNoPath >= 2`) or hits `IsSharedCorridorBlocked`, `Navigate` immediately returns `NavStatus::Stuck` (`NavFailureKind::NoPath`) instead of erasing the request and returning `NavStatus::Blocked` forever.

---

## 5. Failure Classification & Cache Lifecycles

| Cache / Memory | Scope | Capacity & Key | TTL / Backoff | Purpose |
|---|---|---|---|---|
| `NopathCacheEntry` | Per-bot (`LocomotionShard`) | 1 entry per bot `(owner, x, y)` | `1,500 ms` | Prevents same bot from re-running `PathGenerator` on the exact same failed point on back-to-back ticks |
| `SharedNoPathCache` | Global (`LocomotionStateStore`) | 256-entry ring buffer keyed by `(mapId, instanceId, phaseMask, capabilityFlags, startPolyRef, sx/4, sy/4, sz/4, dx/4, dy/4, dz/4)` | `20,000 ms` (blocks at `strikes >= 2` from distinct `botGuid`s; invalidated on `RecordSuccess`) | Prevents 10–50 bots in the same topology/phase/capability context from hammering `PathGenerator` when a shared corridor is unreachable, without false-positive blocking across thin walls or phases |
| `FailureMemory` | Per-bot (`BrainState::failures`) | Up to 192 live entries (`Target`, `Spawn`, `Cluster`, `Quest`, `Npc`, `GatherNode`, `TravelDest`, `NavCorridor`) | `60s – 600s` base, exponential strike multiplier up to `8x` | Remembers failed targets, spawns, clusters, quests, and `DangerMap` corridor cells (`CorridorId(mapId, x, y)`) |
| `BehavioralLoopDetector` | Per-bot (`BrainState::loopDetector`) | Three 8-slot timestamped rings (`_targets`, `_areas`, `_tasks`) | Windowed (`60s` target, `120s` area, `90s` task); cleared on `OnProgress` | Detects and breaks `TargetApproachLoop` (3x), `AreaPingPong` (3x), and `TaskOscillation` (3x) |
| `ReservationTable` | Global (`WorldReservations`) | Exclusive (`Creature`, `GameObject`, `GatherNode`) + shared (`QuestCluster`) | `45s – 120s` lease, swept periodically | Prevents multiple bots from dogpiling the same mob/node or overcrowding a small camp |
| `PopulationGrid` | Global (`PopulationHeatmap`) | `100 x 100 yd` spatial hash cells per `mapId` | Updated per tick; `incomingBots` synced to `TaskPhase::TravelToArea` | Feeds crowd penalties into `WorldUtility` scoring |

---

## 6. Open-World Decision Hysteresis & Danger Route Resilience

1. **Quest & Cluster Commitment Hysteresis (`WorldUtility.h`, `WorldPlanner.cpp`):**
   - `UtilityWeights::activeQuestHysteresis` (`+18.0f`, configurable via `CoaBots.WorldBrain.Weight.ActiveQuestHysteresis`) gives a commitment bonus to objectives belonging to `state.task.questId` (or `state.lastCommittedQuestId`), preventing replan oscillation when two quests have nearly equal utility scores.
   - `ClusterWeights::currentAreaHysteresis` (`+15.0f`, configurable via `CoaBots.WorldBrain.Weight.CurrentAreaHysteresis`) gives a commitment bonus to `state.task.areaIndex`, preventing bots from ping-ponging between two camps when crowd counts fluctuate by 1 bot.
2. **Danger Route Corridor Persistence (`WorldExecutor.cpp`):**
   - `DangerRouteState::routeKey` is keyed by `(type, questId, objectiveIndex, areaIndex, entryId, mapId, qx, qy)` **excluding** `task.sub` so sub-target changes inside an objective do not wipe `route.disabled` or `route.next`.
   - Waypoints returned by `DangerMap::Plan` have their Z coordinate interpolated along the start-to-goal slope and are filtered against `FailureMemory::Has(FailKind::NavCorridor, CorridorId(mapId, wx, wy), now)`.
   - If a danger waypoint fails navigation (`NavStatus::Stuck`), `WorldExecutor::TravelTo` records `FailKind::NavCorridor` for that waypoint cell, increments `dangerWaypointsSkipped`, advances `++route.next` to try the remaining waypoints, and disables the danger route for that task once waypoints are exhausted.
