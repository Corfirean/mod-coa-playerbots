/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Warlock (Class 9)
 */

#include "profiles/ProfileWarlock.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterWarlockProfiles()
    {
        // =========================================================================
        // 1. SPEC 4: DARK APOTHEOSIS (WARLOCK TANK - REBORN)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 9;
            p.specId = 4;
            p.role = BotRole::Tank;
            p.profileName = "Warlock_DarkApotheosis_Tank";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Dark Apotheosis Stance
            {
                AbilityDescriptor d;
                d.name = "Dark Apotheosis";
                d.rootSpellId = 1154321;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 1154321;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

            // Searing Pain (High threat)
            {
                AbilityDescriptor d;
                d.name = "Searing Pain";
                d.rootSpellId = 5676;
                d.tags = AbilityTag::Taunt | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 420.0f;
                p.abilities.push_back(d);
            }

            // Drain Life (< 60% HP)
            {
                AbilityDescriptor d;
                d.name = "Drain Life";
                d.rootSpellId = 689;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.maxSelfHpPct = 60.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // Corruption (DoT)
            {
                AbilityDescriptor d;
                d.name = "Corruption";
                d.rootSpellId = 172;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Shadow Bolt (Filler)
            {
                AbilityDescriptor d;
                d.name = "Shadow Bolt";
                d.rootSpellId = 686;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 2. SPEC 1, 2, 3: AFFLICTION, DEMONOLOGY, DESTRUCTION (DPS)
        // =========================================================================
        for (uint32 specId : { 1u, 2u, 3u })
        {
            CombatProfile p;
            p.classId = 9;
            p.specId = specId;
            p.role = BotRole::Dps;
            p.profileName = specId == 1 ? "Warlock_Affliction_Dps" : specId == 2 ? "Warlock_Demonology_Dps" : "Warlock_Destruction_Dps";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Life Tap (Mana regen when HP > 70% and Mana < 30%)
            {
                AbilityDescriptor d;
                d.name = "Life Tap";
                d.rootSpellId = 1454;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.minSelfHpPct = 70.0f;
                d.baseScore = 350.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetPower(POWER_MANA) * 100 / ctx.bot->GetMaxPower(POWER_MANA) < 30 ? 50.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Corruption (DoT)
            {
                AbilityDescriptor d;
                d.name = "Corruption";
                d.rootSpellId = 172;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Curse of Agony (DoT)
            {
                AbilityDescriptor d;
                d.name = "Curse of Agony";
                d.rootSpellId = 980;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Immolate
            {
                AbilityDescriptor d;
                d.name = "Immolate";
                d.rootSpellId = 348;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Chaos Bolt
            {
                AbilityDescriptor d;
                d.name = "Chaos Bolt";
                d.rootSpellId = 50796;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 360.0f;
                p.abilities.push_back(d);
            }

            // Shadow Bolt (Filler)
            {
                AbilityDescriptor d;
                d.name = "Shadow Bolt";
                d.rootSpellId = 686;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Seed of Corruption (AoE)
            {
                AbilityDescriptor d;
                d.name = "Seed of Corruption";
                d.rootSpellId = 27243;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 3;
                d.baseScore = 330.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
