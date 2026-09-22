/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: HealerEngine implementation
 */

#include "engine/HealerEngine.h"
#include "engine/ActionEvaluator.h"
#include "engine/CastGuard.h"
#include "engine/CombatContext.h"
#include "engine/CombatMovement.h"
#include "engine/CombatReservations.h"
#include "profiles/ProfileRegistry.h"
#include "BotClassRotations.h"
#include "Log.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Unit.h"

namespace BotAI
{
    namespace
    {
        constexpr uint32 RETRY_GATE_MS = 500;
        constexpr uint32 AI_REACTION_GATE_MS = 150;
    }

    CombatResult HealerEngine::Execute(Player* bot, uint32 diff, uint32& nextCastAllowedMs)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return CombatResult::NoAction;

        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        CombatProfile const* profile = ProfileRegistry::FindProfile(bot->getClass(), activeSpec, BotRole::Healer);
        if (!profile)
            return CombatResult::NoAction;

        if (nextCastAllowedMs > diff)
        {
            nextCastAllowedMs -= diff;
            return CombatResult::Busy;
        }
        nextCastAllowedMs = 0;

        CombatContext ctx = CombatContext::Build(bot);

        // Check ongoing cast
        if (CastGuard::IsCurrentlyCasting(bot))
        {
            BotAction candidate = ActionEvaluator::EvaluateBestAction(ctx, profile->abilities);
            if (candidate.IsValid() && CastGuard::ShouldInterruptCurrentCast(ctx, candidate))
                CastGuard::InterruptCurrentCast(bot);
            else
                return CombatResult::Busy; // Let existing cast finish
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

        // Heal reservation (item 11, Phase 2): sized off the winning ability's own tags -- a real
        // EmergencyHeal restores more of a target's health than an AoEHeal's per-target share, so
        // this is a closer estimate than the legacy fallback's flat 20% guess, still deliberately
        // rough (see HealEvaluator's own comment on why exact prediction isn't the goal). Left to
        // expire on its own TTL rather than cleared right after CastSpell returns, since a
        // cast-time heal's actual effect doesn't land until the cast finishes.
        float healFraction = HasTag(action.tags, AbilityTag::EmergencyHeal) ? 0.35f
            : HasTag(action.tags, AbilityTag::AoEHeal) ? 0.15f
            : 0.20f;
        uint32 expectedHeal = uint32(action.target->GetMaxHealth() * healFraction);
        CombatReservations::ReserveHeal(bot->GetGUID(), action.target->GetGUID(), action.spellId, expectedHeal, 6000);
        LOG_INFO("module.coa-playerbots", "CombatAI: bot '{}' reserved ~{} heal on '{}' (spell {}).",
            bot->GetName(), expectedHeal, action.target->GetName(), action.spellId);

        SpellCastResult result = bot->CastSpell(action.target, action.spellId, false);
        if (result == SPELL_CAST_OK)
        {
            if (action.internalThrottleMs > 0 && action.rootSpellId != 0)
                ActionEvaluator::SetThrottle(bot->GetGUID(), action.rootSpellId, action.internalThrottleMs);

            nextCastAllowedMs = AI_REACTION_GATE_MS;
            LOG_INFO("module.coa-playerbots", "DataDrivenAI [Healer]: bot '{}' cast '{}' (spell {}) on '{}' [score {:.1f}].",
                bot->GetName(), action.name, action.spellId, action.target->GetName(), action.score);
            return CombatResult::Cast;
        }

        // Didn't actually go out -- no real heal is coming, so don't hold the reservation.
        CombatReservations::ClearHealReservation(bot->GetGUID());
        BotAI::RecordSpellCastFailure(bot->GetGUID(), action.spellId);
        nextCastAllowedMs = RETRY_GATE_MS;
        LOG_INFO("module.coa-playerbots", "DataDrivenAI [Healer]: bot '{}' failed '{}' (spell {}) on '{}': result {}.",
            bot->GetName(), action.name, action.spellId, action.target->GetName(), static_cast<uint32>(result));
        return CombatResult::Busy;
    }
}
