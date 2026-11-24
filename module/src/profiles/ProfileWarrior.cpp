/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Warrior (Class 1)
 */

#include "profiles/ProfileWarrior.h"
#include "profiles/ProfileRegistry.h"
#include "profiles/ProfileHelpers.h"
#include "engine/CombatContext.h"
#include "engine/SpellResolver.h"
#include "Player.h"

namespace BotAI
{
    void RegisterWarriorProfiles()
    {
        // =========================================================================
        // 1. SPEC 3: PROTECTION (TANK)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 1;
            p.specId = 3;
            p.role = BotRole::Tank;
            p.profileName = "Warrior_Protection_Tank";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Defensive Stance
            {
                AbilityDescriptor d;
                d.name = "Defensive Stance";
                d.rootSpellId = 71;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 71;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

            // Primary Taunt
            {
                AbilityDescriptor d;
                d.name = "Taunt";
                d.rootSpellId = 355;
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

            // Shield Wall (Emergency < 35% HP)
            {
                AbilityDescriptor d;
                d.name = "Shield Wall";
                d.rootSpellId = 871;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.missingAuraOnCaster = 871;
                d.internalThrottleMs = 30000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Last Stand (< 30% HP)
            {
                AbilityDescriptor d;
                d.name = "Last Stand";
                d.rootSpellId = 12975;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 30.0f;
                d.missingAuraOnCaster = 12975;
                d.internalThrottleMs = 30000;
                d.baseScore = 410.0f;
                p.abilities.push_back(d);
            }

            // Shield Block
            {
                AbilityDescriptor d;
                d.name = "Shield Block";
                d.rootSpellId = 2565;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 15000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Shield Slam
            {
                AbilityDescriptor d;
                d.name = "Shield Slam";
                d.rootSpellId = 23922;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Revenge
            {
                AbilityDescriptor d;
                d.name = "Revenge";
                d.rootSpellId = 6572;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 290.0f;
                p.abilities.push_back(d);
            }

            // Thunder Clap (AoE Aggro)
            {
                AbilityDescriptor d;
                d.name = "Thunder Clap";
                d.rootSpellId = 6343;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 2;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // Sunder Armor
            {
                AbilityDescriptor d;
                d.name = "Sunder Armor";
                d.rootSpellId = 7386;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Heroic Strike (Rage dump)
            {
                AbilityDescriptor d;
                d.name = "Heroic Strike";
                d.rootSpellId = 78;
                d.tags = AbilityTag::Filler | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minPowerPct = 40.0f;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            // Shield Bash
            {
                AbilityDescriptor d;
                d.name = "Shield Bash";
                d.rootSpellId = 72;
                d.tags = AbilityTag::Interrupt | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Devastate
            {
                AbilityDescriptor d;
                d.name = "Devastate";
                d.rootSpellId = 20243;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 285.0f;
                p.abilities.push_back(d);
            }

            // Demoralizing Shout
            {
                AbilityDescriptor d;
                d.name = "Demoralizing Shout";
                d.rootSpellId = 1160;
                d.tags = AbilityTag::None;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 235.0f;
                d.requireAuraMissingOnTarget = true;
                d.minPowerPct = 30.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 2. SPEC 1: ARMS (DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 1;
            p.specId = 1;
            p.role = BotRole::Dps;
            p.profileName = "Warrior_Arms_Dps";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Battle Stance: a Reborn warrior fights from a stance, and the Reborn versions of Charge, Rend, Overpower and
            // Execute (spell id + 1100000) refuse to be cast without one. The stock versions the resolver prefers carry no
            // stance requirement in this client data, so what this changes today is the stance itself, not which spells fire;
            // nothing else ever put these bots into one (only the Protection profile entered Defensive Stance).
            {
                AbilityDescriptor d;
                d.name = "Battle Stance";
                d.rootSpellId = 2457;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 2457;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

            // Battle Shout
            {
                AbilityDescriptor d;
                d.name = "Battle Shout";
                d.rootSpellId = 6673;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 6673;
                d.internalThrottleMs = 30000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // Charge
            {
                AbilityDescriptor d;
                d.name = "Charge";
                d.rootSpellId = 100;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 320.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 50.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Execute (< 20% HP)
            {
                AbilityDescriptor d;
                d.name = "Execute";
                d.rootSpellId = 5308;
                d.tags = AbilityTag::Execute | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.maxTargetHpPct = 20.0f;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Overpower
            {
                AbilityDescriptor d;
                d.name = "Overpower";
                d.rootSpellId = 7384;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 310.0f;
                p.abilities.push_back(d);
            }

            // Mortal Strike
            {
                AbilityDescriptor d;
                d.name = "Mortal Strike";
                d.rootSpellId = 12294;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 290.0f;
                p.abilities.push_back(d);
            }

            // Rend (DoT)
            {
                AbilityDescriptor d;
                d.name = "Rend";
                d.rootSpellId = 772;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Heroic Strike (Filler)
            {
                AbilityDescriptor d;
                d.name = "Heroic Strike";
                d.rootSpellId = 78;
                d.tags = AbilityTag::Filler | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minPowerPct = 40.0f;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            // Sweeping Strikes
            {
                AbilityDescriptor d;
                d.name = "Sweeping Strikes";
                d.rootSpellId = 12328;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 330.0f;
                d.missingAuraOnCaster = 12328;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.engagedEnemyCount >= 2 ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Thunder Clap
            {
                AbilityDescriptor d;
                d.name = "Thunder Clap";
                d.rootSpellId = 6343;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 275.0f;
                d.requireAuraMissingOnTarget = true;
                d.minAoETargets = 3;
                p.abilities.push_back(d);
            }

            // Cleave
            {
                AbilityDescriptor d;
                d.name = "Cleave";
                d.rootSpellId = 845;
                d.tags = AbilityTag::AoEDamage | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 260.0f;
                d.minAoETargets = 2;
                d.minPowerPct = 40.0f;
                p.abilities.push_back(d);
            }

            // Bladestorm
            {
                AbilityDescriptor d;
                d.name = "Bladestorm";
                d.rootSpellId = 46924;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Demoralizing Shout
            {
                AbilityDescriptor d;
                d.name = "Demoralizing Shout";
                d.rootSpellId = 1160;
                d.tags = AbilityTag::None;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 235.0f;
                d.requireAuraMissingOnTarget = true;
                d.minPowerPct = 30.0f;
                p.abilities.push_back(d);
            }

            // Intimidating Shout (survival: a hurt bot stuns what is hitting it)
            {
                AbilityDescriptor d;
                d.name = "Intimidating Shout";
                d.rootSpellId = 5246;
                d.tags = AbilityTag::CrowdControl | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 330.0f;
                d.maxSelfHpPct = 30.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (!ctx.targetIsBossOrElite && ctx.engagedEnemyCount >= 1) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 3. SPEC 2: FURY (DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 1;
            p.specId = 2;
            p.role = BotRole::Dps;
            p.profileName = "Warrior_Fury_Dps";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // A Fury warrior fights in Berserker Stance (Whirlwind and Intercept need it, Execute takes it or Battle Stance) once it has
            // it (level 30, from the extra trainer abilities), in Battle Stance before. The Reborn versions of these spells, which the bots
            // now cast, refuse the wrong stance.
            {
                AbilityDescriptor d;
                d.name = "Berserker Stance";
                d.rootSpellId = 2458;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 2458;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

            {
                AbilityDescriptor d;
                d.name = "Battle Stance";
                d.rootSpellId = 2457;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 2457;
                d.baseScore = 500.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return SpellResolver::ResolveSpell(ctx.bot, 2458) ? -1.0f : 0.0f;   // Berserker Stance known: that one
                };
                p.abilities.push_back(d);
            }

            // Battle Shout
            {
                AbilityDescriptor d;
                d.name = "Battle Shout";
                d.rootSpellId = 6673;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 6673;
                d.internalThrottleMs = 30000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // Bloodthirst
            {
                AbilityDescriptor d;
                d.name = "Bloodthirst";
                d.rootSpellId = 23881;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Whirlwind
            {
                AbilityDescriptor d;
                d.name = "Whirlwind";
                d.rootSpellId = 1680;
                d.tags = AbilityTag::AoEDamage | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Execute (< 20% HP)
            {
                AbilityDescriptor d;
                d.name = "Execute";
                d.rootSpellId = 5308;
                d.tags = AbilityTag::Execute | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.maxTargetHpPct = 20.0f;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Heroic Strike (Filler)
            {
                AbilityDescriptor d;
                d.name = "Heroic Strike";
                d.rootSpellId = 78;
                d.tags = AbilityTag::Filler | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minPowerPct = 35.0f;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            // Thunder Clap
            {
                AbilityDescriptor d;
                d.name = "Thunder Clap";
                d.rootSpellId = 6343;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 270.0f;
                d.requireAuraMissingOnTarget = true;
                d.minAoETargets = 3;
                p.abilities.push_back(d);
            }

            // Cleave
            {
                AbilityDescriptor d;
                d.name = "Cleave";
                d.rootSpellId = 845;
                d.tags = AbilityTag::AoEDamage | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 260.0f;
                d.minAoETargets = 2;
                d.minPowerPct = 40.0f;
                p.abilities.push_back(d);
            }

            // Demoralizing Shout
            {
                AbilityDescriptor d;
                d.name = "Demoralizing Shout";
                d.rootSpellId = 1160;
                d.tags = AbilityTag::None;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 235.0f;
                d.requireAuraMissingOnTarget = true;
                d.minPowerPct = 30.0f;
                p.abilities.push_back(d);
            }

            // Intimidating Shout (survival: a hurt bot stuns what is hitting it)
            {
                AbilityDescriptor d;
                d.name = "Intimidating Shout";
                d.rootSpellId = 5246;
                d.tags = AbilityTag::CrowdControl | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 330.0f;
                d.maxSelfHpPct = 30.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (!ctx.targetIsBossOrElite && ctx.engagedEnemyCount >= 1) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
