/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Barbarian Profiles implementation
 * Supports:
 *   - Spec 1: Headhunting (Ranged Thrown / Bleed DPS)
 *   - Spec 2: Brutality (Melee Dual-Wield Berserker DPS)
 *   - Spec 3: Ancestry (Shamanic Melee / Support DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileBarbarian.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"

namespace BotAI
{
    void RegisterBarbarianProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Barbarian - Spec 1: Headhunting (RANGED THROWN DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 12;
            p.specId = 1; // Headhunting
            p.role = BotRole::Dps;
            p.profileName = "Barbarian_Headhunting_Ranged";

            // 1. War Cry (Party Buff: Attack Speed & AP)
            {
                AbilityDescriptor d;
                d.name = "War Cry (Party Buff)";
                d.rootSpellId = 500995;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 500995;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 2. Burst Offensive CD: Savage Rage
            {
                AbilityDescriptor d;
                d.name = "Savage Rage (Haste Burst)";
                d.rootSpellId = 800954;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 3. Mobility / Leap / Root: Berserker Rush
            {
                AbilityDescriptor d;
                d.name = "Berserker Rush (Charge/Root)";
                d.rootSpellId = 560518;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Ranged Thrown Attacks
            {
                AbilityDescriptor d;
                d.name = "Berserker Axe (Axe Throw Burst)";
                d.rootSpellId = 804138;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Headhunter's Spear (Energy Builder)";
                d.rootSpellId = 804137;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Throw Weapon (Filler)";
                d.rootSpellId = 804136;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 5. Melee Fallback: Ancestral Strike, Crush, Whirling Advance
            {
                AbilityDescriptor d;
                d.name = "Ancestral Strike";
                d.rootSpellId = 801576;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Crush (Frontal Stun)";
                d.rootSpellId = 500915;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Whirling Advance (AoE)";
                d.rootSpellId = 500919;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Barbarian - Spec 2: Brutality (MELEE BERSERKER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 12;
            p.specId = 2; // Brutality
            p.role = BotRole::Dps;
            p.profileName = "Barbarian_Brutality_Melee";

            // 1. Buff: War Cry
            {
                AbilityDescriptor d;
                d.name = "War Cry (Party Buff)";
                d.rootSpellId = 500995;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 500995;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 2. Burst: Savage Rage
            {
                AbilityDescriptor d;
                d.name = "Savage Rage (Haste Burst)";
                d.rootSpellId = 800954;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 3. Gap Closer: Berserker Rush
            {
                AbilityDescriptor d;
                d.name = "Berserker Rush (Gap Closer)";
                d.rootSpellId = 560518;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Melee Strikes
            {
                AbilityDescriptor d;
                d.name = "Ancestral Strike (Heavy Strike)";
                d.rootSpellId = 801576;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Crush (Frontal Cone Stun)";
                d.rootSpellId = 500915;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Whirling Advance (AoE Advance)";
                d.rootSpellId = 500919;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 195.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Berserker Axe (Mid-Range Throw)";
                d.rootSpellId = 804138;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 5. Gap Filler: Headhunter's Spear & Throw Weapon
            {
                AbilityDescriptor d;
                d.name = "Headhunter's Spear (Ranged Pull)";
                d.rootSpellId = 804137;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Throw Weapon (Ranged Filler)";
                d.rootSpellId = 804136;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 110.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Barbarian - Spec 3: Ancestry (SUPPORT MELEE DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 12;
            p.specId = 3; // Ancestry
            p.role = BotRole::Support;
            p.profileName = "Barbarian_Ancestry_Support";

            // 1. War Cry (High Priority Support Buff)
            {
                AbilityDescriptor d;
                d.name = "War Cry (Party Buff)";
                d.rootSpellId = 500995;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 500995;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 2. Savage Rage
            {
                AbilityDescriptor d;
                d.name = "Savage Rage (Burst)";
                d.rootSpellId = 800954;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 3. Melee Rotation
            {
                AbilityDescriptor d;
                d.name = "Ancestral Strike";
                d.rootSpellId = 801576;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Crush (Stun/Damage)";
                d.rootSpellId = 500915;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Whirling Advance (AoE)";
                d.rootSpellId = 500919;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 4. Ranged Support / Pull
            {
                AbilityDescriptor d;
                d.name = "Berserker Rush";
                d.rootSpellId = 560518;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Headhunter's Spear";
                d.rootSpellId = 804137;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

