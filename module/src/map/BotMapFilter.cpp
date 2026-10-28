/*
 * mod-coa-playerbots -- CoA World Map Intelligence
 * BotMapFilter: Spatial, faction, group, and mode filtering logic for World Map pins.
 */

#include "BotMapFilter.h"
#include <cmath>
#include <algorithm>

namespace BotMapFilter
{
    bool FilterBot(
        BotMapCommanderView const& commander,
        BotMapMode mode,
        int32 filterMapId,
        uint32 radius,
        BotMapFilterTarget const& bot,
        BotMapPinSnapshot& outPin)
    {
        if (mode == BotMapMode::Off)
            return false;

        if (!bot.isInWorld)
            return false;

        // 1. Never show self
        if (bot.guidLow == commander.guidLow)
            return false;

        // 2. Never show enemy bots
        if (bot.teamId != commander.teamId)
            return false;

        // 3. Exclude own group/raid members (handled natively by WoW UI PartyRaid markers)
        if (commander.groupLeaderGuid != 0 && bot.groupLeaderGuid == commander.groupLeaderGuid)
            return false;

        // 4. Mode-specific spatial and group filtering
        if (mode == BotMapMode::Nearby)
        {
            if (bot.mapId != commander.mapId)
                return false;

            uint32 effectiveRadius = radius ? radius : 1000;
            effectiveRadius = std::clamp(effectiveRadius, 100u, 5000u);
            float maxDistSq = static_cast<float>(effectiveRadius) * static_cast<float>(effectiveRadius);

            float dx = bot.x - commander.x;
            float dy = bot.y - commander.y;
            float dz = bot.z - commander.z;
            if ((dx * dx + dy * dy + dz * dz) > maxDistSq)
                return false;
        }
        else if (mode == BotMapMode::CurrentZone)
        {
            if (bot.mapId != commander.mapId)
                return false;

            uint32 targetZone = (filterMapId > 0) ? static_cast<uint32>(filterMapId) : commander.zoneId;
            if (bot.zoneId != targetZone)
                return false;
        }
        else if (mode == BotMapMode::GuildOnly)
        {
            if (!commander.guildId || bot.guildId != commander.guildId)
                return false;

            // If client specifically requested a map (and not wildcard -1), filter by map
            if (filterMapId >= 0 && bot.mapId != static_cast<uint32>(filterMapId))
                return false;
        }
        else if (mode == BotMapMode::AllAllied)
        {
            // When filterMapId is >= 0, filter specifically by that map (e.g. 0 for Eastern Kingdoms)
            // If filterMapId is -1 (wildcard), allow all maps
            if (filterMapId >= 0 && bot.mapId != static_cast<uint32>(filterMapId))
                return false;
        }

        outPin.guidLow = bot.guidLow;
        outPin.mapId = bot.mapId;
        outPin.worldX = static_cast<int32>(std::round(bot.x));
        outPin.worldY = static_cast<int32>(std::round(bot.y));
        outPin.level = bot.level;
        outPin.classId = bot.classId;
        outPin.role = bot.role;

        uint8 flags = 0;
        if (commander.guildId && bot.guildId == commander.guildId)
            flags |= BotMapPinFlags::GuildMember;
        if (bot.hasTask)
            flags |= BotMapPinFlags::HasTask;
        if (bot.inCombat)
            flags |= BotMapPinFlags::InCombat;
        if (!bot.isAlive || bot.isGhost)
            flags |= BotMapPinFlags::Ghost;

        outPin.flags = flags;
        outPin.activity = static_cast<uint8>(bot.activity);
        return true;
    }

    std::vector<BotMapPinSnapshot> FilterBots(
        BotMapCommanderView const& commander,
        BotMapMode mode,
        int32 filterMapId,
        uint32 radius,
        std::vector<BotMapFilterTarget> const& bots)
    {
        std::vector<BotMapPinSnapshot> results;
        if (mode == BotMapMode::Off)
            return results;

        for (auto const& bot : bots)
        {
            BotMapPinSnapshot pin;
            if (FilterBot(commander, mode, filterMapId, radius, bot, pin))
                results.push_back(pin);
        }
        return results;
    }
}
