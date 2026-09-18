/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Guardian Profiles implementation
 * Supports:
 *   - Spec 21: Vanguard (Heavy Protection Shield Tank)
 *   - Spec 19: Gladiator (1H + Shield Physical DPS)
 *   - Spec 20: Inspiration (Formation & Aura Support DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileGuardian.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"

namespace BotAI
{
    void RegisterGuardianProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Guardian - Spec 21: Vanguard (HEAVY SHIELD TANK)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 18;
            p.specId = 21; // Vanguard
            p.role = BotRole::Tank;
            p.profileName = "Guardian_Vanguard_Tank";

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

            // 2. Active Mitigation: Raise Shield (< 65% HP)
            {
                AbilityDescriptor d;
                d.name = "Raise Shield (Block Mitigation)";
                d.rootSpellId = 500168;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 3. Stance: Tower Formation
            {
                AbilityDescriptor d;
                d.name = "Tower Formation (Tank Stance)";
                d.rootSpellId = 800317;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800317;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 4. Stun / Gap Closer: Ram
            {
                AbilityDescriptor d;
                d.name = "Ram (Shield Charge/Stun)";
                d.rootSpellId = 802284;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 5. Primary Threat Strike: Pulverize
            {
                AbilityDescriptor d;
                d.name = "Pulverize (Shield Slam)";
                d.rootSpellId = 800311;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 6. Counterattack: Reprisal
            {
                AbilityDescriptor d;
                d.name = "Reprisal (Counterattack)";
                d.rootSpellId = 800316;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 7. AoE Threat / AP Debuff: Broad Sweep
            {
                AbilityDescriptor d;
                d.name = "Broad Sweep (AoE Cleave)";
                d.rootSpellId = 805150;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Guardian - Spec 19: Gladiator (1H + SHIELD PHYSICAL DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 18;
            p.specId = 19; // Gladiator
            p.role = BotRole::Dps;
            p.profileName = "Guardian_Gladiator_Dps";

            // 1. Defensive: Raise Shield (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Raise Shield (Defensive)";
                d.rootSpellId = 500168;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 2. Formation: Line Formation
            {
                AbilityDescriptor d;
                d.name = "Line Formation (Speed & Damage)";
                d.rootSpellId = 803130;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 803130;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 3. Stun / Armor Shred: Ram
            {
                AbilityDescriptor d;
                d.name = "Ram (Charge / Stun)";
                d.rootSpellId = 802284;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Shield Strike: Pulverize
            {
                AbilityDescriptor d;
                d.name = "Pulverize (Heavy Shield Slam)";
                d.rootSpellId = 800311;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 5. Counterattack Burst: Reprisal
            {
                AbilityDescriptor d;
                d.name = "Reprisal (Burst Counter)";
                d.rootSpellId = 800316;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 6. AoE Cleave: Broad Sweep
            {
                AbilityDescriptor d;
                d.name = "Broad Sweep (AoE)";
                d.rootSpellId = 805150;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 185.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Guardian - Spec 20: Inspiration (SUPPORT DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 18;
            p.specId = 20; // Inspiration
            p.role = BotRole::Support;
            p.profileName = "Guardian_Inspiration_Support";

            // 1. Support Formation: Line Formation
            {
                AbilityDescriptor d;
                d.name = "Line Formation (Party Buff)";
                d.rootSpellId = 803130;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 803130;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 2. Defensive: Raise Shield (< 45% HP)
            {
                AbilityDescriptor d;
                d.name = "Raise Shield";
                d.rootSpellId = 500168;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 3. AoE Attack Power Debuff: Broad Sweep
            {
                AbilityDescriptor d;
                d.name = "Broad Sweep (Party Protection Debuff)";
                d.rootSpellId = 805150;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 4. Stun Support: Ram
            {
                AbilityDescriptor d;
                d.name = "Ram (Support Stun)";
                d.rootSpellId = 802284;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 5. Strike: Pulverize
            {
                AbilityDescriptor d;
                d.name = "Pulverize";
                d.rootSpellId = 800311;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 6. Strike: Reprisal
            {
                AbilityDescriptor d;
                d.name = "Reprisal";
                d.rootSpellId = 800316;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 175.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

