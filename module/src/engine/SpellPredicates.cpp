/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpellPredicates implementation
 *
 * Moved out of BotAI.cpp's anonymous namespace verbatim (behavior unchanged) so
 * engine/CombatUtility.cpp can reuse it -- see this file's header comment.
 */

#include "engine/SpellPredicates.h"
#include "BotClassRotations.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "Unit.h"
#include <algorithm>
#include <vector>

namespace BotAI
{
    // Same shape as IsUsableOffensiveSpell below: NeedsExplicitUnitTarget() excludes self-buffs,
    // ground-targeted effects, and target-less passives; !IsPositive() rules out a buff/heal;
    // !IsPassive()/CanBeUsedInCombat() excludes passives and out-of-combat-only spells; the final
    // effect check is the actual "is this offensive" signal.
    bool IsUsableOffensiveSpell(SpellInfo const* spellInfo)
    {
        if (!spellInfo || spellInfo->IsPassive() || spellInfo->IsPositive())
            return false;
        if (!spellInfo->NeedsExplicitUnitTarget() || !spellInfo->CanBeUsedInCombat())
            return false;
        if (spellInfo->IsAutoRepeatRangedSpell())
            return false;

        return spellInfo->HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE) ||
            spellInfo->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE) ||
            spellInfo->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL) ||
            spellInfo->HasEffect(SPELL_EFFECT_WEAPON_PERCENT_DAMAGE) ||
            spellInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE) ||
            spellInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE_PERCENT);
    }

    // The two real WotLK taunt shapes: a direct SPELL_EFFECT_ATTACK_ME (e.g. Warrior Taunt) or a
    // SPELL_AURA_MOD_TAUNT aura (e.g. Growl-style pet taunts).
    bool IsUsableTauntSpell(SpellInfo const* spellInfo)
    {
        if (!spellInfo || spellInfo->IsPassive())
            return false;
        if (!spellInfo->NeedsExplicitUnitTarget() || !spellInfo->CanBeUsedInCombat())
            return false;

        return spellInfo->HasEffect(SPELL_EFFECT_ATTACK_ME) || spellInfo->HasAura(SPELL_AURA_MOD_TAUNT);
    }

    // A positive spell with a direct SPELL_EFFECT_HEAL/HEAL_PCT or a SPELL_AURA_PERIODIC_HEAL (HoT).
    bool IsUsableHealSpell(SpellInfo const* spellInfo)
    {
        if (!spellInfo || spellInfo->IsPassive() || !spellInfo->IsPositive())
            return false;
        if (!spellInfo->NeedsExplicitUnitTarget() || !spellInfo->CanBeUsedInCombat())
            return false;

        return spellInfo->HasEffect(SPELL_EFFECT_HEAL) || spellInfo->HasEffect(SPELL_EFFECT_HEAL_PCT) ||
            spellInfo->HasAura(SPELL_AURA_PERIODIC_HEAL);
    }

    // "A buff that reaches the whole party/raid" -- the three AREA_AURA effect types are the real
    // WotLK mechanism real party/raid buffs use (cast on self, the engine propagates the aura to
    // nearby party/raid/friendly members automatically), deliberately not matching single-target
    // heals or ordinary self-buffs (neither need Support's own special handling).
    bool IsUsableBuffSpell(SpellInfo const* spellInfo)
    {
        if (!spellInfo || spellInfo->IsPassive() || !spellInfo->IsPositive())
            return false;

        return spellInfo->HasEffect(SPELL_EFFECT_APPLY_AREA_AURA_PARTY) ||
            spellInfo->HasEffect(SPELL_EFFECT_APPLY_AREA_AURA_RAID) ||
            spellInfo->HasEffect(SPELL_EFFECT_APPLY_AREA_AURA_FRIEND);
    }

    bool IsUsableInterruptSpell(SpellInfo const* spellInfo)
    {
        if (!spellInfo || spellInfo->IsPassive() || spellInfo->IsPositive())
            return false;
        if (!spellInfo->CanBeUsedInCombat())
            return false;

        return spellInfo->HasEffect(SPELL_EFFECT_INTERRUPT_CAST) ||
            spellInfo->HasAura(SPELL_AURA_MOD_SILENCE);
    }

    bool IsUsableDispelSpell(SpellInfo const* spellInfo)
    {
        if (!spellInfo || spellInfo->IsPassive() || !spellInfo->IsPositive())
            return false;
        if (!spellInfo->NeedsExplicitUnitTarget() || !spellInfo->CanBeUsedInCombat())
            return false;

        // The real WotLK dispel shape (Dispel Magic/Cleanse/Remove Curse/Purify all use this) --
        // this fork's SharedDefines.h/SpellAuraDefines.h has no periodic-dispel aura type to also
        // match (that's a later-TrinityCore addition), so SPELL_EFFECT_DISPEL alone is the check.
        return spellInfo->HasEffect(SPELL_EFFECT_DISPEL);
    }

    bool IsDispelCompatible(SpellInfo const* cleanseSpell, SpellInfo const* debuffSpell)
    {
        if (!cleanseSpell || !debuffSpell)
            return false;
        if (debuffSpell->Dispel == DISPEL_NONE)
            return false;

        uint32 debuffMask = SpellInfo::GetDispelMask(DispelType(debuffSpell->Dispel));
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (cleanseSpell->Effects[i].Effect != SPELL_EFFECT_DISPEL)
                continue;
            uint32 cleanseMask = SpellInfo::GetDispelMask(DispelType(cleanseSpell->Effects[i].MiscValue));
            if (cleanseMask & debuffMask)
                return true;
        }
        return false;
    }

    bool IsTargetCastingInterruptibleSpell(Unit const* target, uint32& outSpellId, uint32& outFinishTimeMs)
    {
        outSpellId = 0;
        outFinishTimeMs = 0;

        if (!target || !target->IsAlive())
            return false;

        for (uint32 i = CURRENT_FIRST_NON_MELEE_SPELL; i < CURRENT_AUTOREPEAT_SPELL; ++i)
        {
            if (Spell* spell = target->GetCurrentSpell(CurrentSpellTypes(i)))
            {
                SpellInfo const* curSpellInfo = spell->m_spellInfo;
                if (!curSpellInfo)
                    continue;
                if ((spell->getState() == SPELL_STATE_CASTING || (spell->getState() == SPELL_STATE_PREPARING && spell->GetCastTime() > 0.0f))
                        && spell->IsInterruptable()
                        && ((i == CURRENT_GENERIC_SPELL && (curSpellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT))
                            || (i == CURRENT_CHANNELED_SPELL && (curSpellInfo->ChannelInterruptFlags & CHANNEL_INTERRUPT_FLAG_INTERRUPT))))
                {
                    outSpellId = curSpellInfo->Id;
                    outFinishTimeMs = getMSTime() + uint32(std::max(0, spell->GetCastTimeRemaining()));
                    return true;
                }
            }
        }
        return false;
    }

    bool IsTargetCastingInterruptibleSpell(Unit const* target)
    {
        uint32 unusedSpellId, unusedFinishTime;
        return IsTargetCastingInterruptibleSpell(target, unusedSpellId, unusedFinishTime);
    }

    bool IsUsableAoeSpell(SpellInfo const* spellInfo)
    {
        if (!IsUsableOffensiveSpell(spellInfo))
            return false;

        if (spellInfo->IsAffectingArea() || spellInfo->IsTargetingArea())
            return true;

        if (spellInfo->MaxAffectedTargets > 1)
            return true;

        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spellInfo->Effects[i].IsEffect() && spellInfo->Effects[i].ChainTarget > 1)
                return true;
        }

        return false;
    }

    bool IsUsableSingleTargetOffensiveSpell(SpellInfo const* spellInfo)
    {
        return IsUsableOffensiveSpell(spellInfo) && !IsUsableAoeSpell(spellInfo);
    }

    bool IsBossOrEliteTarget(Unit const* target)
    {
        if (!target)
            return false;

        if (Creature const* creature = target->ToCreature())
            return creature->isElite() || creature->isWorldBoss() || creature->IsDungeonBoss();

        return false;
    }

    bool IsUsableBurstSpell(SpellInfo const* spellInfo)
    {
        if (!IsUsableOffensiveSpell(spellInfo))
            return false;

        uint32 cd = spellInfo->RecoveryTime > spellInfo->CategoryRecoveryTime ? spellInfo->RecoveryTime : spellInfo->CategoryRecoveryTime;
        return cd >= BURST_SPELL_MIN_COOLDOWN_MS;
    }

    bool IsUnitUnderBreakableCrowdControl(Unit const* unit)
    {
        if (!unit)
            return false;

        constexpr uint64 BREAKS_ON_DAMAGE_CC_MASK =
            (1ULL << MECHANIC_CHARM) |
            (1ULL << MECHANIC_DISORIENTED) |
            (1ULL << MECHANIC_FEAR) |
            (1ULL << MECHANIC_SLEEP) |
            (1ULL << MECHANIC_POLYMORPH) |
            (1ULL << MECHANIC_BANISH) |
            (1ULL << MECHANIC_SHACKLE) |
            (1ULL << MECHANIC_HORROR) |
            (1ULL << MECHANIC_SAPPED);

        return unit->HasAuraWithMechanic(BREAKS_ON_DAMAGE_CC_MASK);
    }

    bool IsOffGlobalCooldown(Player* bot, SpellInfo const* spellInfo)
    {
        if (!bot || !spellInfo)
            return false;
        if (spellInfo->StartRecoveryTime == 0)
            return true; // off-GCD ability -- never gated by the shared GCD category at all
        return !bot->GetGlobalCooldownMgr().HasGlobalCooldown(spellInfo);
    }

    bool IsKnownSpellCastable(Player* bot, uint32 spellId, Unit* target, bool positiveRange)
    {
        if (!bot || !spellId || !target)
            return false;

        PlayerSpellMap const& spellMap = bot->GetSpellMap();
        auto spellItr = spellMap.find(spellId);
        if (spellItr == spellMap.end() || !spellItr->second || spellItr->second->State == PLAYERSPELL_REMOVED || !spellItr->second->Active)
            return false;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo)
            return false;
        if (bot->HasSpellCooldown(spellId))
            return false;
        if (!IsOffGlobalCooldown(bot, spellInfo))
            return false;
        if (!bot->HasItemFitToSpellRequirements(spellInfo))
            return false;
        if (spellInfo->CasterAuraState && !bot->HasAuraState(AuraStateType(spellInfo->CasterAuraState)))
            return false;
        if (target && spellInfo->TargetAuraState && !target->HasAuraState(AuraStateType(spellInfo->TargetAuraState)))
            return false;
        if (spellInfo->CasterAuraSpell && !bot->HasAura(spellInfo->CasterAuraSpell))
            return false;
        if (target && spellInfo->TargetAuraSpell && !target->HasAura(spellInfo->TargetAuraSpell))
            return false;

        if (BotAI::IsSpellInFailureCooldown(bot->GetGUID(), spellId))
            return false;

        // Both bounds matter for a ranged spell -- see SelectKnownSpell's header comment.
        float dist = bot->GetDistance(target);
        float maxRange = spellInfo->GetMaxRange(positiveRange, bot);
        if (maxRange > 0.0f)
        {
            if (dist > maxRange)
                return false;
        }
        else if (spellInfo->IsAffectingArea())
        {
            float maxRadius = 0.0f;
            for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            {
                if (spellInfo->Effects[i].IsEffect())
                {
                    float r = spellInfo->Effects[i].CalcRadius(bot);
                    if (r > maxRadius)
                        maxRadius = r;
                }
            }
            if (maxRadius > 0.0f && dist > maxRadius)
                return false;
        }
        float minRange = spellInfo->GetMinRange(positiveRange);
        if (minRange > 0.0f && bot->IsWithinRange(target, minRange + bot->GetMeleeRange(target)))
            return false;

        if (spellInfo->PowerType == POWER_HEALTH)
        {
            int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
            if (cost > 0 && bot->GetHealth() <= (uint32)cost)
                return false;
        }
        else
        {
            int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
            if (cost > 0 && bot->GetPower(Powers(spellInfo->PowerType)) < cost)
                return false;
        }

        return true;
    }

    uint32 SelectKnownSpell(Player* bot, Unit* target, bool positiveRange, bool (*predicate)(SpellInfo const*))
    {
        for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
        {
            if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                continue;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!predicate(spellInfo))
                continue;

            if (IsKnownSpellCastable(bot, spellId, target, positiveRange))
                return spellId;
        }
        return 0;
    }

    uint32 SelectSpell(Player* bot, Unit* target) { return SelectKnownSpell(bot, target, false, IsUsableOffensiveSpell); }
    uint32 SelectTauntSpell(Player* bot, Unit* target) { return SelectKnownSpell(bot, target, false, IsUsableTauntSpell); }
    uint32 SelectHealSpell(Player* bot, Unit* target) { return SelectKnownSpell(bot, target, true, IsUsableHealSpell); }
    uint32 SelectBuffSpell(Player* bot) { return SelectKnownSpell(bot, bot, true, IsUsableBuffSpell); }
    uint32 SelectInterruptSpell(Player* bot, Unit* target) { return SelectKnownSpell(bot, target, false, IsUsableInterruptSpell); }
    uint32 SelectAoeSpell(Player* bot, Unit* target) { return SelectKnownSpell(bot, target, false, IsUsableAoeSpell); }
    uint32 SelectSingleTargetSpell(Player* bot, Unit* target) { return SelectKnownSpell(bot, target, false, IsUsableSingleTargetOffensiveSpell); }
    uint32 SelectBurstSpell(Player* bot, Unit* target) { return SelectKnownSpell(bot, target, false, IsUsableBurstSpell); }
    uint32 SelectDispelSpell(Player* bot, Unit* target) { return SelectKnownSpell(bot, target, true, IsUsableDispelSpell); }

    class HostileEnemyCheck
    {
    public:
        HostileEnemyCheck(Player const* bot, Unit const* center, float range)
            : _bot(bot), _center(center), _range(range) { }

        bool operator()(Unit* u) const
        {
            if (!u || !u->IsAlive() || u == _bot)
                return false;
            if (!_center->IsWithinDistInMap(u, _range))
                return false;
            if (!_bot->IsValidAttackTarget(u))
                return false;
            return true;
        }

    private:
        Player const* _bot;
        Unit const* _center;
        float _range;
    };

    uint32 CountNearbyEnemies(Player const* bot, Unit const* center, float range)
    {
        if (!bot || !center)
            return 0;

        std::vector<Unit*> enemies;
        HostileEnemyCheck check(bot, center, range);
        Acore::UnitListSearcher<HostileEnemyCheck> searcher(center, enemies, check);
        Cell::VisitObjects(center, searcher, range);
        return static_cast<uint32>(enemies.size());
    }

    void GetNearbyEnemies(Player const* bot, Unit const* center, float range, std::vector<Unit*>& out)
    {
        if (!bot || !center)
            return;

        HostileEnemyCheck check(bot, center, range);
        Acore::UnitListSearcher<HostileEnemyCheck> searcher(center, out, check);
        Cell::VisitObjects(center, searcher, range);
    }
}
