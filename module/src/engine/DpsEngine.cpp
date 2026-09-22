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
#include "Timer.h"
#include <unordered_map>

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

        // [botGuid] -> absolute getMSTime() this engine may next bother recomputing
        // EvaluateBestAction after finding nothing castable. Strictly internal: never surfaced to
        // the caller via nextCastAllowedMs (item 1, Phase 2 fixup) -- NoAction must let the
        // caller fall through to the legacy chain on the very same tick it happens, every time.
        // This only paces how often *this engine itself* re-scans a profile that's genuinely
        // found nothing for a while, so a hopeless profile doesn't get rescored every world tick.
        std::unordered_map<ObjectGuid, uint32> s_noActionRetryAt;
    }

    CombatResult DpsEngine::Execute(Player* bot, CombatContext const& ctx, BotRole profileRole, uint32 diff, uint32& nextCastAllowedMs)
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

        // Check ongoing cast -- always allowed to re-evaluate for a higher-priority interrupt,
        // regardless of the internal no-action recompute gate below (that gate only paces "found
        // nothing to do" re-attempts, not "am I still doing the right thing").
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
            // NoAction (item 1): nextCastAllowedMs stays untouched -- the caller must be free to
            // try the legacy chain immediately. Only this engine's own internal gate is set.
            s_noActionRetryAt[botGuid] = now + RETRY_GATE_MS;
            return CombatResult::NoAction;
        }
        s_noActionRetryAt.erase(botGuid);

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

    void DpsEngine::ForgetBot(ObjectGuid botGuid)
    {
        s_noActionRetryAt.erase(botGuid);
    }
}
