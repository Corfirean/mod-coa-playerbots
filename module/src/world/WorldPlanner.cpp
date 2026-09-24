#include "WorldPlanner.h"
#include "CreatureData.h"
#include "IQuestObjectiveHandler.h"
#include "ObjectMgr.h"
#include "ObjectiveCommon.h"
#include "Player.h"
#include "PopulationHeatmap.h"
#include "QuestDef.h"
#include "QuestInteraction.h"
#include "QuestKnowledgeBase.h"
#include "WorldReservations.h"
#include "WorldUtility.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

using namespace WorldBrainInternal;

namespace
{
    // Quest givers considered per planning pass, nearest first.
    constexpr size_t MAX_GIVERS_EVALUATED = 8;
    // Trying a talk-to objective is a last resort: it usually cannot work.
    constexpr float TALK_PENALTY = 80.0f;

    struct Candidate
    {
        WorldTask task;
        float utility = 0.0f;
        ObjectiveDef const* def = nullptr;
        ObjectiveUtilityInput input;
    };

    float Dist(Player const* bot, float x, float y)
    {
        return std::hypot(bot->GetPositionX() - x, bot->GetPositionY() - y);
    }

    bool PhaseVisible(Player const* bot, uint32 mask)
    {
        return !mask || (bot->GetPhaseMask() & mask);
    }

    float XpShare(Player* bot, Quest const* quest)
    {
        uint32 next = bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
        return next ? float(quest->XPValue(bot->GetLevel())) / float(next) : 0.0f;
    }

    bool IsChain(Player* bot, Quest const* quest)
    {
        int32 prev = quest->GetPrevQuestId();
        return prev > 0 && bot->GetQuestRewardStatus(uint32(prev));
    }

    float DropChanceFor(ObjectiveDef const& def, uint32 entry)
    {
        if (!def.itemId)
            return 1.0f;
        if (std::vector<LootSource> const* sources = QuestKB::ItemSources(def.itemId))
            for (LootSource const& source : *sources)
                if (source.entry == entry && source.gameObject == def.targetsAreGameObjects)
                    return source.chance;
        return def.bestChance;
    }

    bool CreatureTooStrong(Player const* bot, uint32 entry, WorldBrainConfig const& cfg)
    {
        CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry);
        return tmpl && tmpl->minlevel > bot->GetLevel() + cfg.maxLevelAbove;
    }

    struct EnderSpot
    {
        uint32 entry = 0;
        bool gameObject = false;
        SpawnPoint spot;
        float distance = 0.0f;
    };

    bool NearestEnder(Player* bot, BrainState& state, QuestKnowledge const& quest, EnderSpot& out)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 now = NowMs();
        bool found = false;

        auto consider = [&](uint32 entry, bool gameObject)
        {
            std::vector<SpawnPoint> const* spawns = gameObject ? QuestKB::GameObjectSpawns(entry) : QuestKB::CreatureSpawns(entry);
            if (!spawns)
                return;
            for (SpawnPoint const& sp : *spawns)
            {
                if (sp.mapId != bot->GetMapId() || !PhaseVisible(bot, sp.phaseMask))
                    continue;
                if (state.failures.Has(FailKind::Npc, sp.spawnId, now))
                    continue;
                float d = Dist(bot, sp.x, sp.y);
                if (d > cfg.maxObjectiveDistance * 1.5f)
                    continue;
                if (!found || d < out.distance)
                {
                    out = EnderSpot{ entry, gameObject, sp, d };
                    found = true;
                }
            }
        };

        for (uint32 entry : quest.enderCreatures)
            consider(entry, false);
        for (uint32 entry : quest.enderObjects)
            consider(entry, true);
        return found;
    }

    WorldTask NpcTask(WorldTaskType type, Player* bot, EnderSpot const& spot, uint32 questId)
    {
        WorldTask task;
        task.type = type;
        task.mapId = bot->GetMapId();
        task.x = spot.spot.x;
        task.y = spot.spot.y;
        task.z = spot.spot.z;
        task.areaRadius = 5.0f;
        task.npcEntry = spot.entry;
        task.npcSpawnId = spot.spot.spawnId;
        task.npcIsGameObject = spot.gameObject;
        task.quest.questId = questId;
        return task;
    }
}

namespace WorldPlanner
{
    AreaPick PickArea(Player* bot, BrainState& state, ObjectiveDef const& def, uint32 excludeAreaId)
    {
        AreaPick best;
        best.score = -1e9f;
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 now = NowMs();
        uint64 botRaw = bot->GetGUID().GetRawValue();
        uint32 mapId = bot->GetMapId();

        for (uint32 entry : def.targets)
        {
            if (!def.targetsAreGameObjects && CreatureTooStrong(bot, entry, cfg))
                continue;
            float chance = DropChanceFor(def, entry);

            for (uint32 areaId : ObjectiveAreas::ForEntry(def.targetsAreGameObjects, entry))
            {
                ObjectiveArea const* area = ObjectiveAreas::Get(areaId);
                if (!area || area->mapId != mapId || areaId == excludeAreaId || !PhaseVisible(bot, area->phaseMask))
                    continue;
                if (state.failures.Has(FailKind::Cluster, areaId, now))
                    continue;
                float d = Dist(bot, area->x, area->y);
                if (d > cfg.maxObjectiveDistance)
                    continue;

                ClusterScoreInput in;
                in.spawnCount = area->spawnCount;
                in.dropChance = chance;
                in.distance = d;
                in.occupancy = WorldReservations::Occupancy(ReservationKind::QuestCluster, areaId, bot->GetGUID());
                in.here = d <= area->radius + 15.0f;
                uint32 crowd = PopulationHeatmap::Around(mapId, area->cx, area->cy).Crowd();
                in.crowd = in.here && crowd ? crowd - 1 : crowd;
                in.jitter = WorldUtility::Jitter(botRaw, areaId);

                float score = WorldUtility::ScoreCluster(in, cfg.cluster);
                if (score > best.score)
                {
                    best.area = area;
                    best.score = score;
                    best.distance = d;
                    best.dropChance = chance;
                }
            }
        }

        if (!best.area)
            best.score = 0.0f;
        return best;
    }

    uint32 PickAreaTrigger(Player* bot, QuestKnowledge const& quest)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 best = 0;
        float bestDist = 0.0f;
        for (uint32 id : quest.areaTriggers)
        {
            AreaTrigger const* trigger = sObjectMgr->GetAreaTrigger(id);
            if (!trigger || trigger->map != bot->GetMapId())
                continue;
            float d = Dist(bot, trigger->x, trigger->y);
            if (d > cfg.maxObjectiveDistance)
                continue;
            if (!best || d < bestDist)
            {
                best = id;
                bestDist = d;
            }
        }
        return best;
    }

    bool ObjectiveReachable(Player* bot, BrainState& state, QuestKnowledge const& quest, ObjectiveDef const& def)
    {
        if (def.providedByQuest)
            return true;
        if (!def.supported)
            return false;
        if (def.type == ObjectiveType::Explore)
            return PickAreaTrigger(bot, quest) != 0;
        return PickArea(bot, state, def).area != nullptr;
    }

    bool EnderReachable(Player* bot, QuestKnowledge const& quest)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        auto reachable = [&](uint32 entry, bool gameObject)
        {
            std::vector<SpawnPoint> const* spawns = gameObject ? QuestKB::GameObjectSpawns(entry) : QuestKB::CreatureSpawns(entry);
            if (!spawns)
                return false;
            for (SpawnPoint const& sp : *spawns)
                if (sp.mapId == bot->GetMapId() && PhaseVisible(bot, sp.phaseMask) &&
                    Dist(bot, sp.x, sp.y) <= cfg.maxObjectiveDistance * 1.5f)
                    return true;
            return false;
        };

        for (uint32 entry : quest.enderCreatures)
            if (reachable(entry, false))
                return true;
        for (uint32 entry : quest.enderObjects)
            if (reachable(entry, true))
                return true;
        return false;
    }

    bool HasQuestWork(Player* bot, BrainState& state)
    {
        if (!QuestKB::IsReady())
            return false;
        uint32 now = NowMs();
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = bot->GetQuestSlotQuestId(slot);
            if (!questId || state.failures.Has(FailKind::Quest, questId, now))
                continue;
            QuestKnowledge const* quest = QuestKB::Get(questId);
            if (!quest)
                continue;

            QuestStatus status = bot->GetQuestStatus(questId);
            if (status == QUEST_STATUS_COMPLETE || (status == QUEST_STATUS_INCOMPLETE && bot->CanCompleteQuest(questId)))
            {
                if (EnderReachable(bot, *quest))
                    return true;
                continue;
            }
            if (status != QUEST_STATUS_INCOMPLETE || !quest->supported)
                continue;
            for (ObjectiveDef const& def : quest->objectives)
                if (!def.providedByQuest && !ObjectiveCommon::IsDone(bot, questId, def) && ObjectiveReachable(bot, state, *quest, def))
                    return true;
        }
        return false;
    }

    WorldTask ChooseTask(Player* bot, BrainState& state)
    {
        WorldTask none;
        if (!QuestKB::IsReady())
            return none;

        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 now = NowMs();
        uint64 botRaw = bot->GetGUID().GetRawValue();
        uint32 mapId = bot->GetMapId();

        std::vector<Candidate> objectives;
        std::vector<Candidate> others;

        // Turn-ins, grouped per ender spawn so one trip hands in everything that NPC takes.
        struct EnderGroup
        {
            EnderSpot spot;
            std::vector<uint32> quests;
            float xp = 0.0f;
        };
        std::vector<EnderGroup> enders;

        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = bot->GetQuestSlotQuestId(slot);
            if (!questId || state.failures.Has(FailKind::Quest, questId, now))
                continue;
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            QuestKnowledge const* info = QuestKB::Get(questId);
            if (!quest || !info)
                continue;

            QuestStatus status = bot->GetQuestStatus(questId);
            if (status == QUEST_STATUS_COMPLETE || (status == QUEST_STATUS_INCOMPLETE && bot->CanCompleteQuest(questId)))
            {
                EnderSpot spot;
                if (!NearestEnder(bot, state, *info, spot))
                    continue;
                auto group = std::find_if(enders.begin(), enders.end(), [&](EnderGroup const& g)
                    { return g.spot.entry == spot.entry && g.spot.spot.spawnId == spot.spot.spawnId; });
                if (group == enders.end())
                {
                    enders.push_back(EnderGroup{ spot, {}, 0.0f });
                    group = std::prev(enders.end());
                }
                group->quests.push_back(questId);
                group->xp += XpShare(bot, quest);
                continue;
            }
            if (status != QUEST_STATUS_INCOMPLETE)
                continue;

            for (uint8 index = 0; index < info->objectives.size(); ++index)
            {
                ObjectiveDef const& def = info->objectives[index];
                if (def.providedByQuest || ObjectiveCommon::IsDone(bot, questId, def))
                    continue;
                if (!def.supported && def.type != ObjectiveType::TalkTo)
                    continue;
                if (!ObjectiveHandlers::For(def))
                    continue;

                Candidate c;
                c.def = &def;
                WorldTask& task = c.task;
                task.type = WorldTaskType::QuestObjective;
                task.mapId = mapId;
                task.quest.questId = questId;
                task.quest.objectiveIndex = index;
                task.quest.objectiveType = def.type;
                task.quest.targetEntry = def.targetEntry;
                task.quest.requiredItemId = def.itemId;
                task.quest.requiredCount = def.requiredCount;
                task.quest.currentCount = ObjectiveCommon::CurrentCount(bot, questId, def);

                float distance = 0.0f;
                if (def.type == ObjectiveType::Explore)
                {
                    uint32 triggerId = PickAreaTrigger(bot, *info);
                    AreaTrigger const* trigger = triggerId ? sObjectMgr->GetAreaTrigger(triggerId) : nullptr;
                    if (!trigger)
                        continue;
                    task.quest.areaTriggerId = triggerId;
                    task.x = trigger->x;
                    task.y = trigger->y;
                    task.z = trigger->z;
                    task.areaRadius = std::max(5.0f, trigger->radius);
                    distance = Dist(bot, trigger->x, trigger->y);
                }
                else
                {
                    AreaPick pick = PickArea(bot, state, def);
                    if (!pick.area)
                        continue;
                    task.x = pick.area->x;
                    task.y = pick.area->y;
                    task.z = pick.area->z;
                    task.areaRadius = std::clamp(pick.area->radius, 15.0f, 100.0f);
                    task.quest.selectedClusterId = pick.area->id;
                    distance = pick.distance;
                }

                ObjectiveUtilityInput& in = c.input;
                in.remainingFraction = def.requiredCount
                    ? 1.0f - std::min(1.0f, float(task.quest.currentCount) / float(def.requiredCount)) : 1.0f;
                in.questXpShare = XpShare(bot, quest);
                in.chainQuest = IsChain(bot, quest);
                in.distance = distance;
                in.crowd = PopulationHeatmap::Around(mapId, task.x, task.y).Crowd();
                auto suspensions = state.questSuspensions.find(questId);
                in.failureStrikes = suspensions == state.questSuspensions.end() ? 0 : suspensions->second;
                in.dangerous = def.elite;
                in.questingTrait = float(state.persona.questing);
                in.jitter = WorldUtility::Jitter(botRaw, (uint64(questId) << 8) | index);
                objectives.push_back(std::move(c));
            }
        }

        for (Candidate& c : objectives)
        {
            c.utility = WorldUtility::ScoreObjective(c.input, cfg.utility);
            if (c.def->type == ObjectiveType::TalkTo)
                c.utility -= TALK_PENALTY;
            c.task.utility = c.utility;
            c.task.why = "objective";
        }

        for (EnderGroup const& group : enders)
        {
            Candidate c;
            c.task = NpcTask(WorldTaskType::QuestTurnIn, bot, group.spot, group.quests.front());
            c.utility = cfg.utility.turnInBase + cfg.utility.turnInBatch * float(group.quests.size() - 1)
                + cfg.utility.xpValue * std::min(1.0f, group.xp)
                - WorldUtility::TravelCost(group.spot.distance, cfg.utility.travelPer100Yards, cfg.utility.farTravelYards,
                    cfg.utility.farTravelMultiplier)
                + 8.0f * WorldUtility::Jitter(botRaw, group.spot.spot.spawnId);
            c.task.utility = c.utility;
            c.task.why = group.quests.size() > 1 ? "hand in several quests" : "hand in a quest";
            others.push_back(std::move(c));
        }

        // New work from quest givers nearby, when the log has room.
        if (QuestInteraction::ActiveQuestCount(bot) < cfg.maxActiveQuests)
        {
            std::vector<GiverSpot const*> spots;
            QuestKB::GiversNear(mapId, bot->GetPositionX(), bot->GetPositionY(), cfg.giverSearchRadius, spots);
            std::sort(spots.begin(), spots.end(), [bot](GiverSpot const* a, GiverSpot const* b)
                { return Dist(bot, a->x, a->y) < Dist(bot, b->x, b->y); });

            std::unordered_set<uint32> seenEntries;
            size_t evaluated = 0;
            for (GiverSpot const* spot : spots)
            {
                if (evaluated >= MAX_GIVERS_EVALUATED)
                    break;
                if (!PhaseVisible(bot, spot->phaseMask) || state.failures.Has(FailKind::Npc, spot->spawnId, now))
                    continue;
                if (!seenEntries.insert(spot->entry | (spot->gameObject ? 0x80000000u : 0u)).second)
                    continue;
                ++evaluated;

                uint32 acceptable = 0;
                bool chain = false;
                for (uint32 questId : spot->quests)
                {
                    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                    char const* reason = "";
                    if (!quest || !QuestInteraction::WouldAccept(bot, state, quest, reason))
                        continue;
                    ++acceptable;
                    chain = chain || IsChain(bot, quest);
                }
                if (!acceptable)
                    continue;

                EnderSpot where{ spot->entry, spot->gameObject, SpawnPoint{ spot->mapId, spot->x, spot->y, spot->z, spot->spawnId,
                    spot->phaseMask }, Dist(bot, spot->x, spot->y) };
                Candidate c;
                c.task = NpcTask(WorldTaskType::QuestAccept, bot, where, 0);
                c.utility = cfg.utility.acceptBase + cfg.utility.acceptPerQuest * float(std::min<uint32>(acceptable, 5))
                    + (chain ? cfg.utility.chainProgress : 0.0f)
                    + cfg.utility.personality * (float(state.persona.questing) - 50.0f)
                    - WorldUtility::TravelCost(where.distance, cfg.utility.travelPer100Yards, cfg.utility.farTravelYards,
                        cfg.utility.farTravelMultiplier)
                    + 8.0f * WorldUtility::Jitter(botRaw, spot->spawnId);
                c.task.utility = c.utility;
                c.task.why = chain ? "pick up the next quest of a chain" : "pick up new quests";
                others.push_back(std::move(c));
            }
        }

        std::vector<Candidate*> all;
        for (Candidate& c : objectives)
            all.push_back(&c);
        for (Candidate& c : others)
            all.push_back(&c);

        if (all.empty())
        {
            state.route.clear();
            return none;
        }

        Candidate* best = *std::max_element(all.begin(), all.end(),
            [](Candidate const* a, Candidate const* b) { return a->utility < b->utility; });
        state.route.clear();
        return best->task;
    }
}
