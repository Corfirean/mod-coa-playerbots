/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Stormbringer Profiles implementation
 * Supports:
 *   - Spec 15: Lightning (Ranged Lightning Caster DPS)
 *   - Spec 13: Wind (Wind Mobility / Support DPS)
 *   - Spec 14: Maelstrom (Tempest / Hybrid Burst DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileStormbringer.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"

namespace BotAI
{
    void RegisterStormbringerProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Stormbringer - Spec 15: Lightning (RANGED CASTER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 16;
            p.specId = 15; // Lightning
            p.role = BotRole::Dps;
            p.profileName = "Stormbringer_Lightning_Caster";

            // 1. Defensive: Gale Guard (< 45% HP)
            {
                AbilityDescriptor d;
                d.name = "Gale Guard (Ward/Shield)";
                d.rootSpellId = 500923;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 2. Emergency Sustain / Self-Heal: Invigorating Surge (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Invigorating Surge (Emergency Heal)";
                d.rootSpellId = 500038;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 3. AoE Field: Static Field
            {
                AbilityDescriptor d;
                d.name = "Static Field (AoE Field)";
                d.rootSpellId = 500924;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 4. AoE Spender: Discharge (Depletes Static / restores mana)
            {
                AbilityDescriptor d;
                d.name = "Discharge (AoE Spender)";
                d.rootSpellId = 805288;
                d.tags = AbilityTag::AoEDamage | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 5. Primary Generator: Shock
            {
                AbilityDescriptor d;
                d.name = "Shock (Generator)";
                d.rootSpellId = 804020;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 6. Chain Lightning (AoE Spreader)
            {
                AbilityDescriptor d;
                d.name = "Chain Lightning";
                d.rootSpellId = 421;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 7. Lightning Bolt (Filler)
            {
                AbilityDescriptor d;
                d.name = "Lightning Bolt";
                d.rootSpellId = 403;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 8. Buff: Call of the Wind
            {
                AbilityDescriptor d;
                d.name = "Call of the Wind";
                d.rootSpellId = 804018;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 804018;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Stormbringer - Spec 13: Wind (WIND SUPPORT DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 16;
            p.specId = 13; // Wind
            p.role = BotRole::Support;
            p.profileName = "Stormbringer_Wind_Support";

            // 1. Primary Support Buff: Call of the Wind
            {
                AbilityDescriptor d;
                d.name = "Call of the Wind (Party Mana Buff)";
                d.rootSpellId = 804018;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 804018;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 2. Defensive: Gale Guard
            {
                AbilityDescriptor d;
                d.name = "Gale Guard (Shield)";
                d.rootSpellId = 500923;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 3. Support Direct Heal: Invigorating Surge (< 55% HP on lowest ally)
            {
                AbilityDescriptor d;
                d.name = "Invigorating Surge (Ally Heal)";
                d.rootSpellId = 500038;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 55.0f;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 4. AoE: Static Field
            {
                AbilityDescriptor d;
                d.name = "Static Field";
                d.rootSpellId = 500924;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 5. Spender: Discharge
            {
                AbilityDescriptor d;
                d.name = "Discharge";
                d.rootSpellId = 805288;
                d.tags = AbilityTag::AoEDamage | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 6. Generator: Shock
            {
                AbilityDescriptor d;
                d.name = "Shock";
                d.rootSpellId = 804020;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Stormbringer - Spec 14: Maelstrom (HYBRID TEMPEST DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 16;
            p.specId = 14; // Maelstrom
            p.role = BotRole::Dps;
            p.profileName = "Stormbringer_Maelstrom_Hybrid";

            // 1. Defensive: Gale Guard
            {
                AbilityDescriptor d;
                d.name = "Gale Guard";
                d.rootSpellId = 500923;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 2. Sustain: Invigorating Surge
            {
                AbilityDescriptor d;
                d.name = "Invigorating Surge";
                d.rootSpellId = 500038;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 3. Spender: Discharge (Burst)
            {
                AbilityDescriptor d;
                d.name = "Discharge (Burst)";
                d.rootSpellId = 805288;
                d.tags = AbilityTag::AoEDamage | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 4. AoE Field: Static Field
            {
                AbilityDescriptor d;
                d.name = "Static Field";
                d.rootSpellId = 500924;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 5. Generator: Shock
            {
                AbilityDescriptor d;
                d.name = "Shock (Generator)";
                d.rootSpellId = 804020;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 6. Chain Lightning
            {
                AbilityDescriptor d;
                d.name = "Chain Lightning";
                d.rootSpellId = 421;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 175.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

