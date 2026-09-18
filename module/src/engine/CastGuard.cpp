/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CastGuard implementation
 */

#include "engine/CastGuard.h"
#include "engine/ActionEvaluator.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    bool CastGuard::IsCurrentlyCasting(Player* bot)
    {
        return bot && bot->IsNonMeleeSpellCast(false);
    }

    bool CastGuard::ShouldInterruptCurrentCast(CombatContext const& ctx, BotAction const& pendingAction)
    {
        if (!ctx.bot || !ctx.isCasting || !pendingAction.IsValid())
            return false;

        // Emergency heal condition: ally in critical danger (< 30%) and pending action is an emergency save
        if (HasTag(pendingAction.tags, AbilityTag::EmergencyHeal) && ctx.lowestAllyHpPct < 30.0f)
            return true;

        // Kick / interrupt condition: enemy casting an interruptible spell
        if (HasTag(pendingAction.tags, AbilityTag::Interrupt) && ctx.victimIsCastingInterruptible)
            return true;

        // Urgent taunt condition: mob hitting non-tank
        if (HasTag(pendingAction.tags, AbilityTag::Taunt) && ctx.victimTargetingNonTank)
            return true;

        return false;
    }

    void CastGuard::InterruptCurrentCast(Player* bot)
    {
        if (bot)
            bot->InterruptNonMeleeSpells(false);
    }
}
