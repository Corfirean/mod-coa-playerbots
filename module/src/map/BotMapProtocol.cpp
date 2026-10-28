/*
 * mod-coa-playerbots -- CoA World Map Intelligence
 * Protocol definitions and serialization routines for COABOT map channel.
 */

#include "BotMapProtocol.h"
#include <sstream>
#include <algorithm>

namespace BotMapProtocol
{
    std::string ActivityToString(BotMapActivity activity)
    {
        switch (activity)
        {
            case BotMapActivity::Idle: return "Idle";
            case BotMapActivity::Questing: return "Questing";
            case BotMapActivity::Traveling: return "Traveling";
            case BotMapActivity::Gathering: return "Gathering";
            case BotMapActivity::Crafting: return "Crafting";
            case BotMapActivity::Fighting: return "In Combat";
            case BotMapActivity::Resting: return "Resting";
            case BotMapActivity::GroupActivity: return "Group Adventure";
            case BotMapActivity::Dungeon: return "In Dungeon";
            case BotMapActivity::WorldEncounter: return "World Encounter";
            case BotMapActivity::Other:
            default:
                return "Wandering";
        }
    }

    char const* TaskTypeToString(BotMapTaskType taskType)
    {
        switch (taskType)
        {
            case BotMapTaskType::Gather: return "Gather";
            case BotMapTaskType::Craft: return "Craft";
            case BotMapTaskType::Gold: return "Gold Order";
            case BotMapTaskType::None:
            default:
                return "None";
        }
    }

    static std::string SanitizeWireString(std::string const& s)
    {
        std::string out = s;
        for (char& c : out)
        {
            if (c == ':' || c == '|' || c == ',' || c == '\t' || c == '\n' || c == '\r')
                c = ' ';
        }
        return out;
    }

    std::string FormatBegin(uint32 reqId, uint32 totalBots, uint32 totalChunks, uint32 revision)
    {
        return "MAPBEGIN:" + std::to_string(reqId) + ":" + std::to_string(totalBots) + ":" +
               std::to_string(totalChunks) + ":" + std::to_string(revision);
    }

    std::string FormatEnd(uint32 reqId)
    {
        return "MAPEND:" + std::to_string(reqId);
    }

    std::vector<std::string> FormatBotsChunks(uint32 reqId, std::vector<BotMapPinSnapshot> const& bots)
    {
        std::vector<std::string> chunks;
        if (bots.empty())
            return chunks;

        std::string const headerPrefix = "MAPBOTS:" + std::to_string(reqId) + ":";
        size_t currentChunkIdx = 0;
        std::string currentBody;

        auto flushChunk = [&]() {
            if (!currentBody.empty())
            {
                chunks.push_back(headerPrefix + std::to_string(currentChunkIdx++) + ":" + currentBody);
                currentBody.clear();
            }
        };

        for (auto const& bot : bots)
        {
            std::string entry = std::to_string(bot.guidLow) + "," +
                                std::to_string(bot.worldX) + "," +
                                std::to_string(bot.worldY) + "," +
                                std::to_string((uint32)bot.level) + "," +
                                std::to_string((uint32)bot.classId) + "," +
                                std::to_string((uint32)bot.role) + "," +
                                std::to_string((uint32)bot.flags) + "," +
                                std::to_string((uint32)bot.activity);

            // Estimate total packet size if this entry is added:
            // headerPrefix + chunkIdx (up to 3 chars) + ":" + currentBody + ("|" + entry)
            size_t projectedLen = headerPrefix.size() + 4 + currentBody.size() + (currentBody.empty() ? 0 : 1) + entry.size();
            if (projectedLen > PACKET_BUDGET_BYTES)
            {
                flushChunk();
            }

            if (!currentBody.empty())
                currentBody += "|";
            currentBody += entry;
        }

        flushChunk();
        return chunks;
    }

    std::string FormatDetail(uint32 reqId, BotMapDetailSnapshot const& detail)
    {
        return "MAPDETAIL:" + std::to_string(reqId) + ":" +
               std::to_string(detail.guidLow) + ":" +
               std::to_string(detail.specId) + ":" +
               std::to_string(detail.avgItemLevel) + ":" +
               SanitizeWireString(detail.specName) + ":" +
               SanitizeWireString(detail.activityName) + ":" +
               std::to_string((uint32)detail.taskType) + ":" +
               std::to_string(detail.taskItemEntry) + ":" +
               std::to_string(detail.taskTargetCount) + ":" +
               std::to_string(detail.taskCompletedCount) + ":" +
               SanitizeWireString(detail.taskStatus);
    }

    std::vector<std::string> FormatEvents(uint32 reqId, std::vector<WorldMapEventSnapshot> const& events)
    {
        std::vector<std::string> chunks;
        if (events.empty())
            return chunks;

        std::string const headerPrefix = "MAPEVENTS:" + std::to_string(reqId) + ":";
        std::string currentBody;

        auto flushChunk = [&]() {
            if (!currentBody.empty())
            {
                chunks.push_back(headerPrefix + currentBody);
                currentBody.clear();
            }
        };

        for (auto const& ev : events)
        {
            // Only format visible events
            if (ev.visibility == WorldMapEventVisibility::Hidden)
                continue;

            std::string entry = std::to_string(ev.eventId) + "," +
                                std::to_string((uint32)ev.eventType) + "," +
                                std::to_string((int32)ev.centerX) + "," +
                                std::to_string((int32)ev.centerY) + "," +
                                std::to_string((uint32)ev.phase) + "," +
                                std::to_string(ev.allianceCount) + "," +
                                std::to_string(ev.hordeCount) + "," +
                                SanitizeWireString(ev.name);

            size_t projectedLen = headerPrefix.size() + currentBody.size() + (currentBody.empty() ? 0 : 1) + entry.size();
            if (projectedLen > PACKET_BUDGET_BYTES)
            {
                flushChunk();
            }

            if (!currentBody.empty())
                currentBody += "|";
            currentBody += entry;
        }

        flushChunk();
        return chunks;
    }

    bool ParseQuery(std::vector<std::string> const& parts, uint32& outReqId, BotMapMode& outMode, uint32& outArg1, uint32& outArg2)
    {
        // Format: MAPQUERY:<reqId>:<mode>[:<arg1>[:<arg2>]]
        if (parts.size() < 3)
            return false;

        outReqId = (uint32)std::strtoul(parts[1].c_str(), nullptr, 10);
        uint32 m = (uint32)std::strtoul(parts[2].c_str(), nullptr, 10);
        if (m > 4)
            return false;
        outMode = static_cast<BotMapMode>(m);

        outArg1 = parts.size() > 3 ? (uint32)std::strtoul(parts[3].c_str(), nullptr, 10) : 0;
        outArg2 = parts.size() > 4 ? (uint32)std::strtoul(parts[4].c_str(), nullptr, 10) : 0;
        return true;
    }
}
