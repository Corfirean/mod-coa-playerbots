# CoA World Map Intelligence — System Architecture & Integration Guide

## Overview

The **CoA World Map Intelligence** subsystem integrates companion bot awareness and dynamic open-world observation directly into World of Warcraft's standard 3.3.5a World Map interface within the `CoABotUI` AddOn.

It allows players to locate and inspect allied bots, monitor guild companions, track resource gathering and crafting progress, and observe dynamic world skirmishes in real time, all styled to match Blizzard's native Wrath of the Lich King interface.

---

## Architecture

```text
               +-------------------------------------------+
               |             WoW 3.3.5a Client             |
               |                                           |
               |   +-----------------------------------+   |
               |   |          WorldMapFrame            |   |
               |   |  - Native Map Pins (Party/Raid)   |   |
               |   |  - CoABotMapMarkers (Bot Pins)    |   |
               |   |  - CoABotMapTooltip (Tooltips)    |   |
               |   |  - CoABotMapSettings (Controls)   |   |
               |   +-----------------------------------+   |
               |                     ^                     |
               |                     | (Pin updates)       |
               |   +-----------------------------------+   |
               |   |         CoABotWorldMap            |   |
               |   |   - Lifecycle (Show/Hide/Cadence) |   |
               |   |   - CoABotWorldMapData (DBC)      |   |
               |   +-----------------------------------+   |
               +-------------------------------------------+
                                     |
                         SendAddonMessage("COABOT")
                                     |
               +-------------------------------------------+
               |            AzerothCore Server             |
               |                                           |
               |   +-----------------------------------+   |
               |   |          BotAddonChat             |   |
               |   +-----------------------------------+   |
               |                     |                     |
               |   +-----------------------------------+   |
               |   |          BotMapService            |   |
               |   |   - Filter by Faction/Zone/Guild  |   |
               |   |   - 200-Byte Packet Chunking      |   |
               |   |   - Activity Resolution           |   |
               |   |   - Event Visibility Policy       |   |
               |   +-----------------------------------+   |
               |         |           |           |         |
               |      BotMgr     WorldBrain  BotEconomy    |
               +-------------------------------------------+
```

---

## 5 Visibility Modes

1. **MODE 0 — OFF**
   - No map pins generated.
   - Network polling timer stopped completely.
   - Zero CPU and network impact when off.

2. **MODE 1 — NEARBY**
   - Allied bots within $N$ yards (100 to 5000 yd, default 1000 yd, step 100 yd).
   - Distance computed in 3D Euclidean space on the server.
   - Refreshed every 3 seconds while map is open.

3. **MODE 2 — CURRENT ZONE**
   - Allied bots in the same map and zone ID as the player.
   - Refreshed every 5 seconds while map is open.

4. **MODE 3 — GUILD ONLY**
   - Only online bots belonging to the player's guild across all maps.
   - Refreshed every 10 seconds while map is open.
   - Displays real-time guild assignment status (gathering/crafting/gold).

5. **MODE 4 — ALL ALLIED**
   - All allied faction bots on the currently viewed map.
   - Server authoritative faction filtering (includes custom CoA races).
   - Refreshed every 15 seconds while map is open.

---

## Coordinate Transformation

WoW world coordinates are non-trivial:
- `worldX`: North (+), South (-)
- `worldY`: West (+), East (-)

On 2D map textures (0,0 is Top-Left, 1,1 is Bottom-Right):
$$\text{normX} = \frac{\text{worldY} - y_1}{y_2 - y_1}$$
$$\text{normY} = \frac{\text{worldX} - x_1}{x_2 - x_1}$$

Boundary data for all 307 zones and continents are extracted directly from `WorldMapArea.dbc` into `CoABotWorldMapData.lua`, guaranteeing exact coordinate projection for standard zones as well as custom Ascension maps (`tolbarad`, `TorWatha`, etc.).

---

## Clustering & Interpolation

- **Clustering**: At continent scale or dense bot gatherings (< 22 screen pixels), individual pins collapse into a cluster badge showing total bot count. Clusters containing guild members display a golden ring.
- **Interpolation**: Positions smoothly interpolate between server updates using an `OnUpdate` handler. Jumps $> 0.2$ map fraction (teleports/flight) snap immediately to avoid visual artifacts.
- **Party Member Safety**: Bots in the player's party or raid are tracked by the native Blizzard party pins; the intelligence layer never renders duplicate pins for them.

---

## Dynamic World Events Foundation

The `WorldMapEventSnapshot` interface and `IWorldEventProvider` abstraction allow public world events (e.g. `FactionSkirmish`) to display on the map when active, while strictly enforcing visibility policies:
- **PUBLIC**: Displayed only once combat / active phase begins.
- **HIDDEN**: Filtered server-side prior to serialization (e.g. `AssassinContract`, `TheWatcher`).
- **DISCOVERABLE**: Filtered until discovery conditions are met.
- The map compiles and functions seamlessly with or without `WorldEncounterDirector`.
