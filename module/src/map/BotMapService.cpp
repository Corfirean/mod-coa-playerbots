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
    uint32 arg1 = 0;
    uint32 arg2 = 0;

    if (!BotMapProtocol::ParseQuery(parts, reqId, mode, arg1, arg2))
    {
        LOG_DEBUG("module.coa-playerbots", "BotMapService: '{}' sent invalid MAPQUERY -- dropped.", commander->GetName());
        return replies;
    }

    // Rate-limiting: minimum 250ms between queries from same player
    uint32 const nowMs = static_cast<uint32>(GameTime::GetGameTimeMS().count());
    uint64 const playerGuid = commander->GetGUID().GetRawValue();
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

    std::vector<BotMapPinSnapshot> matchedPins;
    uint32 const commanderMapId = commander->GetMapId();
    uint32 const commanderZoneId = commander->GetZoneId();
    uint32 const commanderGuildId = commander->GetGuildId();
    float const cX = commander->GetPositionX();
    float const cY = commander->GetPositionY();
    float const cZ = commander->GetPositionZ();

    // Mode 1: NEARBY radius
    float maxDistSq = 1000.0f * 1000.0f;
    if (mode == BotMapMode::Nearby)
    {
        uint32 radius = arg1 ? arg1 : 1000;
        radius = std::clamp(radius, 100u, 5000u);
        maxDistSq = static_cast<float>(radius) * static_cast<float>(radius);
    }

    for (Player* bot : sBotMgr->GetOnlineBots())
    {
        if (!bot || !bot->IsInWorld())
            continue;

        // Common filter checks
        if (bot == commander)
            continue;
        if (bot->GetTeamId() != commander->GetTeamId())
            continue;
        if (bot->GetGroup() && bot->GetGroup() == commander->GetGroup())
            continue;

        // Mode-specific filtering
        if (mode == BotMapMode::Nearby)
        {
            if (bot->GetMapId() != commanderMapId)
                continue;
            float dx = bot->GetPositionX() - cX;
            float dy = bot->GetPositionY() - cY;
            float dz = bot->GetPositionZ() - cZ;
            if ((dx * dx + dy * dy + dz * dz) > maxDistSq)
                continue;
        }
        else if (mode == BotMapMode::CurrentZone)
        {
            if (bot->GetMapId() != commanderMapId)
                continue;
            if (bot->GetZoneId() != commanderZoneId)
                continue;
        }
        else if (mode == BotMapMode::GuildOnly)
        {
            if (!commanderGuildId || bot->GetGuildId() != commanderGuildId)
                continue;
        }
        else if (mode == BotMapMode::AllAllied)
        {
            // If mapId specified, filter by that map
            if (arg1 != 0 && arg1 != 0xFFFFFFFF && bot->GetMapId() != arg1)
                continue;
        }

        BotMapPinSnapshot pin;
        if (BuildPinSnapshot(bot, commander, pin))
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

    uint32 reqId = static_cast<uint32>(std::strtoul(parts[1].c_str(), nullptr, 10));
    uint32 botGuidLow = static_cast<uint32>(std::strtoul(parts[2].c_str(), nullptr, 10));
    if (!botGuidLow)
        return replies;

    Player* bot = sBotMgr->FindBotPlayer(botGuidLow);
    if (!bot || !bot->IsInWorld())
        return replies;

    // Security check: only allied bots can be inspected!
    if (bot->GetTeamId() != commander->GetTeamId())
        return replies;

    BotMapDetailSnapshot detail;
    detail.guidLow = botGuidLow;
    detail.specId = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
    char const* specName = BotAI::GetSpecName(bot->getClass(), detail.specId);
    detail.specName = specName ? specName : "Unknown";
    detail.avgItemLevel = static_cast<uint32>(std::round(bot->GetAverageItemLevel()));

    BotMapActivity activity = ResolveBotActivity(bot);
    detail.activityName = BotMapProtocol::ActivityToString(activity);

    // Guild Assignment check
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
        detail.taskTargetCount = craft->remainingCount; // Remaining items to craft
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
            detail.taskTargetCount = static_cast<uint32>(targetCopper / 10000); // in gold
            detail.taskCompletedCount = static_cast<uint32>(depositedCopper / 10000); // in gold
            detail.taskStatus = "Earning Gold";
        }
    }

    replies.push_back(BotMapProtocol::FormatDetail(reqId, detail));
    return replies;
}

std::vector<std::string> BotMapService::HandleMapEvents(Player* commander, std::vector<std::string> const& parts)
{
    std::vector<std::string> replies;
    if (!commander || parts.size() < 2)
        return replies;

    uint32 reqId = static_cast<uint32>(std::strtoul(parts[1].c_str(), nullptr, 10));
    uint32 mapId = parts.size() > 2 ? static_cast<uint32>(std::strtoul(parts[2].c_str(), nullptr, 10)) : commander->GetMapId();

    if (!_eventProvider)
        return replies;

    std::vector<WorldMapEventSnapshot> activeEvents = _eventProvider->GetActiveEvents(mapId);
    std::vector<std::string> chunks = BotMapProtocol::FormatEvents(reqId, activeEvents);
    for (auto& chunk : chunks)
        replies.push_back(std::move(chunk));

    return replies;
}
