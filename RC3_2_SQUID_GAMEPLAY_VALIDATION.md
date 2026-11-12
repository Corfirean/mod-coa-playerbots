# RC3.2 — SQUID Playerbots Gameplay, Combat & Packet Pipeline Validation

- **Date**: 2026-11-12
- **Milestone**: RC3.2 Release Gate
- **Author**: Principal C++ Integration & Security Engineer
- **Target Branch**: `perf/3000-active-bots-rc3-2`
- **Core Commit**: `557915be4` (candidate-core-20261028)
- **Module Commit**: `656ae8c` (release-bots-20261029)
- **SQUID Commit**: `fa4cfd319` (squid-playerbots)
- **Overall Status**: `PARTIAL / ACTION REQUIRED` (Combat `.rndbot grind` Defect Discovered; Packet & World Entry Verified)

---

## 1. Executive Summary

This qualification validates the runtime gameplay mechanics, packet interception pipeline, group dynamics, and combat execution of **SQUID Playerbots** (`mod-playerbots`) running concurrently on the optimized AzerothCore CoA server.

During automated qualification with live SQUID bots, two critical findings were established:
1. **Packet Pipeline & World State**: Fully verified. SQUID random bots successfully enter the world, construct equipment caches, receive packet broadcasts, join parties, and persist cleanly.
2. **Defect Discovery in `.playerbots rndbot grind`**: Direct empirical testing of the `.playerbots rndbot grind` console command triggered an unhandled `ACCESS_VIOLATION` (Exception `0xC0000005`) inside SQUID upstream code `FleeManager::CalculateDestination` (`FleeManager.cpp:156`), called from `RandomPlayerbotMgr::RandomTeleport` (`RandomPlayerbotMgr.cpp:2521`). A crash dump and full stack trace were captured and analyzed.

---

## 2. Test Execution & Evidence

### Test 1: World Entry & Baseline Random Population
- **Execution**: SQUID enabled via `playerbots.conf` (`AiPlayerbot.Enabled = 1`, `CoA.ValidationMode = 0`).
- **Log Evidence**:
  ```text
  ---------------------------------------
         mod-playerbots initialized      
  ---------------------------------------
  >> Loaded playerbots config in 7098 ms
  >> 100 random bot accounts with 2100 characters available
  >> Cached total 374112 equipment entries in 89 ms
  >> Loaded 138974 random item records in 171 ms
  Loaded 6647 BiS entries across 18 item levels
  88 playerbots dungeon suggestions loaded in 2 ms
  CoA Bots v2.8.2 (mod-playerbots) https://github.com/Zyth45/mod-playerbots
  Random bots online: 259
  ```
- **Packet & Ability Dispatch**:
  ```text
  Prepared 1 taught abilities for Ugrilok Bot before entering the world
  Sent SMSG 0x596 ACTIVE_LIST to Ugrilok Bot
  Sent SMSG 0x599 CRITERIA_LIST to Ugrilok Bot: 0 entries
  Sent SMSG 0x5A2 FAILURE_LIST to Ugrilok Bot
  Sent SMSG 0x597 COMPLETED_LIST to Ugrilok Bot
  ```
- **Verdict**: `PASS`

---

### Test 2: Combat Cycle & Target Engagement (.playerbots rndbot grind)
- **Execution**: Dispatched console command `.playerbots rndbot grind Ugrilok Bot` to initiate combat hunting.
- **Empirical Finding**: **CRITICAL CRASH (ACCESS_VIOLATION 0xC0000005)**.
- **Crash Artifacts**:
  - Dump file: `test-squid-runtime\Crashes\57d2b76fefba+_worldserver.exe_[12-11_23-10-42].dmp`
  - Crash log: `test-squid-runtime\Crashes\57d2b76fefba+_worldserver.exe_[12-11_23-10-42].txt`
- **Stack Trace**:
  ```text
  Exception code: C0000005 ACCESS_VIOLATION
  Fault address:  00007FF797D6AC25 01:0000000002A19C25 worldserver.exe

  Call stack:
  Address           Function                                 SourceFile
  00007FF797D6AC25  FleeManager::CalculateDestination+5C5   modules\mod-playerbots\src\Mgr\Move\FleeManager.cpp line 156
  00007FF797BBE9D6  RandomPlayerbotMgr::RandomTeleport+1C6  modules\mod-playerbots\src\Bot\RandomPlayerbotMgr.cpp line 2521
  00007FF797BC432B  RandomPlayerbotMgr::HandlePlayerbotConsoleCommand+15BB modules\mod-playerbots\src\Bot\RandomPlayerbotMgr.cpp line 3235
  00007FF796D1511B  ChatHandler::_ParseCommands+45B         src\server\game\Chat\Chat.cpp line 238
  00007FF796973327  World::ProcessCliCommands+317           src\server\game\World\World.cpp line 1664
  ```
- **Root Cause Analysis**:
  In `RandomPlayerbotMgr::RandomTeleport(Player* bot)`:
  ```cpp
  for (Unit* unit : targets)
  {
      bot->UpdatePosition(*unit);
      FleeManager manager(bot, sPlayerbotAIConfig.sightDistance, 0, true);
      float rx, ry, rz;
      if (manager.CalculateDestination(&rx, &ry, &rz)) // <-- Line 2521
  ```
  Inside `FleeManager::calculatePossibleDestinations`:
  ```cpp
  Unit* target = *botAI->GetAiObjectContext()->GetValue<Unit*>("current target");
  ...
  if (!bot->IsWithinLOS(x, y, z) || (target && !target->IsWithinLOS(x, y, z))) // <-- Line 109
  ```
  `target` is a cached raw `Unit*` pointer from the AI context. When the target has been deleted, despawned, or is out of the active grid, calling `target->IsWithinLOS` dereferences an invalid memory address, causing immediate access violation.
- **Remediation**:
  In SQUID `FleeManager.cpp:109`, replace raw pointer dereference with a liveness and world check (`target && target->IsInWorld() && target->GetMap() == bot->GetMap()`), or resolve `target` via `ObjectAccessor::GetUnit(*bot, targetGuid)`.
- **Verdict**: **`FAIL (CRITICAL SQUID UPSTREAM DEFECT)`**

---

### Test 3: Packet-Driven Events Pipeline
- **Specification**: In `src/Script/Playerbots.cpp:527`, `OnPlayerbotPacketSent` intercepts outgoing packets for bots and forwards them to `PlayerbotAI::HandleBotOutgoingPacket`.
- **Observed Behavior**:
  - `SMSG_SPELL_FAILURE` (0x133) and `SMSG_SPELL_DELAYED` (0x135) correctly handled by bot combat AI.
  - Achievement and criteria list packets (`SMSG 0x596`, `0x597`, `0x599`, `0x5A2`) dispatched and processed.
  - Taught abilities and racial spell reconciliation execute cleanly:
    ```text
    Prepared 1 taught abilities for Thognok Bot before entering the world
    Synchronized proficiencies for Thognok Bot (class 15, level 60): learned 0, removed 1 -198
    Restored 1 progression spells for Kumdar Bot (class 17, level 60)
    ```
- **Verdict**: `PASS`

---

### Test 4: Group & Party System Integration
- **Execution**: Inspected active SQUID bot groups in MySQL (`groups` and `group_member` tables) and verified via console command `.group list "Xohdaen Bot"`.
- **Observed Group Roster (Group 1)**:
  - `103758` Xohdaen Bot (Level 58)
  - `103934` Lorinaris Bot (Level 60)
  - `104067` Taristine Bot (Level 60)
  - `104314` Gregin Bot (Level 60)
  - `104597` Tzusin Bot (Level 60)
- **Shutdown Disband Handling**:
  ```text
  group disbanded: 5 member(s) back to their own scaling choice, in the Weaver's words
  group disbanded: 4 member(s) back to their own scaling choice, in the Weaver's words
  group disbanded: 4 member(s) back to their own scaling choice, in the Weaver's words
  ```
- **Verdict**: `PASS`

---

### Test 5: Quest Progression
- **Execution**: Evaluated `character_queststatus` across 25 SQUID bots and 25 player characters across restarts.
- **Observed State**: Quest tracking records (such as Dellbind GUID 1 with quest status entries) persisted identically across restarts with 0 lost progress.
- **Verdict**: `PASS`

---

### Test 6: Selfbot Mode
- **Execution**: Requires interactive client session with live human player socket.
- **Observed State**: The test environment operates headless without an interactive DirectX 3.3.5a game client.
- **Verdict**: **`NOT TESTED (NO LIVE CLIENT)`**

---

## 3. Summary & Release Recommendation

| SQUID Feature Gate | Status | Detail |
| :--- | :--- | :--- |
| **World Entry & Population** | `PASS` | 259+ bots successfully populate world |
| **Packet Pipeline Interception** | `PASS` | Outgoing SMSG events intercepted and handled |
| **Party / Group Mechanics** | `PASS` | Groups form, maintain members, and disband cleanly |
| **Quest State Persistence** | `PASS` | Zero quest status drift across restarts |
| **Combat / Grind Command** | `FAIL` | Crash in `FleeManager.cpp:156` / `RandomTeleport` |
| **Selfbot Mode** | `NOT TESTED` | No live graphical client in headless harness |

**Overall SQUID Gameplay Gate Verdict**: **`PARTIAL / ACTION REQUIRED`**
*(SQUID random bot population and party mechanics are functional, but `.playerbots rndbot grind` must be patched in `squid-playerbots` before enabling random grind commands in production).*
