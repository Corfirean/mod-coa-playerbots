/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: TankEngine implementation
 */

#include "engine/TankEngine.h"
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
        constexpr uint32 AI_REACTION_GATE_MS = 150;
    }

    CombatResult TankEngine::Execute(Player* bot, Unit* target, uint32 diff, uint32& nextCastAllowedMs)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !target || !target->IsAlive())
            return CombatResult::NoAction;

        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        CombatProfile const* profile = ProfileRegistry::FindProfile(bot->getClass(), activeSpec, BotRole::Tank);
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

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(action.spellId);
        if (!CombatMovement::ReadyToCast(bot, spellInfo))
        {
            nextCastAllowedMs = RETRY_GATE_MS;
            return CombatResult::Busy;
        }

        SpellCastResult result = bot->CastSpell(action.target, action.spellId, false);
        if (result == SPELL_CAST_OK)
        {
            if (action.internalThrottleMs > 0 && action.rootSpellId != 0)
                ActionEvaluator::SetThrottle(bot->GetGUID(), action.rootSpellId, action.internalThrottleMs);

            nextCastAllowedMs = AI_REACTION_GATE_MS;
            LOG_INFO("module.coa-playerbots", "DataDrivenAI [Tank]: bot '{}' cast '{}' (spell {}) on '{}' [score {:.1f}].",
                bot->GetName(), action.name, action.spellId, action.target->GetName(), action.score);
            return CombatResult::Cast;
        }

        BotAI::RecordSpellCastFailure(bot->GetGUID(), action.spellId);
        nextCastAllowedMs = RETRY_GATE_MS;
        LOG_INFO("module.coa-playerbots", "DataDrivenAI [Tank]: bot '{}' failed '{}' (spell {}) on '{}': result {}.",
            bot->GetName(), action.name, action.spellId, action.target->GetName(), static_cast<uint32>(result));
        return CombatResult::Busy;
    }
}
