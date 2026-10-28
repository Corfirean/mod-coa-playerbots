#include "training/TrainingArenaManager.h"
/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatUtility implementation
 */

#include "engine/BotDebugLog.h"
#include "engine/CombatUtility.h"
#include "engine/CastGuard.h"
#include "engine/CombatContext.h"
#include "engine/CombatMovement.h"
#include "engine/CombatReservations.h"
#include "engine/GroupDirector.h"
#include "engine/CombatResource.h"
#include "engine/CombatDecisions.h"
#include "engine/HealEvaluator.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/SpellPredicates.h"
#include "engine/SpellResolver.h"
#include "BotClassRotations.h"
#include "Config.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>
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

        // `appliedInterrupt` (optional) is set for an interrupt cast that the game accepted AND that left the enemy no longer casting.
        bool TryCast(Player* bot, CombatContext const& ctx, uint32 spellId, Unit* target, char const* verb, uint32& nextCastAllowedMs,
            bool* appliedInterrupt = nullptr)
        {
            if (appliedInterrupt)
                *appliedInterrupt = false;
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

            bool const isInterrupt = std::strstr(verb, "interrupt") != nullptr;
            bool const wasCasting = isInterrupt && target->IsNonMeleeSpellCast(false);
            SpellCastResult result = bot->CastSpell(target, spellId, false);
            // An instant interrupt has done its work when CastSpell returns: SPELL_CAST_OK only says the game accepted the request.
            bool const applied = wasCasting && result == SPELL_CAST_OK && !target->IsNonMeleeSpellCast(false);
            if (appliedInterrupt)
                *appliedInterrupt = applied;
            CoaTraining::CastResult(bot, spellId, uint32(result), target);
            if (isInterrupt)
                CoaTraining::InterruptKick(bot, spellId, target, uint32(result), verb, applied);
            SpecStrategyRegistry::OnActionCastResult(bot, action, result == SPELL_CAST_OK);
            if (result == SPELL_CAST_OK)
            {
                nextCastAllowedMs = UTILITY_REACTION_GATE_MS;
                LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "CombatAI: bot '{}' {} (spell {}) on '{}'.",
                    bot->GetName(), verb, spellId, target->GetName());
                return true;
            }

            BotAI::HandleSpellCastFailure(bot, target, spellId, result);
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
        // Time a heal cast now needs to land (a typical cast plus latency): the window in which a debuff must not be able to kill the ally.
        constexpr uint32 HEAL_LANDING_ASSUMED_MS = 2500;

        // Damage per second the aura itself deals to `victim` (periodic damage and periodic percent damage effects). Stack amounts are taken as
        // the effect reports them; an aura whose effect amount excludes the stack count is under-estimated, which only makes the dispel rarer.
        float DebuffDamagePerSecond(Aura const* aura, Unit const* victim)
        {
            float dps = 0.0f;
            SpellInfo const* info = aura->GetSpellInfo();
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                AuraEffect const* effect = aura->GetEffect(i);
                if (!effect || effect->GetAmplitude() == 0)
                    continue;
                AuraType type = AuraType(info->Effects[i].ApplyAuraName);
                float perTick = 0.0f;
                if (type == SPELL_AURA_PERIODIC_DAMAGE)
                    perTick = float(std::max(effect->GetAmount(), 0));
                else if (type == SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
                    perTick = float(victim->GetMaxHealth()) * float(std::max(effect->GetAmount(), 0)) / 100.0f;
                else
                    continue;
                dps += perTick * 1000.0f / float(effect->GetAmplitude());
            }
            return dps;
        }

        // True for a debuff that cannot wait behind a heal (see Decisions::CleanseOutranksHeal): it controls the unit, it silences the healer
        // itself, or it would kill the unit before a heal could land. A harmless debuff on a badly hurt ally is not urgent: the heal comes first.
        bool IsUrgentDebuff(Player const* bot, Unit const* ally, Aura const* aura)
        {
            constexpr uint64 CONTROL_MECHANICS =
                (1ULL << MECHANIC_STUN) | (1ULL << MECHANIC_FEAR) | (1ULL << MECHANIC_CHARM) | (1ULL << MECHANIC_SLEEP) |
                (1ULL << MECHANIC_POLYMORPH) | (1ULL << MECHANIC_DISORIENTED) |
                (1ULL << MECHANIC_HORROR) | (1ULL << MECHANIC_BANISH) | (1ULL << MECHANIC_SHACKLE);
            uint64 mechanics = aura->GetSpellInfo()->GetAllEffectsMechanicMask();
            Decisions::CleanseFacts facts;
            facts.controlMechanic = (mechanics & CONTROL_MECHANICS) != 0;
            facts.silenceMechanic = (mechanics & (1ULL << MECHANIC_SILENCE)) != 0;
            facts.debuffOnSelf = ally == bot;
            facts.debuffDps = DebuffDamagePerSecond(aura, ally);
            facts.debuffedHealth = float(ally->GetHealth());
            facts.healLandingMs = HEAL_LANDING_ASSUMED_MS;
            return Decisions::CleanseOutranksHeal(facts);
        }

        // True when the enemy cast `enemy` is going to kill a groupmate that no heal this bot could land first would save: the cast does at
        // least as much damage as the target has (shields included) plus the best heal the bot knows. Then the kick is the action that works.
        bool InterruptOutranksHeal(Player* bot, Unit* enemy)
        {
            Group* group = bot->GetGroup();
            if (!group)
                return false;
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || !member->IsAlive() || !member->IsInWorld() || !member->IsWithinDistInMap(bot, CLEANSE_SEARCH_RANGE))
                    continue;
                float damage = 0.0f;
                uint32 remainingMs = 0, schoolMask = 0;
                if (!GetDamagingCastOn(enemy, member, damage, remainingMs, &schoolMask))
                    continue;
                float shield = float(HealEvaluator::UsableAbsorb(member, std::max<uint32>(remainingMs, 1), schoolMask));
                float bestHeal = 0.0f;
                for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
                {
                    if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                        continue;
                    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
                    if (!IsUsableHealSpell(info))
                        continue;
                    bestHeal = std::max(bestHeal, float(HealEvaluator::EstimateRawHeal(bot, member, info)));
                }
                if (Decisions::InterruptOutranksEmergencyHeal(damage, float(member->GetHealth()) + shield, bestHeal))
                    return true;
            }
            return false;
        }

        // `healFirst`: this bot is a healer and someone in the group is critical, so an ordinary cleanse must not delay the heal that keeps
        // them alive. Only a debuff that IsUrgentDebuff still goes first. Bots that do not heal are never held back by it.
        CleansePlan FindCleansePlan(Player* bot, bool healFirst)
        {
            CleansePlan plan;

            std::vector<uint32> knownDispelSpells;
            for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
            {
                if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                    continue;
                uint32 root = sSpellMgr->GetFirstSpellInChain(spellId);
                uint32 highest = SpellResolver::ResolveSpell(bot, root ? root : spellId);
                if (highest != spellId)
                    continue;
                uint32 effective = SpellResolver::EffectiveSpell(bot, highest);
                if (IsUsableDispelSpell(sSpellMgr->GetSpellInfo(effective)) &&
                    std::find(knownDispelSpells.begin(), knownDispelSpells.end(), effective) == knownDispelSpells.end())
                    knownDispelSpells.push_back(effective);
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
                    if (healFirst && !IsUrgentDebuff(bot, ally, aura))
                        continue;

                    for (uint32 dispelSpellId : knownDispelSpells)
                    {
                        SpellInfo const* dispelInfo = sSpellMgr->GetSpellInfo(dispelSpellId);
                        if (!IsDispelCompatible(dispelInfo, debuffInfo))
                            continue;
                        Unit* castTarget = ally;
                        if (!dispelInfo->NeedsExplicitUnitTarget())
                        {
                            // Caster-centred party/raid cleanses have no explicit ally
                            // target. Check the matching dispel effect's own radius,
                            // then cast on self and let the native area selection run.
                            bool reachesAlly = false;
                            uint32 mask = SpellInfo::GetDispelMask(DispelType(debuffInfo->Dispel));
                            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                            {
                                auto const& effect = dispelInfo->Effects[i];
                                if (effect.Effect != SPELL_EFFECT_DISPEL ||
                                    !(SpellInfo::GetDispelMask(DispelType(effect.MiscValue)) & mask)) continue;
                                bool casterGroupTarget = false;
                                for (auto target : {effect.TargetA.GetTarget(), effect.TargetB.GetTarget()})
                                    if (target == TARGET_UNIT_CASTER_AREA_PARTY || target == TARGET_UNIT_CASTER_AREA_RAID)
                                        casterGroupTarget = true;
                                float radius = effect.CalcRadius(bot);
                                if (casterGroupTarget && radius > 0.0f && bot->IsWithinDistInMap(ally, radius))
                                    reachesAlly = true;
                            }
                            if (!reachesAlly) continue;
                            castTarget = bot;
                        }
                        if (!IsKnownSpellCastable(bot, dispelSpellId, castTarget, true))
                            continue;

                        plan.target = castTarget;
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

        // ---- Sustain: potions and mana-restoring abilities in combat -------------------------------------------------

        // Per bot: when the next attempt of each kind is allowed, so a failed or unavailable try is not repeated every tick.
        struct SustainTimers
        {
            uint32 nextHealthPotionMs = 0;
            uint32 nextManaPotionMs = 0;
            uint32 nextManaSpellMs = 0;
        };
        std::unordered_map<ObjectGuid, SustainTimers> s_sustain;

        // Whether a spell gives its caster mana: an energize effect on the caster (or an aura that does it over time, or raises mana regeneration).
        // Derived from the spell data itself, so every class and every custom Ascension ability is covered without a list.
        bool IsSelfManaRestore(SpellInfo const* info)
        {
            static std::unordered_map<uint32, bool> cache;
            if (!info)
                return false;
            auto cached = cache.find(info->Id);
            if (cached != cache.end())
                return cached->second;

            bool restores = false;
            if (!info->IsPassive() && info->IsPositive())
            {
                for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                {
                    auto const& e = info->Effects[i];
                    if (!e.IsEffect() || e.MiscValue != POWER_MANA)
                        continue;
                    bool const self = e.TargetA.GetTarget() == TARGET_UNIT_CASTER || e.TargetA.GetTarget() == TARGET_UNIT_CASTER_AREA_PARTY ||
                        e.TargetA.GetTarget() == TARGET_UNIT_CASTER_AREA_RAID;
                    if (!self || e.CalcValue() <= 0)
                        continue;
                    if (e.Effect == SPELL_EFFECT_ENERGIZE || e.Effect == SPELL_EFFECT_ENERGIZE_PCT)
                        restores = true;
                    else if (e.Effect == SPELL_EFFECT_APPLY_AURA &&
                        (e.ApplyAuraName == SPELL_AURA_PERIODIC_ENERGIZE || e.ApplyAuraName == SPELL_AURA_MOD_POWER_REGEN ||
                         e.ApplyAuraName == SPELL_AURA_MOD_POWER_REGEN_PERCENT))
                    {
                        // A short aura is a restore ability; an hour-long "+mana per 5 seconds" is a consumable-style buff, not something to cast when
                        // the mana is gone (the first soak run re-cast such buffs again and again).
                        int32 const duration = info->GetDuration();
                        if (duration > 0 && duration <= 60000)
                            restores = true;
                    }
                }
            }
            cache[info->Id] = restores;
            return restores;
        }

        // The best mana-restoring spell the bot knows and can cast now (the largest single gain wins), or 0.
        uint32 SelectManaRestoreSpell(Player* bot, CombatContext const& ctx)
        {
            uint32 best = 0;
            int32 bestGain = 0;
            for (auto const& [spellId, learned] : bot->GetSpellMap())
            {
                if (!learned || learned->State == PLAYERSPELL_REMOVED || !learned->Active)
                    continue;
                SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
                if (!info || !IsSelfManaRestore(info) || bot->HasSpellCooldown(spellId) || bot->HasAura(spellId))
                    continue;
                uint32 const root = sSpellMgr->GetFirstSpellInChain(spellId);
                if (SpellResolver::ResolveSpell(bot, root ? root : spellId) != spellId)
                    continue; // a lower rank of a spell the bot knows better
                // An ability paid for with health (Life Tap) only while the bot can spare it.
                if (info->PowerType == POWER_HEALTH && ctx.botHpPct < 70.0f)
                    continue;
                if (bot->IsInCombat() && !info->CanBeUsedInCombat())
                    continue;
                int32 gain = 0;
                for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                    if (info->Effects[i].MiscValue == POWER_MANA)
                        gain = std::max(gain, info->Effects[i].CalcValue());
                if (gain > bestGain)
                {
                    best = spellId;
                    bestGain = gain;
                }
            }
            return best;
        }

        enum class PotionKind : uint8 { Health, Mana };

        // What a potion does, from its use spell: a heal effect, or an energize effect on mana.
        bool PotionIsOfKind(ItemTemplate const* proto, PotionKind kind)
        {
            if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE || proto->SubClass != ITEM_SUBCLASS_POTION)
                return false;
            if (!proto->Spells[0].SpellId || proto->Spells[0].SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
                return false;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(proto->Spells[0].SpellId);
            if (!info)
                return false;
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                auto const& e = info->Effects[i];
                if (kind == PotionKind::Health && (e.Effect == SPELL_EFFECT_HEAL || e.Effect == SPELL_EFFECT_HEAL_PCT) && e.CalcValue() > 0)
                    return true;
                if (kind == PotionKind::Mana && (e.Effect == SPELL_EFFECT_ENERGIZE || e.Effect == SPELL_EFFECT_ENERGIZE_PCT) && e.MiscValue == POWER_MANA)
                    return true;
            }
            return false;
        }

        // The strongest potion of this kind in the bags that the bot's level allows (the one with the highest required level), or null.
        Item* FindPotion(Player* bot, PotionKind kind)
        {
            Item* best = nullptr;
            uint32 bestLevel = 0;
            auto consider = [&](Item* item)
            {
                ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
                if (!proto || proto->RequiredLevel > bot->GetLevel() || !PotionIsOfKind(proto, kind))
                    return;
                if (bot->HasSpellCooldown(proto->Spells[0].SpellId))
                    return;
                if (!best || proto->RequiredLevel > bestLevel)
                {
                    best = item;
                    bestLevel = proto->RequiredLevel;
                }
            };
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
            for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
                if (Bag* bag = bot->GetBagByPos(bagSlot))
                    for (uint32 j = 0; j < bag->GetBagSize(); ++j)
                        consider(bag->GetItemByPos(j));
            return best;
        }

        // Uses the item as a client would (the cooldown, the use spell and the stack are handled by the core).
        bool UsePotion(Player* bot, Item* item)
        {
            if (!item || bot->CanUseItem(item) != EQUIP_ERR_OK)
                return false;
            uint32 const entry = item->GetEntry();
            uint32 const before = bot->GetItemCount(entry);
            uint32 const useSpell = item->GetTemplate()->Spells[0].SpellId;
            SpellCastTargets targets;
            targets.SetUnitTarget(bot);
            bot->CastItemUseSpell(item, targets, 1, 0);
            return bot->GetItemCount(entry) < before || bot->HasSpellCooldown(useSpell);
        }

        // Called from CombatUtility::Execute in combat. Health potion when low, mana-restoring ability and then a mana potion when the mana is
        // nearly gone. A healer restores earlier than the others: a healer that goes dry is the end of the group.
        bool TrySustain(Player* bot, CombatContext const& ctx, uint32& nextCastAllowedMs)
        {
            if (!bot->IsInCombat() || !ctx.victim)
                return false;
            uint32 const now = getMSTime();
            SustainTimers& t = s_sustain[bot->GetGUID()];

            float const hpLimit = ctx.role == BotRole::Tank ? 40.0f : 33.0f;
            if (ctx.botHpPct < hpLimit && now >= t.nextHealthPotionMs)
            {
                t.nextHealthPotionMs = now + 3000;
                if (Item* potion = FindPotion(bot, PotionKind::Health); potion && UsePotion(bot, potion))
                {
                    t.nextHealthPotionMs = now + 120000;
                    nextCastAllowedMs = UTILITY_REACTION_GATE_MS;
                    LOG_INFO("module.coa-playerbots.sustain", "Sustain: '{}' drank a health potion at {:.0f}% health.", bot->GetName(), ctx.botHpPct);
                    return true;
                }
            }

            if (bot->getPowerType() != POWER_MANA || bot->GetMaxPower(POWER_MANA) == 0)
                return false;
            float const manaPct = bot->GetPowerPct(POWER_MANA);
            bool const healer = ctx.role == BotRole::Healer;
            if (manaPct >= (healer ? 30.0f : 20.0f))
                return false;

            if (now >= t.nextManaSpellMs)
            {
                t.nextManaSpellMs = now + 2000;
                if (uint32 spellId = SelectManaRestoreSpell(bot, ctx))
                    if (TryCast(bot, ctx, spellId, bot, "restored mana with an ability", nextCastAllowedMs))
                    {
                        LOG_INFO("module.coa-playerbots.sustain", "Sustain: '{}' restored mana with spell {} at {:.0f}% mana.", bot->GetName(), spellId, manaPct);
                        return true;
                    }
            }
            if (manaPct < (healer ? 20.0f : 12.0f) && now >= t.nextManaPotionMs)
            {
                t.nextManaPotionMs = now + 3000;
                if (Item* potion = FindPotion(bot, PotionKind::Mana); potion && UsePotion(bot, potion))
                {
                    t.nextManaPotionMs = now + 120000;
                    nextCastAllowedMs = UTILITY_REACTION_GATE_MS;
                    LOG_INFO("module.coa-playerbots.sustain", "Sustain: '{}' drank a mana potion at {:.0f}% mana.", bot->GetName(), manaPct);
                    return true;
                }
            }
            return false;
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
        // A healer with a critical groupmate spends its GCD on the heal first; a kick that can wait does not outrank keeping someone alive.
        // The exception is a cast that kills someone no heal could save (InterruptOutranksHeal): there the kick is the only thing that works.
        bool const healerHasEmergency = ctx.role == BotRole::Healer && ctx.criticalAllyCount > 0;

        // A grouped bot kicks what the group director assigned to it (any enemy fighting the group, one primary and one backup per
        // cast, casts that do not matter ignored). A bot without a group keeps the own-target logic below.
        static bool const useDirector = sConfigMgr->GetOption<bool>("CoaBots.Combat.GroupInterruptDirector", false);
        if (useDirector && bot->GetGroup())
        {
            uint32 assignedSpell = 0;
            Unit* assigned = GroupDirector::GetInterruptTarget(bot, assignedSpell);
            if (assigned && healerHasEmergency && !InterruptOutranksHeal(bot, assigned))
                assigned = nullptr;
            if (assigned)
            {
                bool applied = false;
                bool const cast = TryCast(bot, ctx, assignedSpell, assigned, "assigned interrupt", nextCastAllowedMs, &applied);
                GroupDirector::ReportInterruptAttempt(bot, assigned->GetGUID(), cast, applied);
                if (cast)
                    return true;
            }
        }
        else if (ctx.victimIsCastingInterruptible && ctx.victim && !healerHasEmergency)
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

        // 2b. Sustain -- a health potion when low, a mana ability or potion when the mana is nearly gone. A bot that runs dry mid-fight used to
        // stand there and the group waited minutes for it to drink (measured in the soak test: tank "waiting: ... is low on mana").
        if (TrySustain(bot, ctx, nextCastAllowedMs))
            return true;

        // 3. Cleanse -- any role with a known dispel-shaped spell whose DispelType actually
        // matches a debuff present on some ally (item 4, Phase 2 fixup). FindCleansePlan only
        // ever returns a type-compatible pair, so there's nothing incompatible to fail on here --
        // if TryCast still fails, that's a genuine transient issue (range/LoS changed between
        // selection and cast), and recording a failure-cooldown for it is correct.
        // A healer with a critical groupmate heals first; only a debuff that controls someone, silences the healer or would kill its bearer
        // before a heal lands (IsUrgentDebuff) is still worth the GCD. Other roles cleanse as before.
        CleansePlan cleansePlan = FindCleansePlan(bot, healerHasEmergency);
        if (cleansePlan.target && cleansePlan.spellId)
        {
            if (TryCast(bot, ctx, cleansePlan.spellId, cleansePlan.target, "cleansed", nextCastAllowedMs))
                return true;
        }

        // Native caster-area resurrection (e.g. Defibrillate). Never offer it
        // as healing on a living player or while living allies are critical.
        Group* resurrectionGroup = bot->GetGroup();
        bool hasFallenMember = false;
        if (resurrectionGroup)
            for (GroupReference* ref = resurrectionGroup->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    if (!member->IsAlive() && member->IsInWorld() && !member->isResurrectRequested())
                        hasFallenMember = true;
        if (hasFallenMember && !ctx.criticalAllyCount && ctx.botHpPct >= 35.0f)
            if (Group* group = resurrectionGroup)
                for (auto const& [spellId, learned] : bot->GetSpellMap())
                {
                    if (!learned || learned->State == PLAYERSPELL_REMOVED || !learned->Active) continue;
                    uint32 root = sSpellMgr->GetFirstSpellInChain(spellId);
                    uint32 resolved = SpellResolver::ResolveCastSpell(bot, root ? root : spellId);
                    if (SpellResolver::ResolveSpell(bot, root ? root : spellId) != spellId) continue;
                    SpellInfo const* info = sSpellMgr->GetSpellInfo(resolved);
                    if (!info || info->IsPassive() || !info->IsPositive() || info->NeedsExplicitUnitTarget() ||
                        (bot->IsInCombat() && !info->CanBeUsedInCombat())) continue;
                    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                    {
                        auto const& effect = info->Effects[i];
                        if (effect.Effect != SPELL_EFFECT_RESURRECT && effect.Effect != SPELL_EFFECT_RESURRECT_NEW) continue;
                        bool casterArea = false;
                        for (auto target : {effect.TargetA.GetTarget(), effect.TargetB.GetTarget()})
                            if (target == TARGET_UNIT_SRC_AREA_ALLY || target == TARGET_UNIT_CASTER_AREA_PARTY ||
                                target == TARGET_UNIT_CASTER_AREA_RAID) casterArea = true;
                        float radius = effect.CalcRadius(bot);
                        if (!casterArea || radius <= 0.0f) continue;
                        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                        {
                            Player* member = ref->GetSource();
                            if (!member || member->IsAlive() || !member->IsInWorld() || member->isResurrectRequested() ||
                                !bot->IsWithinDistInMap(member, radius)) continue;
                            if (IsKnownSpellCastable(bot, resolved, bot, true) &&
                                TryCast(bot, ctx, resolved, bot, "offered area resurrection", nextCastAllowedMs))
                                return true;
                        }
                    }
                }

        return false;
    }
}
