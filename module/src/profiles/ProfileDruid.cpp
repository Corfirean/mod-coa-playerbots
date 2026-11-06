/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Druid (Class 11)
 */

#include "profiles/ProfileDruid.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterDruidProfiles()
    {
        // =========================================================================
        // 1. SPEC 4: GUARDIAN BEAR (TANK)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 11;
            p.specId = 4;
            p.role = BotRole::Tank;
            p.profileName = "Druid_Guardian_Tank";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Bear Form / Dire Bear Form
            {
                AbilityDescriptor d;
                d.name = "Dire Bear Form";
                d.rootSpellId = 9634;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 500.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetShapeshiftForm() == FORM_BEAR || ctx.bot->GetShapeshiftForm() == FORM_DIREBEAR ? -1.0f : 100.0f;
                };
                p.abilities.push_back(d);
            }

            // Growl (Taunt)
            {
                AbilityDescriptor d;
                d.name = "Growl";
                d.rootSpellId = 6795;
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

            // Frenzied Regeneration (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Frenzied Regeneration";
                d.rootSpellId = 22842;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.internalThrottleMs = 60000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Barkskin (< 60% HP)
            {
                AbilityDescriptor d;
                d.name = "Barkskin";
                d.rootSpellId = 22812;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 60.0f;
                d.missingAuraOnCaster = 22812;
                d.internalThrottleMs = 60000;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // Mangle (Bear)
            {
                AbilityDescriptor d;
                d.name = "Mangle (Bear)";
                d.rootSpellId = 33878;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Maul
            {
                AbilityDescriptor d;
                d.name = "Maul";
                d.rootSpellId = 6807;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minPowerPct = 30.0f;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Swipe (Bear - AoE)
            {
                AbilityDescriptor d;
                d.name = "Swipe (Bear)";
                d.rootSpellId = 779;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 2;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // Demoralizing Roar
            {
                AbilityDescriptor d;
                d.name = "Demoralizing Roar";
                d.rootSpellId = 99;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 2. SPEC 3: RESTORATION (HEALER)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 11;
            p.specId = 3;
            p.role = BotRole::Healer;
            p.profileName = "Druid_Restoration_Healer";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Swiftmend (< 50% HP Emergency)
            {
                AbilityDescriptor d;
                d.name = "Swiftmend";
                d.rootSpellId = 18562;
                d.tags = AbilityTag::EmergencyHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 50.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 440.0f;
                p.abilities.push_back(d);
            }

            // Rejuvenation (HoT < 90% HP)
            {
                AbilityDescriptor d;
                d.name = "Rejuvenation";
                d.rootSpellId = 774;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Lifebloom (HoT)
            {
                AbilityDescriptor d;
                d.name = "Lifebloom";
                d.rootSpellId = 33763;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::TankAlly;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Regrowth (< 70% HP)
            {
                AbilityDescriptor d;
                d.name = "Regrowth";
                d.rootSpellId = 8936;
                d.tags = AbilityTag::DirectHeal | AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 70.0f;
                d.baseScore = 330.0f;
                p.abilities.push_back(d);
            }

            // Healing Touch (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Healing Touch";
                d.rootSpellId = 5185;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 40.0f;
                d.baseScore = 360.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 3. SPEC 1: BALANCE (DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 11;
            p.specId = 1;
            p.role = BotRole::Dps;
            p.profileName = "Druid_Balance_Dps";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Moonkin Form
            {
                AbilityDescriptor d;
                d.name = "Moonkin Form";
                d.rootSpellId = 24858;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 500.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetShapeshiftForm() == FORM_MOONKIN ? -1.0f : 100.0f;
                };
                p.abilities.push_back(d);
            }

            // Moonfire (DoT)
            {
                AbilityDescriptor d;
                d.name = "Moonfire";
                d.rootSpellId = 8921;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Insect Swarm (DoT)
            {
                AbilityDescriptor d;
                d.name = "Insect Swarm";
                d.rootSpellId = 5570;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Starfire
            {
                AbilityDescriptor d;
                d.name = "Starfire";
                d.rootSpellId = 2912;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Wrath (Filler)
            {
                AbilityDescriptor d;
                d.name = "Wrath";
                d.rootSpellId = 5176;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 4. SPEC 2: FERAL CAT (DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 11;
            p.specId = 2;
            p.role = BotRole::Dps;
            p.profileName = "Druid_Feral_Dps";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Cat Form
            {
                AbilityDescriptor d;
                d.name = "Cat Form";
                d.rootSpellId = 768;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 500.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetShapeshiftForm() == FORM_CAT ? -1.0f : 100.0f;
                };
                p.abilities.push_back(d);
            }

            // Savage Roar / Rip (Finisher at 4-5 CP)
            {
                AbilityDescriptor d;
                d.name = "Rip";
                d.rootSpellId = 1079;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 360.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetComboPoints() >= 4 ? 40.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Ferocious Bite (Finisher)
            {
                AbilityDescriptor d;
                d.name = "Ferocious Bite";
                d.rootSpellId = 22568;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 340.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetComboPoints() >= 4 ? 30.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Mangle (Cat)
            {
                AbilityDescriptor d;
                d.name = "Mangle (Cat)";
                d.rootSpellId = 33876;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Claw / Shred (Builder)
            {
                AbilityDescriptor d;
                d.name = "Claw";
                d.rootSpellId = 1082;
                d.tags = AbilityTag::Filler | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
