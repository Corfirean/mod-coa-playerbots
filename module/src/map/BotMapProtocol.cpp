/*
 * mod-coa-playerbots -- CoA World Map Intelligence
 * Protocol definitions and serialization routines for COABOT map channel.
 */

#include "BotMapProtocol.h"
#include <sstream>
#include <algorithm>
#include <cerrno>
#include <cstdlib>

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

    std::string SanitizeWireString(std::string const& s)
    {
        std::string out = s;
        for (char& c : out)
        {
            if (c == ':' || c == '|' || c == ',' || c == '\t' || c == '\n' || c == '\r')
                c = ' ';
        }
        return out;
    }

    std::string TruncateUtf8Safe(std::string const& str, size_t maxBytes)
    {
        if (str.size() <= maxBytes)
            return str;

        size_t len = maxBytes;
        while (len > 0 && (static_cast<uint8>(str[len]) & 0xC0) == 0x80)
        {
            --len;
        }

        if (len == 0)
            return "";

        uint8 lead = static_cast<uint8>(str[len]);
        size_t seqLen = 1;
        if ((lead & 0x80) == 0x00)
            seqLen = 1;
        else if ((lead & 0xE0) == 0xC0)
            seqLen = 2;
        else if ((lead & 0xF0) == 0xE0)
            seqLen = 3;
        else if ((lead & 0xF8) == 0xF0)
            seqLen = 4;

        if (len + seqLen > maxBytes)
            return str.substr(0, len);

        return str.substr(0, len + seqLen);
    }

    bool CanRevealWorldEvent(WorldMapEventSnapshot const& ev)
    {
        // 1. Must be explicitly flagged Public
        if (ev.visibility != WorldMapEventVisibility::Public)
            return false;

        // 2. Only FactionSkirmish is publicly revealable in current version
        if (ev.eventType != WorldMapEventType::FactionSkirmish)
            return false;

        // 3. Must be in Active phase (phase 2)
        // Hidden/Candidate/Prepare phases must never be revealed
        if (ev.phase != 2)
            return false;

        return true;
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

        size_t currentChunkIdx = 0;
        std::string currentBody;

        auto makeHeader = [&](size_t idx) {
            return "MAPBOTS:" + std::to_string(reqId) + ":" + std::to_string(idx) + ":";
        };

        auto flushChunk = [&]() {
            if (!currentBody.empty())
            {
                std::string packet = makeHeader(currentChunkIdx++) + currentBody;
                if (packet.size() > PACKET_BUDGET_BYTES)
                    packet = TruncateUtf8Safe(packet, PACKET_BUDGET_BYTES);
                chunks.push_back(std::move(packet));
                currentBody.clear();
            }
        };

        for (auto const& bot : bots)
        {
            // Wire format: <guidLow>,<mapId>,<worldX>,<worldY>,<level>,<classId>,<role>,<flags>,<activity>
            std::string entry = std::to_string(bot.guidLow) + "," +
                                std::to_string(bot.mapId) + "," +
                                std::to_string(bot.worldX) + "," +
                                std::to_string(bot.worldY) + "," +
                                std::to_string((uint32)bot.level) + "," +
                                std::to_string((uint32)bot.classId) + "," +
                                std::to_string((uint32)bot.role) + "," +
                                std::to_string((uint32)bot.flags) + "," +
                                std::to_string((uint32)bot.activity);

            std::string candidateHeader = makeHeader(currentChunkIdx);
            size_t projectedLen = candidateHeader.size() + currentBody.size() + (currentBody.empty() ? 0 : 1) + entry.size();
            if (projectedLen > PACKET_BUDGET_BYTES && !currentBody.empty())
            {
                flushChunk();
                candidateHeader = makeHeader(currentChunkIdx);
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
        std::string cleanName = SanitizeWireString(detail.name.empty() ? ("Bot-" + std::to_string(detail.guidLow)) : detail.name);
        std::string cleanSpec = SanitizeWireString(detail.specName);
        std::string cleanAct = SanitizeWireString(detail.activityName);
        std::string cleanStatus = SanitizeWireString(detail.taskStatus);

        auto buildPacket = [&](std::string const& n, std::string const& sp, std::string const& act, std::string const& st) {
            return "MAPDETAIL:" + std::to_string(reqId) + ":" +
                   std::to_string(detail.guidLow) + ":" +
                   n + ":" +
                   std::to_string(detail.specId) + ":" +
                   std::to_string(detail.avgItemLevel) + ":" +
                   sp + ":" +
                   act + ":" +
                   std::to_string((uint32)detail.taskType) + ":" +
                   std::to_string(detail.taskItemEntry) + ":" +
                   std::to_string(detail.taskTargetCount) + ":" +
                   std::to_string(detail.taskCompletedCount) + ":" +
                   st;
        };

        std::string result = buildPacket(cleanName, cleanSpec, cleanAct, cleanStatus);
        if (result.size() > PACKET_BUDGET_BYTES)
        {
            cleanStatus = TruncateUtf8Safe(cleanStatus, 16);
            result = buildPacket(cleanName, cleanSpec, cleanAct, cleanStatus);
            if (result.size() > PACKET_BUDGET_BYTES)
            {
                cleanSpec = TruncateUtf8Safe(cleanSpec, 16);
                result = buildPacket(cleanName, cleanSpec, cleanAct, cleanStatus);
                if (result.size() > PACKET_BUDGET_BYTES)
                {
                    cleanAct = TruncateUtf8Safe(cleanAct, 16);
                    result = buildPacket(cleanName, cleanSpec, cleanAct, cleanStatus);
                    if (result.size() > PACKET_BUDGET_BYTES)
                    {
                        cleanName = TruncateUtf8Safe(cleanName, 16);
                        result = buildPacket(cleanName, cleanSpec, cleanAct, cleanStatus);
                    }
                }
            }
        }

        if (result.size() > PACKET_BUDGET_BYTES)
            result = TruncateUtf8Safe(result, PACKET_BUDGET_BYTES);

        return result;
    }

    std::string FormatEventsBegin(uint32 reqId, uint32 totalEvents, uint32 totalChunks)
    {
        return "MAPEVENTSBEGIN:" + std::to_string(reqId) + ":" +
               std::to_string(totalEvents) + ":" + std::to_string(totalChunks);
    }

    std::string FormatEventsEnd(uint32 reqId)
    {
        return "MAPEVENTSEND:" + std::to_string(reqId);
    }

    std::vector<std::string> FormatEventsChunks(uint32 reqId, std::vector<WorldMapEventSnapshot> const& events)
    {
        std::vector<std::string> chunks;
        std::vector<WorldMapEventSnapshot> allowedEvents;
        for (auto const& ev : events)
        {
            if (CanRevealWorldEvent(ev))
                allowedEvents.push_back(ev);
        }

        if (allowedEvents.empty())
            return chunks;

        size_t currentChunkIdx = 0;
        std::string currentBody;

        auto makeHeader = [&](size_t idx) {
            return "MAPEVENTS:" + std::to_string(reqId) + ":" + std::to_string(idx) + ":";
        };

        auto flushChunk = [&]() {
            if (!currentBody.empty())
            {
                std::string packet = makeHeader(currentChunkIdx++) + currentBody;
                if (packet.size() > PACKET_BUDGET_BYTES)
                    packet = TruncateUtf8Safe(packet, PACKET_BUDGET_BYTES);
                chunks.push_back(std::move(packet));
                currentBody.clear();
            }
        };

        for (auto const& ev : allowedEvents)
        {
            std::string cleanName = SanitizeWireString(ev.name);
            cleanName = TruncateUtf8Safe(cleanName, 24);

            std::string entry = std::to_string(ev.eventId) + "," +
                                std::to_string((uint32)ev.eventType) + "," +
                                std::to_string((int32)ev.centerX) + "," +
                                std::to_string((int32)ev.centerY) + "," +
                                std::to_string((uint32)ev.phase) + "," +
                                std::to_string(ev.allianceCount) + "," +
                                std::to_string(ev.hordeCount) + "," +
                                cleanName;

            std::string candidateHeader = makeHeader(currentChunkIdx);
            size_t projectedLen = candidateHeader.size() + currentBody.size() + (currentBody.empty() ? 0 : 1) + entry.size();
            if (projectedLen > PACKET_BUDGET_BYTES && !currentBody.empty())
            {
                flushChunk();
                candidateHeader = makeHeader(currentChunkIdx);
            }

            if (!currentBody.empty())
                currentBody += "|";
            currentBody += entry;
        }

        flushChunk();
        return chunks;
    }

    std::vector<std::string> FormatEvents(uint32 reqId, std::vector<WorldMapEventSnapshot> const& events)
    {
        std::vector<std::string> replies;
        std::vector<std::string> chunks = FormatEventsChunks(reqId, events);
        uint32 totalChunks = static_cast<uint32>(chunks.size());
        uint32 totalEvents = 0;
        for (auto const& ev : events)
        {
            if (CanRevealWorldEvent(ev))
                ++totalEvents;
        }

        replies.push_back(FormatEventsBegin(reqId, totalEvents, totalChunks));
        for (auto& ch : chunks)
            replies.push_back(std::move(ch));
        replies.push_back(FormatEventsEnd(reqId));
        return replies;
    }

    bool ParseQuery(std::vector<std::string> const& parts, uint32& outReqId, BotMapMode& outMode, int32& outArg1, uint32& outArg2)
    {
        // Format: MAPQUERY:<reqId>:<mode>[:<arg1>[:<arg2>]]
        if (parts.size() < 3)
            return false;

        char* endPtr = nullptr;
        errno = 0;
        unsigned long req = std::strtoul(parts[1].c_str(), &endPtr, 10);
        if (endPtr == parts[1].c_str() || *endPtr != '\0')
            return false;
        outReqId = static_cast<uint32>(req);

        unsigned long m = std::strtoul(parts[2].c_str(), &endPtr, 10);
        if (endPtr == parts[2].c_str() || *endPtr != '\0' || m > 4)
            return false;
        outMode = static_cast<BotMapMode>(m);

        outArg1 = -1; // Default: unassigned / wildcard
        if (parts.size() > 3 && !parts[3].empty())
        {
            long long val = std::strtoll(parts[3].c_str(), &endPtr, 10);
            if (endPtr != parts[3].c_str() && *endPtr == '\0')
            {
                if (val == 4294967295LL || val == -1LL)
                    outArg1 = -1;
                else if (val >= 0 && val <= 0x7FFFFFFF)
                    outArg1 = static_cast<int32>(val);
                else
                    outArg1 = -1;
            }
        }

        outArg2 = 0;
        if (parts.size() > 4 && !parts[4].empty())
        {
            unsigned long a2 = std::strtoul(parts[4].c_str(), &endPtr, 10);
            if (endPtr != parts[4].c_str() && *endPtr == '\0')
                outArg2 = static_cast<uint32>(a2);
        }

        return true;
    }
}
