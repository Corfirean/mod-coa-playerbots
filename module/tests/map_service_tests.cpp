/*
 * mod-coa-playerbots -- CoA World Map Intelligence
 * Standalone Unit & Benchmark Test Suite for BotMapProtocol, Filter Logic,
 * Coordinate Projections, Activity Resolution, Event Visibility, and Load Testing.
 */

#include "Define.h"
#include "ObjectGuid.h"
#include "map/BotMapProtocol.h"
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

// Mock data structures for filter testing
struct MockBot
{
    uint32 guidLow;
    uint8 teamId; // 0 = Alliance, 1 = Horde
    uint32 mapId;
    uint32 zoneId;
    uint32 guildId;
    uint32 groupLeaderGuid;
    float x, y, z;
    uint8 level;
    uint8 classId;
    uint8 role;
    bool inCombat;
    bool isAlive;
    bool isGhost;
    bool isInFlight;
    bool isDungeon;
    bool hasCraftOrder;
    bool hasGatherOrder;
    bool inEncounter;
    uint8 brainTaskType; // 0=None, 1=Quest, 2=Travel, 3=Gather, 4=Rest
};

struct MockPlayer
{
    uint32 guidLow;
    uint8 teamId;
    uint32 mapId;
    uint32 zoneId;
    uint32 guildId;
    uint32 groupLeaderGuid;
    float x, y, z;
};

static BotMapActivity SimulateResolveActivity(MockBot const& bot)
{
    if (!bot.isAlive || bot.isGhost)
        return BotMapActivity::Resting;
    if (bot.inCombat)
        return BotMapActivity::Fighting;
    if (bot.isDungeon)
        return BotMapActivity::Dungeon;
    if (bot.isInFlight)
        return BotMapActivity::Traveling;
    if (bot.inEncounter)
        return BotMapActivity::WorldEncounter;
    if (bot.groupLeaderGuid != 0)
        return BotMapActivity::GroupActivity;
    if (bot.hasCraftOrder)
        return BotMapActivity::Crafting;
    if (bot.hasGatherOrder)
        return BotMapActivity::Gathering;
    if (bot.brainTaskType == 1)
        return BotMapActivity::Questing;
    if (bot.brainTaskType == 2)
        return BotMapActivity::Traveling;
    if (bot.brainTaskType == 3)
        return BotMapActivity::Gathering;
    if (bot.brainTaskType == 4)
        return BotMapActivity::Resting;
    return BotMapActivity::Idle;
}

static std::vector<BotMapPinSnapshot> SimulateFilter(
    MockPlayer const& commander,
    BotMapMode mode,
    uint32 radius,
    std::vector<MockBot> const& botPool)
{
    std::vector<BotMapPinSnapshot> matches;
    if (mode == BotMapMode::Off)
        return matches;

    float maxDistSq = static_cast<float>(radius) * static_cast<float>(radius);

    for (auto const& bot : botPool)
    {
        if (bot.guidLow == commander.guidLow)
            continue;
        if (bot.teamId != commander.teamId)
            continue;
        if (commander.groupLeaderGuid != 0 && bot.groupLeaderGuid == commander.groupLeaderGuid)
            continue; // Exclude own group

        if (mode == BotMapMode::Nearby)
        {
            if (bot.mapId != commander.mapId)
                continue;
            float dx = bot.x - commander.x;
            float dy = bot.y - commander.y;
            float dz = bot.z - commander.z;
            if ((dx * dx + dy * dy + dz * dz) > maxDistSq)
                continue;
        }
        else if (mode == BotMapMode::CurrentZone)
        {
            if (bot.mapId != commander.mapId || bot.zoneId != commander.zoneId)
                continue;
        }
        else if (mode == BotMapMode::GuildOnly)
        {
            if (commander.guildId == 0 || bot.guildId != commander.guildId)
                continue;
        }
        else if (mode == BotMapMode::AllAllied)
        {
            if (bot.mapId != commander.mapId)
                continue;
        }

        BotMapPinSnapshot pin;
        pin.guidLow = bot.guidLow;
        pin.worldX = static_cast<int32>(std::round(bot.x));
        pin.worldY = static_cast<int32>(std::round(bot.y));
        pin.level = bot.level;
        pin.classId = bot.classId;
        pin.role = bot.role;
        uint8 flags = 0;
        if (commander.guildId && bot.guildId == commander.guildId)
            flags |= BotMapPinFlags::GuildMember;
        if (bot.hasGatherOrder || bot.hasCraftOrder)
            flags |= BotMapPinFlags::HasTask;
        if (bot.inCombat)
            flags |= BotMapPinFlags::InCombat;
        if (!bot.isAlive || bot.isGhost)
            flags |= BotMapPinFlags::Ghost;
        pin.flags = flags;
        pin.activity = static_cast<uint8>(SimulateResolveActivity(bot));
        matches.push_back(pin);
    }
    return matches;
}

// Coordinate transform simulator
static bool TransformCoord(float worldX, float worldY, float y1, float y2, float x1, float x2, float& normX, float& normY)
{
    float dy = y2 - y1;
    float dx = x2 - x1;
    if (std::abs(dy) < 0.001f || std::abs(dx) < 0.001f)
        return false;
    normX = (worldY - y1) / dy;
    normY = (worldX - x1) / dx;
    return (normX >= 0.0f && normX <= 1.0f && normY >= 0.0f && normY <= 1.0f);
}

// ----------------------------------------------------------------------------
// Test Suites
// ----------------------------------------------------------------------------

static void TestProtocolBasics()
{
    std::cout << "[Test] Protocol Basics & Query Parsing..." << std::endl;

    uint32 reqId = 0, arg1 = 0, arg2 = 0;
    BotMapMode mode = BotMapMode::Off;

    std::vector<std::string> parts = { "MAPQUERY", "42", "1", "1500" };
    CHECK(BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2));
    CHECK(reqId == 42);
    CHECK(mode == BotMapMode::Nearby);
    CHECK(arg1 == 1500);

    // Invalid mode
    parts = { "MAPQUERY", "10", "9" };
    CHECK(!BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2));

    // Malformed parts
    parts = { "MAPQUERY" };
    CHECK(!BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2));

    CHECK(BotMapProtocol::FormatBegin(1, 10, 2, 5) == "MAPBEGIN:1:10:2:5");
    CHECK(BotMapProtocol::FormatEnd(1) == "MAPEND:1");
}

static void TestPacketChunkingBudget()
{
    std::cout << "[Test] Packet Chunking Budget (<= 200 bytes)..." << std::endl;

    std::vector<BotMapPinSnapshot> bots;
    for (uint32 i = 1; i <= 250; ++i)
    {
        BotMapPinSnapshot b;
        b.guidLow = 1000 + i;
        b.worldX = -9400 + static_cast<int32>(i * 10);
        b.worldY = 1500 - static_cast<int32>(i * 10);
        b.level = static_cast<uint8>(1 + (i % 80));
        b.classId = static_cast<uint8>(1 + (i % 11));
        b.role = static_cast<uint8>(i % 4);
        b.flags = static_cast<uint8>(i % 16);
        b.activity = static_cast<uint8>(i % 11);
        bots.push_back(b);
    }

    std::vector<std::string> chunks = BotMapProtocol::FormatBotsChunks(99, bots);
    CHECK(!chunks.empty());

    for (size_t i = 0; i < chunks.size(); ++i)
    {
        auto const& c = chunks[i];
        CHECK(c.size() <= BotMapProtocol::PACKET_BUDGET_BYTES);
        CHECK(c.rfind("MAPBOTS:99:" + std::to_string(i) + ":", 0) == 0);
    }
}

static void TestDetailSerialization()
{
    std::cout << "[Test] Detail Serialization & Sanitization..." << std::endl;

    BotMapDetailSnapshot detail;
    detail.guidLow = 1234;
    detail.specId = 52;
    detail.avgItemLevel = 187;
    detail.specName = "Restoration:Druid"; // Colons must be sanitized!
    detail.activityName = "Gathering|Peacebloom"; // Pipes must be sanitized!
    detail.taskType = BotMapTaskType::Gather;
    detail.taskItemEntry = 2447;
    detail.taskTargetCount = 100;
    detail.taskCompletedCount = 67;
    detail.taskStatus = "In Progress";

    std::string line = BotMapProtocol::FormatDetail(7, detail);
    CHECK(line.find("Restoration Druid") != std::string::npos);
    CHECK(line.find("Gathering Peacebloom") != std::string::npos);
    CHECK(line.rfind("MAPDETAIL:7:1234:52:187:", 0) == 0);
}

static void TestEventVisibilityPolicy()
{
    std::cout << "[Test] Event Visibility Policy..." << std::endl;

    std::vector<WorldMapEventSnapshot> events;

    // Public active event
    WorldMapEventSnapshot ev1;
    ev1.eventId = 101;
    ev1.eventType = WorldMapEventType::FactionSkirmish;
    ev1.phase = 2; // Active
    ev1.visibility = WorldMapEventVisibility::Public;
    ev1.allianceCount = 4;
    ev1.hordeCount = 4;
    ev1.name = "Arathi Skirmish";
    events.push_back(ev1);

    // Hidden event (AssassinContract)
    WorldMapEventSnapshot ev2;
    ev2.eventId = 102;
    ev2.eventType = WorldMapEventType::AssassinContract;
    ev2.phase = 2;
    ev2.visibility = WorldMapEventVisibility::Hidden;
    ev2.name = "Secret Assassin";
    events.push_back(ev2);

    // Prepare phase event (not yet active)
    WorldMapEventSnapshot ev3;
    ev3.eventId = 103;
    ev3.eventType = WorldMapEventType::Caravan;
    ev3.phase = 1; // Prepare
    ev3.visibility = WorldMapEventVisibility::Hidden; // Server enforces hidden during prep
    ev3.name = "Preparing Caravan";
    events.push_back(ev3);

    std::vector<std::string> chunks = BotMapProtocol::FormatEvents(5, events);
    CHECK(!chunks.empty());

    std::string combined;
    for (auto const& c : chunks) combined += c;

    CHECK(combined.find("Arathi Skirmish") != std::string::npos);
    CHECK(combined.find("Secret Assassin") == std::string::npos); // MUST NOT BE IN WIRE STREAM!
    CHECK(combined.find("Preparing Caravan") == std::string::npos); // MUST NOT BE IN WIRE STREAM!
}

static void TestServerFilteringModes()
{
    std::cout << "[Test] Server Filtering Modes (0-4)..." << std::endl;

    MockPlayer player;
    player.guidLow = 1;
    player.teamId = 0; // Alliance
    player.mapId = 0;  // Eastern Kingdoms
    player.zoneId = 12; // Elwynn Forest
    player.guildId = 77;
    player.groupLeaderGuid = 0;
    player.x = -9400.0f;
    player.y = -100.0f;
    player.z = 50.0f;

    std::vector<MockBot> bots;

    // Bot 1: Allied, same zone, nearby (dist = 200 yd), same guild
    MockBot b1;
    b1.guidLow = 10;
    b1.teamId = 0;
    b1.mapId = 0;
    b1.zoneId = 12;
    b1.guildId = 77;
    b1.groupLeaderGuid = 0;
    b1.x = -9400.0f;
    b1.y = 100.0f; // dy = 200
    b1.z = 50.0f;
    b1.isAlive = true;
    b1.isGhost = false;
    bots.push_back(b1);

    // Bot 2: Enemy (Horde), same zone, nearby (dist = 150 yd) -> MUST NEVER BE RETURNED!
    MockBot b2;
    b2.guidLow = 20;
    b2.teamId = 1; // Horde!
    b2.mapId = 0;
    b2.zoneId = 12;
    b2.guildId = 0;
    b2.groupLeaderGuid = 0;
    b2.x = -9400.0f;
    b2.y = 50.0f;
    b2.z = 50.0f;
    b2.isAlive = true;
    b2.isGhost = false;
    bots.push_back(b2);

    // Bot 3: Allied, same zone, far away (dist = 2500 yd), no guild
    MockBot b3;
    b3.guidLow = 30;
    b3.teamId = 0;
    b3.mapId = 0;
    b3.zoneId = 12;
    b3.guildId = 0;
    b3.groupLeaderGuid = 0;
    b3.x = -9400.0f;
    b3.y = 2400.0f; // dy = 2500
    b3.z = 50.0f;
    b3.isAlive = true;
    b3.isGhost = false;
    bots.push_back(b3);

    // Bot 4: Allied, different zone (Westfall zone 40), dist = 3000 yd, same guild
    MockBot b4;
    b4.guidLow = 40;
    b4.teamId = 0;
    b4.mapId = 0;
    b4.zoneId = 40;
    b4.guildId = 77;
    b4.groupLeaderGuid = 0;
    b4.x = -10500.0f;
    b4.y = 1000.0f;
    b4.z = 50.0f;
    b4.isAlive = true;
    b4.isGhost = false;
    bots.push_back(b4);

    // Bot 5: Allied, in player's group (groupLeader = 1) -> MUST BE EXCLUDED!
    MockBot b5;
    b5.guidLow = 50;
    b5.teamId = 0;
    b5.mapId = 0;
    b5.zoneId = 12;
    b5.guildId = 77;
    b5.groupLeaderGuid = 1;
    player.groupLeaderGuid = 1;
    b5.x = -9405.0f;
    b5.y = -105.0f;
    b5.z = 50.0f;
    b5.isAlive = true;
    b5.isGhost = false;
    bots.push_back(b5);

    // Mode 0: OFF -> empty
    auto res0 = SimulateFilter(player, BotMapMode::Off, 1000, bots);
    CHECK(res0.empty());

    // Mode 1: NEARBY (1000 yd)
    // Should include b1 only (b2 is horde, b3 is 2500yd away, b4 is in Westfall, b5 is in group)
    auto res1 = SimulateFilter(player, BotMapMode::Nearby, 1000, bots);
    CHECK(res1.size() == 1);
    CHECK(res1[0].guidLow == 10);

    // Mode 2: CURRENT_ZONE (Elwynn 12)
    // Should include b1 and b3 (both in zone 12, allied, not in group)
    auto res2 = SimulateFilter(player, BotMapMode::CurrentZone, 0, bots);
    CHECK(res2.size() == 2);
    CHECK((res2[0].guidLow == 10 && res2[1].guidLow == 30) || (res2[0].guidLow == 30 && res2[1].guidLow == 10));

    // Mode 3: GUILD_ONLY (Guild 77)
    // Should include b1 and b4 (both in guild 77, allied, not in group)
    auto res3 = SimulateFilter(player, BotMapMode::GuildOnly, 0, bots);
    CHECK(res3.size() == 2);
    CHECK((res3[0].guidLow == 10 && res3[1].guidLow == 40) || (res3[0].guidLow == 40 && res3[1].guidLow == 10));

    // Mode 4: ALL_ALLIED (map 0)
    // Should include b1, b3, b4 (all allied on map 0, not in group)
    auto res4 = SimulateFilter(player, BotMapMode::AllAllied, 0, bots);
    CHECK(res4.size() == 3);
}

static void TestCoordinateTransformation()
{
    std::cout << "[Test] Coordinate Transformation..." << std::endl;

    // Elwynn Forest: y1 = 1535.0, y2 = -1935.0, x1 = -7939.0, x2 = -10254.0
    float y1 = 1535.0f, y2 = -1935.0f, x1 = -7939.0f, x2 = -10254.0f;
    float normX = 0.0f, normY = 0.0f;

    // Top-left corner (North-West): worldX = -7939.0, worldY = 1535.0
    CHECK(TransformCoord(-7939.0f, 1535.0f, y1, y2, x1, x2, normX, normY));
    CHECK(std::abs(normX - 0.0f) < 0.001f);
    CHECK(std::abs(normY - 0.0f) < 0.001f);

    // Bottom-right corner (South-East): worldX = -10254.0, worldY = -1935.0
    CHECK(TransformCoord(-10254.0f, -1935.0f, y1, y2, x1, x2, normX, normY));
    CHECK(std::abs(normX - 1.0f) < 0.001f);
    CHECK(std::abs(normY - 1.0f) < 0.001f);

    // Center of Elwynn: worldX = -9096.5, worldY = -200.0
    CHECK(TransformCoord(-9096.5f, -200.0f, y1, y2, x1, x2, normX, normY));
    CHECK(std::abs(normX - 0.5f) < 0.001f);
    CHECK(std::abs(normY - 0.5f) < 0.001f);

    // Outside zone boundary (North of Elwynn): worldX = -7000.0 -> must be rejected
    CHECK(!TransformCoord(-7000.0f, -200.0f, y1, y2, x1, x2, normX, normY));
}

static void RunSyntheticLoadBenchmark()
{
    std::cout << "\n========================================================" << std::endl;
    std::cout << " CoA World Map Intelligence: Synthetic Load Benchmark" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::vector<uint32> botScales = { 100, 500, 1000, 3000 };
    std::vector<uint32> playerScales = { 1, 5, 20, 50 };

    for (uint32 numBots : botScales)
    {
        // Populate bots across 4 maps and multiple zones
        std::vector<MockBot> pool;
        pool.reserve(numBots);
        for (uint32 i = 0; i < numBots; ++i)
        {
            MockBot b;
            b.guidLow = 100 + i;
            b.teamId = static_cast<uint8>(i % 2); // 50% Alliance, 50% Horde
            b.mapId = (i % 4 == 0) ? 0 : (i % 4 == 1) ? 1 : (i % 4 == 2) ? 530 : 571;
            b.zoneId = 10 + (i % 30);
            b.guildId = (i % 10 == 0) ? 77 : (i % 20);
            b.groupLeaderGuid = 0;
            b.x = -8000.0f - static_cast<float>(i % 2000);
            b.y = -1000.0f + static_cast<float>(i % 3000);
            b.z = 50.0f;
            b.level = static_cast<uint8>(1 + (i % 80));
            b.classId = static_cast<uint8>(1 + (i % 11));
            b.role = static_cast<uint8>(i % 4);
            b.inCombat = (i % 5 == 0);
            b.isAlive = true;
            b.isGhost = false;
            b.isInFlight = false;
            b.isDungeon = (i % 25 == 0);
            b.hasCraftOrder = (i % 30 == 0);
            b.hasGatherOrder = (i % 15 == 0);
            b.inEncounter = (i % 50 == 0);
            b.brainTaskType = static_cast<uint8>(1 + (i % 4));
            pool.push_back(b);
        }

        for (uint32 numPlayers : playerScales)
        {
            std::vector<MockPlayer> players;
            players.reserve(numPlayers);
            for (uint32 p = 0; p < numPlayers; ++p)
            {
                MockPlayer pl;
                pl.guidLow = 10000 + p;
                pl.teamId = 0; // Alliance
                pl.mapId = (p % 2 == 0) ? 0 : 1;
                pl.zoneId = 12;
                pl.guildId = 77;
                pl.groupLeaderGuid = 0;
                pl.x = -9000.0f;
                pl.y = 0.0f;
                pl.z = 50.0f;
                players.push_back(pl);
            }

            auto start = std::chrono::high_resolution_clock::now();

            size_t totalPackets = 0;
            size_t totalBytes = 0;
            size_t totalMatchedBots = 0;

            for (auto const& pl : players)
            {
                // Simulate NEARBY query (1500 yd)
                auto matched = SimulateFilter(pl, BotMapMode::Nearby, 1500, pool);
                totalMatchedBots += matched.size();

                auto chunks = BotMapProtocol::FormatBotsChunks(1, matched);
                totalPackets += chunks.size() + 2; // + BEGIN and END
                totalBytes += BotMapProtocol::FormatBegin(1, static_cast<uint32>(matched.size()), static_cast<uint32>(chunks.size())).size();
                for (auto const& c : chunks)
                    totalBytes += c.size();
                totalBytes += BotMapProtocol::FormatEnd(1).size();
            }

            auto end = std::chrono::high_resolution_clock::now();
            auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            double elapsedMs = elapsedUs / 1000.0;

            std::printf(" Scale: %4u bots | %2u players -> Elapsed: %6.2f ms | Filtered: %4zu bots | Packets: %3zu | Traffic: %5zu bytes (%4.1f KB)\n",
                numBots, numPlayers, elapsedMs, totalMatchedBots, totalPackets, totalBytes, totalBytes / 1024.0);
        }
        std::cout << "--------------------------------------------------------" << std::endl;
    }
}

int main()
{
    std::cout << "Running CoA World Map Intelligence Automated Test Suite...\n" << std::endl;

    TestProtocolBasics();
    TestPacketChunkingBudget();
    TestDetailSerialization();
    TestEventVisibilityPolicy();
    TestServerFilteringModes();
    TestCoordinateTransformation();

    RunSyntheticLoadBenchmark();

    std::cout << "\n========================================================" << std::endl;
    std::cout << " Test Results: " << _totalChecks << " checks, " << _failures << " failures." << std::endl;
    std::cout << "========================================================" << std::endl;

    return _failures == 0 ? 0 : 1;
}
