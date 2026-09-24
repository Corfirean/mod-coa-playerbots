/*
 * mod-coa-playerbots
 *
 * "Kill N of X" (including kill-credit proxies). Engaging is one Attack() call: the combat engine
 * fights, the handler waits for the bot to come out of combat, lets the loot queue empty, reads
 * the quest counter, and picks the next live target.
 */

#include "CreatureTargetHandler.h"
#include "Creature.h"
#include "ObjectiveCommon.h"
#include "ObjectiveHandlerList.h"
#include "Player.h"

using namespace WorldBrainInternal;

namespace
{
    class KillObjectiveHandler final : public CreatureTargetHandler
    {
    public:
        char const* Name() const override { return "kill"; }

        bool CanHandle(ObjectiveDef const& def) const override
        {
            return def.type == ObjectiveType::KillCreature && def.supported;
        }

    protected:
        bool HostileOnly(ObjectiveContext const&) const override { return true; }

        ObjectiveResult Execute(ObjectiveContext& ctx, Creature* target) override
        {
            if (!ctx.bot->Attack(target, true))
            {
                ObjectiveCommon::ForgetTarget(ctx, FailureReason::InteractFailed);
                ObjectiveCommon::BeginSearch(ctx, "attack refused");
                return ObjectiveResult::Running;
            }

            ++ctx.task.quest.attempts;
            Count(ctx.state.metrics, &WorldMetrics::killTargetsSelected);
            SetPhase(ctx.bot, ctx.state, TaskPhase::Combat, "engaged");
            return ObjectiveResult::Running;
        }
    };
}

IQuestObjectiveHandler& KillObjectiveHandlerInstance()
{
    static KillObjectiveHandler handler;
    return handler;
}
