/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Venomancer Profiles implementation
 * Supports:
 *   - Spec 52: Fortitude (Scorpid Shell Heavy Tank)
 *   - Spec 101: Vizier (Antidote & Venom Healer)
 *   - Spec 53: Stalking (Melee Spider/Serpent Stalker DPS)
 *   - Spec 54: Venom (Ranged Poison Caster DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileVenomancer.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterVenomancerProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Venomancer - Spec 52: Fortitude (SCORPID TANK)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 29; // Venomancer
            p.specId = 52; // Fortitude
            p.role = BotRole::Tank;
            p.profileName = "Venomancer_Fortitude_Tank";

            // 1. Primary Taunt
            {
                AbilityDescriptor d;
                d.name = "Taunt";
                d.rootSpellId = 355;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 450.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Pinch (Scorpid Threat Taunt)";
                d.rootSpellId = 704235;
                d.tags = AbilityTag::Taunt | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 440.0f;
                p.abilities.push_back(d);
            }

            // 2. Active Mitigation: Reinforced Shell (< 50% HP)
            {
                AbilityDescriptor d;
                d.name = "Reinforced Shell";
                d.rootSpellId = 705966;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fury of Anub'Rekhan";
                d.rootSpellId = 800894;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 3. Stance / Scorpid Form
            {
                AbilityDescriptor d;
                d.name = "Scorpid Form";
                d.rootSpellId = 803183;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 803183;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 4. Weapon Buff
            {
                AbilityDescriptor d;
                d.name = "Envenom Weapons";
                d.rootSpellId = 803177;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 803177;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 5. Threat Strikes
            {
                AbilityDescriptor d;
                d.name = "Scorpid Claw";
                d.rootSpellId = 803198;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Venom Fang";
                d.rootSpellId = 800880;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Weakening Venom";
                d.rootSpellId = 805778;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Venomancer - Spec 101: Vizier (ANTIDOTE & VENOM HEALER)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 29;
            p.specId = 101; // Vizier
            p.role = BotRole::Healer;
            p.profileName = "Venomancer_Vizier_Healer";

            // 1. Emergency Critical Direct Heal: Lifeblood (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Lifeblood";
                d.rootSpellId = 804963;
                d.tags = AbilityTag::DirectHeal | AbilityTag::DefensiveCD;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 40.0f;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // 2. Primary Direct Heal: Shadra's Vigil (< 80% HP)
            {
                AbilityDescriptor d;
                d.name = "Shadra's Vigil";
                d.rootSpellId = 800870;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 3. HoT: Green Salve (< 90% HP)
            {
                AbilityDescriptor d;
                d.name = "Green Salve";
                d.rootSpellId = 800902;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 4. Brood Swarm
            {
                AbilityDescriptor d;
                d.name = "Spawn (Broodlings)";
                d.rootSpellId = 805568;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 5. Offensive Weaving
            {
                AbilityDescriptor d;
                d.name = "Venom Bolt";
                d.rootSpellId = 800869;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Venomancer - Spec 53: Stalking (MELEE STALKER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 29;
            p.specId = 53; // Stalking
            p.role = BotRole::Dps;
            p.profileName = "Venomancer_Stalking_Dps";

            // 1. Stance / Stalker Form
            {
                AbilityDescriptor d;
                d.name = "Stalker Form";
                d.rootSpellId = 800841;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800841;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 2. Weapon Buff
            {
                AbilityDescriptor d;
                d.name = "Envenom Weapons";
                d.rootSpellId = 803177;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 803177;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 3. Gap Closer: Toxic Stride (> 8 yards)
            {
                AbilityDescriptor d;
                d.name = "Toxic Stride";
                d.rootSpellId = 504347;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 100.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 4. Major Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Avatar of Shadra";
                d.rootSpellId = 503856;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Adrenal Venom";
                d.rootSpellId = 805775;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // 5. Heavy Strikes
            {
                AbilityDescriptor d;
                d.name = "Ambush Predator";
                d.rootSpellId = 800878;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Venom Fang";
                d.rootSpellId = 800880;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spindlebind";
                d.rootSpellId = 800887;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spawn";
                d.rootSpellId = 805568;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 4: Venomancer - Spec 54: Venom (RANGED POISON CASTER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 29;
            p.specId = 54; // Venom
            p.role = BotRole::Dps;
            p.profileName = "Venomancer_Venom_Dps";

            // 1. Emergency Defense: Reinforced Shell (< 35% HP)
            {
                AbilityDescriptor d;
                d.name = "Reinforced Shell";
                d.rootSpellId = 705966;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // 2. Major Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Rot Lich";
                d.rootSpellId = 92142;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Master of Venoms";
                d.rootSpellId = 504326;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // 3. Poisons & DoTs
            {
                AbilityDescriptor d;
                d.name = "Weakening Venom";
                d.rootSpellId = 805778;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spindlebind";
                d.rootSpellId = 800887;
                d.tags = AbilityTag::RangedAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 4. Brood Minions
            {
                AbilityDescriptor d;
                d.name = "Spawn";
                d.rootSpellId = 805568;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 5. Filler: Venom Bolt
            {
                AbilityDescriptor d;
                d.name = "Venom Bolt";
                d.rootSpellId = 800869;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

