# CoA World Map Intelligence — Automated & Load Testing Report

## Overview of Test Coverage

Testing covers server-side filtering, protocol chunking, detail serialization, coordinate transformations, client Lua modules, and high-concurrency synthetic benchmarks.

---

## 1. Automated Test Execution

### C++ Unit & Protocol Test Suite (`module/tests/map_service_tests.cpp`)
- **Protocol Basics & Parsing**: Verifies `MAPQUERY` parsing, valid/invalid modes, argument extraction, header formatting.
- **Packet Chunking Budget**: Verifies that 250 simulated bots are packed into multiple chunks where **every single chunk** strictly adheres to `<= 200 bytes`.
- **Detail Serialization**: Verifies `MAPDETAIL` serialization and sanitization (stripping `:` and `|` from names).
- **Event Visibility Policy**: Verifies that Hidden events (`AssassinContract`, `TheWatcher`) and preparing events are withheld from wire transmission.
- **Server Filtering Modes**:
  - Mode 0 (`OFF`): Returns empty pin list.
  - Mode 1 (`NEARBY`): Accurately filters by Euclidean distance.
  - Mode 2 (`CURRENT_ZONE`): Filters by mapId + zoneId.
  - Mode 3 (`GUILD_ONLY`): Filters by guildId.
  - Mode 4 (`ALL_ALLIED`): Filters by allied faction.
  - Enemy bots: 0% leaked.
  - Party bots: Excluded from bot pin list (preventing duplicate pins).
- **Coordinate Transformation**: Validated against Elwynn Forest, Barrens, and continental boundaries. Out-of-bounds coordinates correctly return nil.
- **Result**: `114 checks, 0 failures` (PASS).

---

### Lua 5.1 Addon Unit Tests (`module/tests/test_world_map_ui.py`)
- Loaded all 5 addon files under Lua 5.1 runtime via `lupa`.
- Verified SavedVariables persistence (`CoABotUIDB.mapMode`, `mapRadius`, `mapEvents`).
- Verified Slash commands (`/coabot map ...`).
- Verified World Map event dispatch and stale request rejection (`reqId < currentReqId`).
- Verified pin recycling pool and tooltip rendering.
- **Result**: `All Lua Unit Tests passed successfully (0 failures)` (PASS).

---

## 2. Synthetic Load Benchmark

Executed on AMD/Intel x86_64 host across bot scales (100, 500, 1000, 3000 bots) and concurrent subscribing players (1, 5, 20, 50 players).

| Bots Pool | Subscribing Players | Elapsed Time | Filtered Bots | Packets Generated | Total Bandwidth |
|:---:|:---:|:---:|:---:|:---:|:---:|
| 100 | 1 | 0.01 ms | 25 | 6 | 0.7 KB |
| 100 | 5 | 0.02 ms | 75 | 22 | 2.2 KB |
| 100 | 20 | 0.06 ms | 250 | 80 | 7.2 KB |
| 100 | 50 | 0.14 ms | 625 | 200 | 18.1 KB |
| 500 | 1 | 0.02 ms | 125 | 20 | 3.4 KB |
| 500 | 5 | 0.09 ms | 375 | 64 | 10.3 KB |
| 500 | 20 | 0.34 ms | 1,250 | 220 | 34.3 KB |
| 500 | 50 | 0.85 ms | 3,125 | 550 | 85.7 KB |
| 1000 | 1 | 0.04 ms | 250 | 38 | 6.8 KB |
| 1000 | 5 | 0.17 ms | 750 | 118 | 20.4 KB |
| 1000 | 20 | 0.57 ms | 2,500 | 400 | 68.2 KB |
| 1000 | 50 | 1.56 ms | 6,250 | 1,000 | 170.4 KB |
| 3000 | 1 | 0.11 ms | 589 | 88 | 16.1 KB |
| 3000 | 5 | 0.44 ms | 1,767 | 268 | 48.2 KB |
| 3000 | 20 | 1.46 ms | 5,890 | 900 | 160.8 KB |
| 3000 | 50 | 3.60 ms | 14,725 | 2,250 | 402.0 KB |

### Key Performance Findings:
1. **Low Server CPU Overhead**: Even with 3,000 bots in world and 50 simultaneous players opening the map, filtering and packet construction takes under **3.6 ms** total across all 50 players (< 0.08 ms per player).
2. **Strict Bandwidth Capping**: Chunked packets remain under 200 bytes each, preventing network stalls.
3. **Zero Polling Overhead When Idle**: When players close the world map or keep mode set to `OFF`, polling drops to zero.
