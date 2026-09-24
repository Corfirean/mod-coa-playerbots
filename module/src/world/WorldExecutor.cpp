#include "WorldExecutor.h"
#include "BotAI.h"
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
        NavStatus status = BotMovement::Navigate(bot, MoveOwner::Quest, goal, x, y, z, radius);

        if (MovementRequest const* after = BotMovement::GetRequest(bot->GetGUID()); after && after->progress.stage > stageBefore)
            Count(state.metrics, &WorldMetrics::travelRetries);
        return status;
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
        BotMovement::ResetRequest(bot->GetGUID());
        // Every reservation a bot holds belongs to its current task.
        WorldReservations::ReleaseAll(bot->GetGUID());
        PopulationHeatmap::ClearIncoming(bot->GetGUID());
        state.task.quest.targetGuid = ObjectGuid::Empty;
    }
}
