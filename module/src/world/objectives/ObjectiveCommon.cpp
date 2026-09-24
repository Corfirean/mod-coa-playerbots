#include "ObjectiveCommon.h"
#include "CellImpl.h"
#include "Creature.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "ObjectAccessor.h"
#include "ObjectiveAreas.h"
#include "Player.h"
#include "QuestDef.h"
#include "WorldExecutor.h"
#include "WorldPlanner.h"
#include "WorldReservations.h"
#include "WorldUtility.h"
#include <algorithm>
#include <cmath>
#include <list>

using namespace WorldBrainInternal;

namespace
{
    // Practice dummies pass every attackability check but can never die. Same test the grind
    // scan uses (see GrindHostileUnitCheck in BotAI.cpp for how it was arrived at).
    bool IsPracticeDummy(Creature const* creature)
    {
        std::string const& script = creature->GetScriptName();
        return script == "npc_training_dummy" || script.rfind("npc_coa_test_", 0) == 0;
    }

    class WantedCreatureCheck
    {
    public:
        WantedCreatureCheck(Player const* bot, std::unordered_set<uint32> const& wanted, float range)
            : _bot(bot), _wanted(wanted), _range(range) { }

        bool operator()(Creature* creature) const
        {
            return _wanted.count(creature->GetEntry()) && _bot->IsWithinDistInMap(creature, _range);
        }

    private:
        Player const* _bot;
        std::unordered_set<uint32> const& _wanted;
        float _range;
    };

    class WantedObjectCheck
    {
    public:
        WantedObjectCheck(Player const* bot, std::unordered_set<uint32> const& wanted, float range)
            : _bot(bot), _wanted(wanted), _range(range) { }

        bool operator()(GameObject* go) const
        {
            return _wanted.count(go->GetEntry()) && go->isSpawned() && _bot->IsWithinDistInMap(go, _range);
        }

    private:
        Player const* _bot;
        std::unordered_set<uint32> const& _wanted;
        float _range;
    };

    float Dist2d(Player const* bot, float x, float y)
    {
        return std::hypot(bot->GetPositionX() - x, bot->GetPositionY() - y);
    }

    // The bot still counts toward its area's occupancy while it works there; the claim would
    // otherwise lapse during a long stay and the area would look emptier than it is.
    void RefreshAreaClaim(ObjectiveContext const& ctx)
    {
        if (ctx.task.quest.selectedClusterId)
            WorldReservations::Join(ReservationKind::QuestCluster, ctx.task.quest.selectedClusterId, ctx.bot->GetGUID(),
                ctx.cfg.clusterJoinMs);
    }

    bool IsObjectiveKindFor(ObjectiveDef const& primary, ObjectiveDef const& other)
    {
        return ActionOf(primary.type) == ActionOf(other.type) && primary.targetsAreGameObjects == other.targetsAreGameObjects;
    }
}

namespace ObjectiveCommon
{
    uint32 CurrentCount(Player* bot, uint32 questId, ObjectiveDef const& def)
    {
        switch (def.type)
        {
            case ObjectiveType::KillCreature:
            case ObjectiveType::UseGameObject:
            case ObjectiveType::CastOnCreature:
            case ObjectiveType::CastOnGameObject:
            case ObjectiveType::TalkTo:
            {
                uint16 slot = bot->FindQuestSlot(questId);
                if (slot >= MAX_QUEST_LOG_SIZE)
                    return 0;
                return bot->GetQuestSlotCounter(slot, def.slot);
            }
            case ObjectiveType::CollectItem:
            case ObjectiveType::LootGameObject:
            case ObjectiveType::UseItemSource:
                return bot->GetItemCount(def.itemId, true);
            case ObjectiveType::Explore:
            case ObjectiveType::Escort:
            {
                auto& statuses = bot->getQuestStatusMap();
                auto itr = statuses.find(questId);
                return itr != statuses.end() && itr->second.Explored ? 1 : 0;
            }
            default:
                return 0;
        }
    }

    bool IsDone(Player* bot, uint32 questId, ObjectiveDef const& def)
    {
        QuestStatus status = bot->GetQuestStatus(questId);
        if (status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_REWARDED)
            return true;
        if (status != QUEST_STATUS_INCOMPLETE)
            return false;
        return CurrentCount(bot, questId, def) >= std::max<uint32>(1, def.requiredCount);
    }

    uint32 TaskProgress(Player* bot, WorldTask const& task)
    {
        uint32 total = 0;
        auto add = [&](uint32 questId, uint8 index)
        {
            QuestKnowledge const* quest = QuestKB::Get(questId);
            if (!quest || index >= quest->objectives.size())
                return;
            ObjectiveDef const& def = quest->objectives[index];
            if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
                total += def.requiredCount;
            else
                total += std::min(CurrentCount(bot, questId, def), def.requiredCount);
        };

        add(task.quest.questId, task.quest.objectiveIndex);
        for (ObjectiveRef const& ref : task.quest.bundle)
            add(ref.questId, ref.objectiveIndex);
        return total;
    }

    void WantedTargets(ObjectiveContext const& ctx, bool gameObjects, std::unordered_set<uint32>& wanted,
        std::unordered_map<uint32, uint32>& served)
    {
        auto add = [&](ObjectiveDef const& def, uint32 questId)
        {
            if (!ObjectiveHandlers::CanExecute(def) || def.targetsAreGameObjects != gameObjects || IsDone(ctx.bot, questId, def))
                return;
            for (uint32 target : def.targets)
            {
                wanted.insert(target);
                ++served[target];
            }
        };

        add(ctx.def, ctx.task.quest.questId);
        for (ObjectiveRef const& ref : ctx.task.quest.bundle)
        {
            QuestKnowledge const* quest = QuestKB::Get(ref.questId);
            if (!quest || ref.objectiveIndex >= quest->objectives.size())
                continue;
            ObjectiveDef const& other = quest->objectives[ref.objectiveIndex];
            if (IsObjectiveKindFor(ctx.def, other))
                add(other, ref.questId);
        }
    }

    bool InArea(ObjectiveContext const& ctx, float slack)
    {
        return ctx.bot->GetMapId() == ctx.task.mapId && Dist2d(ctx.bot, ctx.task.x, ctx.task.y) <= ctx.task.areaRadius + slack;
    }

    ObjectiveResult TravelToArea(ObjectiveContext& ctx)
    {
        if (InArea(ctx))
        {
            WorldExecutor::Dismount(ctx.bot, ctx.state);
            BeginSearch(ctx, "arrived in area");
            return ObjectiveResult::Running;
        }

        if (PhaseElapsed(ctx.state) > ctx.cfg.travelTimeoutMs)
            return FailArea(ctx, FailureReason::Timeout);

        NavStatus status = WorldExecutor::TravelTo(ctx.bot, ctx.state, WorldGoalSub::Area, ctx.task.x, ctx.task.y, ctx.task.z,
            std::max(8.0f, ctx.task.areaRadius * 0.5f));
        if (status == NavStatus::Arrived)
        {
            WorldExecutor::Dismount(ctx.bot, ctx.state);
            BeginSearch(ctx, "arrived in area");
        }
        else if (status == NavStatus::Stuck)
        {
            Count(ctx.state.metrics, &WorldMetrics::movementStuck);
            return FailArea(ctx, FailureReason::Unreachable);
        }
        return ObjectiveResult::Running;
    }

    ObjectiveResult FailArea(ObjectiveContext& ctx, FailureReason reason)
    {
        uint32 oldArea = ctx.task.quest.selectedClusterId;
        if (oldArea)
        {
            ctx.state.failures.Remember(FailKind::Cluster, oldArea, ctx.now, ctx.cfg.clusterFailMs, uint8(reason));
            WorldReservations::Leave(ReservationKind::QuestCluster, oldArea, ctx.bot->GetGUID());
        }
        ReleaseTarget(ctx);
        ctx.task.quest.lastFailure = reason;
        ++ctx.task.quest.badClusters;

        LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' gives up on area {} for quest {} objective {} ({}), {} bad areas so far.",
            ctx.bot->GetName(), oldArea, ctx.task.quest.questId, uint32(ctx.task.quest.objectiveIndex),
            FailureReasonName(reason), ctx.task.quest.badClusters);

        if (ctx.task.quest.badClusters >= ctx.cfg.maxBadClusters || ctx.def.type == ObjectiveType::Explore)
            return ObjectiveResult::Failed;

        AreaPick pick = WorldPlanner::PickArea(ctx.bot, ctx.state, ctx.def, oldArea);
        if (!pick.area)
        {
            ctx.task.quest.lastFailure = FailureReason::NoTargets;
            return ObjectiveResult::Failed;
        }

        ctx.task.x = pick.area->x;
        ctx.task.y = pick.area->y;
        ctx.task.z = pick.area->z;
        ctx.task.areaRadius = std::clamp(pick.area->radius, 15.0f, 100.0f);
        ctx.task.quest.selectedClusterId = pick.area->id;
        ctx.task.wandering = false;
        WorldReservations::Join(ReservationKind::QuestCluster, pick.area->id, ctx.bot->GetGUID(), ctx.cfg.clusterJoinMs);
        BotMovement::ResetRequest(ctx.bot->GetGUID());
        Count(ctx.state.metrics, &WorldMetrics::areaSwitches);
        NoteEvent(ctx.state, Acore::StringFormat("switched to area {} ({:.0f} yd)", pick.area->id, pick.distance));
        SetPhase(ctx.bot, ctx.state, TaskPhase::TravelToArea, "switching area");
        return ObjectiveResult::Running;
    }

    Creature* FindCreature(ObjectiveContext& ctx, std::unordered_set<uint32> const& wanted,
        std::unordered_map<uint32, uint32> const& served, bool acceptHostileOnly, bool wantDead, uint32& corpses)
    {
        corpses = 0;
        RefreshAreaClaim(ctx);
        if (wanted.empty())
            return nullptr;

        Player* bot = ctx.bot;
        float radius = ctx.cfg.searchRadius;
        std::list<Creature*> found;
        WantedCreatureCheck check(bot, wanted, radius);
        Acore::CreatureListSearcher<WantedCreatureCheck> searcher(bot, found, check);
        Cell::VisitObjects(bot, searcher, radius);

        struct Candidate
        {
            Creature* creature;
            float score;
        };
        std::vector<Candidate> candidates;
        ObjectGuid botGuid = bot->GetGUID();

        for (Creature* creature : found)
        {
            bool alive = creature->IsAlive();
            if (!alive)
                ++corpses;
            if (alive == wantDead)
                continue;

            uint64 raw = creature->GetGUID().GetRawValue();
            if (ctx.state.failures.Has(FailKind::Target, raw, ctx.now))
                continue;
            if (WorldReservations::IsHeldByOther(ReservationKind::Creature, raw, botGuid))
                continue;

            if (acceptHostileOnly)
            {
                if (!bot->IsValidAttackTarget(creature) || creature->IsInEvadeMode() || IsPracticeDummy(creature))
                    continue;
                if (creature->GetLevel() > bot->GetLevel() + ctx.cfg.maxLevelAbove)
                    continue;
                // Someone else's mob: no credit, no loot, and stealing it is exactly what a real
                // player would resent.
                if (creature->hasLootRecipient() && !creature->isTappedBy(bot))
                    continue;
                if (creature->IsInCombat() && creature->GetVictim() && creature->GetVictim() != bot)
                    continue;
            }

            TargetScoreInput in;
            in.distance = bot->GetDistance(creature);
            in.searchRadius = radius;
            auto itr = served.find(creature->GetEntry());
            in.objectivesServed = itr == served.end() ? 1 : itr->second;
            in.verticalGap = std::fabs(creature->GetPositionZ() - bot->GetPositionZ());
            in.levelAbove = int32(creature->GetLevel()) - int32(bot->GetLevel());
            in.jitter = WorldUtility::Jitter(botGuid.GetRawValue(), raw);
            candidates.push_back(Candidate{ creature, WorldUtility::ScoreTarget(in, ctx.cfg.target) });
        }

        std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b) { return a.score > b.score; });
        for (Candidate const& candidate : candidates)
        {
            if (WorldReservations::TryReserve(ReservationKind::Creature, candidate.creature->GetGUID().GetRawValue(), botGuid,
                    ctx.cfg.targetReserveMs))
                return candidate.creature;
            Count(ctx.state.metrics, &WorldMetrics::reservationConflicts);
        }
        return nullptr;
    }

    GameObject* FindObject(ObjectiveContext& ctx, std::unordered_set<uint32> const& wanted,
        std::function<bool(GameObject*)> const& usable)
    {
        RefreshAreaClaim(ctx);
        if (wanted.empty())
            return nullptr;

        Player* bot = ctx.bot;
        float radius = ctx.cfg.searchRadius;
        std::list<GameObject*> found;
        WantedObjectCheck check(bot, wanted, radius);
        Acore::GameObjectListSearcher<WantedObjectCheck> searcher(bot, found, check);
        Cell::VisitObjects(bot, searcher, radius);

        ObjectGuid botGuid = bot->GetGUID();
        std::vector<std::pair<GameObject*, float>> candidates;
        for (GameObject* go : found)
        {
            uint64 raw = go->GetGUID().GetRawValue();
            if (ctx.state.failures.Has(FailKind::GameObject, raw, ctx.now))
                continue;
            if (WorldReservations::IsHeldByOther(ReservationKind::GameObject, raw, botGuid))
                continue;
            if (usable && !usable(go))
                continue;

            float d = bot->GetDistance(go);
            float score = 100.0f - d - std::fabs(go->GetPositionZ() - bot->GetPositionZ()) * 1.5f
                + 8.0f * WorldUtility::Jitter(botGuid.GetRawValue(), raw);
            candidates.emplace_back(go, score);
        }

        std::sort(candidates.begin(), candidates.end(), [](auto const& a, auto const& b) { return a.second > b.second; });
        for (auto const& [go, score] : candidates)
        {
            if (WorldReservations::TryReserve(ReservationKind::GameObject, go->GetGUID().GetRawValue(), botGuid, ctx.cfg.objectReserveMs))
                return go;
            Count(ctx.state.metrics, &WorldMetrics::reservationConflicts);
        }
        return nullptr;
    }

    NavStatus Approach(ObjectiveContext& ctx, WorldObject* target, float range)
    {
        Player* bot = ctx.bot;
        float dist = bot->GetDistance(target);
        float dz = std::fabs(target->GetPositionZ() - bot->GetPositionZ());
        bool inRange = dist <= range && dz < 12.0f;
        bool los = inRange && bot->IsWithinLOSInMap(target);
        if (inRange && los)
        {
            BotMovement::Release(bot, MoveOwner::Quest);
            return NavStatus::Arrived;
        }

        // In range but something in the way: walk right up to it instead of stopping at range.
        float accept = inRange ? 3.0f : std::max(2.0f, range * 0.7f);
        NavStatus status = WorldExecutor::TravelTo(bot, ctx.state, WorldGoalSub::Target, target->GetPositionX(),
            target->GetPositionY(), target->GetPositionZ(), accept, target->GetGUID().GetCounter(), dist > 60.0f);
        // Navigate judges arrival by distance only; line of sight is re-checked next tick.
        return status == NavStatus::Arrived ? NavStatus::Moving : status;
    }

    void Wander(ObjectiveContext& ctx)
    {
        ObjectiveArea const* area = ObjectiveAreas::Get(ctx.task.quest.selectedClusterId);
        if (!area || area->points.empty())
            return;

        uint32 count = uint32(area->points.size());
        if (!ctx.task.wandering)
        {
            // Next point: a small random step through the list, skipping points right next to the
            // bot, so the walk covers the camp instead of pacing between two spawns.
            for (uint32 tries = 0; tries < count; ++tries)
            {
                ctx.task.wanderIndex = (ctx.task.wanderIndex + 1 + RollRange(ctx.state, 0x3a7d, 0, 2)) % count;
                SpawnPoint const& candidate = area->points[ctx.task.wanderIndex];
                if (Dist2d(ctx.bot, candidate.x, candidate.y) > 12.0f &&
                    !ctx.state.failures.Has(FailKind::Spawn, candidate.spawnId, ctx.now))
                    break;
            }
            ctx.task.wandering = true;
        }

        SpawnPoint const& point = area->points[ctx.task.wanderIndex % count];
        NavStatus status = WorldExecutor::TravelTo(ctx.bot, ctx.state, WorldGoalSub::Wander, point.x, point.y, point.z, 5.0f,
            ctx.task.wanderIndex + 1, false);
        if (status == NavStatus::Stuck)
        {
            ctx.state.failures.Remember(FailKind::Spawn, point.spawnId, ctx.now, ctx.cfg.clusterFailMs, uint8(FailureReason::Unreachable));
            Count(ctx.state.metrics, &WorldMetrics::movementStuck);
        }
        if (status == NavStatus::Arrived || status == NavStatus::Stuck)
            ctx.task.wandering = false;
    }

    void ReleaseTarget(ObjectiveContext& ctx)
    {
        ObjectGuid target = ctx.task.quest.targetGuid;
        if (target.IsEmpty())
            return;
        ReservationKind kind = target.IsGameObject() ? ReservationKind::GameObject : ReservationKind::Creature;
        WorldReservations::Release(kind, target.GetRawValue(), ctx.bot->GetGUID());
        ctx.task.quest.targetGuid = ObjectGuid::Empty;
    }

    void ForgetTarget(ObjectiveContext& ctx, FailureReason reason)
    {
        ObjectGuid target = ctx.task.quest.targetGuid;
        if (!target.IsEmpty())
        {
            bool object = target.IsGameObject();
            ctx.state.failures.Remember(object ? FailKind::GameObject : FailKind::Target, target.GetRawValue(), ctx.now,
                object ? ctx.cfg.objectFailMs : ctx.cfg.targetFailMs, uint8(reason));
            LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' lost target {} ({}).", ctx.bot->GetName(), target.ToString(),
                FailureReasonName(reason));
        }
        ReleaseTarget(ctx);
        ++ctx.task.quest.retryCount;
        ctx.task.quest.lastFailure = reason;
    }

    bool Settle(Player* bot, BrainState& state)
    {
        BotMovement::Release(bot, MoveOwner::Quest);
        if (bot->isMoving())
        {
            bot->StopMoving();
            return false;
        }
        WorldExecutor::Dismount(bot, state);
        return true;
    }

    void BeginSearch(ObjectiveContext& ctx, char const* why)
    {
        ReleaseTarget(ctx);
        ctx.task.wandering = false;
        ctx.task.nextScanMs = ctx.now + RollRange(ctx.state, 0x5ea7, ctx.cfg.reactionMinMs, ctx.cfg.reactionMaxMs);
        SetPhase(ctx.bot, ctx.state, TaskPhase::Search, why);
    }

    ObjectiveResult VerifyAttempt(ObjectiveContext& ctx, uint32 dryLimit)
    {
        uint32 progress = TaskProgress(ctx.bot, ctx.task);
        if (progress > ctx.task.quest.progressMark)
        {
            Count(ctx.state.metrics, &WorldMetrics::objectiveProgress, progress - ctx.task.quest.progressMark);
            ctx.task.quest.dryAttempts = 0;
            ctx.task.quest.retryCount = 0;
            LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' ObjectiveProgress quest {} objective {}: {}/{}.",
                ctx.bot->GetName(), ctx.task.quest.questId, uint32(ctx.task.quest.objectiveIndex),
                CurrentCount(ctx.bot, ctx.task.quest.questId, ctx.def), ctx.def.requiredCount);
        }
        else
            ++ctx.task.quest.dryAttempts;

        ctx.task.quest.progressMark = progress;
        ctx.task.quest.currentCount = CurrentCount(ctx.bot, ctx.task.quest.questId, ctx.def);

        if (IsDone(ctx.bot, ctx.task.quest.questId, ctx.def))
            return ObjectiveResult::Completed;

        if (ctx.task.quest.dryAttempts >= dryLimit)
        {
            ctx.task.quest.lastFailure = FailureReason::NoProgress;
            return ObjectiveResult::Failed;
        }
        return ObjectiveResult::Running;
    }

    uint32 SearchBudgetMs(BrainState const& state, WorldBrainConfig const& cfg)
    {
        uint32 budget = cfg.searchTimeoutMs * (50 + state.persona.patience) / 100;
        if (state.task.corpsesSeen > 0)
            budget = budget * 3 / 2;
        return budget;
    }

    uint32 PhaseBudgetMs(BrainState const& state, WorldBrainConfig const& cfg)
    {
        switch (state.task.phase)
        {
            case TaskPhase::TravelToArea: return cfg.travelTimeoutMs;
            case TaskPhase::Search:       return SearchBudgetMs(state, cfg);
            case TaskPhase::Approach:     return cfg.approachTimeoutMs;
            case TaskPhase::Loot:         return cfg.lootTimeoutMs;
            default:                      return 0;
        }
    }

    uint32 DryAttemptLimit(ObjectiveDef const& def)
    {
        switch (def.type)
        {
            case ObjectiveType::CollectItem:
            case ObjectiveType::LootGameObject:
                return std::clamp<uint32>(uint32(std::ceil(3.0f / std::max(0.05f, def.bestChance))), 4, 30);
            case ObjectiveType::KillCreature:
                return 4;
            default:
                return 3;
        }
    }
}
