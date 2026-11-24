/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Paladin (Class 2)
 */

#include "profiles/ProfilePaladin.h"
#include "profiles/ProfileRegistry.h"
#include "profiles/ProfileHelpers.h"
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

            // Avenger's Shield
            {
                AbilityDescriptor d;
                d.name = "Avenger's Shield";
                d.rootSpellId = 31935;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 310.0f;
                p.abilities.push_back(d);
            }

            // Divine Protection
            {
                AbilityDescriptor d;
                d.name = "Divine Protection";
                d.rootSpellId = 498;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 390.0f;
                d.maxSelfHpPct = 45.0f;
                d.internalThrottleMs = 30000;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->HasAura(25771) ? -1.0f : 0.0f;  // Forbearance
                };
                p.abilities.push_back(d);
            }

            // Seal: Judgement needs one up
            {
                AbilityDescriptor d;
                d.name = "Seal of Righteousness";
                d.rootSpellId = 21084;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 360.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return HasAuraOfAny(ctx.bot, { 20154, 21084, 20375, 31801, 20165, 20166, 20164, 53736, 53739 }) ? -1.0f : 0.0f;
                };
                p.abilities.push_back(d);
            }

            // Hammer of Justice (stops a cast)
            {
                AbilityDescriptor d;
                d.name = "Hammer of Justice";
                d.rootSpellId = 853;
                d.tags = AbilityTag::Interrupt | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 385.0f;
                p.abilities.push_back(d);
            }

            // Hammer of Justice (survival: a hurt bot stuns what is hitting it)
            {
                AbilityDescriptor d;
                d.name = "Hammer of Justice";
                d.rootSpellId = 853;
                d.tags = AbilityTag::CrowdControl | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 335.0f;
                d.maxSelfHpPct = 40.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return !ctx.targetIsBossOrElite ? 0.0f : -1.0f;
                };
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

            // Beacon of Light
            {
                AbilityDescriptor d;
                d.name = "Beacon of Light";
                d.rootSpellId = 53563;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::TankAlly;
                d.baseScore = 330.0f;
                d.requireAuraMissingOnTarget = true;
                p.abilities.push_back(d);
            }

            // Sacred Shield
            {
                AbilityDescriptor d;
                d.name = "Sacred Shield";
                d.rootSpellId = 53601;
                d.tags = AbilityTag::Shield | AbilityTag::Buff;
                d.targetType = TargetType::TankAlly;
                d.baseScore = 320.0f;
                d.requireAuraMissingOnTarget = true;
                p.abilities.push_back(d);
            }

            // Divine Plea
            {
                AbilityDescriptor d;
                d.name = "Divine Plea";
                d.rootSpellId = 54428;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 340.0f;
                d.missingAuraOnCaster = 54428;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.botPowerPct < 40.0f ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Divine Illumination
            {
                AbilityDescriptor d;
                d.name = "Divine Illumination";
                d.rootSpellId = 31842;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 300.0f;
                d.missingAuraOnCaster = 31842;
                d.internalThrottleMs = 60000;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.botPowerPct < 60.0f ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Hammer of Justice (survival: a hurt bot stuns what is hitting it)
            {
                AbilityDescriptor d;
                d.name = "Hammer of Justice";
                d.rootSpellId = 853;
                d.tags = AbilityTag::CrowdControl | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 335.0f;
                d.maxSelfHpPct = 40.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return !ctx.targetIsBossOrElite ? 0.0f : -1.0f;
                };
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

            // Avenging Wrath
            {
                AbilityDescriptor d;
                d.name = "Avenging Wrath";
                d.rootSpellId = 31884;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 340.0f;
                d.missingAuraOnCaster = 31884;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->HasAura(25771) ? -1.0f : 0.0f;  // Forbearance
                };
                p.abilities.push_back(d);
            }

            // Seal: Judgement needs one up (the better one first)
            {
                AbilityDescriptor d;
                d.name = "Seal of Command";
                d.rootSpellId = 20375;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 362.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return HasAuraOfAny(ctx.bot, { 20154, 21084, 20375, 31801, 20165, 20166, 20164, 53736, 53739 }) ? -1.0f : 0.0f;
                };
                p.abilities.push_back(d);
            }

            // Seal: Judgement needs one up
            {
                AbilityDescriptor d;
                d.name = "Seal of Righteousness";
                d.rootSpellId = 21084;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 360.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return HasAuraOfAny(ctx.bot, { 20154, 21084, 20375, 31801, 20165, 20166, 20164, 53736, 53739 }) ? -1.0f : 0.0f;
                };
                p.abilities.push_back(d);
            }

            // Hammer of Justice (stops a cast)
            {
                AbilityDescriptor d;
                d.name = "Hammer of Justice";
                d.rootSpellId = 853;
                d.tags = AbilityTag::Interrupt | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 385.0f;
                p.abilities.push_back(d);
            }

            // Hammer of Justice (survival: a hurt bot stuns what is hitting it)
            {
                AbilityDescriptor d;
                d.name = "Hammer of Justice";
                d.rootSpellId = 853;
                d.tags = AbilityTag::CrowdControl | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 335.0f;
                d.maxSelfHpPct = 40.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return !ctx.targetIsBossOrElite ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
