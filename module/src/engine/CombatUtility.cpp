/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatUtility implementation
 */

#include "engine/CombatUtility.h"
#include "engine/CastGuard.h"
#include "engine/CombatContext.h"
#include "engine/CombatMovement.h"
#include "engine/CombatReservations.h"
#include "engine/SpellPredicates.h"
#include "BotClassRotations.h"
#include "Group.h"
#include "Log.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <algorithm>

namespace BotAI
{
    namespace
    {
        constexpr uint32 UTILITY_REACTION_GATE_MS = 150;
        constexpr uint32 UTILITY_RETRY_GATE_MS = 500;
        // Floor/ceiling on how long an interrupt reservation lives -- floor covers a cast whose
        // remaining time reads as ~0 (about to finish, still worth blocking a duplicate kick for
        // one more tick), ceiling stops a stale entry (e.g. the enemy died without the reconcile
        // path running) from blocking a sibling bot for an unreasonable length of time.
        constexpr uint32 INTERRUPT_RESERVATION_MIN_MS = 500;
        constexpr uint32 INTERRUPT_RESERVATION_MAX_MS = 6000;
        // How far from the bot a groupmate is still considered for cleanse -- matches
        // CombatContext's own 40yd ally-triage scan radius, not the tighter heal-engage band
        // (a dispel is cheap/instant far more often than it's a real heal, and this layer runs
        // for every role, not just Healer).
        constexpr float CLEANSE_SEARCH_RANGE = 40.0f;

        bool TryCast(Player* bot, uint32 spellId, Unit* target, char const* verb, uint32& nextCastAllowedMs)
        {
            if (!spellId || !target)
                return false;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!CombatMovement::ReadyToCast(bot, spellInfo))
            {
                nextCastAllowedMs = UTILITY_REACTION_GATE_MS;
                return false;
            }

            SpellCastResult result = bot->CastSpell(target, spellId, false);
            if (result == SPELL_CAST_OK)
            {
                nextCastAllowedMs = UTILITY_REACTION_GATE_MS;
                LOG_INFO("module.coa-playerbots", "CombatAI: bot '{}' {} (spell {}) on '{}'.",
                    bot->GetName(), verb, spellId, target->GetName());
                return true;
            }

            BotAI::RecordSpellCastFailure(bot->GetGUID(), spellId);
            nextCastAllowedMs = UTILITY_RETRY_GATE_MS;
            return false;
        }

        // First groupmate (self included) carrying a negative, dispel-typed aura within range --
        // see IsUsableDispelSpell's own comment on why this doesn't try to match the specific
        // DispelType against what the bot's known spell actually removes.
        Unit* FindDispellableAlly(Player* bot)
        {
            auto hasDispellableDebuff = [](Unit* unit) -> bool
            {
                for (auto const& pair : unit->GetAppliedAuras())
                {
                    AuraApplication const* app = pair.second;
                    if (!app || app->IsPositive())
                        continue;
                    Aura const* aura = app->GetBase();
                    SpellInfo const* si = aura ? aura->GetSpellInfo() : nullptr;
                    if (si && si->Dispel != DISPEL_NONE)
                        return true;
                }
                return false;
            };

            if (Group* group = bot->GetGroup())
            {
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                {
                    Player* member = ref->GetSource();
                    if (!member || !member->IsAlive() || !member->IsInWorld())
                        continue;
                    if (!member->IsWithinDistInMap(bot, CLEANSE_SEARCH_RANGE))
                        continue;
                    if (hasDispellableDebuff(member))
                        return member;
                }
                return nullptr;
            }

            return hasDispellableDebuff(bot) ? bot : nullptr;
        }
    }

    bool CombatUtility::Execute(Player* bot, CombatContext const& ctx, uint32 diff, uint32& nextCastAllowedMs)
    {
        if (!bot || !bot->IsAlive() || !bot->IsInWorld())
            return false;

        // Shared AI reaction gate -- read-only here. Only the caller's own RoleEngine gate
        // decrements it on a tick where nothing was cast, so it isn't double-consumed by both
        // this layer and RoleEngine running back-to-back in the same tick.
        if (nextCastAllowedMs > diff)
            return false;

        if (CastGuard::IsCurrentlyCasting(bot))
            return false; // don't preempt the bot's own in-progress cast for utility work

        // 1. Taunt -- Tank only, only while not currently holding this target's aggro.
        if (ctx.role == BotRole::Tank && ctx.victim && ctx.victim->IsAlive() && ctx.victim->GetVictim() != bot)
        {
            if (uint32 spellId = SelectTauntSpell(bot, ctx.victim))
                if (TryCast(bot, spellId, ctx.victim, "reflexively taunted", nextCastAllowedMs))
                    return true;
        }

        // 2. Interrupt -- any role, reservation-gated (see engine/CombatReservations.h) so
        // several bots that all happen to know an interrupt don't all spend it on the same cast.
        if (ctx.victimIsCastingInterruptible && ctx.victim)
        {
            ObjectGuid enemyGuid = ctx.victim->GetGUID();
            CombatReservations::ReconcileInterruptReservation(enemyGuid, ctx.victimCastingSpellId);

            if (!CombatReservations::IsInterruptReserved(enemyGuid, bot->GetGUID()))
            {
                if (uint32 spellId = SelectInterruptSpell(bot, ctx.victim))
                {
                    uint32 remainingMs = (ctx.victimCastFinishTimeMs > ctx.currentMSTime)
                        ? (ctx.victimCastFinishTimeMs - ctx.currentMSTime) : 0;
                    uint32 reserveMs = std::min(INTERRUPT_RESERVATION_MAX_MS,
                        std::max(INTERRUPT_RESERVATION_MIN_MS, remainingMs));

                    // Reserve before casting so a sibling bot evaluated later this same tick
                    // already sees it, not just after this cast resolves next tick.
                    CombatReservations::TryReserveInterrupt(enemyGuid, ctx.victimCastingSpellId, bot->GetGUID(), reserveMs);

                    if (TryCast(bot, spellId, ctx.victim, "reserved and used interrupt", nextCastAllowedMs))
                    {
                        CombatReservations::ClearInterruptReservation(enemyGuid);
                        return true;
                    }

                    // Cast attempt failed (range/LoS changed between selection and cast, etc) --
                    // release immediately rather than block a sibling bot for the full duration.
                    CombatReservations::ClearInterruptReservation(enemyGuid);
                }
            }
        }

        // 3. Cleanse -- any role with a known dispel-shaped spell.
        if (Unit* dispelTarget = FindDispellableAlly(bot))
        {
            if (uint32 spellId = SelectDispelSpell(bot, dispelTarget))
                if (TryCast(bot, spellId, dispelTarget, "cleansed", nextCastAllowedMs))
                    return true;
        }

        return false;
    }
}
