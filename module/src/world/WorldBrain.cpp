#include "WorldBrain.h"
#include "BotMovement.h"
#include "Chat.h"
#include "GameTime.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "ObjectiveAreas.h"
#include "ObjectiveCommon.h"
#include "Player.h"
#include "PopulationHeatmap.h"
#include "QuestDef.h"
#include "QuestExecutor.h"
#include "QuestInteraction.h"
#include "QuestKnowledgeBase.h"
#include "WorldBrainState.h"
#include "WorldExecutor.h"
#include "WorldPlanner.h"
#include "WorldReservations.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

using namespace WorldBrainInternal;

namespace
{
    std::unordered_map<ObjectGuid, BrainState> _states;

    // A task paused (grouped, commanded) for longer than this is dropped and re-planned on return
    // rather than resumed: the world has moved on.
    constexpr uint32 SUSPEND_DROP_MS = 5 * MINUTE * IN_MILLISECONDS;

    // A fallback activity that keeps finding nothing hands over to ambient life after this.
    constexpr uint32 ACTIVITY_IDLE_SWITCH_MS = 15000;

    // Quests abandoned for good are not taken again for this long.
    constexpr uint32 ABANDONED_QUEST_MEMORY_MS = 2 * HOUR * IN_MILLISECONDS;

    // Planner backoff ceiling when it keeps finding nothing (the bot grinds/gathers meanwhile).
    constexpr uint32 MAX_EMPTY_PLAN_BACKOFF_MS = 3 * MINUTE * IN_MILLISECONDS;

    // How often the quest log is checked for quests nothing can ever finish.
    constexpr uint32 LOG_CLEANUP_MS = 2 * MINUTE * IN_MILLISECONDS;

    // Mirror of BotAI's SoloIntent numbering (see WorldPersona::lean).
    constexpr uint8 LEAN_QUEST = 1;
    constexpr uint8 LEAN_GATHER = 2;
    constexpr uint8 LEAN_FISH = 3;
    constexpr uint8 LEAN_GRIND = 4;
    constexpr uint8 LEAN_EXPLORE = 5;

    struct BrainExtra
    {
        uint32 suspendedAtMs = 0;
        bool deathHandled = false;
        bool pausedByAmbient = false;
    };
    std::unordered_map<ObjectGuid, BrainExtra> _extra;

    uint32 Mix(uint32 x)
    {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    BrainState& StateFor(Player* bot, WorldPersona const& persona)
    {
        auto [itr, fresh] = _states.try_emplace(bot->GetGUID());
        BrainState& state = itr->second;
        state.persona = persona;
        if (fresh)
        {
            WorldBrainConfig const& cfg = WorldBrainSettings::Get();
            state.seed = persona.seed ^ Mix(bot->GetGUID().GetCounter() * 2654435761U);
            state.mountThreshold = cfg.mountDistanceMin + RollFloat(state, 0x40a7) * (cfg.mountDistanceMax - cfg.mountDistanceMin);
            // First decision staggered across the whole planner window: a server start or a big
            // spawn batch must not have every bot plan on the same tick.
            state.nextPlanMs = NowMs() + RollRange(state, 0x91a0, 500, cfg.plannerMaxMs * 2);
            state.lastMapId = bot->GetMapId();
        }
        return state;
    }

    PopulationActivity ActivityOf(BrainState const& state)
    {
        switch (state.goal)
        {
            case WorldGoal::Questing:
            case WorldGoal::Traveling: return PopulationActivity::Questing;
            case WorldGoal::Gathering:
            case WorldGoal::Fishing:   return PopulationActivity::Gathering;
            case WorldGoal::None:      return PopulationActivity::Idle;
            default:                   return PopulationActivity::Other;
        }
    }

    void SuspendQuest(Player* bot, BrainState& state, uint32 questId, FailureReason reason)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 now = NowMs();
        state.failures.Remember(FailKind::Quest, questId, now, cfg.questSuspendMs, uint8(reason));
        uint32 suspensions = ++state.questSuspensions[questId];
        Count(state.metrics, &WorldMetrics::questsSuspended);

        QuestKnowledge const* info = QuestKB::Get(questId);
        bool hopeless = reason == FailureReason::Unsupported || suspensions >= cfg.abandonAfterSuspensions ||
            (info && !info->supported);
        if (hopeless)
        {
            QuestInteraction::Abandon(bot, questId);
            state.failures.Remember(FailKind::Quest, questId, now, ABANDONED_QUEST_MEMORY_MS, uint8(reason));
            state.questSuspensions.erase(questId);
            Count(state.metrics, &WorldMetrics::questsAbandoned);
            NoteEvent(state, Acore::StringFormat("abandoned quest {} ({})", questId, FailureReasonName(reason)));
        }
        else
            NoteEvent(state, Acore::StringFormat("set quest {} aside for a while ({})", questId, FailureReasonName(reason)));
    }

    // Quests in the log that no bot can ever finish: unsupported ones (typically taken before this
    // layer existed) and ones whose quest-provided item is gone. A player abandons those; leaving
    // them in the log blocks it from ever taking new work.
    void CleanupQuestLog(Player* bot, BrainState& state)
    {
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = bot->GetQuestSlotQuestId(slot);
            if (!questId || bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE || bot->CanCompleteQuest(questId))
                continue;
            QuestKnowledge const* info = QuestKB::Get(questId);
            if (!info)
                continue;

            bool doable = false;
            bool anyOpen = false;
            for (ObjectiveDef const& def : info->objectives)
            {
                if (ObjectiveCommon::IsDone(bot, questId, def))
                    continue;
                anyOpen = true;
                // A talk-to objective gets one honest try (TalkObjectiveHandler) before anything
                // is abandoned; a lost quest-provided item cannot come back.
                if ((def.supported && !def.providedByQuest) || def.type == ObjectiveType::TalkTo)
                    doable = true;
            }
            if (anyOpen && !doable)
                SuspendQuest(bot, state, questId, FailureReason::Unsupported);
        }
    }

    void EndTask(Player* bot, BrainState& state, ExecResult result)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 now = NowMs();
        WorldTask& task = state.task;

        WorldExecutor::ReleaseTask(bot, state);
        Count(state.metrics, &WorldMetrics::replans);

        if (result == ExecResult::Completed)
        {
            Count(state.metrics, &WorldMetrics::tasksCompleted);
            LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' TaskCompleted {} #{} (quest {}) after {}s.", bot->GetName(),
                WorldTaskTypeName(task.type), task.id, task.quest.questId, (now - task.startedMs) / IN_MILLISECONDS);
            NoteEvent(state, Acore::StringFormat("completed {} (quest {})", WorldTaskTypeName(task.type), task.quest.questId));
            // A beat before the next decision -- nobody turns on their heel the instant a mob dies.
            state.nextPlanMs = now + RollRange(state, 0x2e0d, cfg.reactionMinMs, cfg.reactionMaxMs * 2);
            state.emptyPlans = 0;
        }
        else
        {
            Count(state.metrics, &WorldMetrics::tasksFailed);
            FailureReason reason = task.quest.lastFailure;
            LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' TaskFailed {} #{} (quest {}, phase {}): {}.", bot->GetName(),
                WorldTaskTypeName(task.type), task.id, task.quest.questId, TaskPhaseName(task.phase), FailureReasonName(reason));
            NoteEvent(state, Acore::StringFormat("gave up on {} (quest {}): {}", WorldTaskTypeName(task.type), task.quest.questId,
                FailureReasonName(reason)));

            switch (task.type)
            {
                case WorldTaskType::QuestObjective:
                    switch (reason)
                    {
                        case FailureReason::NoProgress:
                        case FailureReason::NoTargets:
                        case FailureReason::Unsupported:
                        case FailureReason::Unreachable:
                        case FailureReason::Timeout:
                        case FailureReason::CastFailed:
                        case FailureReason::Died:
                            SuspendQuest(bot, state, task.quest.questId, reason);
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
            state.nextPlanMs = now + RollRange(state, 0x2e0e, 1000, 3000);
        }

        state.task = WorldTask();
    }

    void DropTask(Player* bot, BrainState& state, FailureReason reason)
    {
        if (!state.task.IsValid())
            return;
        state.task.quest.lastFailure = reason;
        LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' Replan: dropping {} #{} ({}).", bot->GetName(),
            WorldTaskTypeName(state.task.type), state.task.id, FailureReasonName(reason));
        WorldExecutor::ReleaseTask(bot, state);
        Count(state.metrics, &WorldMetrics::replans);
        state.task = WorldTask();
        state.nextPlanMs = NowMs() + RollRange(state, 0x2e0f, 500, 2500);
    }

    void StartTask(Player* bot, BrainState& state, WorldTask task)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 now = NowMs();

        task.id = state.nextTaskId++;
        task.phase = TaskPhase::Planning;
        task.startedMs = now;
        task.phaseStartedMs = now;
        task.deadlineMs = now + cfg.taskTimeoutMs;
        state.task = std::move(task);
        WorldTask& t = state.task;
        t.quest.progressMark = t.type == WorldTaskType::QuestObjective ? ObjectiveCommon::TaskProgress(bot, t) : 0;

        state.goal = WorldGoal::Questing;
        state.activity = WorldDirective::Idle;

        if (t.quest.selectedClusterId)
            WorldReservations::Join(ReservationKind::QuestCluster, t.quest.selectedClusterId, bot->GetGUID(), cfg.clusterJoinMs);
        PopulationHeatmap::SetIncoming(bot->GetGUID(), t.mapId, t.x, t.y);
        Count(state.metrics, &WorldMetrics::tasksStarted);

        float dist = std::hypot(bot->GetPositionX() - t.x, bot->GetPositionY() - t.y);
        LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' TaskSelected {} #{} quest {} objective {} utility {:.0f} ({}), "
            "area {} at {:.0f} yd, {} bundled.", bot->GetName(), WorldTaskTypeName(t.type), t.id, t.quest.questId,
            uint32(t.quest.objectiveIndex), t.utility, t.why, t.quest.selectedClusterId, dist, t.quest.bundle.size());
        NoteEvent(state, Acore::StringFormat("started {} (quest {}): {}", WorldTaskTypeName(t.type), t.quest.questId, t.why));
    }

    bool HasGatherSkill(Player* bot)
    {
        return bot->GetSkillValue(SKILL_HERBALISM) || bot->GetSkillValue(SKILL_MINING);
    }

    WorldDirective ChooseActivity(Player* bot, BrainState& state)
    {
        uint32 now = NowMs();
        if (state.activity != WorldDirective::Idle && now < state.activityUntilMs)
            return state.activity;

        WorldPersona const& p = state.persona;
        struct Option
        {
            WorldDirective directive;
            uint32 weight;
        };
        Option options[] =
        {
            { WorldDirective::Gather, HasGatherSkill(bot) ? uint32(p.gathering) + (p.lean == LEAN_GATHER ? 80u : 0u) : 0u },
            { WorldDirective::Fish, p.lean == LEAN_FISH ? uint32(p.fishing) + 80u : uint32(p.fishing) / 3 },
            { WorldDirective::Grind, uint32(p.grinding) + 20u + (p.lean == LEAN_GRIND ? 80u : 0u) + (p.lean == LEAN_QUEST ? 20u : 0u) },
            { WorldDirective::Ambient, 30u + uint32(p.sociability) / 4 + (p.lean == LEAN_EXPLORE ? 80u : 0u) },
        };

        uint32 total = 0;
        for (Option const& option : options)
            total += option.weight;
        uint32 roll = Roll(state, 0xac71) % std::max<uint32>(1, total);
        WorldDirective chosen = WorldDirective::Ambient;
        for (Option const& option : options)
        {
            if (roll < option.weight)
            {
                chosen = option.directive;
                break;
            }
            roll -= option.weight;
        }

        state.activity = chosen;
        state.activityUntilMs = now + RollRange(state, 0xac72, 60000, 180000) / 100 * (50 + p.patience);
        state.activityIdleSinceMs = 0;
        switch (chosen)
        {
            case WorldDirective::Gather: state.goal = WorldGoal::Gathering; break;
            case WorldDirective::Fish:   state.goal = WorldGoal::Fishing; break;
            case WorldDirective::Grind:  state.goal = WorldGoal::Grinding; break;
            default:                     state.goal = WorldGoal::Exploring; break;
        }
        LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' has no quest work here; activity {} for {}s.", bot->GetName(),
            WorldDirectiveName(chosen), (state.activityUntilMs - now) / IN_MILLISECONDS);
        return chosen;
    }

    std::string SecondsText(uint32 ms)
    {
        return Acore::StringFormat("{:.1f}s", float(ms) / 1000.0f);
    }
}

namespace WorldBrainInternal
{
    uint32 NowMs()
    {
        return uint32(GameTime::GetGameTimeMS().count());
    }

    uint32 Roll(BrainState& state, uint32 salt)
    {
        return Mix(state.seed ^ Mix(++state.generation * 2654435761U) ^ salt);
    }

    uint32 RollRange(BrainState& state, uint32 salt, uint32 lo, uint32 hi)
    {
        if (hi <= lo)
            return lo;
        return lo + Roll(state, salt) % (hi - lo + 1);
    }

    float RollFloat(BrainState& state, uint32 salt)
    {
        return float(Roll(state, salt) % 100000) / 100000.0f;
    }

    void SetPhase(Player* bot, BrainState& state, TaskPhase phase, char const* why)
    {
        if (state.task.phase == phase)
            return;
        LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' PhaseChanged {} -> {} ({}) task #{} quest {}.", bot->GetName(),
            TaskPhaseName(state.task.phase), TaskPhaseName(phase), why, state.task.id, state.task.quest.questId);
        state.task.phase = phase;
        state.task.phaseStartedMs = NowMs();
    }

    void NoteEvent(BrainState& state, std::string text)
    {
        state.lastEvent = std::move(text);
        state.lastEventMs = NowMs();
    }

    uint32 PhaseElapsed(BrainState const& state)
    {
        return NowMs() - state.task.phaseStartedMs;
    }
}

namespace WorldBrain
{
    void Initialize()
    {
        WorldBrainSettings::Load();
        QuestKB::Initialize();
    }

    void GlobalUpdate(uint32 diff)
    {
        WorldReservations::Update(diff);
    }

    WorldDirective Update(Player* bot, uint32 diff, WorldPersona const& persona)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        BrainState& state = StateFor(bot, persona);
        BrainExtra& extra = _extra[bot->GetGUID()];
        uint32 now = NowMs();
        extra.deathHandled = false;

        if (!cfg.enabled || !QuestKB::IsReady())
            return ChooseActivity(bot, state);

        Map* map = bot->GetMap();
        if (map && map->Instanceable())
        {
            Suspend(bot, map->IsBattlegroundOrArena() ? SuspendReason::Battleground : SuspendReason::Instance);
            return WorldDirective::Idle;
        }

        if (state.suspended)
        {
            state.suspended = false;
            if (state.task.IsValid())
            {
                if (now - extra.suspendedAtMs > SUSPEND_DROP_MS)
                    DropTask(bot, state, FailureReason::Suspended);
                else
                    SetPhase(bot, state, TaskPhase::Recover, "resumed");
            }
            NoteEvent(state, "resumed after a suspension");
            state.nextPlanMs = std::min(state.nextPlanMs, now + RollRange(state, 0x7e5a, 500, 2500));
        }

        if (bot->GetMapId() != state.lastMapId)
        {
            DropTask(bot, state, FailureReason::MapChanged);
            state.lastMapId = bot->GetMapId();
        }

        if (extra.pausedByAmbient)
        {
            extra.pausedByAmbient = false;
            state.task.paused = false;
        }
        state.lastUpdateMs = now;

        if (now >= state.nextPresenceMs)
        {
            PopulationHeatmap::UpdatePresence(bot->GetGUID(), bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(),
                ActivityOf(state));
            state.nextPresenceMs = now + RollRange(state, 0x9e5e, cfg.presenceMinMs, cfg.presenceMaxMs);
        }

        if (state.task.IsValid())
        {
            if (now >= state.task.deadlineMs)
            {
                state.task.quest.lastFailure = FailureReason::Timeout;
                EndTask(bot, state, ExecResult::Failed);
                return WorldDirective::Busy;
            }
            ExecResult result = WorldExecutor::Update(bot, state, diff);
            if (result != ExecResult::Running)
                EndTask(bot, state, result);
            return WorldDirective::Busy;
        }

        if (now >= state.nextPlanMs)
        {
            if (now >= state.nextLogCleanupMs)
            {
                state.nextLogCleanupMs = now + LOG_CLEANUP_MS;
                CleanupQuestLog(bot, state);
            }

            WorldTask task = WorldPlanner::ChooseTask(bot, state);

            // Nothing found: back off exponentially while the bot does something else meanwhile.
            uint32 backoff = std::min<uint32>(MAX_EMPTY_PLAN_BACKOFF_MS,
                cfg.plannerMaxMs << std::min<uint32>(state.emptyPlans, 5));
            state.nextPlanMs = now + (task.IsValid() ? RollRange(state, 0x91a0, cfg.plannerMinMs, cfg.plannerMaxMs)
                                                     : RollRange(state, 0x91a1, backoff / 2, backoff));
            if (!task.IsValid())
                ++state.emptyPlans;

            if (task.IsValid())
            {
                state.emptyPlans = 0;
                StartTask(bot, state, std::move(task));
                ExecResult result = WorldExecutor::Update(bot, state, diff);
                if (result != ExecResult::Running)
                    EndTask(bot, state, result);
                return WorldDirective::Busy;
            }
        }

        return ChooseActivity(bot, state);
    }

    void ReportActivity(Player* bot, WorldDirective directive, bool started)
    {
        auto itr = _states.find(bot->GetGUID());
        if (itr == _states.end())
            return;
        BrainState& state = itr->second;
        uint32 now = NowMs();

        if (directive != state.activity)
            return;

        if (started)
        {
            state.activityIdleSinceMs = 0;
            return;
        }
        if (!state.activityIdleSinceMs)
        {
            state.activityIdleSinceMs = now;
            return;
        }
        if (now - state.activityIdleSinceMs > ACTIVITY_IDLE_SWITCH_MS && state.activity != WorldDirective::Ambient)
        {
            state.activity = WorldDirective::Ambient;
            state.activityUntilMs = now + 60000;
            state.activityIdleSinceMs = 0;
            state.goal = WorldGoal::Exploring;
            NoteEvent(state, "nothing to do for that activity here, wandering instead");
        }
    }

    void NotifyAmbientBusy(Player* bot)
    {
        auto itr = _states.find(bot->GetGUID());
        if (itr == _states.end() || !itr->second.task.IsValid())
            return;
        BrainExtra& extra = _extra[bot->GetGUID()];
        if (extra.pausedByAmbient)
            return;
        extra.pausedByAmbient = true;
        itr->second.task.paused = true;
        // Let the errand (a repair, a vendor) have the legs; the task walks on when it's done.
        BotMovement::Release(bot, MoveOwner::Quest);
    }

    void Suspend(Player* bot, SuspendReason reason)
    {
        auto itr = _states.find(bot->GetGUID());
        if (itr == _states.end() || itr->second.suspended)
            return;
        BrainState& state = itr->second;
        BrainExtra& extra = _extra[bot->GetGUID()];

        WorldExecutor::ReleaseTask(bot, state);
        PopulationHeatmap::Remove(bot->GetGUID());
        state.nextPresenceMs = 0;
        state.suspended = true;
        state.suspendReason = reason;
        extra.suspendedAtMs = NowMs();
        LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' brain suspended ({}), task #{} kept.", bot->GetName(), uint32(reason),
            state.task.id);
        NoteEvent(state, "suspended");
    }

    void OnDeath(Player* bot)
    {
        auto itr = _states.find(bot->GetGUID());
        if (itr == _states.end())
            return;
        BrainExtra& extra = _extra[bot->GetGUID()];
        if (extra.deathHandled)
            return;
        extra.deathHandled = true;

        BrainState& state = itr->second;
        if (!state.task.IsValid())
            return;

        WorldExecutor::ReleaseTask(bot, state);
        ++state.task.quest.deaths;
        state.task.quest.lastFailure = FailureReason::Died;
        SetPhase(bot, state, TaskPhase::Recover, "died");
        NoteEvent(state, Acore::StringFormat("died during task #{} (quest {})", state.task.id, state.task.quest.questId));
    }

    bool HasQuestWork(Player* bot)
    {
        if (!QuestKB::IsReady())
            return false;
        auto itr = _states.find(bot->GetGUID());
        if (itr != _states.end())
            return itr->second.task.IsValid() || WorldPlanner::HasQuestWork(bot, itr->second);
        BrainState scratch;
        return WorldPlanner::HasQuestWork(bot, scratch);
    }

    WorldGoal GetGoal(ObjectGuid botGuid)
    {
        auto itr = _states.find(botGuid);
        return itr == _states.end() ? WorldGoal::None : itr->second.goal;
    }

    void Describe(Player* bot, ChatHandler* handler)
    {
        if (!bot || !handler)
            return;

        auto itr = _states.find(bot->GetGUID());
        handler->PSendSysMessage("Bot '{}' (guid {}, level {}) map {} zone {} at ({:.0f}, {:.0f}, {:.0f}).", bot->GetName(),
            bot->GetGUID().GetCounter(), uint32(bot->GetLevel()), bot->GetMapId(), bot->GetZoneId(), bot->GetPositionX(),
            bot->GetPositionY(), bot->GetPositionZ());
        if (itr == _states.end())
        {
            handler->PSendSysMessage("  WorldBrain: no state yet (grouped, in combat, or not ticked idle since login).");
            return;
        }

        BrainState const& state = itr->second;
        uint32 now = NowMs();
        handler->PSendSysMessage("  WorldGoal: {}{}", WorldGoalName(state.goal),
            state.suspended ? Acore::StringFormat(" (suspended, reason {})", uint32(state.suspendReason)) : std::string());

        WorldTask const& task = state.task;
        if (task.IsValid())
        {
            handler->PSendSysMessage("  Task #{}: {} -- phase {} for {}, utility {:.0f} ({}){}", task.id, WorldTaskTypeName(task.type),
                TaskPhaseName(task.phase), SecondsText(now - task.phaseStartedMs), task.utility, task.why, task.paused ? ", paused" : "");

            if (task.quest.questId)
            {
                Quest const* quest = sObjectMgr->GetQuestTemplate(task.quest.questId);
                handler->PSendSysMessage("  Quest: {} - {} [{}]", task.quest.questId, quest ? quest->GetTitle() : "?",
                    QuestKB::DescribeQuest(task.quest.questId));
            }

            if (task.type == WorldTaskType::QuestObjective)
            {
                QuestKnowledge const* info = QuestKB::Get(task.quest.questId);
                if (info && task.quest.objectiveIndex < info->objectives.size())
                {
                    ObjectiveDef const& def = info->objectives[task.quest.objectiveIndex];
                    handler->PSendSysMessage("  Objective {}: {} via '{}' -- progress {}/{} (target entry {}, item {}), {} attempts, {} dry, "
                        "{} target failures, {} bad areas{}", uint32(task.quest.objectiveIndex), ObjectiveTypeName(def.type),
                        QuestExecutor::HandlerName(task), ObjectiveCommon::CurrentCount(bot, task.quest.questId, def),
                        def.requiredCount, def.targetEntry, def.itemId, task.quest.attempts, task.quest.dryAttempts,
                        task.quest.retryCount, task.quest.badClusters, task.quest.useItemMode ? ", using quest item" : "");
                }

                if (ObjectiveArea const* area = ObjectiveAreas::Get(task.quest.selectedClusterId))
                {
                    PopulationCell cell = PopulationHeatmap::Around(area->mapId, area->cx, area->cy);
                    handler->PSendSysMessage("  Area {}: entry {}, {} spawns, radius {:.0f}, {:.0f} yd away -- crowd {} bots ({} incoming), "
                        "{} assigned", area->id, area->entry, area->spawnCount, area->radius,
                        std::hypot(bot->GetPositionX() - area->x, bot->GetPositionY() - area->y), uint32(cell.activeBots),
                        uint32(cell.incomingBots), WorldReservations::Occupancy(ReservationKind::QuestCluster, area->id));
                }
                else if (task.quest.areaTriggerId)
                    handler->PSendSysMessage("  Area trigger {} at ({:.0f}, {:.0f}), {:.0f} yd away.", task.quest.areaTriggerId, task.x,
                        task.y, std::hypot(bot->GetPositionX() - task.x, bot->GetPositionY() - task.y));

                if (!task.quest.bundle.empty())
                {
                    std::string bundle;
                    for (ObjectiveRef const& ref : task.quest.bundle)
                        bundle += Acore::StringFormat(" {}:{}", ref.questId, uint32(ref.objectiveIndex));
                    handler->PSendSysMessage("  Bundled objectives:{}", bundle);
                }
            }
            else if (task.npcEntry)
                handler->PSendSysMessage("  Quest npc: entry {} ({}), spawn {}, {:.0f} yd away.", task.npcEntry,
                    task.npcIsGameObject ? "object" : "creature", task.npcSpawnId,
                    std::hypot(bot->GetPositionX() - task.x, bot->GetPositionY() - task.y));
            else
                handler->PSendSysMessage("  Destination ({:.0f}, {:.0f}), {:.0f} yd away.", task.x, task.y,
                    std::hypot(bot->GetPositionX() - task.x, bot->GetPositionY() - task.y));

            if (!task.quest.targetGuid.IsEmpty())
            {
                WorldObject* target = task.quest.targetGuid.IsGameObject()
                    ? static_cast<WorldObject*>(ObjectAccessor::GetGameObject(*bot, task.quest.targetGuid))
                    : static_cast<WorldObject*>(ObjectAccessor::GetCreature(*bot, task.quest.targetGuid));
                handler->PSendSysMessage("  Reserved target: {} ({}{})", task.quest.targetGuid.ToString(),
                    target ? target->GetName() : "not visible",
                    target ? Acore::StringFormat(", {:.0f} yd", bot->GetDistance(target)) : std::string());
            }
        }
        else
        {
            handler->PSendSysMessage("  Task: none -- activity {} for another {}.", WorldDirectiveName(state.activity),
                SecondsText(state.activityUntilMs > now ? state.activityUntilMs - now : 0));
        }

        handler->PSendSysMessage("  {}", BotMovement::Describe(bot));

        if (!state.route.empty())
        {
            std::string route;
            for (RouteStep const& step : state.route)
                route += Acore::StringFormat(" -> {} q{}:{} ({:.0f},{:.0f})", WorldTaskTypeName(step.type), step.questId,
                    uint32(step.objectiveIndex), step.x, step.y);
            handler->PSendSysMessage("  Route:{}", route);
        }

        std::string failures;
        state.failures.ForEachLive(now, [&failures](FailKind kind, uint64 id, uint32 remaining, uint8 reason, uint8 strikes)
        {
            if (failures.size() < 400)
                failures += Acore::StringFormat(" [{} {} {} x{}, {}s]", FailKindName(kind), id, FailureReasonName(FailureReason(reason)),
                    uint32(strikes), remaining / IN_MILLISECONDS);
        });
        handler->PSendSysMessage("  Failures:{}", failures.empty() ? std::string(" none") : failures);

        handler->PSendSysMessage("  Next planner evaluation: {}.", SecondsText(state.nextPlanMs > now ? state.nextPlanMs - now : 0));

        if (!state.lastEvent.empty())
            handler->PSendSysMessage("  Last event ({} ago): {}", SecondsText(now - state.lastEventMs), state.lastEvent);

        WorldMetrics const& m = state.metrics;
        handler->PSendSysMessage("  Metrics: quests accepted {}, completed {}, turned in {}, suspended {}, abandoned {}; objectives {}; "
            "kill targets {}; tasks {}/{} ok/failed; stuck {}; reservation conflicts {}; area switches {}.",
            m.questsAccepted, m.questsCompleted, m.questsTurnedIn, m.questsSuspended, m.questsAbandoned, m.objectivesCompleted,
            m.killTargetsSelected, m.tasksCompleted, m.tasksFailed, m.movementStuck, m.reservationConflicts, m.areaSwitches);
    }

    void DescribeGlobal(ChatHandler* handler)
    {
        if (!handler)
            return;

        KnowledgeStats const& kb = QuestKB::Stats();
        handler->PSendSysMessage("QuestKB: {} quests ({} supported), {} loot-resolved items, {} giver spawns, {} hubs, {} area triggers "
            "(built in {} ms). Objective areas cached: {}.", kb.quests, kb.supported, kb.lootItems, kb.giverSpots, kb.hubs,
            kb.areaTriggers, kb.buildMs, ObjectiveAreas::CachedAreas());

        std::array<uint32, 12> byType{};
        std::array<uint32, 12> byPhase{};
        uint32 suspended = 0;
        for (auto const& [guid, state] : _states)
        {
            ++byType[std::min<size_t>(size_t(state.task.type), byType.size() - 1)];
            if (state.task.IsValid())
                ++byPhase[std::min<size_t>(size_t(state.task.phase), byPhase.size() - 1)];
            if (state.suspended)
                ++suspended;
        }
        handler->PSendSysMessage("Brains: {} ({} suspended). Tasks: none {}, objective {}, accept {}, turn-in {}.",
            _states.size(), suspended, byType[size_t(WorldTaskType::None)], byType[size_t(WorldTaskType::QuestObjective)],
            byType[size_t(WorldTaskType::QuestAccept)], byType[size_t(WorldTaskType::QuestTurnIn)]);
        handler->PSendSysMessage("Phases: travel {}, search {}, approach {}, execute {}, combat {}, loot {}, verify {}, recover {}.",
            byPhase[size_t(TaskPhase::TravelToArea)], byPhase[size_t(TaskPhase::Search)], byPhase[size_t(TaskPhase::Approach)],
            byPhase[size_t(TaskPhase::Execute)], byPhase[size_t(TaskPhase::Combat)], byPhase[size_t(TaskPhase::Loot)],
            byPhase[size_t(TaskPhase::Verify)], byPhase[size_t(TaskPhase::Recover)]);

        WorldMetrics const& m = WorldMetricsGlobal::Get();
        handler->PSendSysMessage("Quests: accepted {}, completed {}, turned in {}, suspended {}, abandoned {}. Objectives: {} done "
            "({} loot, {} use, {} explore), {} failed, {} counter increments.", m.questsAccepted, m.questsCompleted, m.questsTurnedIn,
            m.questsSuspended, m.questsAbandoned, m.objectivesCompleted, m.lootObjectivesCompleted, m.useObjectivesCompleted,
            m.exploreObjectivesCompleted, m.failedObjectives, m.objectiveProgress);
        handler->PSendSysMessage("Tasks: started {}, completed {}, failed {}, replans {}. Kill targets selected {}. "
            "Area switches {}.", m.tasksStarted, m.tasksCompleted, m.tasksFailed, m.replans,
            m.killTargetsSelected, m.areaSwitches);

        MovementStats const& mv = BotMovement::Stats();
        double avgRetries = m.tasksStarted ? double(m.travelRetries) / double(m.tasksStarted) : 0.0;
        handler->PSendSysMessage("Movement: {} MovePoints issued, {} redundant requests skipped, {} stalls ({} repaths, {} detours, {} gave up), "
            "{:.2f} travel retries per task.", mv.issued, mv.redundantSkipped, mv.stuckEvents, mv.repaths, mv.detours, mv.gaveUp,
            avgRetries);

        ReservationTable const& table = WorldReservations::Table();
        PopulationGrid const& grid = PopulationHeatmap::Grid();
        handler->PSendSysMessage("Reservations: {} exclusive, {} shared, {} conflicts. Heatmap: {} bots in {} cells.",
            table.ExclusiveCount(), table.SharedCount(), table.Conflicts(), grid.BotCount(), grid.CellCount());
    }

    void Forget(ObjectGuid botGuid)
    {
        WorldReservations::ReleaseAll(botGuid);
        PopulationHeatmap::Remove(botGuid);
        _states.erase(botGuid);
        _extra.erase(botGuid);
    }
}
