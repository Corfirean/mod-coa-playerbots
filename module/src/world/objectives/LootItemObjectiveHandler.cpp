/*
 * mod-coa-playerbots
 *
 * "Bring me 10 Candles": the item drops from creatures the reverse loot index resolved (kobolds
 * for the candles), so this is the kill flow with a different yardstick -- Verify counts the item
 * in the bags, and the dry-attempt budget scales with the drop chance (a 15% drop is allowed more
 * empty kills than a 100% one before the handler decides the source is wrong).
 */

#include "CreatureTargetHandler.h"
#include "Creature.h"
#include "ObjectiveCommon.h"
#include "ObjectiveHandlerList.h"
#include "Player.h"

using namespace WorldBrainInternal;

namespace
{
    class LootItemObjectiveHandler final : public CreatureTargetHandler
    {
    public:
        char const* Name() const override { return "collect"; }

        bool CanHandle(ObjectiveDef const& def) const override
        {
            return def.type == ObjectiveType::CollectItem && def.supported && !def.targetsAreGameObjects && !def.providedByQuest;
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
            SetPhase(ctx.bot, ctx.state, TaskPhase::Combat, "engaged for loot");
            return ObjectiveResult::Running;
        }
    };
}

IQuestObjectiveHandler& LootItemObjectiveHandlerInstance()
{
    static LootItemObjectiveHandler handler;
    return handler;
}
