/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Felsworn Profiles implementation
 * Supports:
 *   - Spec 8: Slayer (Melee Havoc / Dual-Wield DPS)
 *   - Spec 9: Tyrant (Demon Metamorphosis Tank)
 *   - Spec 7: Infernal (Chaos / Fire Ranged Caster DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileFelsworn.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"

namespace BotAI
{
    void RegisterFelswornProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Felsworn - Spec 8: Slayer (MELEE HAVOC DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 14;
            p.specId = 8; // Slayer
            p.role = BotRole::Dps;
            p.profileName = "Felsworn_Slayer_Melee";

            // 1. Steroid / Metamorphosis: The Demon Within
            {
                AbilityDescriptor d;
                d.name = "The Demon Within (Burst)";
                d.rootSpellId = 800222;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 2. Crowd Control: Chaos Nova (AoE Stun)
            {
                AbilityDescriptor d;
                d.name = "Chaos Nova (AoE Stun)";
                d.rootSpellId = 802025;
                d.tags = AbilityTag::CrowdControl | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 3. Mobility: Chaos Rush (Fel Rush)
            {
                AbilityDescriptor d;
                d.name = "Chaos Rush (Fel Rush)";
                d.rootSpellId = 500028;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 4. Area Fire Pulse: Immolation Aura
            {
                AbilityDescriptor d;
                d.name = "Immolation Aura";
                d.rootSpellId = 800207;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800207;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 5. Periodic: Demonfire Pact (DoT)
            {
                AbilityDescriptor d;
                d.name = "Demonfire Pact (DoT)";
                d.rootSpellId = 800031;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 195.0f;
                p.abilities.push_back(d);
            }

            // 6. High Damage Spender: Felrend
            {
                AbilityDescriptor d;
                d.name = "Felrend (Heavy Strike)";
                d.rootSpellId = 800210;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 7. Primary Builder: Twin Slice
            {
                AbilityDescriptor d;
                d.name = "Twin Slice (Builder)";
                d.rootSpellId = 801901;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 8. Self Buff: Illidari Intuition
            {
                AbilityDescriptor d;
                d.name = "Illidari Intuition";
                d.rootSpellId = 800212;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800212;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Felsworn - Spec 9: Tyrant (DEMON TANK)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 14;
            p.specId = 9; // Tyrant
            p.role = BotRole::Tank;
            p.profileName = "Felsworn_Tyrant_Tank";

            // 1. Taunt: Standard Tank Taunt
            {
                AbilityDescriptor d;
                d.name = "Taunt";
                d.rootSpellId = 355;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 450.0f;
                p.abilities.push_back(d);
            }

            // 2. Metamorphosis / Form: The Demon Within (< 70% HP or boss combat)
            {
                AbilityDescriptor d;
                d.name = "The Demon Within (Demon Form)";
                d.rootSpellId = 800222;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 3. AoE Control: Chaos Nova (AoE Stun)
            {
                AbilityDescriptor d;
                d.name = "Chaos Nova (AoE Stun)";
                d.rootSpellId = 802025;
                d.tags = AbilityTag::CrowdControl | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 4. AoE Threat: Immolation Aura
            {
                AbilityDescriptor d;
                d.name = "Immolation Aura (AoE Threat)";
                d.rootSpellId = 800207;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800207;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 5. AoE Slam / Threat: Infernal Strike
            {
                AbilityDescriptor d;
                d.name = "Infernal Strike (AoE Slam)";
                d.rootSpellId = 801016;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 6. High Threat Spender: Felrend
            {
                AbilityDescriptor d;
                d.name = "Felrend (Threat Strike)";
                d.rootSpellId = 800210;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 7. Threat / Debuff: Demonfire Pact
            {
                AbilityDescriptor d;
                d.name = "Demonfire Pact";
                d.rootSpellId = 800031;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 8. Primary Generator: Twin Slice
            {
                AbilityDescriptor d;
                d.name = "Twin Slice";
                d.rootSpellId = 801901;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Felsworn - Spec 7: Infernal (CHAOS / FIRE CASTER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 14;
            p.specId = 7; // Infernal
            p.role = BotRole::Dps;
            p.profileName = "Felsworn_Infernal_Caster";

            // 1. Steroid: The Demon Within
            {
                AbilityDescriptor d;
                d.name = "The Demon Within";
                d.rootSpellId = 800222;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 2. Crowd Control: Chaos Nova
            {
                AbilityDescriptor d;
                d.name = "Chaos Nova";
                d.rootSpellId = 802025;
                d.tags = AbilityTag::CrowdControl | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 3. Primary Ranged Nuke: Tormentor
            {
                AbilityDescriptor d;
                d.name = "Tormentor (Chaos Bolt)";
                d.rootSpellId = 800162;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 4. DoT: Demonfire Pact
            {
                AbilityDescriptor d;
                d.name = "Demonfire Pact (DoT)";
                d.rootSpellId = 800031;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 5. AoE: Immolation Aura
            {
                AbilityDescriptor d;
                d.name = "Immolation Aura";
                d.rootSpellId = 800207;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800207;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 6. Melee Spender Fallback: Felrend
            {
                AbilityDescriptor d;
                d.name = "Felrend";
                d.rootSpellId = 800210;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            // 7. Melee Generator Fallback: Twin Slice
            {
                AbilityDescriptor d;
                d.name = "Twin Slice";
                d.rootSpellId = 801901;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

