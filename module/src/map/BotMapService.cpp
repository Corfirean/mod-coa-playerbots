/*
 * mod-coa-playerbots -- CoA World Map Intelligence
 * BotMapService implementation.
 */

#include "BotMapService.h"
#include "BotMgr.h"
#include "BotAI.h"
#include "BotEconomy.h"
#include "world/WorldBrain.h"
#include "world/WorldTask.h"
#include "Player.h"
#include "Group.h"
#include "GameTime.h"
#include "Log.h"
#include "SharedDefines.h"
#include <cmath>
#include <algorithm>

namespace
{
    class DefaultEventProvider : public IWorldEventProvider
    {
    public:
        std::vector<WorldMapEventSnapshot> GetActiveEvents(uint32 /*mapId*/) override
        {
            return {};
        }

        bool IsBotInActiveEncounter(uint64 /*botGuidLow*/) override
        {
            return false;
        }
    };
}

BotMapService::BotMapService()
    : _eventProvider(std::make_shared<DefaultEventProvider>())
{
}

BotMapService* BotMapService::instance()
{
    static BotMapService instance;
    return &instance;
}

void BotMapService::SetEventProvider(std::shared_ptr<IWorldEventProvider> provider)
{
    if (provider)
        _eventProvider = provider;
    else
        _eventProvider = std::make_shared<DefaultEventProvider>();
}

void BotMapService::CleanStaleRateLimits(uint32 nowMs)
{
    auto pruneMap = [nowMs](std::unordered_map<uint64, uint32>& m) {
        if (m.size() > 1024)
        {
            for (auto it = m.begin(); it != m.end(); )
            {
                if ((nowMs - it->second) > 60000)
                    it = m.erase(it);
                else
                    ++it;
            }
        }
    };

    pruneMap(_lastQueryTimes);
    pruneMap(_lastDetailTimes);
    pruneMap(_lastEventsTimes);
}

BotMapActivity BotMapService::ResolveBotActivity(Player* bot) const
{
    if (!bot)
        return BotMapActivity::Idle;

    // 1. Dead or Ghost
    if (!bot->IsAlive() || bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
        return BotMapActivity::Resting;

    // 2. Combat
    if (bot->IsInCombat())
        return BotMapActivity::Fighting;

    // 3. Dungeon / Instance
    if (bot->GetMap() && bot->GetMap()->IsDungeon())
        return BotMapActivity::Dungeon;

    // 4. In Flight (taxi)
    if (bot->IsInFlight())
        return BotMapActivity::Traveling;

    // 5. Active World Encounter
    if (_eventProvider && _eventProvider->IsBotInActiveEncounter(bot->GetGUID().GetCounter()))
        return BotMapActivity::WorldEncounter;

    // 6. Grouped (outside instance)
    if (bot->GetGroup())
        return BotMapActivity::GroupActivity;

    // 7. Craft order active
    if (sBotMgr->GetCraftOrder(bot->GetGUID()))
        return BotMapActivity::Crafting;

    // 8. Guild gather order active
    if (sBotMgr->GetGuildGatherOrder(bot->GetGUID()))
        return BotMapActivity::Gathering;

    // 9. WorldBrain state
    WorldGoal goal = WorldGoal::None;
    WorldTaskType taskType = WorldTaskType::None;
    TaskPhase phase = TaskPhase::Planning;
    bool isBreak = false;

    if (WorldBrain::GetBrainActivity(bot->GetGUID(), goal, taskType, phase, isBreak))
    {
        if (isBreak)
            return BotMapActivity::Resting;

        if (phase == TaskPhase::TravelToArea || taskType == WorldTaskType::Travel)
            return BotMapActivity::Traveling;

        if (taskType == WorldTaskType::Gather)
            return BotMapActivity::Gathering;

        if (taskType == WorldTaskType::QuestObjective ||
            taskType == WorldTaskType::QuestAccept ||
            taskType == WorldTaskType::QuestTurnIn)
            return BotMapActivity::Questing;

        if (taskType == WorldTaskType::Vendor || taskType == WorldTaskType::Repair)
            return BotMapActivity::Other;

        if (goal == WorldGoal::Questing)
            return BotMapActivity::Questing;

        if (goal == WorldGoal::Gathering)
            return BotMapActivity::Gathering;

        if (goal == WorldGoal::Traveling)
            return BotMapActivity::Traveling;

        if (goal == WorldGoal::Break)
            return BotMapActivity::Resting;
    }

    return BotMapActivity::Idle;
}

bool BotMapService::BuildPinSnapshot(Player* bot, Player* commander, BotMapPinSnapshot& outPin) const
{
    if (!bot || !commander || !bot->IsInWorld())
        return false;

    // Never show self
    if (bot == commander)
        return false;

    // Never show enemy bots
    if (bot->GetTeamId() != commander->GetTeamId())
        return false;

    // Never duplicate party/raid members (WoW native pins handle them)
    if (bot->GetGroup() && bot->GetGroup() == commander->GetGroup())
        return false;

    outPin.guidLow = bot->GetGUID().GetCounter();
    outPin.mapId = bot->GetMapId();
    outPin.worldX = static_cast<int32>(std::round(bot->GetPositionX()));
    outPin.worldY = static_cast<int32>(std::round(bot->GetPositionY()));
    outPin.level = static_cast<uint8>(bot->GetLevel());
    outPin.classId = static_cast<uint8>(bot->getClass());

    // Role mapping: 0=dps, 1=tank, 2=healer, 3=support
    BotRole role = BotAI::GetRole(bot->GetGUID());
    outPin.role = (role == BotRole::Tank ? 1 : role == BotRole::Healer ? 2 : role == BotRole::Support ? 3 : 0);

    // Flags
    uint8 flags = 0;
    uint32 guildId = commander->GetGuildId();
    if (guildId && bot->GetGuildId() == guildId)
        flags |= BotMapPinFlags::GuildMember;

    if (sBotMgr->GetGuildGatherOrder(bot->GetGUID()) || sBotMgr->GetCraftOrder(bot->GetGUID()))
        flags |= BotMapPinFlags::HasTask;
    else
    {
        uint64 targetC = 0, depC = 0;
        if (BotEconomy::GetGoldOrderInfo(bot, targetC, depC) && targetC > 0)
            flags |= BotMapPinFlags::HasTask;
    }

    if (bot->IsInCombat())
        flags |= BotMapPinFlags::InCombat;

    if (!bot->IsAlive() || bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
        flags |= BotMapPinFlags::Ghost;

    outPin.flags = flags;
    outPin.activity = static_cast<uint8>(ResolveBotActivity(bot));
    return true;
}

std::vector<std::string> BotMapService::HandleMapQuery(Player* commander, std::vector<std::string> const& parts)
{
    std::vector<std::string> replies;
    if (!commander)
        return replies;

    uint32 reqId = 0;
    BotMapMode mode = BotMapMode::Off;
    int32 arg1 = -1;
    uint32 arg2 = 0;

    if (!BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2))
    {
        LOG_DEBUG("module.coa-playerbots", "BotMapService: '{}' sent invalid MAPQUERY -- dropped.", commander->GetName());
        return replies;
    }

    // Rate-limiting: minimum 250ms between queries from same player
    uint32 const nowMs = static_cast<uint32>(GameTime::GetGameTimeMS().count());
    uint64 const playerGuid = commander->GetGUID().GetRawValue();
    CleanStaleRateLimits(nowMs);

    auto itQuery = _lastQueryTimes.find(playerGuid);
    if (itQuery != _lastQueryTimes.end() && (nowMs - itQuery->second) < 250)
    {
        // Dropped for flood protection
        return replies;
    }
    _lastQueryTimes[playerGuid] = nowMs;

    // Mode 0: OFF
    if (mode == BotMapMode::Off)
    {
        replies.push_back(BotMapProtocol::FormatBegin(reqId, 0, 0, _revision));
        replies.push_back(BotMapProtocol::FormatEnd(reqId));
        return replies;
    }

    BotMapCommanderView view;
    view.guidLow = commander->GetGUID().GetCounter();
    view.teamId = commander->GetTeamId();
    view.mapId = commander->GetMapId();
    view.zoneId = commander->GetZoneId();
    view.guildId = commander->GetGuildId();
    view.groupLeaderGuid = commander->GetGroup() ? commander->GetGroup()->GetLeaderGUID().GetCounter() : 0;
    view.x = commander->GetPositionX();
    view.y = commander->GetPositionY();
    view.z = commander->GetPositionZ();

    uint32 radius = (mode == BotMapMode::Nearby && arg1 > 0) ? static_cast<uint32>(arg1) : 1000;
    int32 filterMap = (mode == BotMapMode::AllAllied || mode == BotMapMode::GuildOnly) ? arg1 : -1;
    int32 filterArea = (arg2 > 0) ? static_cast<int32>(arg2) : 0;

    std::vector<BotMapPinSnapshot> matchedPins;
    for (Player* bot : sBotMgr->GetOnlineBots())
    {
        if (!bot || !bot->IsInWorld())
            continue;

        BotMapFilterTarget target;
        target.guidLow = bot->GetGUID().GetCounter();
        target.teamId = bot->GetTeamId();
        target.mapId = bot->GetMapId();
        target.zoneId = bot->GetZoneId();
        target.guildId = bot->GetGuildId();
        target.groupLeaderGuid = bot->GetGroup() ? bot->GetGroup()->GetLeaderGUID().GetCounter() : 0;
        target.x = bot->GetPositionX();
        target.y = bot->GetPositionY();
        target.z = bot->GetPositionZ();
        target.level = static_cast<uint8>(bot->GetLevel());
        target.classId = static_cast<uint8>(bot->getClass());

        BotRole role = BotAI::GetRole(bot->GetGUID());
        target.role = (role == BotRole::Tank ? 1 : role == BotRole::Healer ? 2 : role == BotRole::Support ? 3 : 0);
        target.inCombat = bot->IsInCombat();
        target.isGhost = bot->HasPlayerFlag(PLAYER_FLAGS_GHOST);
        target.isAlive = bot->IsAlive();
        target.isInWorld = bot->IsInWorld();
        target.activity = ResolveBotActivity(bot);

        if (sBotMgr->GetGuildGatherOrder(bot->GetGUID()) || sBotMgr->GetCraftOrder(bot->GetGUID()))
            target.hasTask = true;
        else
        {
            uint64 targetC = 0, depC = 0;
            if (BotEconomy::GetGoldOrderInfo(bot, targetC, depC) && targetC > 0)
                target.hasTask = true;
        }

        BotMapPinSnapshot pin;
        if (BotMapFilter::FilterBot(view, mode, filterMap, filterArea, radius, target, pin))
            matchedPins.push_back(pin);
    }

    // Format chunked replies
    std::vector<std::string> chunks = BotMapProtocol::FormatBotsChunks(reqId, matchedPins);
    uint32 const totalChunks = static_cast<uint32>(chunks.size());
    uint32 const totalBots = static_cast<uint32>(matchedPins.size());

    replies.push_back(BotMapProtocol::FormatBegin(reqId, totalBots, totalChunks, ++_revision));
    for (auto& chunk : chunks)
        replies.push_back(std::move(chunk));
    replies.push_back(BotMapProtocol::FormatEnd(reqId));

    return replies;
}

std::vector<std::string> BotMapService::HandleMapDetail(Player* commander, std::vector<std::string> const& parts)
{
    std::vector<std::string> replies;
    if (!commander || parts.size() < 3)
        return replies;

    char* endPtr = nullptr;
    uint32 reqId = static_cast<uint32>(std::strtoul(parts[1].c_str(), &endPtr, 10));
    if (endPtr == parts[1].c_str())
        return replies;

    uint32 botGuidLow = static_cast<uint32>(std::strtoul(parts[2].c_str(), &endPtr, 10));
    if (endPtr == parts[2].c_str() || !botGuidLow)
        return replies;

    // Rate-limiting for detail queries (100ms per player)
    uint32 const nowMs = static_cast<uint32>(GameTime::GetGameTimeMS().count());
    uint64 const playerGuid = commander->GetGUID().GetRawValue();
    auto itDetail = _lastDetailTimes.find(playerGuid);
    if (itDetail != _lastDetailTimes.end() && (nowMs - itDetail->second) < 100)
    {
        return replies;
    }
    _lastDetailTimes[playerGuid] = nowMs;

    Player* bot = sBotMgr->FindBotPlayer(botGuidLow);
    if (!bot || !bot->IsInWorld())
        return replies;

    // Security check: only allied bots can be inspected!
    if (bot->GetTeamId() != commander->GetTeamId())
        return replies;

    BotMapDetailSnapshot detail;
    detail.guidLow = botGuidLow;
    detail.name = bot->GetName();
    detail.specId = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
    char const* specName = BotAI::GetSpecName(bot->getClass(), detail.specId);
    detail.specName = specName ? specName : "Unknown";
    detail.avgItemLevel = static_cast<uint32>(std::round(bot->GetAverageItemLevel()));

    BotMapActivity activity = ResolveBotActivity(bot);
    detail.activityName = BotMapProtocol::ActivityToString(activity);

    // Privacy Gate: guild tasks are exclusively disclosed to members of the bot's guild!
    bool const isSameGuild = (commander->GetGuildId() != 0 && bot->GetGuildId() == commander->GetGuildId());
    if (isSameGuild)
    {
        if (auto const* gather = sBotMgr->GetGuildGatherOrder(bot->GetGUID()))
        {
            detail.taskType = BotMapTaskType::Gather;
            detail.taskItemEntry = gather->itemEntry;
            detail.taskTargetCount = gather->targetCount;
            detail.taskCompletedCount = gather->gatheredCount;
            detail.taskStatus = "In Progress";
        }
        else if (auto const* craft = sBotMgr->GetCraftOrder(bot->GetGUID()))
        {
            detail.taskType = BotMapTaskType::Craft;
            detail.taskItemEntry = craft->itemEntry;
            detail.taskTargetCount = craft->remainingCount;
            detail.taskCompletedCount = 0;
            detail.taskStatus = craft->awaitingCastResult ? "Crafting" : "Gathering Reagents";
        }
        else
        {
            uint64 targetCopper = 0;
            uint64 depositedCopper = 0;
            if (BotEconomy::GetGoldOrderInfo(bot, targetCopper, depositedCopper) && targetCopper > 0)
            {
                detail.taskType = BotMapTaskType::Gold;
                detail.taskItemEntry = 0;
                detail.taskTargetCount = static_cast<uint32>(targetCopper / 10000);
                detail.taskCompletedCount = static_cast<uint32>(depositedCopper / 10000);
                detail.taskStatus = "Earning Gold";
            }
            else
            {
                detail.taskType = BotMapTaskType::None;
                detail.taskItemEntry = 0;
                detail.taskTargetCount = 0;
                detail.taskCompletedCount = 0;
                detail.taskStatus = "";
            }
        }
    }
    else
    {
        // Non-guild commander gets zeroed task fields
        detail.taskType = BotMapTaskType::None;
        detail.taskItemEntry = 0;
        detail.taskTargetCount = 0;
        detail.taskCompletedCount = 0;
        detail.taskStatus = "";
    }

    replies.push_back(BotMapProtocol::FormatDetail(reqId, detail));
    return replies;
}

std::vector<std::string> BotMapService::HandleMapEvents(Player* commander, std::vector<std::string> const& parts)
{
    std::vector<std::string> replies;
    if (!commander || parts.size() < 2)
        return replies;

    char* endPtr = nullptr;
    uint32 reqId = static_cast<uint32>(std::strtoul(parts[1].c_str(), &endPtr, 10));
    if (endPtr == parts[1].c_str())
        return replies;

    int32 requestedMap = -1;
    if (parts.size() > 2 && !parts[2].empty())
    {
        long long val = std::strtoll(parts[2].c_str(), &endPtr, 10);
        if (endPtr != parts[2].c_str() && *endPtr == '\0')
        {
            if (val >= 0 && val <= 0x7FFFFFFF)
                requestedMap = static_cast<int32>(val);
        }
    }
    uint32 mapId = (requestedMap >= 0) ? static_cast<uint32>(requestedMap) : commander->GetMapId();

    // Rate limiting: 500ms between event queries
    uint32 const nowMs = static_cast<uint32>(GameTime::GetGameTimeMS().count());
    uint64 const playerGuid = commander->GetGUID().GetRawValue();
    auto itEvents = _lastEventsTimes.find(playerGuid);
    if (itEvents != _lastEventsTimes.end() && (nowMs - itEvents->second) < 500)
    {
        return replies;
    }
    _lastEventsTimes[playerGuid] = nowMs;

    if (!_eventProvider)
        return replies;

    std::vector<WorldMapEventSnapshot> activeEvents = _eventProvider->GetActiveEvents(mapId);
    return BotMapProtocol::FormatEvents(reqId, activeEvents);
}
