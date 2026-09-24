/*
 * mod-coa-playerbots
 *
 * "Scout the ridge", "find the lost caravan": exploration credit comes from an area trigger. The
 * server never notices a player walking into one on its own -- the client detects it and sends
 * CMSG_AREATRIGGER -- so the bot walks into the trigger's volume (radius or box, as the core's own
 * IsInAreaTriggerRadius judges it) and then sends that same message through the same handler,
 * which runs the real distance check and AreaExploredOrEventHappens.
 *
 * A trigger that gives no credit even from inside (script-driven, phased) marks the objective as
 * unsupported for this bot instead of walking in circles around it.
 */

#include "ObjectMgr.h"
#include "ObjectiveCommon.h"
#include "ObjectiveHandlerList.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldExecutor.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>

using namespace WorldBrainInternal;

namespace
{
    constexpr uint32 TRIGGER_ATTEMPTS = 2;

    class ExploreObjectiveHandler final : public IQuestObjectiveHandler
    {
    public:
        char const* Name() const override { return "explore"; }

        bool CanHandle(ObjectiveDef const& def) const override
        {
            return def.type == ObjectiveType::Explore && def.supported;
        }

        ObjectiveResult Update(ObjectiveContext& ctx) override
        {
            WorldTask& task = ctx.task;
            AreaTrigger const* trigger = sObjectMgr->GetAreaTrigger(task.quest.areaTriggerId);
            if (!trigger || trigger->map != ctx.bot->GetMapId())
            {
                task.quest.lastFailure = FailureReason::Unsupported;
                return ObjectiveResult::Failed;
            }

            switch (task.phase)
            {
                case TaskPhase::Planning:
                case TaskPhase::Recover:
                case TaskPhase::Search:
                    SetPhase(ctx.bot, ctx.state, TaskPhase::TravelToArea, "heading for the trigger");
                    [[fallthrough]];
                case TaskPhase::TravelToArea:
                case TaskPhase::Approach:
                {
                    if (ctx.bot->IsInAreaTriggerRadius(trigger))
                    {
                        SetPhase(ctx.bot, ctx.state, TaskPhase::Execute, "inside the trigger");
                        return ObjectiveResult::Running;
                    }
                    if (PhaseElapsed(ctx.state) > ctx.cfg.travelTimeoutMs)
                    {
                        task.quest.lastFailure = FailureReason::Timeout;
                        return ObjectiveResult::Failed;
                    }
                    // Aim for the middle: a box trigger's edge is easy to miss on a slope.
                    float radius = trigger->radius > 0.0f ? std::max(1.0f, trigger->radius * 0.4f)
                        : std::max(1.0f, std::min(trigger->length, trigger->width) * 0.2f);
                    NavStatus status = WorldExecutor::TravelTo(ctx.bot, ctx.state, WorldGoalSub::Trigger, trigger->x, trigger->y,
                        trigger->z, radius, trigger->entry);
                    if (status == NavStatus::Stuck)
                    {
                        Count(ctx.state.metrics, &WorldMetrics::movementStuck);
                        task.quest.lastFailure = FailureReason::Unreachable;
                        return ObjectiveResult::Failed;
                    }
                    if (status == NavStatus::Arrived && !ctx.bot->IsInAreaTriggerRadius(trigger))
                    {
                        // Arrived by distance, but not inside by the trigger's own test (height,
                        // box orientation): the client would have sent it anyway from here.
                        SetPhase(ctx.bot, ctx.state, TaskPhase::Execute, "at the trigger");
                    }
                    return ObjectiveResult::Running;
                }
                case TaskPhase::Execute:
                {
                    WorldPacket packet(CMSG_AREATRIGGER, 4);
                    packet << uint32(trigger->entry);
                    ctx.bot->GetSession()->HandleAreaTriggerOpcode(packet);
                    ++task.quest.attempts;
                    task.waitUntilMs = ctx.now + 600;
                    SetPhase(ctx.bot, ctx.state, TaskPhase::Verify, "trigger sent");
                    return ObjectiveResult::Running;
                }
                case TaskPhase::Verify:
                {
                    if (ctx.now < task.waitUntilMs)
                        return ObjectiveResult::Running;
                    if (ObjectiveCommon::IsDone(ctx.bot, task.quest.questId, ctx.def))
                    {
                        Count(ctx.state.metrics, &WorldMetrics::objectiveProgress);
                        return ObjectiveResult::Completed;
                    }
                    if (task.quest.attempts >= TRIGGER_ATTEMPTS)
                    {
                        task.quest.lastFailure = FailureReason::Unsupported;
                        return ObjectiveResult::Failed;
                    }
                    SetPhase(ctx.bot, ctx.state, TaskPhase::TravelToArea, "no credit yet, stepping further in");
                    return ObjectiveResult::Running;
                }
                default:
                    return ObjectiveResult::Failed;
            }
        }
    };
}

IQuestObjectiveHandler& ExploreObjectiveHandlerInstance()
{
    static ExploreObjectiveHandler handler;
    return handler;
}
