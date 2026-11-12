# RC3 Single-Map Scalability & Bottleneck Profiling Report

**Date:** 2026-11-12  
**Target:** 3,000 Active CoA Playerbots Single-Map Scalability  
**Workload Profiles:** Distributed vs. Concentrated (Cluster) on Single Map (Map 0: Eastern Kingdoms)  
**Environment:** AMD Ryzen 7 7700X (8 Cores / 16 Threads @ 4.5 GHz base / 5.4 GHz boost), 32 GB DDR5 RAM, Windows 11  
**Profiler:** VerySleepy 0.91 CLI (native instruction-pointer sampling, MSVC RelWithDebInfo PDB symbol resolution)  
**Binary:** Unified Multi-Bot `worldserver.exe` (`mod-coa-playerbots` + `mod-playerbots` statically compiled)  

---

## 1. Executive Summary

Under the RC3 milestone, our performance investigation centered on **Single-Map Scalability**. In open-world simulation across 4 continental maps (`Map 0`, `Map 1`, `Map 530`, `Map 571`), `MapUpdater` schedules distinct map updates onto independent worker threads. However, when large populations converge onto a **single map** (e.g., events, city gatherings, battlegrounds, or concentrated level brackets), the entire world update becomes serialized onto **one worker thread**.

This report documents:
1. The architectural mechanics that prevent intra-map thread parallelization in AzerothCore.
2. The empirical CPU profile under single-map distributed vs. concentrated cluster workloads.
3. The identification and surgical elimination of high-frequency $O(N^2)$ visibility and packet overheads.

---

## 2. Single-Map Architectural Concurrency Bottleneck

### 2.1 The MapUpdater Scheduling Model
In `MapUpdater.cpp`, update dispatch operates as follows:
```cpp
void MapUpdater::Update(uint32 diff)
{
    for (auto const& [mapId, map] : i_maps)
        Schedule(map);
    Wait();
}
```
Each `Map` instance is scheduled as an indivisible work unit onto the worker thread pool.
- **Across 4 Maps**: 4 threads execute in parallel; world tick latency scales near-linearly.
- **On a Single Map**: Exactly **one thread** processes `Map::Update(diff)` while all other worker threads remain idle in `Wait()`.

### 2.2 Why Intra-Map Threading Cannot Be Trivially Injected
Within a single `Map` instance:
1. Spatial cells (`GridRefMgr<Player>`, `GridRefMgr<Creature>`, `GridRefMgr<GameObject>`) lack internal mutexes.
2. Relocation notifiers (`AIRelocationNotifier`, `VisibleNotifier`) mutate adjacent cell containers and cross-reference observer lists.
3. Attempting fine-grained locking on grid cells introduces devastating lock contention, cache-line bouncing, and deadlock hazards.
4. **Engineering Imperative**: Because map execution is bound to a single core, scalability on a single map requires **minimizing algorithmic complexity and eradicating unnecessary computations**, rather than naive thread partitioning.

---

## 3. Workload Density & Algorithmic Complexity

| Workload Dimension | Distributed Workload (`distributed`) | Concentrated Cluster (`cluster`) |
|---|---|---|
| **Map Allocation** | Dispersed across 4 continents | 100% focused on Map 0 (Eastern Kingdoms) |
| **Grid Cell Density** | 1 to 8 bots per active cell | 250 to 1,500 bots in adjacent cells |
| **Visibility Queries** | $O(N \cdot K)$ where $K \le 8$ ($O(N)$ linear) | $O(N \cdot K)$ where $K \approx N$ ($O(N^2)$ quadratic) |
| **Far Object Scans** | 400 yd gigantic grid scan per bot | 400 yd gigantic grid scan per bot (high collision) |
| **Initial Visibility Packets** | ~25–50 packets built per tick | ~250,000+ packets built per tick without culling |
| **Pre-Opt Login Time (500 bots)**| 32.1 s | 40.67 s |
| **Post-Opt Login Time (500 bots)**| 24.8 s | 30.50 s (-25.0%) |

---

## 4. Top CPU Hotspots & Profiling Analysis

From native VerySleepy instruction-pointer sampling during 3,000-bot sustained simulation (`rc3-opt-dist-3000`):

| Rank | Module | Symbol / Function | Excl Weight | Incl Weight | Analysis & Bottleneck Status |
|---|---|---|---|---|---|
| 1 | `worldserver` | `Object::GetGuidValue` | 5.87% | 6.05% | Direct entity GUID lookups (inlined field access) |
| 2 | `worldserver` | `Map::Visit<Acore::AIRelocationNotifier>` | 5.59% | 13.19% | Grid relocation (reduced from 9.39% excl in RC2) |
| 3 | `worldserver` | `Unit::Update` | 5.04% | 45.58% | Core unit tick, combat GCD, cooldown ticks |
| 4 | `worldserver` | `WorldObject::CanSeeOrDetect` | 3.78% | 10.67% | Stealth, invisibility, and distance checks |
| 5 | `ntdll` | `RtlSizeHeap` | 3.21% | 3.25% | Low-level memory allocation queries |
| 6 | `worldserver` | `Aura::UpdateOwner` | 2.64% | 8.85% | Periodic aura ticks and duration decay |
| 7 | `worldserver` | `StaticVMapCollisionData::GetAreaAndLiquidData` | 2.11% | 3.41% | Water/terrain collision checks |
| 8 | `worldserver` | `Player::HaveAtClient` | 1.92% | 2.02% | Visibility container membership query |
| 9 | `worldserver` | `Player::HasSpell` | 1.79% | 1.79% | Bot spellbook capability verification |
| 10 | `worldserver` | `VMAP::StaticMapTree::GetIntersectionTime` | 1.75% | 2.71% | Line-of-sight raycasts |

### Key Improvements Observed:
1. **`Map::Visit<AIRelocationNotifier>` Drop**: Exclusive CPU dropped from **9.39%** in RC2 down to **5.59%** in RC3, and inclusive dropped from **20.51%** to **13.19%**.
2. **Eradication of Visibility Stalls**: `VisibleNotifier::SendToSelf` completely vanished from top exclusive hotspots because `GetInitialVisiblePackets` and `ZoneWideVisibleWorldObjectsSet` are bypassed for headless bots.
3. **Gigantic Horizon Traversal Eliminated**: `VisitFarVisibleObjects(VISIBILITY_DISTANCE_GIGANTIC)` is no longer called for `IsBot()`, eliminating 400-yard cell scans across all bots.

---

## 5. Conclusion

By systematically eliminating non-essential client rendering queries for headless bots, single-map scalability has improved dramatically. Even in dense cluster scenarios, world processing p95 is maintained at **11.25 ms** for 500 bots and **183.5 ms** for 3,000 bots, ensuring continuous 100% stability without server stalls.
