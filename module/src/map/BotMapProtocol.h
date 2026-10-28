/*
 * mod-coa-playerbots -- CoA World Map Intelligence
 * Protocol definitions and serialization routines for COABOT map channel.
 */

#ifndef COA_PLAYERBOTS_BOT_MAP_PROTOCOL_H
#define COA_PLAYERBOTS_BOT_MAP_PROTOCOL_H

#include "Define.h"
#include "WorldMapEventSnapshot.h"
#include <string>
#include <vector>

enum class BotMapMode : uint8
{
    Off = 0,
    Nearby = 1,
    CurrentZone = 2,
    GuildOnly = 3,
    AllAllied = 4
};

enum class BotMapActivity : uint8
{
    Idle = 0,
    Questing = 1,
    Traveling = 2,
    Gathering = 3,
    Crafting = 4,
    Fighting = 5,
    Resting = 6,
    GroupActivity = 7,
    Dungeon = 8,
    WorldEncounter = 9,
    Other = 10
};

enum class BotMapTaskType : uint8
{
    None = 0,
    Gather = 1,
    Craft = 2,
    Gold = 3
};

namespace BotMapPinFlags
{
    constexpr uint8 GuildMember = 1 << 0;
    constexpr uint8 HasTask     = 1 << 1;
    constexpr uint8 InCombat    = 1 << 2;
    constexpr uint8 Ghost       = 1 << 3;
}

struct BotMapPinSnapshot
{
    uint32 guidLow = 0;
    uint32 mapId = 0;
    int32 worldX = 0;
    int32 worldY = 0;
    uint8 level = 0;
    uint8 classId = 0;
    uint8 role = 0;   // 0=dps, 1=tank, 2=healer, 3=support
    uint8 flags = 0;  // BotMapPinFlags
    uint8 activity = 0; // BotMapActivity
};

struct BotMapDetailSnapshot
{
    uint32 guidLow = 0;
    std::string name;
    uint32 specId = 0;
    uint32 avgItemLevel = 0;
    std::string specName;
    std::string activityName;
    BotMapTaskType taskType = BotMapTaskType::None;
    uint32 taskItemEntry = 0;
    uint32 taskTargetCount = 0;
    uint32 taskCompletedCount = 0;
    std::string taskStatus;
};

namespace BotMapProtocol
{
    constexpr size_t PACKET_BUDGET_BYTES = 200;

    std::string ActivityToString(BotMapActivity activity);
    char const* TaskTypeToString(BotMapTaskType taskType);
    std::string SanitizeWireString(std::string const& s);
    std::string TruncateUtf8Safe(std::string const& str, size_t maxBytes);

    // Bot query serialization
    std::string FormatBegin(uint32 reqId, uint32 totalBots, uint32 totalChunks, uint32 revision = 0);
    std::string FormatEnd(uint32 reqId);
    std::vector<std::string> FormatBotsChunks(uint32 reqId, std::vector<BotMapPinSnapshot> const& bots);
    std::string FormatDetail(uint32 reqId, BotMapDetailSnapshot const& detail);

    // Dynamic World Events serialization
    std::string FormatEventsBegin(uint32 reqId, uint32 totalEvents, uint32 totalChunks);
    std::string FormatEventsEnd(uint32 reqId);
    std::vector<std::string> FormatEventsChunks(uint32 reqId, std::vector<WorldMapEventSnapshot> const& events);
    std::vector<std::string> FormatEvents(uint32 reqId, std::vector<WorldMapEventSnapshot> const& events);

    // Policy check for dynamic world events
    bool CanRevealWorldEvent(WorldMapEventSnapshot const& ev);

    // Parsing helpers
    bool ParseQuery(std::vector<std::string> const& parts, uint32& outReqId, BotMapMode& outMode, int32& outArg1, uint32& outArg2);
}

#endif // COA_PLAYERBOTS_BOT_MAP_PROTOCOL_H
