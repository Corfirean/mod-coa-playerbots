/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Rogue (Class 4)
 */

#include "profiles/ProfileRogue.h"
#include "profiles/ProfileRegistry.h"
#include "profiles/ProfileHelpers.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterRogueProfiles()
    {
        for (uint32 specId : { 1u, 2u, 3u })
        {
            CombatProfile p;
            p.classId = 4;
            p.specId = specId;
            p.role = BotRole::Dps;
            p.profileName = specId == 1 ? "Rogue_Assassination_Dps" : specId == 2 ? "Rogue_Combat_Dps" : "Rogue_Subtlety_Dps";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Evasion (Defensive < 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Evasion";
                d.rootSpellId = 5277;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.missingAuraOnCaster = 5277;
                d.internalThrottleMs = 60000;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // Kick (Interrupt)
            {
                AbilityDescriptor d;
                d.name = "Kick";
                d.rootSpellId = 1766;
                d.tags = AbilityTag::Interrupt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 400.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (ctx.victim && ctx.victim->IsNonMeleeSpellCast(false, false, true))
                        return 100.0f;
                    return -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Slice and Dice (Buff upkeep with combo points)
            {
                AbilityDescriptor d;
                d.name = "Slice and Dice";
                d.rootSpellId = 5171;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 5171;
                d.baseScore = 310.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetComboPoints() >= 2 ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Eviscerate (Finisher at 4-5 CP)
            {
                AbilityDescriptor d;
                d.name = "Eviscerate";
                d.rootSpellId = 2098;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 340.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetComboPoints() >= 4 ? 50.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Rupture (Finisher DoT)
            {
                AbilityDescriptor d;
                d.name = "Rupture";
                d.rootSpellId = 1943;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetComboPoints() >= 4 ? 30.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Sinister Strike / Mutilate (Builder)
            {
                AbilityDescriptor d;
                d.name = "Sinister Strike";
                d.rootSpellId = 1752;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Mutilate
            {
                AbilityDescriptor d;
                d.name = "Mutilate";
                d.rootSpellId = 1329;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Envenom
            {
                AbilityDescriptor d;
                d.name = "Envenom";
                d.rootSpellId = 32645;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 335.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetComboPoints() >= 4 ? 60.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Hemorrhage
            {
                AbilityDescriptor d;
                d.name = "Hemorrhage";
                d.rootSpellId = 16511;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 245.0f;
                p.abilities.push_back(d);
            }

            // Fan of Knives
            {
                AbilityDescriptor d;
                d.name = "Fan of Knives";
                d.rootSpellId = 51723;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 260.0f;
                d.minAoETargets = 3;
                p.abilities.push_back(d);
            }

            // Blade Flurry
            {
                AbilityDescriptor d;
                d.name = "Blade Flurry";
                d.rootSpellId = 13877;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 330.0f;
                d.missingAuraOnCaster = 13877;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.engagedEnemyCount >= 2 ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Adrenaline Rush
            {
                AbilityDescriptor d;
                d.name = "Adrenaline Rush";
                d.rootSpellId = 13750;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 340.0f;
                d.missingAuraOnCaster = 13750;
                p.abilities.push_back(d);
            }

            // Killing Spree
            {
                AbilityDescriptor d;
                d.name = "Killing Spree";
                d.rootSpellId = 51690;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Kidney Shot (stops a cast)
            {
                AbilityDescriptor d;
                d.name = "Kidney Shot";
                d.rootSpellId = 408;
                d.tags = AbilityTag::Interrupt | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 390.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetComboPoints() >= 1 ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Kidney Shot (survival: a hurt bot stuns what is hitting it)
            {
                AbilityDescriptor d;
                d.name = "Kidney Shot";
                d.rootSpellId = 408;
                d.tags = AbilityTag::CrowdControl | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 335.0f;
                d.maxSelfHpPct = 40.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (!ctx.targetIsBossOrElite && ctx.bot->GetComboPoints() >= 2) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
