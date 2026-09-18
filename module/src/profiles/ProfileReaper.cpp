/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Reaper Profiles implementation
 */

#include "profiles/ProfileReaper.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterReaperProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Reaper - Spec 57: Domination (DARK SOUL PLATE TANK)
        // Heavy armor, soul shielding, high threat and leech.
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 30;
            p.specId = 57; // Domination
            p.role = BotRole::Tank;
            p.profileName = "Reaper_Domination_Tank";

            // 1. Personal Defensives
            {
                AbilityDescriptor d;
                d.name = "Underwalk (Shadow Escape)";
                d.rootSpellId = 800797;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bolstered Form (Plate Armor)";
                d.rootSpellId = 680337;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 70.0f;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Soul Protector (Barrier)";
                d.rootSpellId = 300553;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 60.0f;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 2. Taunt / Threat
            {
                AbilityDescriptor d;
                d.name = "Tormentor (Taunt)";
                d.rootSpellId = 92147;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (!ctx.victim) return -1.0f;
                    Unit* curVictim = ctx.victim->GetVictim();
                    if (curVictim && curVictim != ctx.bot && curVictim->IsPlayer())
                        return 210.0f;
                    return -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 3. Gap Closer
            {
                AbilityDescriptor d;
                d.name = "Scythe Rush (Gap Closer)";
                d.rootSpellId = 500359;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 190.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 4. Melee Attacks & Leech Sustain
            {
                AbilityDescriptor d;
                d.name = "Soul Strike (Leech Attack)";
                d.rootSpellId = 500517;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::DirectHeal;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Murder (Heavy Strike)";
                d.rootSpellId = 500376;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Deathwind (AoE Leech)";
                d.rootSpellId = 800174;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reap (Primary Builder)";
                d.rootSpellId = 500357;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Dirge (Offhand Finisher)";
                d.rootSpellId = 801328;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Reaper - Spec 56: Harvest (MELEE 2H/DUAL WIELD DPS)
        // Pure melee executioner with scythe slashes and dark momentum.
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 30;
            p.specId = 56; // Harvest
            p.role = BotRole::Dps;
            p.profileName = "Reaper_Harvest_MeleeDps";

            // 1. Personal Defense
            {
                AbilityDescriptor d;
                d.name = "Underwalk (Shadow Escape)";
                d.rootSpellId = 800797;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bolstered Form";
                d.rootSpellId = 680337;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 2. Gap Closer
            {
                AbilityDescriptor d;
                d.name = "Scythe Rush (Gap Closer)";
                d.rootSpellId = 500359;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 210.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 3. Melee Slashes & Finishers
            {
                AbilityDescriptor d;
                d.name = "Murder (Execution Strike)";
                d.rootSpellId = 500376;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Soul Strike (Weapon Attack)";
                d.rootSpellId = 500517;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Shudder Scythe (Cleave)";
                d.rootSpellId = 801321;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 165.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Dirge (Finisher)";
                d.rootSpellId = 801328;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 155.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Deathwind (Area Leech)";
                d.rootSpellId = 800174;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reap (Primary Generator)";
                d.rootSpellId = 500357;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Reaper - Spec 55: Soul (SHADOW & NETHER CASTER DPS)
        // Ranged dark magic, soul draining, and death hexes.
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 30;
            p.specId = 55; // Soul
            p.role = BotRole::Dps;
            p.profileName = "Reaper_Soul_CasterDps";

            // 1. Personal Defense & Mana/Soul Management
            {
                AbilityDescriptor d;
                d.name = "Underwalk (Defensive)";
                d.rootSpellId = 800797;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Soul Tap (Resource Regen)";
                d.rootSpellId = 807397;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::Self;
                d.minSelfHpPct = 40.0f;
                d.baseScore = 210.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (ctx.botPowerPct < 40.0f && ctx.botHpPct > 40.0f)
                        return 210.0f;
                    return -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 2. Ranged Shadow Spells & DoTs
            {
                AbilityDescriptor d;
                d.name = "Tormented Souls (Shadow DoT)";
                d.rootSpellId = 500483;
                d.tags = AbilityTag::PeriodicDamage | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Withering Touch (Curse DoT)";
                d.rootSpellId = 573071;
                d.tags = AbilityTag::PeriodicDamage | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Soul Harvest (Burst Channel)";
                d.rootSpellId = 504012;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Ghastly Screech (AoE Shadow)";
                d.rootSpellId = 806146;
                d.tags = AbilityTag::AoEDamage | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Deathwind (AoE Leech)";
                d.rootSpellId = 800174;
                d.tags = AbilityTag::AoEDamage | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reap (Fallback Strike)";
                d.rootSpellId = 500357;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 100.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

