/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Paladin (Class 2)
 */

#include "profiles/ProfilePaladin.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterPaladinProfiles()
    {
        // =========================================================================
        // 1. SPEC 2: PROTECTION (TANK)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 2;
            p.specId = 2;
            p.role = BotRole::Tank;
            p.profileName = "Paladin_Protection_Tank";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Righteous Fury
            {
                AbilityDescriptor d;
                d.name = "Righteous Fury";
                d.rootSpellId = 25780;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 25780;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

            // Hand of Reckoning / Taunt
            {
                AbilityDescriptor d;
                d.name = "Hand of Reckoning";
                d.rootSpellId = 62124;
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

            // Divine Shield / Lay on Hands (< 25% HP)
            {
                AbilityDescriptor d;
                d.name = "Lay on Hands";
                d.rootSpellId = 633;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::EmergencyHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 25.0f;
                d.internalThrottleMs = 60000;
                d.baseScore = 480.0f;
                p.abilities.push_back(d);
            }

            // Holy Shield
            {
                AbilityDescriptor d;
                d.name = "Holy Shield";
                d.rootSpellId = 20925;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 20925;
                d.internalThrottleMs = 8000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // Consecration
            {
                AbilityDescriptor d;
                d.name = "Consecration";
                d.rootSpellId = 26573;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Hammer of the Righteous
            {
                AbilityDescriptor d;
                d.name = "Hammer of the Righteous";
                d.rootSpellId = 53595;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Judgement
            {
                AbilityDescriptor d;
                d.name = "Judgement of Light";
                d.rootSpellId = 20271;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 2. SPEC 1: HOLY (HEALER)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 2;
            p.specId = 1;
            p.role = BotRole::Healer;
            p.profileName = "Paladin_Holy_Healer";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Lay on Hands (< 20% HP)
            {
                AbilityDescriptor d;
                d.name = "Lay on Hands (Emergency)";
                d.rootSpellId = 633;
                d.tags = AbilityTag::EmergencyHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 20.0f;
                d.internalThrottleMs = 60000;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

            // Holy Shock (< 60% HP)
            {
                AbilityDescriptor d;
                d.name = "Holy Shock";
                d.rootSpellId = 20473;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 60.0f;
                d.internalThrottleMs = 6000;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // Flash of Light (< 80% HP)
            {
                AbilityDescriptor d;
                d.name = "Flash of Light";
                d.rootSpellId = 19750;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Holy Light (< 50% HP)
            {
                AbilityDescriptor d;
                d.name = "Holy Light";
                d.rootSpellId = 635;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 50.0f;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Cleanse
            {
                AbilityDescriptor d;
                d.name = "Cleanse";
                d.rootSpellId = 4987;
                d.tags = AbilityTag::Cleanse;
                d.targetType = TargetType::AnyInjuredAlly;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 3. SPEC 3: RETRIBUTION (DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 2;
            p.specId = 3;
            p.role = BotRole::Dps;
            p.profileName = "Paladin_Retribution_Dps";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Crusader Strike
            {
                AbilityDescriptor d;
                d.name = "Crusader Strike";
                d.rootSpellId = 35395;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Judgement
            {
                AbilityDescriptor d;
                d.name = "Judgement of Wisdom";
                d.rootSpellId = 20271;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Divine Storm
            {
                AbilityDescriptor d;
                d.name = "Divine Storm";
                d.rootSpellId = 53385;
                d.tags = AbilityTag::AoEDamage | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 290.0f;
                p.abilities.push_back(d);
            }

            // Hammer of Wrath (< 20% HP)
            {
                AbilityDescriptor d;
                d.name = "Hammer of Wrath";
                d.rootSpellId = 24275;
                d.tags = AbilityTag::Execute | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.maxTargetHpPct = 20.0f;
                d.baseScore = 420.0f;
                p.abilities.push_back(d);
            }

            // Consecration
            {
                AbilityDescriptor d;
                d.name = "Consecration";
                d.rootSpellId = 26573;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
