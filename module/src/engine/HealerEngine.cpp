/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: HealerEngine implementation
 */

#include "engine/HealerEngine.h"
#include "engine/ActionEvaluator.h"
#include "engine/CastGuard.h"
#include "engine/CombatContext.h"
#include "profiles/ProfileRegistry.h"
#include "BotClassRotations.h"
#include "Log.h"
#include "Player.h"

namespace BotAI
{
    namespace
    {
        constexpr uint32 APPROX_GCD_MS = 1500;
        constexpr uint32 RETRY_GATE_MS = 500;
    }

    bool HealerEngine::Execute(Player* bot, uint32 diff, uint32& nextCastAllowedMs)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        CombatProfile const* profile = ProfileRegistry::FindProfile(bot->getClass(), activeSpec, BotRole::Healer);
        if (!profile)
            return false;

        if (nextCastAllowedMs > diff)
        {
            nextCastAllowedMs -= diff;
            return true;
        }
        nextCastAllowedMs = 0;

        CombatContext ctx = CombatContext::Build(bot);

        // Check ongoing cast
        if (CastGuard::IsCurrentlyCasting(bot))
        {
            BotAction candidate = ActionEvaluator::EvaluateBestAction(ctx, profile->abilities);
            if (candidate.IsValid() && CastGuard::ShouldInterruptCurrentCast(ctx, candidate))
            {
                CastGuard::InterruptCurrentCast(bot);
            }
            else
            {
                return true; // Let existing cast finish
            }
        }

        BotAction action = ActionEvaluator::EvaluateBestAction(ctx, profile->abilities);
        if (action.IsValid())
        {
            // Apply internal throttle if descriptor specifies it
            for (AbilityDescriptor const& desc : profile->abilities)
            {
                if (desc.rootSpellId != 0 && desc.internalThrottleMs > 0)
                {
                    ActionEvaluator::SetThrottle(bot->GetGUID(), desc.rootSpellId, desc.internalThrottleMs);
                }
            }

            SpellCastResult result = bot->CastSpell(action.target, action.spellId, false);
            if (result == SPELL_CAST_OK)
            {
                nextCastAllowedMs = APPROX_GCD_MS;
                LOG_INFO("module.coa-playerbots", "DataDrivenAI [Healer]: bot '{}' cast '{}' (spell {}) on '{}' [score {:.1f}].",
                    bot->GetName(), action.name, action.spellId, action.target->GetName(), action.score);
            }
            else
            {
                BotAI::RecordSpellCastFailure(bot->GetGUID(), action.spellId);
                nextCastAllowedMs = RETRY_GATE_MS;
                LOG_INFO("module.coa-playerbots", "DataDrivenAI [Healer]: bot '{}' failed '{}' (spell {}) on '{}': result {}.",
                    bot->GetName(), action.name, action.spellId, action.target->GetName(), static_cast<uint32>(result));
            }
            return true;
        }

        nextCastAllowedMs = RETRY_GATE_MS;
        return true;
    }
}
