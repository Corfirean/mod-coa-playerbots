/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CastGuard implementation
 */

#include "engine/CastGuard.h"
#include "engine/BotDebugLog.h"
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
    namespace
    {
        Spell* GetProtectedSpell(Player* bot)
        {
            if (!bot)
                return nullptr;

            if (Spell* generic = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            {
                // Matches the original IsNonMeleeSpellCast(withDelayed=false, ..., skipInstant=true)
                // semantics this replaced: a pushback-delayed cast does not count as "protected" --
                // only genuinely in-flight or abandoned casts do.
                if (generic->getState() != SPELL_STATE_FINISHED && generic->getState() != SPELL_STATE_DELAYED &&
                    generic->GetCastTime() > 0)
                    return generic;
            }

            if (Spell* channel = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
                if (channel->getState() != SPELL_STATE_FINISHED)
                    return channel;

            return nullptr;
        }

        bool IsCurrentHeal(Player* bot)
        {
            Spell* spell = GetProtectedSpell(bot);
            return spell && IsUsableHealSpell(spell->GetSpellInfo());
        }
    }

    bool CastGuard::IsCurrentlyCasting(Player* bot)
    {
        return GetProtectedSpell(bot) != nullptr;
    }

    // A generic cast whose remaining time has sat at (clamped) zero for longer than this is
    // abandoned: Spell::update() clamps m_timer to 0 and calls cast()/finish() the same tick it
    // reaches zero in the normal case, so a Spell* that is still non-finished with 0ms remaining
    // this long means the core failed to finish it (interrupted-but-not-cleared, a failed
    // LoS/target recheck, etc.) -- confirmed live as the root cause of a bot freezing indefinitely
    // (no attack, no cast, no movement) while a target keeps hitting it, since IsNonMeleeSpellCast
    // has no timeout of its own and reports "casting" as long as the Spell* is non-null and
    // non-finished.
    constexpr uint32 STALE_CAST_GRACE_MS = 1500;

    bool CastGuard::ForceClearIfStaleCast(Player* bot, uint32 heldZeroMs)
    {
        if (heldZeroMs <= STALE_CAST_GRACE_MS)
            return false;

        Spell* spell = GetProtectedSpell(bot);
        if (!spell)
            return false;

        LOG_ERROR(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
            "CastGuard: bot '{}' force-cleared a stale generic/channeled "
            "cast (spell {}) that sat at 0ms remaining for {}ms without the core ever finishing it.",
            bot->GetName(), spell->GetSpellInfo()->Id, heldZeroMs);
        bot->InterruptSpell(CURRENT_GENERIC_SPELL, false, true, true);
        bot->InterruptSpell(CURRENT_CHANNELED_SPELL, true, true, true);
        return true;
    }

    uint32 CastGuard::CurrentSpellId(Player* bot)
    {
        Spell* spell = GetProtectedSpell(bot);
        return spell ? spell->GetSpellInfo()->Id : 0;
    }

    uint32 CastGuard::CurrentSpellRemainingMs(Player* bot)
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
        {
            LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
                "CastGuard: bot '{}' preemption = LethalGroundHazard.", bot->GetName());
            return CastInterruptReason::LethalGroundHazard;
        }
        if (BotAvoidance::HasCriticalBossMechanic(bot, ctx.victim))
        {
            LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
                "CastGuard: bot '{}' preemption = LethalBossMechanic.", bot->GetName());
            return CastInterruptReason::LethalBossMechanic;
        }

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
            {
                LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
                    "CastGuard: bot '{}' preemption = EmergencyTankSave (target '{}', {:.1f}% HP).",
                    bot->GetName(), criticalTarget->GetName(), criticalHp);
                return CastInterruptReason::EmergencyTankSave;
            }
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
            {
                LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
                    "CastGuard: bot '{}' preemption = HighPriorityInterrupt (victim '{}', {}ms left on its cast).",
                    bot->GetName(), ctx.victim->GetName(), enemyRemainingMs);
                return CastInterruptReason::HighPriorityInterrupt;
            }
        }

        // Taunt preemption is reserved for a boss/elite or a genuinely endangered non-tank.
        if (ctx.role == BotRole::Tank && ctx.victimTargetingNonTank && ctx.victim)
        {
            Unit* currentTarget = ctx.victim->GetVictim();
            bool urgent = ctx.targetIsBossOrElite || (currentTarget && currentTarget->GetHealthPct() < 50.0f);
            if (urgent && SelectTauntSpell(bot, ctx.victim) != 0)
            {
                LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
                    "CastGuard: bot '{}' preemption = CriticalTaunt (victim '{}').", bot->GetName(), ctx.victim->GetName());
                return CastInterruptReason::CriticalTaunt;
            }
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
        LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
            "CastGuard: bot '{}' deliberately interrupted spell {} with {}ms remaining: {}.",
            bot->GetName(), spellId, remainingMs, str);

        // Do not cancel CURRENT_AUTOREPEAT_SPELL here. It is not the cast we own and the core
        // will suspend/resume it as appropriate around the replacement action.
        if (bot->GetCurrentSpell(CURRENT_GENERIC_SPELL))
            bot->InterruptSpell(CURRENT_GENERIC_SPELL, false, true, true);
        if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            bot->InterruptSpell(CURRENT_CHANNELED_SPELL, true, true, true);
    }
}
