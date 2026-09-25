/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CastGuard implementation
 */

#include "engine/CastGuard.h"
#include "engine/CombatContext.h"
#include "engine/CombatReservations.h"
#include "engine/SpellPredicates.h"
#include "BotAvoidance.h"
#include "Log.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include <algorithm>

namespace BotAI
{
    bool CastGuard::IsCurrentlyCasting(Player const* bot)
    {
        if (!bot)
            return false;

        // withDelayed = false, skipChanneled = false, skipAutorepeat = true, isAutoshoot = false, skipInstant = true
        return bot->IsNonMeleeSpellCast(false, false, true, false, true);
    }

    namespace
    {
        Spell* GetProtectedSpell(Player const* bot)
        {
            if (!bot)
                return nullptr;

            if (Spell* generic = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                if (generic->getState() != SPELL_STATE_FINISHED && generic->GetCastTime() > 0)
                    return generic;

            if (Spell* channel = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
                if (channel->getState() != SPELL_STATE_FINISHED)
                    return channel;

            return nullptr;
        }

        bool IsCurrentHeal(Player const* bot)
        {
            Spell* spell = GetProtectedSpell(bot);
            return spell && IsUsableHealSpell(spell->GetSpellInfo());
        }
    }

    uint32 CastGuard::CurrentSpellId(Player const* bot)
    {
        Spell* spell = GetProtectedSpell(bot);
        return spell ? spell->GetSpellInfo()->Id : 0;
    }

    uint32 CastGuard::CurrentSpellRemainingMs(Player const* bot)
    {
        Spell* spell = GetProtectedSpell(bot);
        return spell ? static_cast<uint32>(std::max<int32>(0, spell->GetCastTimeRemaining())) : 0;
    }

    CastInterruptReason CastGuard::EvaluatePreemption(Player* bot, CombatContext const& ctx)
    {
        if (!bot || !IsCurrentlyCasting(bot))
            return CastInterruptReason::None;

        // Only critical movement owns the right to break the atomic cast. Ordinary hostile
        // puddles and formation/facing corrections wait until the cast naturally releases.
        if (BotAvoidance::GetGroundHazardSeverity(bot) == BotAvoidance::GroundHazardSeverity::Critical)
            return CastInterruptReason::LethalGroundHazard;
        if (BotAvoidance::HasCriticalBossMechanic(bot, ctx.victim))
            return CastInterruptReason::LethalBossMechanic;

        // Do not turn every low-health snapshot into a cancel loop. There must be a living ally
        // in immediate danger, an actually castable heal, and the current cast must not already
        // be a nearly-complete heal.
        if (ctx.role == BotRole::Healer)
        {
            Player* criticalTarget = nullptr;
            float criticalHp = 100.0f;
            if (ctx.tankAlly && ctx.tankAllyHpPct < criticalHp)
            {
                criticalTarget = ctx.tankAlly;
                criticalHp = ctx.tankAllyHpPct;
            }
            if (ctx.lowestAlly && ctx.lowestAllyHpPct < criticalHp)
            {
                criticalTarget = ctx.lowestAlly;
                criticalHp = ctx.lowestAllyHpPct;
            }

            bool immediateDanger = criticalTarget && (criticalHp < 15.0f ||
                (criticalHp < 25.0f && !criticalTarget->getAttackers().empty()));
            if (immediateDanger && SelectHealSpell(bot, criticalTarget) != 0 &&
                !(IsCurrentHeal(bot) && CurrentSpellRemainingMs(bot) <= 800))
                return CastInterruptReason::EmergencyTankSave;
        }

        // Interrupt only when it is both important and still feasible. A cast with <=150 ms left
        // cannot be reacted to reliably; a nearly-finished own cast is preserved when it can land
        // and still leave a reaction window before the hostile cast completes.
        if (ctx.victim && ctx.victimIsCastingInterruptible && !CombatReservations::IsInterruptReserved(ctx.victim->GetGUID(), bot->GetGUID()))
        {
            uint32 enemyRemainingMs = ctx.victimCastFinishTimeMs > ctx.currentMSTime
                ? ctx.victimCastFinishTimeMs - ctx.currentMSTime : 0;
            uint32 ownRemainingMs = CurrentSpellRemainingMs(bot);
            Unit* enemyTarget = ctx.victim->GetVictim();
            bool dangerousTarget = ctx.targetIsBossOrElite || enemyTarget == bot ||
                (enemyTarget && enemyTarget->GetHealthPct() < 40.0f);
            bool ownCastCanFinishFirst = ownRemainingMs <= 250 && enemyRemainingMs > ownRemainingMs + 200;
            if (dangerousTarget && enemyRemainingMs > 150 && !ownCastCanFinishFirst &&
                SelectInterruptSpell(bot, ctx.victim) != 0)
                return CastInterruptReason::HighPriorityInterrupt;
        }

        // Taunt preemption is reserved for a boss/elite or a genuinely endangered non-tank.
        if (ctx.role == BotRole::Tank && ctx.victimTargetingNonTank && ctx.victim)
        {
            Unit* currentTarget = ctx.victim->GetVictim();
            bool urgent = ctx.targetIsBossOrElite || (currentTarget && currentTarget->GetHealthPct() < 50.0f);
            if (urgent && SelectTauntSpell(bot, ctx.victim) != 0)
                return CastInterruptReason::CriticalTaunt;
        }

        return CastInterruptReason::None;
    }

    void CastGuard::InterruptCurrentCast(Player* bot, CastInterruptReason reason)
    {
        if (!bot || reason == CastInterruptReason::None)
            return;

        char const* str = "unknown";
        switch (reason)
        {
            case CastInterruptReason::LethalGroundHazard:    str = "lethal ground hazard"; break;
            case CastInterruptReason::LethalBossMechanic:    str = "lethal boss mechanic"; break;
            case CastInterruptReason::EmergencyTankSave:     str = "emergency tank/ally save"; break;
            case CastInterruptReason::HighPriorityInterrupt:  str = "high-priority interrupt"; break;
            case CastInterruptReason::CriticalTaunt:         str = "critical taunt"; break;
            case CastInterruptReason::ManualOverride:        str = "manual override"; break;
            default: break;
        }

        uint32 spellId = CurrentSpellId(bot);
        uint32 remainingMs = CurrentSpellRemainingMs(bot);
        LOG_DEBUG("module.coa-playerbots", "CastGuard: bot '{}' deliberately interrupted spell {} with {}ms remaining: {}.",
            bot->GetName(), spellId, remainingMs, str);

        // Do not cancel CURRENT_AUTOREPEAT_SPELL here. It is not the cast we own and the core
        // will suspend/resume it as appropriate around the replacement action.
        if (bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            bot->InterruptSpell(CURRENT_GENERIC_SPELL, false, true, true);
        if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            bot->InterruptSpell(CURRENT_CHANNELED_SPELL, true, true, true);
    }
}
