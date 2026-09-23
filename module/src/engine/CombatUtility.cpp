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
#include "engine/CombatResource.h"
#include "engine/SpecStrategyRegistry.h"
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
#include <vector>

#include "engine/ActionEvaluator.h"

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

        bool TryCast(Player* bot, CombatContext const& ctx, uint32 spellId, Unit* target, char const* verb, uint32& nextCastAllowedMs)
        {
            if (!spellId || !target)
                return false;

            BotAction action;
            action.spellId = spellId;
            action.rootSpellId = spellId;
            action.target = target;
            action.score = 100.0f;
            action.name = verb;
            if (!ActionEvaluator::ValidateAction(ctx, action))
                return false;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!CombatMovement::ReadyToCast(bot, spellInfo))
            {
                nextCastAllowedMs = UTILITY_REACTION_GATE_MS;
                return false;
            }

            SpellCastResult result = bot->CastSpell(target, spellId, false);
            SpecStrategyRegistry::OnActionCastResult(bot, action, result == SPELL_CAST_OK);
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

        struct CleansePlan
        {
            Unit* target = nullptr;
            uint32 spellId = 0;
        };

        // Finds a (target, spell) pair where spellId's real DispelType actually removes some
        // debuff currently on target -- item 4, Phase 2 fixup. The old version picked any ally
        // with any dispellable debuff and any known dispel-shaped spell independently, so a bot
        // that only knows Remove Curse would still try (and fail) against a Magic debuff. Bounded
        // cost: the bot's own known-dispel-spell list is gathered once (usually 0-2 entries for
        // any class), then checked against each candidate ally's own (usually small) debuff list.
        CleansePlan FindCleansePlan(Player* bot)
        {
            CleansePlan plan;

            std::vector<uint32> knownDispelSpells;
            for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
            {
                if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                    continue;
                if (IsUsableDispelSpell(sSpellMgr->GetSpellInfo(spellId)))
                    knownDispelSpells.push_back(spellId);
            }
            if (knownDispelSpells.empty())
                return plan;

            auto considerAlly = [&](Unit* ally) -> bool
            {
                for (auto const& pair : ally->GetAppliedAuras())
                {
                    AuraApplication const* app = pair.second;
                    if (!app || app->IsPositive())
                        continue;
                    Aura const* aura = app->GetBase();
                    SpellInfo const* debuffInfo = aura ? aura->GetSpellInfo() : nullptr;
                    if (!debuffInfo || debuffInfo->Dispel == DISPEL_NONE)
                        continue;

                    for (uint32 dispelSpellId : knownDispelSpells)
                    {
                        SpellInfo const* dispelInfo = sSpellMgr->GetSpellInfo(dispelSpellId);
                        if (!IsDispelCompatible(dispelInfo, debuffInfo))
                            continue;
                        if (!IsKnownSpellCastable(bot, dispelSpellId, ally, true))
                            continue;

                        plan.target = ally;
                        plan.spellId = dispelSpellId;
                        return true;
                    }
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
                    if (considerAlly(member))
                        return plan;
                }
                return plan;
            }

            considerAlly(bot);
            return plan;
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

        SpecStrategyRuntime const& runtime = SpecStrategyRegistry::GetRuntime(bot->GetGUID());
        bool inBaseline = (runtime.lastStateStatus == CombatStateStatus::Ready);

        // 1. Taunt -- Tank only, only while not currently holding this target's aggro, and only in baseline form!
        if (ctx.role == BotRole::Tank && inBaseline && ctx.victim && ctx.victim->IsAlive() && ctx.victim->GetVictim() != bot)
        {
            if (uint32 spellId = SelectTauntSpell(bot, ctx.victim))
            {
                if (TryCast(bot, ctx, spellId, ctx.victim, "reflexively taunted", nextCastAllowedMs))
                    return true;
            }
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

                    if (TryCast(bot, ctx, spellId, ctx.victim, "reserved and used interrupt", nextCastAllowedMs))
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

        // 3. Cleanse -- any role with a known dispel-shaped spell whose DispelType actually
        // matches a debuff present on some ally (item 4, Phase 2 fixup). FindCleansePlan only
        // ever returns a type-compatible pair, so there's nothing incompatible to fail on here --
        // if TryCast still fails, that's a genuine transient issue (range/LoS changed between
        // selection and cast), and recording a failure-cooldown for it is correct.
        CleansePlan cleansePlan = FindCleansePlan(bot);
        if (cleansePlan.target && cleansePlan.spellId)
        {
            if (TryCast(bot, ctx, cleansePlan.spellId, cleansePlan.target, "cleansed", nextCastAllowedMs))
                return true;
        }

        return false;
    }
}
