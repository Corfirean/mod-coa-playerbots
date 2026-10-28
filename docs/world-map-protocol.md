# CoA World Map Intelligence — Wire Protocol Specification

Companion specification for the World Map Intelligence layer communication between the client-side `CoABotUI` AddOn and the server-side `BotMapService`.

## Channel & Transport

- Addon Message Prefix: `COABOT`
- Client -> Server Transport: `SendAddonMessage("COABOT", body, "WHISPER", UnitName("player"))`
- Server -> Client Transport: Synthetic `CHAT_MSG_WHISPER` / `LANG_ADDON` packet delivered to player's session (`SendCoaBotReply`).
- Packet Size Limit: All serialized reply chunks strictly adhere to `<= 200 bytes` payload budget to prevent chat throttle and truncation in WoW 3.3.5a. Progressive UTF-8-safe truncation is enforced server-side.
- Isolated Request IDs: Monotonically increasing request IDs are separated by channel (`queryReqId`, `detailReqId`, `eventsReqId`) to prevent tooltip hovering or dynamic events from invalidating active bot snapshots.

---

## Verbs & Message Flow

### 1. `MAPQUERY` (Client -> Server)
Queries online allied bots based on active display mode.

**Wire Format:**
```text
MAPQUERY:<reqId>:<mode>[:<param1>[:<param2>]]
```

**Parameters:**
- `reqId`: Monotonically increasing request identifier (integer) used for transactional snapshot matching.
- `mode`:
  - `0`: `OFF` (returns empty result, server clears state)
  - `1`: `NEARBY` (`param1` = radius in yards, clamped to [100, 5000], default 1000)
  - `2`: `CURRENT_ZONE` (`param1` = zoneId or 0/-1 for server auto-detect)
  - `3`: `GUILD_ONLY` (`param1` = mapId or -1 for worldwide guild query)
  - `4`: `ALL_ALLIED` (`param1` = mapId; explicitly supports Eastern Kingdoms `0`, Kalimdor `1`, Outland `530`, Northrend `571`, or `-1` for all maps)

---

### 2. `MAPBEGIN` (Server -> Client)
Header announcing a fresh snapshot.

**Wire Format:**
```text
MAPBEGIN:<reqId>:<totalBots>:<totalChunks>:<revision>
```
- Client drops the message if `reqId ~= queryReqId`.
- On receipt, client initializes a transactional staging buffer expecting exactly `totalChunks` chunks and `totalBots` entries.

---

### 3. `MAPBOTS` (Server -> Client)
Chunked list of compact bot pin snapshots.

**Wire Format:**
```text
MAPBOTS:<reqId>:<chunkIdx>:<bot1>|<bot2>|...
```

**Bot Entry Format:**
```text
<guidLow>,<mapId>,<worldX>,<worldY>,<level>,<classId>,<role>,<flags>,<activity>
```
- `guidLow`: Bot character low GUID (integer).
- `mapId`: Authoritative server map ID (e.g. 0=Eastern Kingdoms, 1=Kalimdor, 530=Outland, 571=Northrend). Used by client to ensure bots are never projected on the wrong continent.
- `worldX`, `worldY`: Rounded integer world coordinates in yards.
- `level`: Character level (1-80).
- `classId`: Character base class ID (including custom Ascension classes 12-32).
- `role`: Effective role (0=DPS, 1=Tank, 2=Healer, 3=Support).
- `flags`: Bitmask:
  - `bit 0 (0x01)`: `GuildMember` (bot belongs to requesting player's guild)
  - `bit 1 (0x02)`: `HasTask` (bot has active gather/craft/gold order)
  - `bit 2 (0x04)`: `InCombat`
  - `bit 3 (0x08)`: `Ghost` / Dead
- `activity`: Resolved AI activity ID:
  - `0`: Idle
  - `1`: Questing
  - `2`: Traveling
  - `3`: Gathering
  - `4`: Crafting
  - `5`: In Combat
  - `6`: Resting
  - `7`: Group Adventure
  - `8`: In Dungeon
  - `9`: World Encounter
  - `10`: Wandering / Other

---

### 4. `MAPEND` (Server -> Client)
Signals transmission completion for `reqId`.

**Wire Format:**
```text
MAPEND:<reqId>
```
- Client verifies transactional integrity: ensures `receivedChunkCount == expectedChunks` and `#pins == expectedBots`.
- If incomplete (e.g. dropped chunks), transaction is safely discarded to prevent corrupted display.
- If complete, atomically commits staging buffer into active pins, recalculates screen projection, clusters dense pins using spatial hashing, and updates the display.

---

### 5. `MAPDETAIL` (Bidirectional)
On-demand query for heavy bot metadata (character name, specialization, iLvl, guild assignment progress).

**Client -> Server Request:**
```text
MAPDETAIL:<reqId>:<botGuidLow>
```

**Server -> Client Reply:**
```text
MAPDETAIL:<reqId>:<botGuidLow>:<name>:<specId>:<avgIlvl>:<specName>:<activityName>:<taskType>:<taskItem>:<taskTarget>:<taskDone>:<taskStatus>
```
- `name`: Real character name (e.g. "Arthur", "Malfurion"). Sanitized against wire delimiters.
- `taskType`: `0`=None, `1`=Gather, `2`=Craft, `3`=Gold.
- `taskItem`: Item ID (or 0 for Gold).
- `taskTarget`, `taskDone`: Numeric progress values.
- **Privacy Enforcement**: Task details (`taskType`, `taskItem`, `taskTarget`, `taskDone`, `taskStatus`) are only populated if `commander->GetGuildId() != 0 && bot->GetGuildId() == commander->GetGuildId()`. Otherwise zeroed out server-side.

---

### 6. `MAPEVENTS` (Bidirectional, Transactional)
Dynamic world event query and notification.

**Client -> Server Request:**
```text
MAPEVENTS:<reqId>:<mapId>
```

**Server -> Client Reply:**
```text
MAPEVENTSBEGIN:<reqId>:<totalEvents>:<totalChunks>
MAPEVENTS:<reqId>:<chunkIdx>:<event1>|<event2>|...
MAPEVENTSEND:<reqId>
```
- **Server Visibility Policy**: Multi-tier server policy strictly filters events. Only `FactionSkirmish` events in phase `Active` (2) marked `Public` are revealed. All hidden, candidate, prepare, TheWatcher, and AssassinContract events are rejected.
- When no events exist, server sends `MAPEVENTSBEGIN:<reqId>:0:0` and `MAPEVENTSEND:<reqId>`, clearing stale markers on the client.
