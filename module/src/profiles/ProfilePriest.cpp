/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Priest (Class 5)
 */

#include "profiles/ProfilePriest.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterPriestProfiles()
    {
        // =========================================================================
        // 1. SPEC 1 & 2: DISCIPLINE & HOLY (HEALER)
        // =========================================================================
        for (uint32 specId : { 1u, 2u })
        {
            CombatProfile p;
            p.classId = 5;
            p.specId = specId;
            p.role = BotRole::Healer;
            p.profileName = specId == 1 ? "Priest_Discipline_Healer" : "Priest_Holy_Healer";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Power Word: Shield (< 85% HP)
            {
                AbilityDescriptor d;
                d.name = "Power Word: Shield";
                d.rootSpellId = 17;
                d.tags = AbilityTag::Shield | AbilityTag::EmergencyHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 85.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 420.0f;
                p.abilities.push_back(d);
            }

            // Flash Heal (< 75% HP)
            {
                AbilityDescriptor d;
                d.name = "Flash Heal";
                d.rootSpellId = 2061;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 75.0f;
                d.baseScore = 360.0f;
                p.abilities.push_back(d);
            }

            // Renew (HoT < 90% HP)
            {
                AbilityDescriptor d;
                d.name = "Renew";
                d.rootSpellId = 139;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Greater Heal (< 50% HP)
            {
                AbilityDescriptor d;
                d.name = "Greater Heal";
                d.rootSpellId = 2060;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 50.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // Prayer of Healing (Group Heal)
            {
                AbilityDescriptor d;
                d.name = "Prayer of Healing";
                d.rootSpellId = 596;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minInjuredAllies = 3;
                d.injuredAllyHpPctThreshold = 75.0f;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // Penance
            {
                AbilityDescriptor d;
                d.name = "Penance";
                d.rootSpellId = 47540;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 70.0f;
                d.internalThrottleMs = 12000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Circle of Healing
            {
                AbilityDescriptor d;
                d.name = "Circle of Healing";
                d.rootSpellId = 34861;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minInjuredAllies = 3;
                d.baseScore = 390.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 2. SPEC 3: SHADOW (DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 5;
            p.specId = 3;
            p.role = BotRole::Dps;
            p.profileName = "Priest_Shadow_Dps";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Shadowform
            {
                AbilityDescriptor d;
                d.name = "Shadowform";
                d.rootSpellId = 15473;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 15473;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

            // Vampiric Touch
            {
                AbilityDescriptor d;
                d.name = "Vampiric Touch";
                d.rootSpellId = 34914;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Shadow Word: Pain
            {
                AbilityDescriptor d;
                d.name = "Shadow Word: Pain";
                d.rootSpellId = 589;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Mind Blast
            {
                AbilityDescriptor d;
                d.name = "Mind Blast";
                d.rootSpellId = 8092;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Mind Flay (Channeled Filler)
            {
                AbilityDescriptor d;
                d.name = "Mind Flay";
                d.rootSpellId = 15407;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Smite (Fallback Filler)
            {
                AbilityDescriptor d;
                d.name = "Smite";
                d.rootSpellId = 585;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
