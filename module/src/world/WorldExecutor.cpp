#include "WorldExecutor.h"
#include "BotAI.h"
#include "BotWorldBehavior.h"
#include "ObjectiveCommon.h"
#include "Player.h"
#include "PopulationHeatmap.h"
#include "QuestExecutor.h"
#include "QuestInteraction.h"
#include "WorldReservations.h"
#include <cmath>

using namespace WorldBrainInternal;

namespace
{
    // No remounting this soon after getting off: mount/dismount flapping reads as a bot, not a
    // player.
    constexpr uint32 REMOUNT_COOLDOWN_MS = 10000;

    MoveOwner OwnerFor(WorldTask const& task)
    {
        return task.type == WorldTaskType::Travel ? MoveOwner::Travel : MoveOwner::Quest;
    }

    ExecResult UpdateTravel(Player* bot, BrainState& state)
    {
        WorldTask& task = state.task;
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();

        if (task.phase == TaskPhase::Planning || task.phase == TaskPhase::Recover)
            SetPhase(bot, state, TaskPhase::TravelToArea, "setting off");

        float dist = std::hypot(bot->GetPositionX() - task.x, bot->GetPositionY() - task.y);
        if (bot->GetMapId() == task.mapId && dist <= task.areaRadius)
        {
            WorldExecutor::Dismount(bot, state);
            return ExecResult::Completed;
        }

        if (PhaseElapsed(state) > cfg.travelTimeoutMs)
        {
            task.quest.lastFailure = FailureReason::Timeout;
            return ExecResult::Failed;
        }

        if (!task.taxiRequested && dist > cfg.taxiMinDistance)
        {
            task.taxiRequested = true;
            if (WorldExecutor::TryRequestFlight(bot, state, task.x, task.y, task.z))
                return ExecResult::Running;
        }

        NavStatus status = WorldExecutor::TravelTo(bot, state, WorldGoalSub::Hub, task.x, task.y, task.z,
            std::max(10.0f, task.areaRadius * 0.5f));
        if (status == NavStatus::Arrived)
        {
            WorldExecutor::Dismount(bot, state);
            return ExecResult::Completed;
        }
        if (status == NavStatus::Stuck)
        {
            Count(state.metrics, &WorldMetrics::movementStuck);
            task.quest.lastFailure = FailureReason::Unreachable;
            return ExecResult::Failed;
        }
        return ExecResult::Running;
    }
}

namespace WorldExecutor
{
    ExecResult Update(Player* bot, BrainState& state, uint32 diff)
    {
        switch (state.task.type)
        {
            case WorldTaskType::QuestObjective:
                return QuestExecutor::Update(bot, state, diff);
            case WorldTaskType::QuestAccept:
            case WorldTaskType::QuestTurnIn:
                return QuestInteraction::UpdateNpcTask(bot, state);
            case WorldTaskType::Travel:
                return UpdateTravel(bot, state);
            default:
                return ExecResult::Failed;
        }
    }

    NavStatus TravelTo(Player* bot, BrainState& state, uint8 sub, float x, float y, float z, float radius, uint64 goalSalt,
        bool allowMount)
    {
        uint32 now = NowMs();
        float dist = std::hypot(bot->GetPositionX() - x, bot->GetPositionY() - y);

        if (allowMount && !bot->IsMounted() && dist > state.mountThreshold && !bot->IsInCombat() &&
            now - state.lastDismountMs > REMOUNT_COOLDOWN_MS)
            BotAI::TryMountForTravel(bot);

        MovementRequest const* before = BotMovement::GetRequest(bot->GetGUID());
        uint8 stageBefore = before ? before->progress.stage : 0;

        uint64 goal = state.task.GoalId(sub) + (goalSalt << 40);
        NavStatus status = BotMovement::Navigate(bot, OwnerFor(state.task), goal, x, y, z, radius);

        if (MovementRequest const* after = BotMovement::GetRequest(bot->GetGUID()); after && after->progress.stage > stageBefore)
            Count(state.metrics, &WorldMetrics::travelRetries);
        return status;
    }

    bool TryRequestFlight(Player* bot, BrainState& state, float x, float y, float z)
    {
        if (!BotWorldBehavior::RequestTravel(bot, bot->GetMapId(), x, y, z, false))
            return false;

        Count(state.metrics, &WorldMetrics::travels);
        BotMovement::Release(bot, MoveOwner::Quest);
        BotMovement::Release(bot, MoveOwner::Travel);
        NoteEvent(state, Acore::StringFormat("taking a flight toward ({:.0f}, {:.0f})", x, y));
        LOG_DEBUG("module.coa-playerbots.navigation", "Bot '{}' requested a flight toward ({:.0f}, {:.0f}) for task {}.",
            bot->GetName(), x, y, state.task.id);
        return true;
    }

    void Dismount(Player* bot, BrainState& state)
    {
        if (!bot->IsMounted())
            return;
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        state.lastDismountMs = NowMs();
    }

    void PauseTask(Player* bot, BrainState& state, char const* why)
    {
        WorldTask& task = state.task;
        if (!task.IsValid() || task.paused)
            return;

        task.Pause(NowMs());
        BotMovement::Release(bot, MoveOwner::Quest);
        BotMovement::Release(bot, MoveOwner::Travel);
        BotMovement::ResetRequest(bot->GetGUID());
        PopulationHeatmap::ClearIncoming(bot->GetGUID());

        // A paused task holds no mob: the reservation would lapse during the interruption anyway,
        // and another bot is welcome to it meanwhile. An objective that was walking up to its
        // target looks for one again when it resumes.
        if (!task.quest.targetGuid.IsEmpty())
        {
            ReservationKind kind = task.quest.targetGuid.IsGameObject() ? ReservationKind::GameObject : ReservationKind::Creature;
            WorldReservations::Release(kind, task.quest.targetGuid.GetRawValue(), bot->GetGUID());
            task.quest.targetGuid = ObjectGuid::Empty;
            if (task.type == WorldTaskType::QuestObjective && (task.phase == TaskPhase::Approach || task.phase == TaskPhase::Execute))
                SetPhase(bot, state, TaskPhase::Search, "paused: target released");
        }
        LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' task #{} paused ({}).", bot->GetName(), task.id, why);
    }

    void ResumeTask(Player* bot, BrainState& state)
    {
        WorldTask& task = state.task;
        if (!task.paused)
            return;
        uint32 length = task.Resume(NowMs());
        LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' task #{} resumed after {}s paused; clocks moved on by as much.",
            bot->GetName(), task.id, length / IN_MILLISECONDS);
    }

    uint32 PhaseBudgetMs(BrainState const& state)
    {
        switch (state.task.type)
        {
            case WorldTaskType::QuestObjective:
                return ObjectiveCommon::PhaseBudgetMs(state, WorldBrainSettings::Get());
            case WorldTaskType::QuestAccept:
            case WorldTaskType::QuestTurnIn:
                return QuestInteraction::PhaseBudgetMs(state);
            case WorldTaskType::Travel:
                return state.task.phase == TaskPhase::TravelToArea ? WorldBrainSettings::Get().travelTimeoutMs : 0;
            default:
                return 0;
        }
    }

    void SyncIncoming(Player* bot, BrainState const& state)
    {
        WorldTask const& task = state.task;
        if (task.CountsAsIncoming())
            PopulationHeatmap::SetIncoming(bot->GetGUID(), task.mapId, task.x, task.y);
        else
            PopulationHeatmap::ClearIncoming(bot->GetGUID());
    }

    void ReleaseTask(Player* bot, BrainState& state)
    {
        BotMovement::Release(bot, MoveOwner::Quest);
        BotMovement::Release(bot, MoveOwner::Travel);
        BotMovement::ResetRequest(bot->GetGUID());
        // Every reservation a bot holds belongs to its current task.
        WorldReservations::ReleaseAll(bot->GetGUID());
        PopulationHeatmap::ClearIncoming(bot->GetGUID());
        state.task.quest.targetGuid = ObjectGuid::Empty;
    }
}
