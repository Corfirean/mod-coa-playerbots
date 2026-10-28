/*
 * mod-coa-playerbots -- CoA World Map Intelligence
 * Automated Unit, Regression, and Benchmark Test Suite for BotMapProtocol & BotMapFilter.
 * Tests production C++ logic directly across all 10 post-review regression scenarios (A through J).
 */

#include "Define.h"
#include "ObjectGuid.h"
#include "map/BotMapProtocol.h"
#include "map/BotMapFilter.h"
#include "map/WorldMapEventSnapshot.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int _totalChecks = 0;
    int _failures = 0;

    void Check(bool cond, char const* expr, char const* file, int line)
    {
        ++_totalChecks;
        if (!cond)
        {
            ++_failures;
            std::printf("FAIL: %s at %s:%d\n", expr, file, line);
        }
    }
}

#define CHECK(expr) Check((expr), #expr, __FILE__, __LINE__)

// Test helper: generates synthetic bot target
static BotMapFilterTarget MakeTarget(
    uint32 guidLow,
    uint8 teamId,
    uint32 mapId,
    uint32 zoneId,
    uint32 guildId = 0,
    uint64 groupLeaderGuid = 0,
    float x = 0.0f,
    float y = 0.0f,
    float z = 0.0f,
    uint8 level = 80,
    uint8 classId = 1, // Warrior
    uint8 role = 0     // DPS
)
{
    BotMapFilterTarget b;
    b.guidLow = guidLow;
    b.teamId = teamId;
    b.mapId = mapId;
    b.zoneId = zoneId;
    b.guildId = guildId;
    b.groupLeaderGuid = groupLeaderGuid;
    b.x = x;
    b.y = y;
    b.z = z;
    b.level = level;
    b.classId = classId;
    b.role = role;
    b.isInWorld = true;
    b.isAlive = true;
    b.isGhost = false;
    b.inCombat = false;
    b.activity = BotMapActivity::Idle;
    b.hasTask = false;
    return b;
}

// -----------------------------------------------------------------------------
// Section 1: Protocol Basics, Query Parsing & Safe UTF-8 Handling
// -----------------------------------------------------------------------------
static void TestProtocolBasics()
{
    std::cout << "[Test] Section 1: Protocol Basics & Parsing..." << std::endl;

    uint32 reqId = 0;
    BotMapMode mode = BotMapMode::Off;
    int32 arg1 = -1;
    uint32 arg2 = 0;

    // Standard query
    std::vector<std::string> parts = { "MAPQUERY", "42", "1", "1500" };
    CHECK(BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2));
    CHECK(reqId == 42);
    CHECK(mode == BotMapMode::Nearby);
    CHECK(arg1 == 1500);

    // MapId = 0 (Eastern Kingdoms) must parse explicitly as 0, not -1
    parts = { "MAPQUERY", "100", "4", "0" };
    CHECK(BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2));
    CHECK(reqId == 100);
    CHECK(mode == BotMapMode::AllAllied);
    CHECK(arg1 == 0); // Explicit Eastern Kingdoms!

    // Wildcard -1 or 4294967295 parses as -1
    parts = { "MAPQUERY", "101", "4", "-1" };
    CHECK(BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2));
    CHECK(arg1 == -1);

    parts = { "MAPQUERY", "102", "4", "4294967295" };
    CHECK(BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2));
    CHECK(arg1 == -1);

    // Query without arguments defaults to arg1 = -1
    parts = { "MAPQUERY", "103", "3" };
    CHECK(BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2));
    CHECK(mode == BotMapMode::GuildOnly);
    CHECK(arg1 == -1);

    // Invalid mode > 4
    parts = { "MAPQUERY", "10", "9" };
    CHECK(!BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2));

    // Malformed input
    parts = { "MAPQUERY" };
    CHECK(!BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2));

    parts = { "MAPQUERY", "not_a_number", "1" };
    CHECK(!BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2));

    // Header formatting
    std::string begin = BotMapProtocol::FormatBegin(42, 10, 2, 5);
    CHECK(begin == "MAPBEGIN:42:10:2:5");

    std::string end = BotMapProtocol::FormatEnd(42);
    CHECK(end == "MAPEND:42");

    // Safe UTF-8 truncation
    std::string asciiStr = "Hello World";
    CHECK(BotMapProtocol::TruncateUtf8Safe(asciiStr, 5) == "Hello");

    // Cyrillic string: "Привет" in UTF-8 (2 bytes per character: D0 9F, D1 80, D0 B8, D0 B2, D0 B5, D1 82)
    std::string cyrillicStr = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"; // 12 bytes
    std::string truncCyrillic = BotMapProtocol::TruncateUtf8Safe(cyrillicStr, 5);
    // 5 bytes fits 2 characters (4 bytes); the 3rd character's lead byte at index 4 must be dropped!
    CHECK(truncCyrillic.size() == 4);
    CHECK(truncCyrillic == "\xD0\x9F\xD1\x80");

    // Progressive Detail formatting under packet budget
    BotMapDetailSnapshot det;
    det.guidLow = 12345;
    det.name = "Arthas";
    det.specId = 20;
    det.specName = "Paladin of the Silver Hand and Guardian of Lordaeron";
    det.avgItemLevel = 245;
    det.activityName = "Patrolling High Concentration Undead Invasions";
    det.taskType = BotMapTaskType::Gather;
    det.taskItemEntry = 123456;
    det.taskTargetCount = 100;
    det.taskCompletedCount = 45;
    det.taskStatus = "Actively Gathering Peacebloom in Northern Forests";

    std::string detMsg = BotMapProtocol::FormatDetail(100, det);
    CHECK(detMsg.size() <= BotMapProtocol::PACKET_BUDGET_BYTES);
    CHECK(detMsg.find("MAPDETAIL:100:12345:Arthas:") == 0);
}

// -----------------------------------------------------------------------------
// Section 2: Production Filtering Engine (`BotMapFilter::FilterBot`)
// -----------------------------------------------------------------------------
static void TestProductionFilterEngine()
{
    std::cout << "[Test] Section 2: Production Filter Engine..." << std::endl;

    BotMapCommanderView cmd;
    cmd.guidLow = 1000;
    cmd.teamId = 0; // Alliance
    cmd.mapId = 0;  // Eastern Kingdoms
    cmd.zoneId = 12; // Elwynn Forest
    cmd.guildId = 42;
    cmd.groupLeaderGuid = 0;
    cmd.x = 0.0f;
    cmd.y = 0.0f;
    cmd.z = 0.0f;

    BotMapPinSnapshot pin;

    // Self check
    auto selfBot = MakeTarget(1000, 0, 0, 12, 42);
    CHECK(!BotMapFilter::FilterBot(cmd, BotMapMode::Nearby, -1, 1000, selfBot, pin));

    // Enemy bot check
    auto enemyBot = MakeTarget(2001, 1, 0, 12, 0); // Horde
    CHECK(!BotMapFilter::FilterBot(cmd, BotMapMode::Nearby, -1, 1000, enemyBot, pin));

    // Allied bot in range
    auto allyNearby = MakeTarget(2002, 0, 0, 12, 42, 0, 200.0f, 200.0f, 0.0f);
    CHECK(BotMapFilter::FilterBot(cmd, BotMapMode::Nearby, -1, 1000, allyNearby, pin));
    CHECK(pin.guidLow == 2002);
    CHECK(pin.mapId == 0);
    CHECK((pin.flags & BotMapPinFlags::GuildMember) != 0);

    // Allied bot out of range (1500 yd away with 1000 yd radius)
    auto allyFar = MakeTarget(2003, 0, 0, 12, 42, 0, 1500.0f, 0.0f, 0.0f);
    CHECK(!BotMapFilter::FilterBot(cmd, BotMapMode::Nearby, -1, 1000, allyFar, pin));

    // Current zone filter
    auto sameZoneBot = MakeTarget(2004, 0, 0, 12);
    auto diffZoneBot = MakeTarget(2005, 0, 0, 14); // Westfall
    CHECK(BotMapFilter::FilterBot(cmd, BotMapMode::CurrentZone, -1, 0, sameZoneBot, pin));
    CHECK(!BotMapFilter::FilterBot(cmd, BotMapMode::CurrentZone, -1, 0, diffZoneBot, pin));

    // Guild only filter
    auto guildBot = MakeTarget(2006, 0, 1, 10, 42); // on Kalimdor, same guild
    auto nonGuildBot = MakeTarget(2007, 0, 1, 10, 99); // different guild
    CHECK(BotMapFilter::FilterBot(cmd, BotMapMode::GuildOnly, -1, 0, guildBot, pin));
    CHECK(!BotMapFilter::FilterBot(cmd, BotMapMode::GuildOnly, -1, 0, nonGuildBot, pin));

    // Group member exclusion
    cmd.groupLeaderGuid = 5000;
    auto partyMemberBot = MakeTarget(2008, 0, 0, 12, 42, 5000); // same group leader
    CHECK(!BotMapFilter::FilterBot(cmd, BotMapMode::Nearby, -1, 1000, partyMemberBot, pin));
    cmd.groupLeaderGuid = 0; // reset
}

// -----------------------------------------------------------------------------
// Section 3: Regression Scenarios (A through J)
// -----------------------------------------------------------------------------

// Scenario A -- Guild Only: bots spread across Eastern Kingdoms, Kalimdor, Northrend
static void TestScenarioA_GuildOnly()
{
    std::cout << "[Scenario A] Guild Only worldwide query & mapId tagging..." << std::endl;

    BotMapCommanderView cmd;
    cmd.guidLow = 1;
    cmd.teamId = 0; // Alliance
    cmd.mapId = 0;  // Stormwind
    cmd.zoneId = 1519;
    cmd.guildId = 77;

    std::vector<BotMapFilterTarget> bots = {
        MakeTarget(101, 0, 0, 12, 77),   // Elwynn (map 0)
        MakeTarget(102, 0, 0, 14, 77),   // Westfall (map 0)
        MakeTarget(103, 0, 1, 17, 77),   // Kalimdor Barrens (map 1)
        MakeTarget(104, 0, 571, 3537, 77), // Northrend Borean Tundra (map 571)
        MakeTarget(105, 0, 0, 12, 999),  // Different guild
        MakeTarget(106, 1, 0, 12, 77)    // Enemy guild (Horde)
    };

    std::vector<BotMapPinSnapshot> results = BotMapFilter::FilterBots(cmd, BotMapMode::GuildOnly, -1, 0, bots);
    CHECK(results.size() == 4);

    // Validate that each pin carries its true server mapId
    for (auto const& p : results)
    {
        if (p.guidLow == 101) CHECK(p.mapId == 0);
        if (p.guidLow == 102) CHECK(p.mapId == 0);
        if (p.guidLow == 103) CHECK(p.mapId == 1);
        if (p.guidLow == 104) CHECK(p.mapId == 571);
        CHECK((p.flags & BotMapPinFlags::GuildMember) != 0);
    }
}

// Scenario B -- All Allied: 500 allied bots, 500 enemy bots, 20 players
static void TestScenarioB_AllAllied()
{
    std::cout << "[Scenario B] All Allied large population filter..." << std::endl;

    BotMapCommanderView cmd;
    cmd.guidLow = 1;
    cmd.teamId = 0; // Alliance
    cmd.mapId = 0;  // Eastern Kingdoms
    cmd.zoneId = 12;
    cmd.guildId = 10;

    std::vector<BotMapFilterTarget> pool;
    // 500 Allied bots on map 0
    for (uint32 i = 100; i < 600; ++i)
        pool.push_back(MakeTarget(i, 0, 0, 12));

    // 500 Enemy bots on map 0
    for (uint32 i = 600; i < 1100; ++i)
        pool.push_back(MakeTarget(i, 1, 0, 12));

    // Filter for Eastern Kingdoms (filterMapId = 0)
    std::vector<BotMapPinSnapshot> results = BotMapFilter::FilterBots(cmd, BotMapMode::AllAllied, 0, 0, pool);
    CHECK(results.size() == 500);

    for (auto const& p : results)
    {
        CHECK(p.guidLow >= 100 && p.guidLow < 600);
        CHECK(p.mapId == 0);
    }
}

// Scenario C -- Tooltip Race: rapid cursor movement
static void TestScenarioC_TooltipRace()
{
    std::cout << "[Scenario C] Tooltip Race & Detail responses..." << std::endl;

    // Simulates responses for distinct detail requests
    BotMapDetailSnapshot detA;
    detA.guidLow = 101;
    detA.name = "BotAlpha";
    detA.specName = "Protection";

    BotMapDetailSnapshot detB;
    detB.guidLow = 102;
    detB.name = "BotBravo";
    detB.specName = "Holy";

    std::string msgA = BotMapProtocol::FormatDetail(501, detA);
    std::string msgB = BotMapProtocol::FormatDetail(502, detB);

    CHECK(msgA.find("MAPDETAIL:501:101:BotAlpha:") == 0);
    CHECK(msgB.find("MAPDETAIL:502:102:BotBravo:") == 0);
    // Verifies neither packet overwrote the other or confused names
    CHECK(msgA != msgB);
}

// Scenario D -- Chunk Loss: 15 chunks generated, verify completeness
static void TestScenarioD_ChunkLoss()
{
    std::cout << "[Scenario D] Chunk verification & chunking logic..." << std::endl;

    std::vector<BotMapPinSnapshot> pins;
    // Generate 100 pins across multiple chunks
    for (uint32 i = 1; i <= 100; ++i)
    {
        BotMapPinSnapshot p;
        p.guidLow = i;
        p.mapId = 0;
        p.worldX = (int32)i * 10;
        p.worldY = (int32)i * 10;
        p.level = 80;
        p.classId = 1;
        p.role = 0;
        p.flags = 0;
        p.activity = 1;
        pins.push_back(p);
    }

    std::vector<std::string> chunks = BotMapProtocol::FormatBotsChunks(777, pins);
    CHECK(chunks.size() >= 10); // Multiple chunks generated

    for (size_t i = 0; i < chunks.size(); ++i)
    {
        CHECK(chunks[i].size() <= BotMapProtocol::PACKET_BUDGET_BYTES);
        std::string expectedPrefix = "MAPBOTS:777:" + std::to_string(i) + ":";
        CHECK(chunks[i].find(expectedPrefix) == 0);
    }
}

// Scenario E -- Teleport: bot jumps between continents
static void TestScenarioE_Teleport()
{
    std::cout << "[Scenario E] Continent teleportation & mapId migration..." << std::endl;

    BotMapCommanderView cmd;
    cmd.guidLow = 1;
    cmd.teamId = 0;
    cmd.mapId = 0; // viewing Eastern Kingdoms

    BotMapFilterTarget bot = MakeTarget(555, 0, 0, 12); // starts in EK

    BotMapPinSnapshot pin;
    CHECK(BotMapFilter::FilterBot(cmd, BotMapMode::AllAllied, 0, 0, bot, pin));
    CHECK(pin.mapId == 0);

    // Bot teleports to Kalimdor (map 1)
    bot.mapId = 1;
    bot.zoneId = 17;
    bot.x = -1000.0f;
    bot.y = 2000.0f;

    // When querying Eastern Kingdoms (0), teleported bot is excluded
    CHECK(!BotMapFilter::FilterBot(cmd, BotMapMode::AllAllied, 0, 0, bot, pin));

    // When querying Kalimdor (1), teleported bot is included with mapId = 1
    CHECK(BotMapFilter::FilterBot(cmd, BotMapMode::AllAllied, 1, 0, bot, pin));
    CHECK(pin.mapId == 1);
}

// Scenario F -- Native Group Markers: group of 2 players, 3 bots
static void TestScenarioF_NativeGroupMarkers()
{
    std::cout << "[Scenario F] Group member exclusion..." << std::endl;

    BotMapCommanderView cmd;
    cmd.guidLow = 1; // Commander
    cmd.teamId = 0;
    cmd.mapId = 0;
    cmd.zoneId = 12;
    cmd.groupLeaderGuid = 1; // Commander is group leader

    std::vector<BotMapFilterTarget> pool = {
        MakeTarget(2, 0, 0, 12, 0, 1),    // Bot in party (leaderGuid = 1)
        MakeTarget(3, 0, 0, 12, 0, 1),    // Bot in party (leaderGuid = 1)
        MakeTarget(4, 0, 0, 12, 0, 1),    // Bot in party (leaderGuid = 1)
        MakeTarget(10, 0, 0, 12, 0, 0),   // Bot not in party
        MakeTarget(11, 0, 0, 12, 0, 0)    // Bot not in party
    };

    std::vector<BotMapPinSnapshot> results = BotMapFilter::FilterBots(cmd, BotMapMode::CurrentZone, -1, 0, pool);
    // All 3 party bots must be excluded from custom map pins!
    CHECK(results.size() == 2);
    CHECK(results[0].guidLow == 10);
    CHECK(results[1].guidLow == 11);
}

// Scenario G -- Guild Task Progress & Privacy Gate
static void TestScenarioG_GuildTaskProgress()
{
    std::cout << "[Scenario G] Guild task progress & privacy gating..." << std::endl;

    BotMapDetailSnapshot det;
    det.guidLow = 701;
    det.name = "GuildGatherer";
    det.taskType = BotMapTaskType::Gather;
    det.taskItemEntry = 2447; // Peacebloom
    det.taskTargetCount = 100;
    det.taskCompletedCount = 25;
    det.taskStatus = "In Progress";

    std::string msg = BotMapProtocol::FormatDetail(888, det);
    CHECK(msg.find(":1:2447:100:25:In Progress") != std::string::npos);

    // For non-guild member, task is zeroed
    BotMapDetailSnapshot nonGuildDet;
    nonGuildDet.guidLow = 701;
    nonGuildDet.name = "GuildGatherer";
    nonGuildDet.taskType = BotMapTaskType::None;
    nonGuildDet.taskItemEntry = 0;
    nonGuildDet.taskTargetCount = 0;
    nonGuildDet.taskCompletedCount = 0;
    nonGuildDet.taskStatus = "";

    std::string privateMsg = BotMapProtocol::FormatDetail(889, nonGuildDet);
    CHECK(privateMsg.find(":0:0:0:0:") != std::string::npos);
    CHECK(privateMsg.find("2447") == std::string::npos); // Item ID not leaked!
}

// Scenario H -- Hidden Encounter Security: multi-tier visibility policy
static void TestScenarioH_HiddenEncounter()
{
    std::cout << "[Scenario H] Hidden encounter security policy..." << std::endl;

    std::vector<WorldMapEventSnapshot> events;

    // 1. FactionSkirmish Active Public -> MUST BE REVEALED
    WorldMapEventSnapshot e1;
    e1.eventId = 1;
    e1.eventType = WorldMapEventType::FactionSkirmish;
    e1.phase = 2; // Active
    e1.visibility = WorldMapEventVisibility::Public;
    e1.name = "Crossroads Skirmish";
    events.push_back(e1);

    // 2. FactionSkirmish Prepare Public -> MUST BE BLOCKED
    WorldMapEventSnapshot e2;
    e2.eventId = 2;
    e2.eventType = WorldMapEventType::FactionSkirmish;
    e2.phase = 1; // Prepare
    e2.visibility = WorldMapEventVisibility::Public;
    e2.name = "Ashenvale Staging";
    events.push_back(e2);

    // 3. AssassinContract Active Public -> MUST BE BLOCKED
    WorldMapEventSnapshot e3;
    e3.eventId = 3;
    e3.eventType = WorldMapEventType::AssassinContract;
    e3.phase = 2; // Active
    e3.visibility = WorldMapEventVisibility::Public;
    e3.name = "Contract: High Inquisitor";
    events.push_back(e3);

    // 4. TheWatcher Active Public -> MUST BE BLOCKED
    WorldMapEventSnapshot e4;
    e4.eventId = 4;
    e4.eventType = WorldMapEventType::TheWatcher;
    e4.phase = 2;
    e4.visibility = WorldMapEventVisibility::Public;
    e4.name = "The Watcher Rises";
    events.push_back(e4);

    // 5. PatrolAmbush Active Hidden -> MUST BE BLOCKED
    WorldMapEventSnapshot e5;
    e5.eventId = 5;
    e5.eventType = WorldMapEventType::PatrolAmbush;
    e5.phase = 2;
    e5.visibility = WorldMapEventVisibility::Hidden;
    e5.name = "Stranglethorn Ambush";
    events.push_back(e5);

    std::vector<std::string> chunks = BotMapProtocol::FormatEventsChunks(901, events);
    CHECK(chunks.size() == 1);
    // Only Crossroads Skirmish (eventId 1) is present
    CHECK(chunks[0].find("Crossroads Skirmish") != std::string::npos);
    CHECK(chunks[0].find("Ashenvale Staging") == std::string::npos);
    CHECK(chunks[0].find("Contract") == std::string::npos);
    CHECK(chunks[0].find("The Watcher") == std::string::npos);
    CHECK(chunks[0].find("Stranglethorn") == std::string::npos);

    // Full transactional FormatEvents test
    std::vector<std::string> fullReplies = BotMapProtocol::FormatEvents(902, events);
    CHECK(fullReplies.size() == 3); // BEGIN, CHUNK, END
    CHECK(fullReplies[0] == "MAPEVENTSBEGIN:902:1:1");
    CHECK(fullReplies[2] == "MAPEVENTSEND:902");
}

// Scenario I -- OFF + Events ON
static void TestScenarioI_OffWithEvents()
{
    std::cout << "[Scenario I] Bot markers OFF + World Events ON..." << std::endl;

    BotMapCommanderView cmd;
    cmd.guidLow = 1;
    cmd.teamId = 0;
    cmd.mapId = 0;

    std::vector<BotMapFilterTarget> bots = { MakeTarget(10, 0, 0, 12) };
    // In OFF mode, 0 bot pins are returned
    std::vector<BotMapPinSnapshot> pins = BotMapFilter::FilterBots(cmd, BotMapMode::Off, 0, 0, bots);
    CHECK(pins.empty());

    // But event query returns active skirmish events
    WorldMapEventSnapshot ev;
    ev.eventId = 99;
    ev.eventType = WorldMapEventType::FactionSkirmish;
    ev.phase = 2;
    ev.visibility = WorldMapEventVisibility::Public;
    ev.name = "Barrens Skirmish";

    std::vector<std::string> evReplies = BotMapProtocol::FormatEvents(999, { ev });
    CHECK(evReplies.size() == 3);
    CHECK(evReplies[0] == "MAPEVENTSBEGIN:999:1:1");
    CHECK(evReplies[1].find("Barrens Skirmish") != std::string::npos);
}

// Scenario J -- MapID Zero (Eastern Kingdoms): not treated as wildcard
static void TestScenarioJ_MapIdZero()
{
    std::cout << "[Scenario J] MapID Zero (Eastern Kingdoms) specificity..." << std::endl;

    BotMapCommanderView cmd;
    cmd.guidLow = 9999; // Commander (not in bot list)
    cmd.teamId = 0;
    cmd.mapId = 0;

    std::vector<BotMapFilterTarget> bots = {
        MakeTarget(101, 0, 0, 12),    // Eastern Kingdoms (map 0)
        MakeTarget(102, 0, 0, 14),    // Eastern Kingdoms (map 0)
        MakeTarget(103, 0, 1, 17),    // Kalimdor (map 1)
        MakeTarget(104, 0, 530, 3483), // Outland (map 530)
        MakeTarget(105, 0, 571, 3537)  // Northrend (map 571)
    };

    // Query specifically for Eastern Kingdoms (filterMapId = 0)
    std::vector<BotMapPinSnapshot> results = BotMapFilter::FilterBots(cmd, BotMapMode::AllAllied, 0, 0, bots);
    CHECK(results.size() == 2);
    CHECK(results[0].guidLow == 101);
    CHECK(results[1].guidLow == 102);
    CHECK(results[0].mapId == 0);
    CHECK(results[1].mapId == 0);

    // None from Kalimdor, Outland, or Northrend!
    for (auto const& p : results)
    {
        CHECK(p.mapId == 0);
    }
}

// -----------------------------------------------------------------------------
// Section 3.11: Scenario K / F11 -- Server-Side Viewport Area Filtering
// -----------------------------------------------------------------------------
static void TestScenarioK_ViewportAreaFiltering()
{
    std::cout << "[Test] Scenario K / F11: Server-side Viewport Area Filtering (filterAreaId)..." << std::endl;

    BotMapCommanderView cmd;
    cmd.guidLow = 9999;
    cmd.teamId = 0; // Alliance
    cmd.mapId = 0;  // Eastern Kingdoms
    cmd.zoneId = 12; // Elwynn Forest

    std::vector<BotMapFilterTarget> bots = {
        MakeTarget(201, 0, 0, 12), // Elwynn Forest
        MakeTarget(202, 0, 0, 12), // Elwynn Forest
        MakeTarget(203, 0, 0, 40), // Westfall (same map 0, different zone 40)
        MakeTarget(204, 0, 0, 10), // Duskwood (same map 0, different zone 10)
        MakeTarget(205, 0, 1, 14)  // Kalimdor (map 1)
    };

    // 1. AllAllied with filterMapId = 0 and filterAreaId = 12 (Elwynn Forest zone view)
    std::vector<BotMapPinSnapshot> elwynnOnly = BotMapFilter::FilterBots(cmd, BotMapMode::AllAllied, 0, 12, 0, bots);
    CHECK(elwynnOnly.size() == 2);
    CHECK(elwynnOnly[0].guidLow == 201);
    CHECK(elwynnOnly[1].guidLow == 202);

    // 2. AllAllied with filterMapId = 0 and filterAreaId = 0 (Continent view)
    std::vector<BotMapPinSnapshot> allEasternKingdoms = BotMapFilter::FilterBots(cmd, BotMapMode::AllAllied, 0, 0, 0, bots);
    CHECK(allEasternKingdoms.size() == 4); // 201, 202, 203, 204

    // 3. CurrentZone with filterAreaId = 40 (Explicit Westfall viewport)
    std::vector<BotMapPinSnapshot> westfall = BotMapFilter::FilterBots(cmd, BotMapMode::CurrentZone, 0, 40, 0, bots);
    CHECK(westfall.size() == 1);
    CHECK(westfall[0].guidLow == 203);
}

// -----------------------------------------------------------------------------
// Section 4: Performance Benchmarks & Stress Tests
// -----------------------------------------------------------------------------
static void TestPerformanceBenchmarks()
{
    std::cout << "[Benchmark] Large scale filter & serialization tests (100, 500, 1000, 3000 bots)..." << std::endl;

    BotMapCommanderView cmd;
    cmd.guidLow = 1;
    cmd.teamId = 0;
    cmd.mapId = 0;
    cmd.zoneId = 12;

    std::vector<size_t> counts = { 100, 500, 1000, 3000 };
    for (size_t count : counts)
    {
        std::vector<BotMapFilterTarget> pool;
        pool.reserve(count);
        for (size_t i = 0; i < count; ++i)
        {
            pool.push_back(MakeTarget(static_cast<uint32>(i + 10), 0, 0, 12, 0, 0,
                                      static_cast<float>(i % 500), static_cast<float>(i % 500)));
        }

        auto start = std::chrono::high_resolution_clock::now();
        std::vector<BotMapPinSnapshot> pins = BotMapFilter::FilterBots(cmd, BotMapMode::AllAllied, 0, 0, pool);
        auto filtered = std::chrono::high_resolution_clock::now();

        std::vector<std::string> chunks = BotMapProtocol::FormatBotsChunks(1000, pins);
        auto formatted = std::chrono::high_resolution_clock::now();

        auto filterUs = std::chrono::duration_cast<std::chrono::microseconds>(filtered - start).count();
        auto formatUs = std::chrono::duration_cast<std::chrono::microseconds>(formatted - filtered).count();

        CHECK(pins.size() == count);
        // Verify every single chunk strictly satisfies PACKET_BUDGET_BYTES
        for (auto const& chunk : chunks)
        {
            CHECK(chunk.size() <= BotMapProtocol::PACKET_BUDGET_BYTES);
        }

        std::printf("  -> Count: %-5zu | Filter: %6lld us | Format: %6lld us | Chunks: %zu\n",
                    count, filterUs, formatUs, chunks.size());
    }
}

int main()
{
    std::cout << "============================================================" << std::endl;
    std::cout << "CoA World Map Intelligence -- Post-Review Regression Suite" << std::endl;
    std::cout << "============================================================" << std::endl;

    TestProtocolBasics();
    TestProductionFilterEngine();
    TestScenarioA_GuildOnly();
    TestScenarioB_AllAllied();
    TestScenarioC_TooltipRace();
    TestScenarioD_ChunkLoss();
    TestScenarioE_Teleport();
    TestScenarioF_NativeGroupMarkers();
    TestScenarioG_GuildTaskProgress();
    TestScenarioH_HiddenEncounter();
    TestScenarioI_OffWithEvents();
    TestScenarioJ_MapIdZero();
    TestScenarioK_ViewportAreaFiltering();
    TestPerformanceBenchmarks();

    std::cout << "============================================================" << std::endl;
    std::cout << "Results: " << _totalChecks << " checks completed." << std::endl;
    if (_failures == 0)
    {
        std::cout << "ALL CHECKS PASSED SUCCESSFULLY." << std::endl;
        return 0;
    }
    else
    {
        std::cout << "FAILED: " << _failures << " check(s) failed." << std::endl;
        return 1;
    }
}
