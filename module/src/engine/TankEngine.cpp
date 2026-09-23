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
#include "engine/SpecStrategyRegistry.h"
#include "profiles/ProfileRegistry.h"
#include "BotClassRotations.h"
#include "Log.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <unordered_map>

namespace BotAI
{
    namespace
    {
        constexpr uint32 RETRY_GATE_MS = 500;
        constexpr uint32 AI_REACTION_GATE_MS = 150;

        // See DpsEngine.cpp's own comment on this pattern (item 1, Phase 2 fixup).
        std::unordered_map<ObjectGuid, uint32> s_noActionRetryAt;
    }

    CombatResult TankEngine::Execute(Player* bot, CombatContext const& ctx, BotRole profileRole, uint32 diff, uint32& nextCastAllowedMs)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !ctx.victim || !ctx.victim->IsAlive())
            return CombatResult::NoAction;

        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        CombatProfile const* profile = ProfileRegistry::FindProfile(bot->getClass(), activeSpec, profileRole);
        if (!profile)
            return CombatResult::NoAction;

        if (nextCastAllowedMs > diff)
        {
            nextCastAllowedMs -= diff;
            return CombatResult::Busy;
        }
        nextCastAllowedMs = 0;

        if (CastGuard::IsCurrentlyCasting(bot))
        {
            BotAction candidate = ActionEvaluator::EvaluateBestAction(ctx, profile->abilities);
            if (candidate.IsValid() && CastGuard::ShouldInterruptCurrentCast(ctx, candidate))
                CastGuard::InterruptCurrentCast(bot);
            else
                return CombatResult::Busy;
        }

        ObjectGuid botGuid = bot->GetGUID();
        uint32 now = getMSTime();
        auto retryItr = s_noActionRetryAt.find(botGuid);
        if (retryItr != s_noActionRetryAt.end() && now < retryItr->second)
            return CombatResult::NoAction;

        BotAction action = ActionEvaluator::EvaluateBestAction(ctx, profile->abilities);
        if (!action.IsValid())
        {
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

        SpellCastResult result = bot->CastSpell(action.target, action.spellId, false);
        if (result == SPELL_CAST_OK)
        {
            if (action.internalThrottleMs > 0 && action.rootSpellId != 0)
                ActionEvaluator::SetThrottle(bot->GetGUID(), action.rootSpellId, action.internalThrottleMs);

            SpecStrategyRuntime& runtime = SpecStrategyRegistry::GetRuntime(bot->GetGUID());
            runtime.successfulCastsCount++;

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

    void TankEngine::ForgetBot(ObjectGuid botGuid)
    {
        s_noActionRetryAt.erase(botGuid);
        SpecStrategyRegistry::ForgetBot(botGuid);
    }
}
