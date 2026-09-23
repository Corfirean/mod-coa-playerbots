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
#include "engine/SpecStrategyRegistry.h"
#include "profiles/ProfileRegistry.h"
#include "BotClassRotations.h"
#include "Log.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Unit.h"
#include <unordered_map>

namespace BotAI
{
    namespace
    {
        constexpr uint32 RETRY_GATE_MS = 500;
        constexpr uint32 AI_REACTION_GATE_MS = 150;

        // See DpsEngine.cpp's own comment on this pattern (item 1, Phase 2 fixup).
        std::unordered_map<ObjectGuid, uint32> s_noActionRetryAt;

        // Only these tags represent an actual incoming heal worth coordinating across healers
        // (item 3, Phase 2 fixup) -- a damage spell, buff, utility cast, or offensive cooldown a
        // Healer profile might pick must never fake a heal reservation. Shield is deliberately
        // excluded too: an absorb isn't incoming *healing*, and coordinating shields (if it's
        // ever needed) belongs in its own reservation model, not piggybacked on this one as a
        // fake heal.
        constexpr AbilityTag HEALING_RESERVATION_TAGS =
            AbilityTag::EmergencyHeal | AbilityTag::DirectHeal | AbilityTag::PeriodicHeal | AbilityTag::AoEHeal;
    }

    CombatResult HealerEngine::Execute(Player* bot, CombatContext const& ctx, uint32 diff, uint32& nextCastAllowedMs)
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

        // Check ongoing cast
        if (CastGuard::IsCurrentlyCasting(bot))
        {
            BotAction candidate = ActionEvaluator::EvaluateBestAction(ctx, profile->abilities);
            if (candidate.IsValid() && CastGuard::ShouldInterruptCurrentCast(ctx, candidate))
                CastGuard::InterruptCurrentCast(bot);
            else
                return CombatResult::Busy; // Let existing cast finish
        }

        ObjectGuid botGuid = bot->GetGUID();
        uint32 now = getMSTime();
        auto retryItr = s_noActionRetryAt.find(botGuid);
        if (retryItr != s_noActionRetryAt.end() && now < retryItr->second)
            return CombatResult::NoAction;

        BotAction action = ActionEvaluator::EvaluateBestAction(ctx, profile->abilities);
        if (!action.IsValid())
        {
            // NoAction (item 1): nextCastAllowedMs stays untouched -- the caller must be free to
            // try the legacy heal rotation immediately, not wait out a timer this engine set.
            s_noActionRetryAt[botGuid] = now + RETRY_GATE_MS;
            return CombatResult::NoAction;
        }
        s_noActionRetryAt.erase(botGuid);

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(action.spellId);
        if (!CombatMovement::ReadyToCast(bot, spellInfo))
        {
            nextCastAllowedMs = RETRY_GATE_MS;
            return CombatResult::Busy;
        }

        // Heal reservation (items 2/3, Phase 2 fixup): only for an action actually tagged as a
        // heal -- a damage/buff/utility/offensive-cooldown pick from a Healer profile must not
        // create a fake reservation. Sized off the winning ability's own tags (EmergencyHeal
        // restores more than an AoEHeal's per-target share), and tied to the spell's real cast
        // time so the reservation's lifetime actually matches how long the heal is in flight
        // (see CombatReservations::ReserveHeal's own comment on the lazy liveness check this
        // feeds -- landed/failed/interrupted/replaced casts all drop it well before any fixed
        // timeout would).
        bool isHealingAction = spellInfo && HasTag(action.tags, HEALING_RESERVATION_TAGS);
        if (isHealingAction)
        {
            float healFraction = HasTag(action.tags, AbilityTag::EmergencyHeal) ? 0.35f
                : HasTag(action.tags, AbilityTag::AoEHeal) ? 0.15f
                : 0.20f;
            uint32 expectedHeal = uint32(action.target->GetMaxHealth() * healFraction);
            uint32 castTimeMs = spellInfo->CalcCastTime(bot);
            CombatReservations::ReserveHeal(bot->GetGUID(), action.target->GetGUID(), action.spellId, expectedHeal, castTimeMs);
            LOG_INFO("module.coa-playerbots", "CombatAI: bot '{}' reserved ~{} heal on '{}' (spell {}, cast {}ms).",
                bot->GetName(), expectedHeal, action.target->GetName(), action.spellId, castTimeMs);
        }

        SpellCastResult result = bot->CastSpell(action.target, action.spellId, false);
        SpecStrategyRegistry::OnActionCastResult(bot, action, result == SPELL_CAST_OK);
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
        if (isHealingAction)
            CombatReservations::ClearHealReservation(bot->GetGUID());
        BotAI::RecordSpellCastFailure(bot->GetGUID(), action.spellId);
        nextCastAllowedMs = RETRY_GATE_MS;
        LOG_INFO("module.coa-playerbots", "DataDrivenAI [Healer]: bot '{}' failed '{}' (spell {}) on '{}': result {}.",
            bot->GetName(), action.name, action.spellId, action.target->GetName(), static_cast<uint32>(result));
        return CombatResult::Busy;
    }

    void HealerEngine::ForgetBot(ObjectGuid botGuid)
    {
        s_noActionRetryAt.erase(botGuid);
        SpecStrategyRegistry::ForgetBot(botGuid);
    }
}
