#include "ObjectTargetHandler.h"
#include "GameObject.h"
#include "ObjectAccessor.h"
#include "ObjectiveCommon.h"
#include "Player.h"
#include "WorldExecutor.h"
#include <cmath>

using namespace WorldBrainInternal;

namespace
{
    constexpr float EARLY_SCAN_SLACK = 40.0f;
    constexpr uint32 OBJECT_FAILURE_LIMIT = 5;
}

bool ObjectTargetHandler::IsUsable(ObjectiveContext const&, GameObject* go) const
{
    return go->isSpawned() && go->getLootState() == GO_READY && !go->HasGameObjectFlag(GO_FLAG_IN_USE) &&
        !go->HasGameObjectFlag(GO_FLAG_NOT_SELECTABLE);
}

float ObjectTargetHandler::UseRange(ObjectiveContext const&, GameObject* go) const
{
    return go->GetInteractionDistance();
}

bool ObjectTargetHandler::InUseRange(ObjectiveContext const& ctx, GameObject* go) const
{
    return go->IsWithinDistInMap(ctx.bot, UseRange(ctx, go));
}

GameObject* ObjectTargetHandler::CurrentObject(ObjectiveContext const& ctx) const
{
    if (!ctx.task.quest.targetGuid.IsGameObject())
        return nullptr;
    return ObjectAccessor::GetGameObject(*ctx.bot, ctx.task.quest.targetGuid);
}

GameObject* ObjectTargetHandler::Scan(ObjectiveContext& ctx)
{
    std::unordered_set<uint32> wanted;
    std::unordered_map<uint32, uint32> served;
    ObjectiveCommon::WantedTargets(ctx, true, wanted, served);
    return ObjectiveCommon::FindObject(ctx, wanted, [this, &ctx](GameObject* go) { return IsUsable(ctx, go); });
}

void ObjectTargetHandler::StartApproach(ObjectiveContext& ctx, GameObject* go)
{
    ctx.task.quest.targetGuid = go->GetGUID();
    ctx.task.quest.progressMark = ObjectiveCommon::TaskProgress(ctx.bot, ctx.task);
    ctx.task.wandering = false;
    LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' TargetSelected object {} ({}) for quest {} objective {} at {:.0f} yd.",
        ctx.bot->GetName(), go->GetEntry(), go->GetGUID().ToString(), ctx.task.quest.questId,
        uint32(ctx.task.quest.objectiveIndex), ctx.bot->GetDistance(go));
    SetPhase(ctx.bot, ctx.state, TaskPhase::Approach, "object reserved");
}

ObjectiveResult ObjectTargetHandler::Update(ObjectiveContext& ctx)
{
    WorldTask& task = ctx.task;

    switch (task.phase)
    {
        case TaskPhase::Planning:
        case TaskPhase::Recover:
            SetPhase(ctx.bot, ctx.state, TaskPhase::TravelToArea, "planned");
            return ObjectiveResult::Running;

        case TaskPhase::TravelToArea:
        {
            float dist = std::hypot(ctx.bot->GetPositionX() - task.x, ctx.bot->GetPositionY() - task.y);
            if (ctx.bot->GetMapId() == task.mapId && dist <= task.areaRadius + EARLY_SCAN_SLACK && ctx.now >= task.nextScanMs)
            {
                task.nextScanMs = ctx.now + RollRange(ctx.state, 0x0b5c, ctx.cfg.scanMinMs, ctx.cfg.scanMaxMs);
                if (GameObject* go = Scan(ctx))
                {
                    StartApproach(ctx, go);
                    return ObjectiveResult::Running;
                }
            }
            return ObjectiveCommon::TravelToArea(ctx);
        }

        case TaskPhase::Search:
        {
            if (!ObjectiveCommon::InArea(ctx, 45.0f))
            {
                SetPhase(ctx.bot, ctx.state, TaskPhase::TravelToArea, "drifted out of the area");
                return ObjectiveResult::Running;
            }
            if (ctx.now >= task.nextScanMs)
            {
                task.nextScanMs = ctx.now + RollRange(ctx.state, 0x0b5c, ctx.cfg.scanMinMs, ctx.cfg.scanMaxMs);
                if (GameObject* go = Scan(ctx))
                {
                    StartApproach(ctx, go);
                    return ObjectiveResult::Running;
                }
            }
            uint32 timeout = ctx.cfg.searchTimeoutMs * (50 + ctx.state.persona.patience) / 100;
            if (PhaseElapsed(ctx.state) > timeout)
                return ObjectiveCommon::FailArea(ctx, FailureReason::NoTargets);
            ObjectiveCommon::Wander(ctx);
            return ObjectiveResult::Running;
        }

        case TaskPhase::Approach:
        {
            GameObject* go = CurrentObject(ctx);
            if (!go || !IsUsable(ctx, go))
            {
                ObjectiveCommon::BeginSearch(ctx, "object gone or in use");
                return ObjectiveResult::Running;
            }
            if (PhaseElapsed(ctx.state) > ctx.cfg.approachTimeoutMs)
            {
                ObjectiveCommon::ForgetTarget(ctx, FailureReason::Timeout);
                if (task.quest.retryCount >= OBJECT_FAILURE_LIMIT)
                    return ObjectiveCommon::FailArea(ctx, FailureReason::Unreachable);
                ObjectiveCommon::BeginSearch(ctx, "approach took too long");
                return ObjectiveResult::Running;
            }
            if (!InUseRange(ctx, go))
            {
                float dist = ctx.bot->GetDistance(go);
                NavStatus status = WorldExecutor::TravelTo(ctx.bot, ctx.state, WorldGoalSub::Target, go->GetPositionX(),
                    go->GetPositionY(), go->GetPositionZ(), std::max(1.5f, UseRange(ctx, go) - 1.5f), go->GetGUID().GetCounter(),
                    dist > 60.0f);
                if (status == NavStatus::Stuck)
                {
                    Count(ctx.state.metrics, &WorldMetrics::movementStuck);
                    ObjectiveCommon::ForgetTarget(ctx, FailureReason::Unreachable);
                    if (task.quest.retryCount >= OBJECT_FAILURE_LIMIT)
                        return ObjectiveCommon::FailArea(ctx, FailureReason::Unreachable);
                    ObjectiveCommon::BeginSearch(ctx, "object unreachable");
                }
                return ObjectiveResult::Running;
            }
            SetPhase(ctx.bot, ctx.state, TaskPhase::Execute, "at the object");
            [[fallthrough]];
        }

        case TaskPhase::Execute:
        {
            GameObject* go = CurrentObject(ctx);
            if (!go || !IsUsable(ctx, go))
            {
                ObjectiveCommon::BeginSearch(ctx, "object gone before use");
                return ObjectiveResult::Running;
            }
            // Casting (opening, clicking a goober with a spell) needs the bot standing still.
            if (!ObjectiveCommon::Settle(ctx.bot, ctx.state))
                return ObjectiveResult::Running;
            ctx.bot->SetFacingToObject(go);
            return Act(ctx, go);
        }

        case TaskPhase::Loot:
        {
            if (ctx.bot->IsNonMeleeSpellCast(false))
                return ObjectiveResult::Running;
            if (GameObject* go = CurrentObject(ctx))
                Collect(ctx, go);
            if (ctx.now < task.waitUntilMs)
                return ObjectiveResult::Running;
            SetPhase(ctx.bot, ctx.state, TaskPhase::Verify, "interaction finished");
            [[fallthrough]];
        }

        case TaskPhase::Verify:
        {
            uint32 before = task.quest.dryAttempts;
            ObjectiveResult result = ObjectiveCommon::VerifyAttempt(ctx, ObjectiveCommon::DryAttemptLimit(ctx.def));
            if (result == ObjectiveResult::Running)
            {
                // An object that gave nothing is not tried again soon; one that worked is spent.
                if (task.quest.dryAttempts > before)
                    ObjectiveCommon::ForgetTarget(ctx, FailureReason::NoProgress);
                ObjectiveCommon::BeginSearch(ctx, "next object");
            }
            return result;
        }

        default:
            return ObjectiveResult::Failed;
    }
}
