#include "WorldExecutor.h"
#include "BotAI.h"
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
        uint8 retriesBefore = before ? before->retries : 0;

        uint64 goal = state.task.GoalId(sub) + (goalSalt << 40);
        NavStatus status = BotMovement::Navigate(bot, MoveOwner::Quest, goal, x, y, z, radius);

        if (MovementRequest const* after = BotMovement::GetRequest(bot->GetGUID()); after && after->retries > retriesBefore)
            Count(state.metrics, &WorldMetrics::travelRetries);

        if (status == NavStatus::Moving)
            PopulationHeatmap::SetIncoming(bot->GetGUID(), bot->GetMapId(), x, y);
        return status;
    }

    void Dismount(Player* bot, BrainState& state)
    {
        if (!bot->IsMounted())
            return;
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        state.lastDismountMs = NowMs();
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
