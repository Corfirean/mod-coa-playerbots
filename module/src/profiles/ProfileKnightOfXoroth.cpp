/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Knight of Xoroth Profiles implementation
 * Supports:
 *   - Spec 17: Defiance (Chaos & Fire Melee Tank)
 *   - Spec 18: War (2H Chaos Melee DPS)
 *   - Spec 16: Hellfire (Destruction / Fire Hybrid DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileKnightOfXoroth.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"

namespace BotAI
{
    void RegisterKnightOfXorothProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Knight of Xoroth - Spec 17: Defiance (CHAOS TANK)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 17;
            p.specId = 17; // Defiance
            p.role = BotRole::Tank;
            p.profileName = "KnightOfXoroth_Defiance_Tank";

            // 1. Taunt
            {
                AbilityDescriptor d;
                d.name = "Taunt";
                d.rootSpellId = 355;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 450.0f;
                p.abilities.push_back(d);
            }

            // 2. Defensive Buff: Demon's Blood (< 80% HP or keep active)
            {
                AbilityDescriptor d;
                d.name = "Demon's Blood (Armor/Healing)";
                d.rootSpellId = 800999;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800999;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 3. AoE Threat / Damage: Flames of Xoroth
            {
                AbilityDescriptor d;
                d.name = "Flames of Xoroth (AoE Fire Threat)";
                d.rootSpellId = 801059;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Fire Threat Strike: Infernal Strike
            {
                AbilityDescriptor d;
                d.name = "Infernal Strike (Threat Strike)";
                d.rootSpellId = 801016;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 5. Rage Generator: Sever
            {
                AbilityDescriptor d;
                d.name = "Sever (Rage Generator)";
                d.rootSpellId = 500904;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            // 6. Ranged Pull: Chaos Bolt
            {
                AbilityDescriptor d;
                d.name = "Chaos Bolt (Pull)";
                d.rootSpellId = 802057;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Knight of Xoroth - Spec 18: War (2H CHAOS MELEE DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 17;
            p.specId = 18; // War
            p.role = BotRole::Dps;
            p.profileName = "KnightOfXoroth_War_Melee";

            // 1. Buff: Demon's Blood
            {
                AbilityDescriptor d;
                d.name = "Demon's Blood (Buff)";
                d.rootSpellId = 800999;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800999;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            // 2. Primary Heavy Strike: Infernal Strike
            {
                AbilityDescriptor d;
                d.name = "Infernal Strike (Heavy Fire Strike)";
                d.rootSpellId = 801016;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 3. AoE Burst: Flames of Xoroth
            {
                AbilityDescriptor d;
                d.name = "Flames of Xoroth (AoE Burst)";
                d.rootSpellId = 801059;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Generator: Sever
            {
                AbilityDescriptor d;
                d.name = "Sever (Generator)";
                d.rootSpellId = 500904;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 5. Heavy Ranged Nuke / Opener: Chaos Bolt
            {
                AbilityDescriptor d;
                d.name = "Chaos Bolt (Burst Nuke)";
                d.rootSpellId = 802057;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Knight of Xoroth - Spec 16: Hellfire (HYBRID CASTER/MELEE DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 17;
            p.specId = 16; // Hellfire
            p.role = BotRole::Dps;
            p.profileName = "KnightOfXoroth_Hellfire_Hybrid";

            // 1. Primary Ranged Burst: Chaos Bolt
            {
                AbilityDescriptor d;
                d.name = "Chaos Bolt (Primary Burst)";
                d.rootSpellId = 802057;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 2. AoE Fire: Flames of Xoroth
            {
                AbilityDescriptor d;
                d.name = "Flames of Xoroth";
                d.rootSpellId = 801059;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 3. Melee Fire Strike: Infernal Strike
            {
                AbilityDescriptor d;
                d.name = "Infernal Strike";
                d.rootSpellId = 801016;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 4. Rage Generator: Sever
            {
                AbilityDescriptor d;
                d.name = "Sever";
                d.rootSpellId = 500904;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 5. Buff: Demon's Blood
            {
                AbilityDescriptor d;
                d.name = "Demon's Blood";
                d.rootSpellId = 800999;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800999;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

