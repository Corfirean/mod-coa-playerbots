#include "WorldBrain.h"
#include "BotMovement.h"
#include "BotZoneProgression.h"
#include "BotWorldPoi.h"
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
#include "QuestPolicy.h"
#include "WorldBrainState.h"
#include "WorldExecutor.h"
#include "WorldParties.h"
#include "WorldPlanner.h"
#include "WorldReservations.h"
#include "WorldSocial.h"
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

    // The brain normally ticks every world update while the bot is idle. A longer gap means the
    // bot was busy with the task's own business somewhere else in BotAI (a fight, resting,
    // looting, a corpse run): that time is taken off the current phase's budget.
    constexpr uint32 BRAIN_GAP_MS = 2000;

    // Farther than this from where the last brain tick saw the bot means it was moved (a
    // teleport, a flight): the task was planned from somewhere else and is re-planned.
    constexpr float RELOCATION_JUMP_YARDS = 400.0f;

    // A dead-end quest is remembered (and logged) once per this long.
    constexpr uint32 DEAD_END_MEMORY_MS = HOUR * IN_MILLISECONDS;

    // Opportunity detours (a herb next to the path).
    constexpr uint32 OPPORTUNITY_MAX_MS = 30000;
    constexpr uint32 OPPORTUNITY_START_GRACE_MS = 4500;

    // A fallback activity that keeps finding nothing hands over to ambient life after this.
    constexpr uint32 ACTIVITY_IDLE_SWITCH_MS = 15000;

    // Quests abandoned for good are not taken again for this long.
    constexpr uint32 ABANDONED_QUEST_MEMORY_MS = 2 * HOUR * IN_MILLISECONDS;

    // Planner backoff ceiling when it keeps finding nothing (the bot grinds/gathers meanwhile).
    constexpr uint32 MAX_EMPTY_PLAN_BACKOFF_MS = 3 * MINUTE * IN_MILLISECONDS;

    // A bot with no work on its whole map asks zone progression to move it at most this often.
    constexpr uint32 RELOCATION_ASK_MS = 10 * MINUTE * IN_MILLISECONDS;

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
        uint32 opportunityBeganMs = 0;
        bool opportunityStarted = false;
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

    void StartSession(BrainState& state, uint32 now)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        state.sessionStartMs = now;
        state.sessionLengthMs = RollRange(state, 0x5e55, cfg.sessionMinMs, cfg.sessionMaxMs) / 100 * (50 + state.persona.patience);
    }

    // A transient failure (no targets, no path, no progress, a death...): the quest is set aside,
    // longer each time in a row, and retried later. Never abandoned from here -- whether a quest
    // is a dead end is decided from what it is (CleanupQuestLog), not from how one run went.
    void SuspendQuest(Player* bot, BrainState& state, uint32 questId, FailureReason reason)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 now = NowMs();
        uint32 suspensions = ++state.questSuspensions[questId];
        uint32 ms = QuestPolicy::SuspendMs(cfg.questSuspendMs, suspensions, cfg.questSuspendMaxMs);
        state.failures.Forget(FailKind::Quest, questId);
        state.failures.Remember(FailKind::Quest, questId, now, ms, uint8(reason));
        Count(state.metrics, &WorldMetrics::questsSuspended);
        LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' sets quest {} aside for {}s ({}, {} in a row).", bot->GetName(), questId,
            ms / IN_MILLISECONDS, FailureReasonName(reason), suspensions);
        NoteEvent(state, Acore::StringFormat("set quest {} aside for {}s ({}, {} in a row)", questId, ms / IN_MILLISECONDS,
            FailureReasonName(reason), suspensions));
    }

    // Dead ends in the log (QuestPolicy.h): quests that have failed, can never be completed, or
    // have an open objective this build cannot execute -- typically taken before this layer
    // existed, or added by hand. The planner already leaves every one of them alone (the same
    // Workable() question); here they are noted once, and one is abandoned per pass only when the
    // log is so full it blocks new work. Quests the knowledge base does not know are never touched.
    void CleanupQuestLog(Player* bot, BrainState& state)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 now = NowMs();
        uint32 inLog = 0;
        uint32 victim = 0;
        bool victimFailed = false;

        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 questId = bot->GetQuestSlotQuestId(slot);
            if (!questId)
                continue;
            ++inLog;

            QuestStatus status = bot->GetQuestStatus(questId);
            char const* why = nullptr;
            if (status == QUEST_STATUS_FAILED)
                why = "the quest has failed";
            else if (status == QUEST_STATUS_INCOMPLETE && !bot->CanCompleteQuest(questId))
            {
                QuestKnowledge const* info = QuestKB::Get(questId);
                if (!info || QuestInteraction::Workable(bot, questId, *info, &why))
                    continue;
            }
            if (!why)
                continue;

            // Noted once an hour. Only a note: the planner skips the quest because Workable() says
            // so, not because of this, so a quest that becomes workable again is picked up at once.
            if (!state.failures.Has(FailKind::DeadEnd, questId, now))
            {
                state.failures.Remember(FailKind::DeadEnd, questId, now, DEAD_END_MEMORY_MS, uint8(FailureReason::Unsupported));
                LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' leaves quest {} alone: {}.", bot->GetName(), questId, why);
                NoteEvent(state, Acore::StringFormat("quest {} is a dead end ({})", questId, why));
            }

            bool failed = status == QUEST_STATUS_FAILED;
            if (!victim || (failed && !victimFailed))
            {
                victim = questId;
                victimFailed = failed;
            }
        }

        if (victim && QuestPolicy::ShouldAbandonDeadEnd(inLog, cfg.maxActiveQuests))
        {
            QuestInteraction::Abandon(bot, victim);
            state.failures.Remember(FailKind::Quest, victim, now, ABANDONED_QUEST_MEMORY_MS, uint8(FailureReason::Unsupported));
            state.questSuspensions.erase(victim);
            Count(state.metrics, &WorldMetrics::questsAbandoned);
            LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' abandons dead-end quest {} to make room ({} quests in the log).",
                bot->GetName(), victim, inLog);
            NoteEvent(state, Acore::StringFormat("abandoned dead-end quest {} to make room in the log", victim));
        }
    }

    void EndTask(Player* bot, BrainState& state, ExecResult result)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 now = NowMs();
        WorldTask& task = state.task;

        WorldExecutor::ReleaseTask(bot, state);
        WorldParties::OnLeaderTaskEnded(bot->GetGUID());
        Count(state.metrics, &WorldMetrics::replans);

        if (result == ExecResult::Completed)
        {
            // Work on the quest succeeded: its next failure starts the back-off from scratch.
            if (task.type == WorldTaskType::QuestObjective)
                state.questSuspensions.erase(task.quest.questId);
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
                        case FailureReason::Unreachable:
                        case FailureReason::Timeout:
                        case FailureReason::CastFailed:
                        case FailureReason::InteractFailed:
                        case FailureReason::Died:
                            SuspendQuest(bot, state, task.quest.questId, reason);
                            break;
                        case FailureReason::Unsupported:
                            // Nothing to execute after all: set aside like any failure, and let the
                            // log cleanup look at what the quest is on the next planner pass.
                            SuspendQuest(bot, state, task.quest.questId, reason);
                            state.nextLogCleanupMs = now;
                            break;
                        default:
                            break;
                    }
                    break;
                case WorldTaskType::Travel:
                    state.failures.Remember(FailKind::Hub, task.npcSpawnId, now, cfg.clusterFailMs * 2, uint8(reason));
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
        WorldParties::OnLeaderTaskEnded(bot->GetGUID());
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

        state.goal = t.type == WorldTaskType::Travel ? WorldGoal::Traveling : WorldGoal::Questing;
        state.activity = WorldDirective::Idle;
        if (!state.sessionStartMs)
            StartSession(state, now);

        if (t.quest.selectedClusterId)
            WorldReservations::Join(ReservationKind::QuestCluster, t.quest.selectedClusterId, bot->GetGUID(), cfg.clusterJoinMs);
        Count(state.metrics, &WorldMetrics::tasksStarted);

        float dist = std::hypot(bot->GetPositionX() - t.x, bot->GetPositionY() - t.y);
        LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' TaskSelected {} #{} quest {} objective {} utility {:.0f} ({}), "
            "area {} at {:.0f} yd, {} bundled.", bot->GetName(), WorldTaskTypeName(t.type), t.id, t.quest.questId,
            uint32(t.quest.objectiveIndex), t.utility, t.why, t.quest.selectedClusterId, dist, t.quest.bundle.size());
        NoteEvent(state, Acore::StringFormat("started {} (quest {}): {}", WorldTaskTypeName(t.type), t.quest.questId, t.why));

        // Company for a kill quest, if there is a like-minded bot around.
        WorldParties::TryForm(bot, state);
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

    void EndOpportunity(BrainState& state, BrainExtra& extra, char const* why)
    {
        state.opportunityActive = false;
        state.task.paused = false;
        extra.opportunityStarted = false;
        state.nextOpportunityCheckMs = NowMs() + RollRange(state, 0x0dd1, 20000, 45000);
        NoteEvent(state, Acore::StringFormat("back on the task after a detour ({})", why));
    }

    // A gathering node right next to the path: step off, pick it, step back on. The primary task
    // stays as it was; only its movement is paused.
    bool TryOpportunity(Player* bot, BrainState& state, BrainExtra& extra)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 now = NowMs();
        if (!cfg.gatherDetours || now < state.nextOpportunityCheckMs)
            return false;
        state.nextOpportunityCheckMs = now + RollRange(state, 0x0dd2, 4000, 8000);

        TaskPhase phase = state.task.phase;
        if (phase != TaskPhase::TravelToArea && phase != TaskPhase::Search)
            return false;
        if (bot->IsInCombat() || !HasGatherSkill(bot))
            return false;
        // Gatherers can't walk past a herb; everyone else only sometimes stops.
        if (Roll(state, 0x0dd3) % 100 >= state.persona.gathering)
            return false;

        uint16 herb = bot->GetSkillValue(SKILL_HERBALISM);
        uint16 ore = bot->GetSkillValue(SKILL_MINING);
        std::vector<Poi const*> nodes;
        if (herb)
            BotWorldPoi::Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), cfg.detourRadius, PoiKind::Herb, nodes);
        if (ore)
            BotWorldPoi::Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), cfg.detourRadius, PoiKind::Ore, nodes);

        bool any = std::any_of(nodes.begin(), nodes.end(), [herb, ore](Poi const* poi)
            { return poi->requiredSkill <= (poi->kind == PoiKind::Herb ? herb : ore); });
        if (!any)
            return false;

        state.opportunityActive = true;
        state.opportunity = WorldDirective::Gather;
        state.opportunityUntilMs = now + OPPORTUNITY_MAX_MS;
        extra.opportunityBeganMs = now;
        extra.opportunityStarted = false;
        state.task.paused = true;
        BotMovement::Release(bot, MoveOwner::Quest);
        BotMovement::Release(bot, MoveOwner::Travel);
        Count(state.metrics, &WorldMetrics::opportunities);
        NoteEvent(state, "detour: gathering node next to the path");
        return true;
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
        // A phase entered while the task is paused (a death on an errand, the pause itself
        // releasing a target) starts at the frozen clock, so Resume() moves it to the resume time
        // like every other clock of the task -- never into the future.
        state.task.phaseStartedMs = state.task.ClockNow(NowMs());
    }

    void NoteEvent(BrainState& state, std::string text)
    {
        state.lastEvent = std::move(text);
        state.lastEventMs = NowMs();
    }

    uint32 PhaseElapsed(BrainState const& state)
    {
        uint32 clock = state.task.ClockNow(NowMs());
        return clock > state.task.phaseStartedMs ? clock - state.task.phaseStartedMs : 0;
    }

    BrainState* FindState(ObjectGuid guid)
    {
        auto itr = _states.find(guid);
        return itr == _states.end() ? nullptr : &itr->second;
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
        WorldParties::Update(diff);
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

        // Back from something outside the task (a group, a manual command, an errand): the pause
        // ends and the task's clocks move on by its length, so none of it counts as searching,
        // approaching or travelling. A long suspension drops the task instead: the world has
        // moved on.
        bool resumed = false;
        if (state.suspended)
        {
            state.suspended = false;
            extra.pausedByAmbient = false;
            if (state.task.IsValid())
            {
                if (now - extra.suspendedAtMs > SUSPEND_DROP_MS)
                    DropTask(bot, state, FailureReason::Suspended);
                else
                {
                    WorldExecutor::ResumeTask(bot, state);
                    SetPhase(bot, state, TaskPhase::Recover, "resumed");
                }
            }
            NoteEvent(state, "resumed after a suspension");
            state.nextPlanMs = std::min(state.nextPlanMs, now + RollRange(state, 0x7e5a, 500, 2500));
            resumed = true;
        }
        else if (extra.pausedByAmbient)
        {
            extra.pausedByAmbient = false;
            WorldExecutor::ResumeTask(bot, state);
            resumed = true;
        }

        if (bot->GetMapId() != state.lastMapId)
        {
            DropTask(bot, state, FailureReason::MapChanged);
            state.lastMapId = bot->GetMapId();
        }
        else if (state.task.IsValid() && state.lastUpdateMs &&
            std::hypot(bot->GetPositionX() - state.lastX, bot->GetPositionY() - state.lastY) > RELOCATION_JUMP_YARDS)
            // Teleported or flown somewhere while doing something else: planned from elsewhere.
            DropTask(bot, state, FailureReason::Relocated);

        // Time the task's own business took while the brain was not ticking -- a fight with an
        // add, resting, looting, a corpse run -- is not time spent in the phase it interrupted.
        if (!resumed && state.task.IsValid() && state.lastUpdateMs && now - state.lastUpdateMs > BRAIN_GAP_MS)
            state.task.ShiftPhaseClock(now - state.lastUpdateMs);

        state.lastUpdateMs = now;
        state.lastX = bot->GetPositionX();
        state.lastY = bot->GetPositionY();

        if (now >= state.nextPresenceMs)
        {
            PopulationHeatmap::UpdatePresence(bot->GetGUID(), bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(),
                ActivityOf(state));
            state.nextPresenceMs = now + RollRange(state, 0x9e5e, cfg.presenceMinMs, cfg.presenceMaxMs);
        }

        if (state.opportunityActive)
        {
            if (now >= state.opportunityUntilMs)
                EndOpportunity(state, extra, "took too long");
            else
                return state.opportunity;
        }

        // Someone nearby needs a hand (a fight they are losing, a corpse to resurrect).
        if (WorldSocial::IsHelping(bot, state) || WorldSocial::TryHelp(bot, state))
            return WorldDirective::Busy;

        if (state.task.IsValid())
        {
            if (now >= state.task.deadlineMs)
            {
                state.task.quest.lastFailure = FailureReason::Timeout;
                EndTask(bot, state, ExecResult::Failed);
                return WorldDirective::Busy;
            }
            if (TryOpportunity(bot, state, extra))
                return state.opportunity;

            ExecResult result = WorldExecutor::Update(bot, state, diff);
            if (result != ExecResult::Running)
                EndTask(bot, state, result);
            WorldExecutor::SyncIncoming(bot, state);
            return WorldDirective::Busy;
        }

        // Between tasks: the session rhythm -- a stretch of questing, then a break in town.
        if (cfg.sessionBreaks)
        {
            if (!state.sessionStartMs)
                StartSession(state, now);
            if (state.breakUntilMs)
            {
                if (now < state.breakUntilMs)
                    return WorldDirective::Ambient;
                state.breakUntilMs = 0;
                StartSession(state, now);
                NoteEvent(state, "break over");
            }
            else if (state.goal == WorldGoal::Questing && now - state.sessionStartMs > state.sessionLengthMs)
            {
                state.breakUntilMs = now + RollRange(state, 0xb4ea, cfg.breakMinMs, cfg.breakMaxMs);
                state.goal = WorldGoal::Break;
                Count(state.metrics, &WorldMetrics::breaks);
                NoteEvent(state, Acore::StringFormat("taking a break for {}", SecondsText(state.breakUntilMs - now)));
                return WorldDirective::Ambient;
            }
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

            // No quest work and no hub anywhere on this map: zone progression moves the bot on
            // (by flight where it can, teleport only as its last resort).
            if (state.noWorkOnMap && now >= state.nextRelocationAskMs)
            {
                state.nextRelocationAskMs = now + RELOCATION_ASK_MS;
                BotZoneProgression::QueueRelocation(bot);
                NoteEvent(state, "no quest work left on this map, asking to move on");
            }

            // A bot in a grinding mood does not always drop everything to travel to a new hub.
            bool grindMood = state.activity == WorldDirective::Grind && now < state.activityUntilMs &&
                persona.lean == LEAN_GRIND && Roll(state, 0x6a1d) % 100 < 60;
            if (task.IsValid() && !(grindMood && task.type == WorldTaskType::Travel))
            {
                state.emptyPlans = 0;
                StartTask(bot, state, std::move(task));
                ExecResult result = WorldExecutor::Update(bot, state, diff);
                if (result != ExecResult::Running)
                    EndTask(bot, state, result);
                WorldExecutor::SyncIncoming(bot, state);
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
        BrainExtra& extra = _extra[bot->GetGUID()];
        uint32 now = NowMs();

        if (state.opportunityActive && directive == state.opportunity)
        {
            if (started)
                extra.opportunityStarted = true;
            else if (extra.opportunityStarted)
                EndOpportunity(state, extra, "node gathered");
            else if (now - extra.opportunityBeganMs > OPPORTUNITY_START_GRACE_MS)
                EndOpportunity(state, extra, "node out of reach");
            return;
        }

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
        if (itr == _states.end() || !itr->second.task.IsValid() || itr->second.suspended)
            return;
        BrainExtra& extra = _extra[bot->GetGUID()];
        if (extra.pausedByAmbient)
            return;
        extra.pausedByAmbient = true;
        // Let the errand (a repair, a vendor, a flight) have the legs; the task's clocks stop
        // until it is back (the errand is not the task's failure), and it walks on from there.
        WorldExecutor::PauseTask(bot, itr->second, "ambient errand");
    }

    void Suspend(Player* bot, SuspendReason reason)
    {
        auto itr = _states.find(bot->GetGUID());
        if (itr == _states.end() || itr->second.suspended)
            return;
        BrainState& state = itr->second;
        BrainExtra& extra = _extra[bot->GetGUID()];

        // Everything the task holds is let go (the group or the command owns the bot now); the
        // task itself is kept with its clocks stopped, in case the bot is back soon.
        WorldExecutor::ReleaseTask(bot, state);
        if (state.task.IsValid())
            state.task.Pause(NowMs());
        PopulationHeatmap::Remove(bot->GetGUID());
        state.nextPresenceMs = 0;
        state.suspended = true;
        state.suspendReason = reason;
        state.opportunityActive = false;
        extra.suspendedAtMs = NowMs();
        LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' brain suspended ({}), task #{} kept with its clocks stopped.",
            bot->GetName(), SuspendReasonName(reason), state.task.id);
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
        state.opportunityActive = false;
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
            state.suspended ? Acore::StringFormat(" (suspended: {})", SuspendReasonName(state.suspendReason)) : std::string());

        WorldTask const& task = state.task;
        if (task.IsValid())
        {
            // While paused the clocks stand still at pausedAtMs; show them as they will be on resume.
            uint32 clockNow = task.ClockNow(now);
            uint32 inPhase = PhaseElapsed(state);
            handler->PSendSysMessage("  Task #{}: {} -- phase {} for {}, utility {:.0f} ({}){}", task.id, WorldTaskTypeName(task.type),
                TaskPhaseName(task.phase), SecondsText(inPhase), task.utility, task.why,
                task.paused ? Acore::StringFormat(", PAUSED for {} (clocks stopped)", SecondsText(now - task.pausedAtMs)) : std::string());

            uint32 budget = WorldExecutor::PhaseBudgetMs(state);
            PopulationGrid::BotStatus heat = PopulationHeatmap::StatusOf(bot->GetGUID());
            handler->PSendSysMessage("  Clocks: phase budget {}; task deadline in {}. Heatmap: {}, {}.",
                budget ? Acore::StringFormat("{} of {} left", SecondsText(budget > inPhase ? budget - inPhase : 0), SecondsText(budget))
                       : std::string("none"),
                SecondsText(task.deadlineMs > clockNow ? task.deadlineMs - clockNow : 0),
                heat.present ? "present" : "not present", heat.incoming ? "incoming to the task's destination" : "not incoming");

            if (task.quest.questId)
            {
                Quest const* quest = sObjectMgr->GetQuestTemplate(task.quest.questId);
                std::string workable;
                if (QuestKnowledge const* info = QuestKB::Get(task.quest.questId); info && task.type == WorldTaskType::QuestObjective)
                {
                    char const* why = nullptr;
                    workable = QuestInteraction::Workable(bot, task.quest.questId, *info, &why) ? "; workable"
                        : Acore::StringFormat("; NOT workable: {}", why ? why : "?");
                }
                handler->PSendSysMessage("  Quest: {} - {} [{}{}]", task.quest.questId, quest ? quest->GetTitle() : "?",
                    QuestKB::DescribeQuest(task.quest.questId), workable);
            }

            if (task.type == WorldTaskType::QuestObjective)
            {
                QuestKnowledge const* info = QuestKB::Get(task.quest.questId);
                if (info && task.quest.objectiveIndex < info->objectives.size())
                {
                    ObjectiveDef const& def = info->objectives[task.quest.objectiveIndex];
                    handler->PSendSysMessage("  Objective {}: {} via '{}' -- progress {}/{} (target entry {}, item {}), {} attempts, {} dry "
                        "of {}, {} target failures, {} bad areas{}", uint32(task.quest.objectiveIndex), ObjectiveTypeName(def.type),
                        QuestExecutor::HandlerName(task), ObjectiveCommon::CurrentCount(bot, task.quest.questId, def),
                        def.requiredCount, def.targetEntry, def.itemId, task.quest.attempts, task.quest.dryAttempts,
                        ObjectiveCommon::DryAttemptLimit(def), task.quest.retryCount, task.quest.badClusters,
                        task.quest.useItemMode ? ", using quest item" : "");
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

        handler->PSendSysMessage("  Next planner evaluation: {}; opportunity: {}; session: {}{}.",
            SecondsText(state.nextPlanMs > now ? state.nextPlanMs - now : 0), state.opportunityActive ? "detour in progress" : "none",
            state.sessionStartMs ? SecondsText(now - state.sessionStartMs) : std::string("-"),
            state.breakUntilMs > now ? Acore::StringFormat(", on a break for {}", SecondsText(state.breakUntilMs - now)) : std::string());

        std::string party = WorldParties::Describe(bot->GetGUID());
        if (!party.empty())
            handler->PSendSysMessage("  {}", party);

        if (!state.lastEvent.empty())
            handler->PSendSysMessage("  Last event ({} ago): {}", SecondsText(now - state.lastEventMs), state.lastEvent);

        WorldMetrics const& m = state.metrics;
        handler->PSendSysMessage("  Metrics: quests accepted {}, completed {}, turned in {}, suspended {}, abandoned {}; objectives {}; "
            "kill targets {}; tasks {}/{} ok/failed; stuck {}; reservation conflicts {}; area switches {}; detours {}; "
            "helped {}, resurrected {}, parties {}.",
            m.questsAccepted, m.questsCompleted, m.questsTurnedIn, m.questsSuspended, m.questsAbandoned, m.objectivesCompleted,
            m.killTargetsSelected, m.tasksCompleted, m.tasksFailed, m.movementStuck, m.reservationConflicts, m.areaSwitches,
            m.opportunities, m.assists, m.resurrections, m.partiesFormed);
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
        handler->PSendSysMessage("Brains: {} ({} suspended). Tasks: none {}, objective {}, accept {}, turn-in {}, travel {}.",
            _states.size(), suspended, byType[size_t(WorldTaskType::None)], byType[size_t(WorldTaskType::QuestObjective)],
            byType[size_t(WorldTaskType::QuestAccept)], byType[size_t(WorldTaskType::QuestTurnIn)],
            byType[size_t(WorldTaskType::Travel)]);
        handler->PSendSysMessage("Phases: travel {}, search {}, approach {}, execute {}, combat {}, loot {}, verify {}, recover {}.",
            byPhase[size_t(TaskPhase::TravelToArea)], byPhase[size_t(TaskPhase::Search)], byPhase[size_t(TaskPhase::Approach)],
            byPhase[size_t(TaskPhase::Execute)], byPhase[size_t(TaskPhase::Combat)], byPhase[size_t(TaskPhase::Loot)],
            byPhase[size_t(TaskPhase::Verify)], byPhase[size_t(TaskPhase::Recover)]);

        WorldMetrics const& m = WorldMetricsGlobal::Get();
        handler->PSendSysMessage("Quests: accepted {}, completed {}, turned in {}, suspended {}, abandoned {}. Objectives: {} done "
            "({} loot, {} use, {} explore), {} failed, {} counter increments.", m.questsAccepted, m.questsCompleted, m.questsTurnedIn,
            m.questsSuspended, m.questsAbandoned, m.objectivesCompleted, m.lootObjectivesCompleted, m.useObjectivesCompleted,
            m.exploreObjectivesCompleted, m.failedObjectives, m.objectiveProgress);
        handler->PSendSysMessage("Tasks: started {}, completed {}, failed {}, replans {}. Kill targets selected {}. Flights {}. "
            "Area switches {}. Detours {}. Breaks {}.", m.tasksStarted, m.tasksCompleted, m.tasksFailed, m.replans,
            m.killTargetsSelected, m.travels, m.areaSwitches, m.opportunities, m.breaks);

        handler->PSendSysMessage("Social: {} fights helped, {} resurrections, {} temporary parties formed ({} active), {} ended.",
            m.assists, m.resurrections, m.partiesFormed, WorldParties::Count(), m.partiesEnded);

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
        WorldParties::Forget(botGuid);
        WorldReservations::ReleaseAll(botGuid);
        PopulationHeatmap::Remove(botGuid);
        _states.erase(botGuid);
        _extra.erase(botGuid);
    }
}
