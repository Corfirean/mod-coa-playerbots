/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Death Knight (Class 6)
 */

#include "profiles/ProfileDeathKnight.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterDeathKnightProfiles()
    {
        // =========================================================================
        // 1. SPEC 1: BLOOD (TANK)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 6;
            p.specId = 1;
            p.role = BotRole::Tank;
            p.profileName = "DeathKnight_Blood_Tank";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Frost Presence (or Blood Presence)
            {
                AbilityDescriptor d;
                d.name = "Frost Presence";
                d.rootSpellId = 48263;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 48263;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

            // Dark Command (Taunt)
            {
                AbilityDescriptor d;
                d.name = "Dark Command";
                d.rootSpellId = 56222;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 450.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (!ctx.victimTargetingNonTank)
                        return -1.0f;
                    return 0.0f;
                };
                p.abilities.push_back(d);
            }

            // Icebound Fortitude (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Icebound Fortitude";
                d.rootSpellId = 48792;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.missingAuraOnCaster = 48792;
                d.internalThrottleMs = 60000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Death Strike (Self Heal strike)
            {
                AbilityDescriptor d;
                d.name = "Death Strike";
                d.rootSpellId = 49998;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::EmergencyHeal;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 360.0f;
                p.abilities.push_back(d);
            }

            // Icy Touch (Frost Fever)
            {
                AbilityDescriptor d;
                d.name = "Icy Touch";
                d.rootSpellId = 45477;
                d.tags = AbilityTag::PeriodicDamage | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Plague Strike (Blood Plague)
            {
                AbilityDescriptor d;
                d.name = "Plague Strike";
                d.rootSpellId = 45462;
                d.tags = AbilityTag::PeriodicDamage | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 330.0f;
                p.abilities.push_back(d);
            }

            // Heart Strike / Blood Strike
            {
                AbilityDescriptor d;
                d.name = "Heart Strike";
                d.rootSpellId = 55050;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Horn of Winter (Buff)
            {
                AbilityDescriptor d;
                d.name = "Horn of Winter";
                d.rootSpellId = 57330;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 57330;
                d.internalThrottleMs = 30000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 2. SPEC 2 & 3: FROST & UNHOLY (DPS)
        // =========================================================================
        for (uint32 specId : { 2u, 3u })
        {
            CombatProfile p;
            p.classId = 6;
            p.specId = specId;
            p.role = BotRole::Dps;
            p.profileName = specId == 2 ? "DeathKnight_Frost_Dps" : "DeathKnight_Unholy_Dps";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Icy Touch
            {
                AbilityDescriptor d;
                d.name = "Icy Touch";
                d.rootSpellId = 45477;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Plague Strike
            {
                AbilityDescriptor d;
                d.name = "Plague Strike";
                d.rootSpellId = 45462;
                d.tags = AbilityTag::PeriodicDamage | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 330.0f;
                p.abilities.push_back(d);
            }

            // Frost Strike
            {
                AbilityDescriptor d;
                d.name = "Frost Strike";
                d.rootSpellId = 49143;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Scourge Strike
            {
                AbilityDescriptor d;
                d.name = "Scourge Strike";
                d.rootSpellId = 55090;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 310.0f;
                p.abilities.push_back(d);
            }

            // Blood Strike (Filler)
            {
                AbilityDescriptor d;
                d.name = "Blood Strike";
                d.rootSpellId = 45902;
                d.tags = AbilityTag::Filler | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
