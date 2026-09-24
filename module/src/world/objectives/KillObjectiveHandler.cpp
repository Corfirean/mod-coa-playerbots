/*
 * mod-coa-playerbots
 *
 * "Kill N of X" (including kill-credit proxies). Engaging is one Attack() call: the combat engine
 * fights, the handler waits for the bot to come out of combat, lets the loot queue empty, reads
 * the quest counter, and picks the next live target.
 *
 * Some quests name a hostile mob but only credit a quest item used on it ("use the Blessed Torch
 * on a Zombie"). The knowledge base can't always tell those apart from plain kill quests, so the
 * handler finds out the honest way: when several kills in a row advance nothing and the quest
 * handed out a usable item, it switches to using the item on the target instead.
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
            return def.type == ObjectiveType::KillCreature;
        }

    protected:
        bool HostileOnly(ObjectiveContext const& ctx) const override
        {
            return !ctx.task.quest.useItemMode;
        }

        bool WantDead(ObjectiveContext const& ctx) const override
        {
            return ctx.task.quest.useItemMode && ctx.task.quest.requireDeadTarget;
        }

        float EngageRange(ObjectiveContext const& ctx) const override
        {
            return ctx.task.quest.useItemMode ? ItemUse::ItemRange(ctx.def) : ctx.cfg.engageRange;
        }

        ObjectiveResult Execute(ObjectiveContext& ctx, Creature* target) override
        {
            if (ctx.task.quest.useItemMode)
                return ExecuteItemUse(ctx, target);

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

        bool OnDryLimit(ObjectiveContext& ctx) override
        {
            if (ctx.task.quest.useItemMode || !ctx.def.castItemId || !ctx.bot->GetItemByEntry(ctx.def.castItemId))
                return false;
            ctx.task.quest.useItemMode = true;
            ctx.task.quest.dryAttempts = 0;
            NoteEvent(ctx.state, Acore::StringFormat("kills give no credit for quest {}, using quest item {} instead",
                ctx.task.quest.questId, ctx.def.castItemId));
            return true;
        }
    };
}

IQuestObjectiveHandler& KillObjectiveHandlerInstance()
{
    static KillObjectiveHandler handler;
    return handler;
}
