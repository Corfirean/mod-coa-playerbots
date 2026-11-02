#include "engine/BotDebugLog.h"
#include "WorldExecutor.h"
#include "DangerMap.h"
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
        float distZ = std::fabs(bot->GetPositionZ() - task.z);
        if (bot->GetMapId() == task.mapId && dist <= task.areaRadius &&
            (task.z == 0.0f || distZ <= NavigateTransaction::MaxArrivalZ(task.areaRadius)))
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
        std::optional<MovementRequest> before = BotMovement::GetRequestSnapshot(bot->GetGUID());
        uint8 stageBefore = before ? before->progress.stage : 0;

        uint64 goal = state.task.GoalId(sub) + (goalSalt << 40);

        // Around places a bot of this level would not survive, when the way is long enough to matter: a route of waypoints
        // from the danger map, walked one by one. A waypoint that cannot be reached is remembered in FailureMemory and
        // skipped so the bot tries remaining safe waypoints before falling back to the direct destination.
        TravelRoute& route = state.travelRoute;
        uint64 routeKey = (uint64(state.task.id) << 32) ^ (uint64(bot->GetMapId()) << 52) ^ (uint64(uint32(std::lround(x / 8.0f))) * 0x9E3779B1ULL) ^ (uint64(uint32(std::lround(y / 8.0f))) << 20);
        uint32 now = NowMs();
        if (route.key != routeKey || (route.next == 0 && !route.points.empty() && now - route.plannedAtMs > 180000))
        {
            route = TravelRoute();
            route.key = routeKey;
            route.plannedAtMs = now;
            std::vector<DangerMap::Waypoint> planned = DangerMap::Plan(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), x, y, bot->GetLevel(), uint8(bot->GetTeamId()));
            for (DangerMap::Waypoint const& wp : planned)
            {
                if (!state.failures.Has(FailKind::NavCorridor, FailureMemory::CorridorId(bot->GetMapId(), wp.x, wp.y), now))
                    route.points.push_back(wp);
            }
            if (!route.points.empty())
                LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "Bot '{}' goes around danger toward ({:.0f}, {:.0f}) by {} waypoint(s).",
                    bot->GetName(), x, y, route.points.size());
        }

        float const bx = bot->GetPositionX();
        float const by = bot->GetPositionY();
        float const bz = bot->GetPositionZ();
        float const totalDist = std::max(1.0f, std::hypot(x - bx, y - by));

        NavStatus status = NavStatus::Moving;
        for (int pass = 0; pass < 3; ++pass)
        {
            bool viaWaypoint = !route.disabled && route.next < route.points.size();
            float tx = viaWaypoint ? route.points[route.next].x : x;
            float ty = viaWaypoint ? route.points[route.next].y : y;
            float frac = viaWaypoint ? std::clamp(std::hypot(tx - bx, ty - by) / totalDist, 0.0f, 1.0f) : 1.0f;
            float tz = viaWaypoint ? (bz + (z - bz) * frac) : z;
            float tr = viaWaypoint ? 20.0f : radius;
            uint64 leg = viaWaypoint ? goal + ((uint64(route.next) + 1) << 56) : goal;

            status = BotMovement::Navigate(bot, OwnerFor(state.task), leg, tx, ty, tz, tr, &state.travelToken, allowMount);
            std::optional<MovementRequest> reqSnap = BotMovement::GetRequestSnapshot(bot->GetGUID());
            NavFailureKind const failureKind = reqSnap ? reqSnap->lastFailureKind : NavFailureKind::None;
            uint32 phaseShiftMs = 0;
            WaypointStepAction const action = TravelWaypointPolicy::EvaluateStep(
                status, failureKind, route.blockedSinceMs, now, phaseShiftMs);
            if (phaseShiftMs > 0)
                state.task.ShiftPhaseClock(phaseShiftMs);

            if (!viaWaypoint)
                break;
            if (action == WaypointStepAction::AdvanceArrived)
            {
                ++route.next;
                status = NavStatus::Moving;
                continue;
            }
            if (action == WaypointStepAction::SkipStuckWaypoint)
            {
                state.failures.Remember(FailKind::NavCorridor, FailureMemory::CorridorId(bot->GetMapId(), tx, ty), now,
                    WorldBrainSettings::Get().clusterFailMs, uint8(FailureReason::Unreachable));
                Count(state.metrics, &WorldMetrics::dangerWaypointsSkipped);
                ++route.next;
                if (route.next >= route.points.size())
                    route.disabled = true;
                BotMovement::ResetRequest(bot->GetGUID());
                status = NavStatus::Moving;
                continue;
            }
            break;
        }

        std::optional<MovementRequest> after = BotMovement::GetRequestSnapshot(bot->GetGUID());
        if (after && after->progress.stage > stageBefore)
            Count(state.metrics, &WorldMetrics::travelRetries);
        return status;
    }

    bool TryRequestFlight(Player* bot, BrainState& state, float x, float y, float z)
    {
        if (!BotWorldBehavior::RequestTravel(bot, bot->GetMapId(), x, y, z, false))
            return false;

        Count(state.metrics, &WorldMetrics::travels);
        if (state.travelToken.IsValid())
            BotMovement::Release(bot, state.travelToken);
        else
        {
            BotMovement::ForceReleaseOwner(bot, MoveOwner::Quest);
            BotMovement::ForceReleaseOwner(bot, MoveOwner::Travel);
        }
        state.travelToken = {};
        NoteEvent(state, Acore::StringFormat("taking a flight toward ({:.0f}, {:.0f})", x, y));
        LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "Bot '{}' requested a flight toward ({:.0f}, {:.0f}) for task {}.",
            bot->GetName(), x, y, state.task.id);
        return true;
    }

    void Dismount(Player* bot, BrainState& state)
    {
        if (!bot->IsMounted())
            return;
        BotMovement::RequestDismount(bot, OwnerFor(state.task), DismountReason::Manual);
        state.lastDismountMs = NowMs();
    }

    void PauseTask(Player* bot, BrainState& state, char const* why)
    {
        WorldTask& task = state.task;
        if (!task.IsValid() || task.paused)
            return;

        task.Pause(NowMs());
        if (state.travelToken.IsValid())
            BotMovement::Release(bot, state.travelToken);
        else
            BotMovement::ForceReleaseOwner(bot, OwnerFor(task));
        state.travelToken = {};
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
        LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "Bot '{}' task #{} paused ({}).", bot->GetName(), task.id, why);
    }

    void ResumeTask(Player* bot, BrainState& state)
    {
        WorldTask& task = state.task;
        if (!task.paused)
            return;
        uint32 length = task.Resume(NowMs());
        LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "Bot '{}' task #{} resumed after {}s paused; clocks moved on by as much.",
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
        if (state.travelToken.IsValid())
            BotMovement::Release(bot, state.travelToken);
        else
        {
            BotMovement::ForceReleaseOwner(bot, MoveOwner::Quest);
            BotMovement::ForceReleaseOwner(bot, MoveOwner::Travel);
        }
        state.travelToken = {};
        BotMovement::ResetRequest(bot->GetGUID());
        // Every reservation a bot holds belongs to its current task.
        WorldReservations::ReleaseAll(bot->GetGUID());
        PopulationHeatmap::ClearIncoming(bot->GetGUID());
        state.task.quest.targetGuid = ObjectGuid::Empty;
    }
}
