# CoA World Map Intelligence — Wire Protocol Specification

Companion specification for the World Map Intelligence layer communication between the client-side `CoABotUI` AddOn and the server-side `BotMapService`.

## Channel & Transport

- Addon Message Prefix: `COABOT`
- Client -> Server Transport: `SendAddonMessage("COABOT", body, "WHISPER", UnitName("player"))`
- Server -> Client Transport: Synthetic `CHAT_MSG_WHISPER` / `LANG_ADDON` packet delivered to player's session (`SendCoaBotReply`).
- Packet Size Limit: All serialized reply chunks strictly adhere to `<= 200 bytes` payload budget to prevent chat throttle and truncation in WoW 3.3.5a.

---

## Verbs & Message Flow

### 1. `MAPQUERY` (Client -> Server)
Queries online allied bots based on active display mode.

**Wire Format:**
```text
MAPQUERY:<reqId>:<mode>[:<param1>[:<param2>]]
```

**Parameters:**
- `reqId`: Monotonically increasing request identifier (integer) used for stale packet detection.
- `mode`:
  - `0`: `OFF` (returns empty result, server clears state)
  - `1`: `NEARBY` (`param1` = radius in yards, clamped to [100, 5000], default 1000)
  - `2`: `CURRENT_ZONE` (`param1` = zoneId or 0 for server auto-detect)
  - `3`: `GUILD_ONLY` (no parameters; queries player's guild members worldwide)
  - `4`: `ALL_ALLIED` (`param1` = mapId or 0 for current map)

---

### 2. `MAPBEGIN` (Server -> Client)
Header announcing a fresh snapshot.

**Wire Format:**
```text
MAPBEGIN:<reqId>:<totalBots>:<totalChunks>:<revision>
```
- Client drops the message if `reqId < currentReqId`.
- On receipt, client initializes a fresh staging buffer for pins.

---

### 3. `MAPBOTS` (Server -> Client)
Chunked list of compact bot pin snapshots.

**Wire Format:**
```text
MAPBOTS:<reqId>:<chunkIdx>:<bot1>|<bot2>|...
```

**Bot Entry Format:**
```text
<guidLow>,<worldX>,<worldY>,<level>,<classId>,<role>,<flags>,<activity>
```
- `guidLow`: Bot character low GUID (integer).
- `worldX`, `worldY`: Rounded integer world coordinates in yards.
- `level`: Character level (1-80).
- `classId`: Character base class ID.
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
- Client commits staging buffer into active pins, recalculates screen projection, clusters dense pins, and updates the display.

---

### 5. `MAPDETAIL` (Bidirectional)
On-demand query for heavy bot metadata (specialization, iLvl, guild assignment progress).

**Client -> Server Request:**
```text
MAPDETAIL:<reqId>:<botGuidLow>
```

**Server -> Client Reply:**
```text
MAPDETAIL:<reqId>:<botGuidLow>:<specId>:<avgIlvl>:<specName>:<activityName>:<taskType>:<taskItem>:<taskTarget>:<taskDone>:<taskStatus>
```
- `taskType`: `0`=None, `1`=Gather, `2`=Craft, `3`=Gold.
- `taskItem`: Item ID (or 0 for Gold).
- `taskTarget`, `taskDone`: Numeric progress values.
- Text fields are sanitized (colons and pipes stripped).

---

### 6. `MAPEVENTS` (Bidirectional)
Dynamic world event query and notification.

**Client -> Server Request:**
```text
MAPEVENTS:<reqId>:<mapId>
```

**Server -> Client Reply:**
```text
MAPEVENTS:<reqId>:<eventId>,<eventType>,<centerX>,<centerY>,<phase>,<allianceCount>,<hordeCount>,<name>|...
```
- Server visibility policy: Hidden events (e.g. Assassin contracts) and prepare phases are filtered out on the server before serialization. Only active public/discoverable events are returned.
