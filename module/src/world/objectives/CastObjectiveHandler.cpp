/*
 * mod-coa-playerbots
 *
 * "Use the Soothing Balm on 6 wounded soldiers": the quest hands out an item whose use spell
 * credits the target. Same live search and approach as a kill, but Execute uses the item on the
 * target (through the item's own spell, charges and all) instead of attacking it, and follows the
 * spell's own feedback when it wants a corpse rather than a live target.
 */

#include "CreatureTargetHandler.h"
#include "GameObject.h"
#include "ObjectTargetHandler.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Creature.h"
#include "ObjectiveCommon.h"
#include "ObjectiveHandlerList.h"
#include "Player.h"

using namespace WorldBrainInternal;

namespace
{
    class CastObjectiveHandler final : public CreatureTargetHandler
    {
    public:
        char const* Name() const override { return "use-item-on"; }

        bool CanHandle(ObjectiveDef const& def) const override
        {
            return def.type == ObjectiveType::CastOnCreature && def.supported && def.castSpellId;
        }

    protected:
        bool HostileOnly(ObjectiveContext const&) const override { return false; }

        bool WantDead(ObjectiveContext const& ctx) const override
        {
            return ctx.task.quest.requireDeadTarget;
        }

        float EngageRange(ObjectiveContext const& ctx) const override
        {
            return ItemUse::ItemRange(ctx.def);
        }

        ObjectiveResult Execute(ObjectiveContext& ctx, Creature* target) override
        {
            return ExecuteItemUse(ctx, target);
        }
    };
}

IQuestObjectiveHandler& CastObjectiveHandlerInstance()
{
    static CastObjectiveHandler handler;
    return handler;
}

namespace
{
    // Mirrors ObjectTargetHandler.cpp's own OBJECT_FAILURE_LIMIT (not exported across
    // translation units). ObjectTargetHandler::Update only ever compares retryCount to that limit
    // for its own Approach-phase failures (unreachable/timeout); a cast this handler's own Act()
    // refuses over and over -- wrong conditions, a spell that never succeeds on this object -- was
    // free to grow retryCount forever without ever being checked against it, so the objective just
    // kept retrying instead of failing cleanly.
    constexpr uint32 CAST_FAILURE_LIMIT = 5;

    // "Use the torch on the tents": the quest item's spell acts on an object. Same flow as any
    // object objective, the interaction is the item's spell cast on it.
    class CastOnObjectObjectiveHandler final : public ObjectTargetHandler
    {
    public:
        char const* Name() const override { return "use-item-on-object"; }

        bool CanHandle(ObjectiveDef const& def) const override
        {
            return def.type == ObjectiveType::CastOnGameObject && def.supported && def.castSpellId;
        }

    protected:
        bool IsUsable(ObjectiveContext const&, GameObject* go) const override
        {
            return go->isSpawned();
        }

        float UseRange(ObjectiveContext const& ctx, GameObject*) const override
        {
            return ItemUse::ItemRange(ctx.def);
        }

        ObjectiveResult Act(ObjectiveContext& ctx, GameObject* go) override
        {
            switch (ItemUse::CastQuestItem(ctx, go))
            {
                case ItemUse::CastOutcome::Casting:
                {
                    ++ctx.task.quest.attempts;
                    SpellInfo const* spell = sSpellMgr->GetSpellInfo(ctx.def.castSpellId);
                    ctx.task.waitUntilMs = ctx.now + (spell ? spell->CalcCastTime(ctx.bot) : 0) + 400;
                    SetPhase(ctx.bot, ctx.state, TaskPhase::Loot, "quest item used on object");
                    return ObjectiveResult::Running;
                }
                case ItemUse::CastOutcome::Settling:
                    return ObjectiveResult::Running;
                case ItemUse::CastOutcome::NoItem:
                    ctx.task.quest.lastFailure = FailureReason::Unsupported;
                    return ObjectiveResult::Failed;
                default:
                    ObjectiveCommon::ForgetTarget(ctx, FailureReason::CastFailed);
                    if (ctx.task.quest.retryCount >= CAST_FAILURE_LIMIT)
                    {
                        ctx.task.quest.lastFailure = FailureReason::CastFailed;
                        return ObjectiveResult::Failed;
                    }
                    ObjectiveCommon::BeginSearch(ctx, "item use refused");
                    return ObjectiveResult::Running;
            }
        }
    };
}

IQuestObjectiveHandler& CastOnObjectObjectiveHandlerInstance()
{
    static CastOnObjectObjectiveHandler handler;
    return handler;
}
