/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: DpsEngine implementation
 */

#include "engine/DpsEngine.h"
#include "engine/ActionEvaluator.h"
#include "engine/CastGuard.h"
#include "engine/CombatContext.h"
#include "engine/CombatMovement.h"
#include "profiles/ProfileRegistry.h"
#include "BotClassRotations.h"
#include "Log.h"
#include "Player.h"
#include "SpellMgr.h"

namespace BotAI
{
    namespace
    {
        constexpr uint32 RETRY_GATE_MS = 500;
        // See BotAI.cpp's own AI_REACTION_GATE_MS comment -- real per-spell GCD is enforced by
        // ActionEvaluator::CanCast querying the engine's GlobalCooldownMgr directly (item 6 of
        // the combat-engine rework); this is only the AI's own decision-tick throttle after a
        // successful cast, not a simulated GCD.
        constexpr uint32 AI_REACTION_GATE_MS = 150;
    }

    CombatResult DpsEngine::Execute(Player* bot, Unit* target, uint32 diff, uint32& nextCastAllowedMs)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !target || !target->IsAlive())
            return CombatResult::NoAction;

        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        CombatProfile const* profile = ProfileRegistry::FindProfile(bot->getClass(), activeSpec, BotRole::Dps);
        if (!profile)
            return CombatResult::NoAction;

        if (nextCastAllowedMs > diff)
        {
            nextCastAllowedMs -= diff;
            return CombatResult::Busy;
        }
        nextCastAllowedMs = 0;

        CombatContext ctx = CombatContext::Build(bot, target);

        // Check ongoing cast
        if (CastGuard::IsCurrentlyCasting(bot))
        {
            BotAction candidate = ActionEvaluator::EvaluateBestAction(ctx, profile->abilities);
            if (candidate.IsValid() && CastGuard::ShouldInterruptCurrentCast(ctx, candidate))
                CastGuard::InterruptCurrentCast(bot);
            else
                return CombatResult::Busy;
        }

        BotAction action = ActionEvaluator::EvaluateBestAction(ctx, profile->abilities);
        if (!action.IsValid())
        {
            nextCastAllowedMs = RETRY_GATE_MS;
            return CombatResult::NoAction;
        }

        // Cast-time spell while still repositioning would guarantee SPELL_FAILED_MOVING -- defer
        // one tick instead (see CombatMovement::ReadyToCast). This is still "Busy," not
        // "NoAction": a real castable action was found, it just isn't safe to fire yet.
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(action.spellId);
        if (!CombatMovement::ReadyToCast(bot, spellInfo))
        {
            nextCastAllowedMs = RETRY_GATE_MS;
            return CombatResult::Busy;
        }

        SpellCastResult result = bot->CastSpell(action.target, action.spellId, false);
        if (result == SPELL_CAST_OK)
        {
            // Throttle only the ability that was actually cast, keyed on its root spell id (not
            // every internalThrottleMs-bearing entry in the whole profile -- see item 1 of the
            // combat-engine rework), and only now that the cast has genuinely succeeded.
            if (action.internalThrottleMs > 0 && action.rootSpellId != 0)
                ActionEvaluator::SetThrottle(bot->GetGUID(), action.rootSpellId, action.internalThrottleMs);

            nextCastAllowedMs = AI_REACTION_GATE_MS;
            LOG_INFO("module.coa-playerbots", "DataDrivenAI [DPS]: bot '{}' cast '{}' (spell {}) on '{}' [score {:.1f}].",
                bot->GetName(), action.name, action.spellId, action.target->GetName(), action.score);
            return CombatResult::Cast;
        }

        // A failed cast must not throttle the ability for its full internal cooldown -- only the
        // ordinary failure-backoff applies (item 1: a failed cast shouldn't cost the same as a
        // successful one). The tick still counts as handled (Busy, not NoAction/Cast): the
        // profile did find a valid candidate, it just failed for a transient reason (range/LoS/
        // interrupted) -- falling through to the legacy chain would likely hit the same wall.
        BotAI::RecordSpellCastFailure(bot->GetGUID(), action.spellId);
        nextCastAllowedMs = RETRY_GATE_MS;
        LOG_INFO("module.coa-playerbots", "DataDrivenAI [DPS]: bot '{}' failed '{}' (spell {}) on '{}': result {}.",
            bot->GetName(), action.name, action.spellId, action.target->GetName(), static_cast<uint32>(result));
        return CombatResult::Busy;
    }
}
