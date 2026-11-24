/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Hunter (Class 3)
 */

#include "profiles/ProfileHunter.h"
#include "profiles/ProfileRegistry.h"
#include "profiles/ProfileHelpers.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterHunterProfiles()
    {
        // Hunter specs (Beast Mastery: 1, Marksmanship: 2, Survival: 3)
        for (uint32 specId : { 1u, 2u, 3u })
        {
            CombatProfile p;
            p.classId = 3;
            p.specId = specId;
            p.role = BotRole::Dps;
            p.profileName = specId == 1 ? "Hunter_BM_Dps" : specId == 2 ? "Hunter_MM_Dps" : "Hunter_Survival_Dps";
            p.useRangedAutoRepeat = true;
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Hunter's Mark
            {
                AbilityDescriptor d;
                d.name = "Hunter's Mark";
                d.rootSpellId = 1130;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Kill Shot (< 20% HP finisher)
            {
                AbilityDescriptor d;
                d.name = "Kill Shot";
                d.rootSpellId = 53351;
                d.tags = AbilityTag::Execute | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.maxTargetHpPct = 20.0f;
                d.baseScore = 420.0f;
                p.abilities.push_back(d);
            }

            // Serpent Sting (DoT)
            {
                AbilityDescriptor d;
                d.name = "Serpent Sting";
                d.rootSpellId = 1978;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Aimed Shot / Chimera Shot
            {
                AbilityDescriptor d;
                d.name = "Aimed Shot";
                d.rootSpellId = 19434;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 10000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Arcane Shot
            {
                AbilityDescriptor d;
                d.name = "Arcane Shot";
                d.rootSpellId = 3044;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Multi-Shot (AoE)
            {
                AbilityDescriptor d;
                d.name = "Multi-Shot";
                d.rootSpellId = 2643;
                d.tags = AbilityTag::AoEDamage | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 2;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // Steady Shot (Filler)
            {
                AbilityDescriptor d;
                d.name = "Steady Shot";
                d.rootSpellId = 34120;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            // Concussive Shot (Slow)
            {
                AbilityDescriptor d;
                d.name = "Concussive Shot";
                d.rootSpellId = 5116;
                d.tags = AbilityTag::CrowdControl | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // Rapid Fire
            {
                AbilityDescriptor d;
                d.name = "Rapid Fire";
                d.rootSpellId = 3045;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 330.0f;
                d.missingAuraOnCaster = 3045;
                p.abilities.push_back(d);
            }

            // Chimera Shot
            {
                AbilityDescriptor d;
                d.name = "Chimera Shot";
                d.rootSpellId = 53209;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Explosive Shot
            {
                AbilityDescriptor d;
                d.name = "Explosive Shot";
                d.rootSpellId = 53301;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // Black Arrow
            {
                AbilityDescriptor d;
                d.name = "Black Arrow";
                d.rootSpellId = 3674;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 320.0f;
                d.requireAuraMissingOnTarget = true;
                p.abilities.push_back(d);
            }

            // Silencing Shot
            {
                AbilityDescriptor d;
                d.name = "Silencing Shot";
                d.rootSpellId = 34490;
                d.tags = AbilityTag::Interrupt | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Raptor Strike
            {
                AbilityDescriptor d;
                d.name = "Raptor Strike";
                d.rootSpellId = 2973;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.victim && ctx.bot->IsWithinMeleeRange(ctx.victim) ? 0.0f : -1.0f;  // only when something is on top of the hunter
                };
                p.abilities.push_back(d);
            }

            // Aspect of the Hawk
            {
                AbilityDescriptor d;
                d.name = "Aspect of the Hawk";
                d.rootSpellId = 13165;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 345.0f;
                d.missingAuraOnCaster = 13165;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (ctx.botPowerPct >= 50.0f || !HasAuraOfAny(ctx.bot, { 34074 })) ? 0.0f : -1.0f;  // not while Viper is feeding the mana bar
                };
                p.abilities.push_back(d);
            }

            // Aspect of the Viper
            {
                AbilityDescriptor d;
                d.name = "Aspect of the Viper";
                d.rootSpellId = 34074;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 346.0f;
                d.missingAuraOnCaster = 34074;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.botPowerPct < 25.0f ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Mend Pet (the pet is hurt)
            {
                AbilityDescriptor d;
                d.name = "Mend Pet";
                d.rootSpellId = 136;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 335.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    Pet* pet = ctx.bot->GetPet(); return (pet && pet->IsAlive() && pet->GetHealthPct() < 50.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            if (specId == 1u)
            {
                // Kill Command
                {
                    AbilityDescriptor d;
                    d.name = "Kill Command";
                    d.rootSpellId = 34026;
                    d.tags = AbilityTag::None;
                    d.targetType = TargetType::Self;
                    d.baseScore = 325.0f;
                    d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                        Pet* pet = ctx.bot->GetPet(); return (pet && pet->IsAlive() && pet->GetVictim()) ? 0.0f : -1.0f;
                    };
                    p.abilities.push_back(d);
                }
            }

            if (specId == 1u)
            {
                // Bestial Wrath
                {
                    AbilityDescriptor d;
                    d.name = "Bestial Wrath";
                    d.rootSpellId = 19574;
                    d.tags = AbilityTag::OffensiveCD;
                    d.targetType = TargetType::Self;
                    d.baseScore = 335.0f;
                    d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                        Pet* pet = ctx.bot->GetPet(); return (pet && pet->IsAlive() && pet->GetVictim()) ? 0.0f : -1.0f;
                    };
                    p.abilities.push_back(d);
                }
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
