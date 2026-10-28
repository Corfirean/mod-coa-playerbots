/*
 * mod-coa-playerbots -- CoA World Map Intelligence
 * BotMapFilter: Spatial, faction, group, and mode filtering logic for World Map pins.
 */

#ifndef COA_PLAYERBOTS_BOT_MAP_FILTER_H
#define COA_PLAYERBOTS_BOT_MAP_FILTER_H

#include "BotMapProtocol.h"
#include <vector>

struct BotMapFilterTarget
{
    uint32 guidLow = 0;
    uint8 teamId = 0;
    uint32 mapId = 0;
    uint32 zoneId = 0;
    uint32 guildId = 0;
    uint64 groupLeaderGuid = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint8 level = 0;
    uint8 classId = 0;
    uint8 role = 0;
    bool inCombat = false;
    bool isGhost = false;
    bool isAlive = true;
    bool isInWorld = true;
    BotMapActivity activity = BotMapActivity::Idle;
    bool hasTask = false;
};

struct BotMapCommanderView
{
    uint32 guidLow = 0;
    uint8 teamId = 0;
    uint32 mapId = 0;
    uint32 zoneId = 0;
    uint32 guildId = 0;
    uint64 groupLeaderGuid = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

namespace BotMapFilter
{
    bool FilterBot(
        BotMapCommanderView const& commander,
        BotMapMode mode,
        int32 filterMapId,
        uint32 radius,
        BotMapFilterTarget const& bot,
        BotMapPinSnapshot& outPin);

    std::vector<BotMapPinSnapshot> FilterBots(
        BotMapCommanderView const& commander,
        BotMapMode mode,
        int32 filterMapId,
        uint32 radius,
        std::vector<BotMapFilterTarget> const& bots);
}

#endif // COA_PLAYERBOTS_BOT_MAP_FILTER_H
