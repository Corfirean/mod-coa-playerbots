/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Primalist Profiles implementation
 */

#include "profiles/ProfilePrimalist.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "engine/SpecStrategyRegistry.h"
#include "Player.h"

namespace BotAI
{
    void RegisterPrimalistProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Primalist - Spec 59: Wildwalker (DPS / PET BRUISER)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 31;
            p.specId = 59; // Wildwalker
            p.role = BotRole::Dps;
            p.profileName = "Primalist_Wildwalker_Dps";

            // 1. Personal Defense: Rock Barrier (< 45% HP)
            {
                AbilityDescriptor d;
                d.name = "Rock Barrier (Shield)";
                d.rootSpellId = 503630;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.missingAuraOnCaster = 503630;
                d.internalThrottleMs = 15000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 2. Self Buffs: Boon of the Hawk
            {
                AbilityDescriptor d;
                d.name = "Boon of the Hawk";
                d.rootSpellId = 500943;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 500943;
                d.internalThrottleMs = 15000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 3. Major Burst Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Wildheart";
                d.rootSpellId = 803980;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 803980;
                d.internalThrottleMs = 30000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Magma Fissure";
                d.rootSpellId = 802793;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 20000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 4. DoTs: Seismic Tremor
            {
                AbilityDescriptor d;
                d.name = "Seismic Tremor";
                d.rootSpellId = 680442;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 5. Heavy Ranged Nukes
            {
                AbilityDescriptor d;
                d.name = "Rylak's Bite";
                d.rootSpellId = 706490;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Seismic Wave";
                d.rootSpellId = 805462;
                d.tags = AbilityTag::RangedAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Stoneshard";
                d.rootSpellId = 681112;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 110.0f;
                p.abilities.push_back(d);
            }

            // 6. Spammer / Builder
            {
                AbilityDescriptor d;
                d.name = "Terrasurge";
                d.rootSpellId = 681119;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 80.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);

            // SpecStrategy for Wildwalker
            SpecStrategy s;
            s.classId = 31;
            s.specId = 59;
            s.role = BotRole::Dps;
            s.strategyName = "Primalist_Wildwalker_Dps_Strategy";
            s.requiredState = RequiredCombatState{ 500943, 500943, {} };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::RangedAttack, 1.3f, 30.0f },
                { AbilityTag::PeriodicDamage, 1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f },
                { AbilityTag::Buff, 1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 2.0f, 60.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 50.0f;
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 2: Primalist - Spec 60: Mountain King (TANK)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 31;
            p.specId = 60; // Mountain King
            p.role = BotRole::Tank;
            p.profileName = "Primalist_MountainKing_Tank";

            // 1. Taunt & Threat Control
            {
                AbilityDescriptor d;
                d.name = "Protective Roar (Taunt)";
                d.rootSpellId = 802782;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 450.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (!ctx.victimTargetingNonTank)
                        return -1.0f;
                    return 0.0f;
                };
                p.abilities.push_back(d);
            }

            // 2. Personal Defenses: Rock Barrier & Bearskin & Boon of the Turtle
            {
                AbilityDescriptor d;
                d.name = "Rock Barrier (Tank Shield)";
                d.rootSpellId = 503630;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.missingAuraOnCaster = 503630;
                d.internalThrottleMs = 15000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Boon of the Turtle (Tank Stance)";
                d.rootSpellId = 500935;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 500935;
                d.internalThrottleMs = 15000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bearskin (Armor)";
                d.rootSpellId = 800094;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800094;
                d.internalThrottleMs = 20000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 3. Gap Closer & Control
            {
                AbilityDescriptor d;
                d.name = "Primal Rush (Charge)";
                d.rootSpellId = 500696;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Ursoc's Bellow (Debuff)";
                d.rootSpellId = 704101;
                d.tags = AbilityTag::CrowdControl | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 4. Melee Smashes (High Threat)
            {
                AbilityDescriptor d;
                d.name = "Mountain Hammer";
                d.rootSpellId = 681130;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Seismic Smash";
                d.rootSpellId = 300693;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Totemic Smash";
                d.rootSpellId = 800178;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 5. AoE Threat
            {
                AbilityDescriptor d;
                d.name = "Quake (AoE Threat)";
                d.rootSpellId = 803974;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 110.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);

            // SpecStrategy for Mountain King
            SpecStrategy s;
            s.classId = 31;
            s.specId = 60;
            s.role = BotRole::Tank;
            s.strategyName = "Primalist_MountainKing_Tank_Strategy";
            s.requiredState = RequiredCombatState{ 500935, 500935, {} };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::MeleeAttack, 1.5f, 40.0f },
                { AbilityTag::Taunt,       2.0f, 80.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::MeleeAttack, 1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 2.0f, 60.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 65.0f;
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 3: Primalist - Spec 58: Grovekeeper (HEALER & SUPPORT)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 31;
            p.specId = 58; // Grovekeeper
            p.role = BotRole::Healer;
            p.profileName = "Primalist_Grovekeeper_Healer";

            // 1. Emergency Direct Heal (< 45% HP)
            {
                AbilityDescriptor d;
                d.name = "Hand of the Earthmother (Emergency)";
                d.rootSpellId = 800135;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 45.0f;
                d.internalThrottleMs = 1500;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 2. Shield Tank: Rock Barrier on tank
            {
                AbilityDescriptor d;
                d.name = "Rock Barrier (Tank Shield)";
                d.rootSpellId = 503630;
                d.tags = AbilityTag::Shield;
                d.targetType = TargetType::TankAlly;
                d.maxTargetHpPct = 85.0f;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 12000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 3. AoE / Group Heal
            {
                AbilityDescriptor d;
                d.name = "Earthmother's Roar (Group Heal)";
                d.rootSpellId = 301306;
                d.tags = AbilityTag::DirectHeal | AbilityTag::AoEHeal;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 15000;
                d.baseScore = 180.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (ctx.criticalAllyCount >= 2 || ctx.lowestAllyHpPct < 55.0f)
                        return 50.0f;
                    return -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 4. Triage Direct Heal (< 80% HP)
            {
                AbilityDescriptor d;
                d.name = "Hand of the Earthmother (Triage)";
                d.rootSpellId = 800135;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 80.0f;
                d.internalThrottleMs = 2500;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 5. Self Buff: Boon of the Wolf
            {
                AbilityDescriptor d;
                d.name = "Boon of the Wolf";
                d.rootSpellId = 800137;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800137;
                d.internalThrottleMs = 20000;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            // 6. Offensive contribution when party is safe
            {
                AbilityDescriptor d;
                d.name = "Seismic Tremor";
                d.rootSpellId = 680442;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 40.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return (ctx.lowestAllyHpPct > 85.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Stoneshard";
                d.rootSpellId = 681112;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 20.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return (ctx.lowestAllyHpPct > 85.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);

            // SpecStrategy for Grovekeeper (Healer)
            SpecStrategy sHeal;
            sHeal.classId = 31;
            sHeal.specId = 58;
            sHeal.role = BotRole::Healer;
            sHeal.strategyName = "Primalist_Grovekeeper_Healer_Strategy";
            sHeal.requiredState = RequiredCombatState{ 800137, 800137, {} };

            sHeal.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::Shield,     1.5f, 40.0f },
                { AbilityTag::DirectHeal, 1.3f, 30.0f }
            };
            sHeal.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::EmergencyHeal, 2.0f, 60.0f },
                { AbilityTag::DirectHeal,    1.5f, 40.0f }
            };
            sHeal.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEHeal,    2.0f, 50.0f },
                { AbilityTag::DirectHeal, 1.3f, 30.0f }
            };

            sHeal.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 40) &&
                       (bot->GetHealthPct() >= 60.0f);
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(sHeal));
        }

        // -------------------------------------------------------------
        // Profile 4: Primalist - Spec 95: Geomancy (EARTH ELEMENTAL DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 31;
            p.specId = 95; // Geomancy
            p.role = BotRole::Dps;
            p.profileName = "Primalist_Geomancy_EarthDps";

            // 1. Defense: Rock Barrier (< 50% HP)
            {
                AbilityDescriptor d;
                d.name = "Rock Barrier";
                d.rootSpellId = 503630;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.missingAuraOnCaster = 503630;
                d.internalThrottleMs = 15000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 2. Cooldowns: Magma Fissure
            {
                AbilityDescriptor d;
                d.name = "Magma Fissure";
                d.rootSpellId = 802793;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 20000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 3. DoTs: Seismic Tremor
            {
                AbilityDescriptor d;
                d.name = "Seismic Tremor";
                d.rootSpellId = 680442;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 4. Heavy Earth Spells
            {
                AbilityDescriptor d;
                d.name = "Seismic Wave";
                d.rootSpellId = 805462;
                d.tags = AbilityTag::RangedAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Terrasurge";
                d.rootSpellId = 681119;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Stoneshard";
                d.rootSpellId = 681112;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 80.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);

            // SpecStrategy for Geomancy
            SpecStrategy s;
            s.classId = 31;
            s.specId = 95;
            s.role = BotRole::Dps;
            s.strategyName = "Primalist_Geomancy_Dps_Strategy";

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::PeriodicDamage, 1.5f, 40.0f },
                { AbilityTag::RangedAttack,   1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f },
                { AbilityTag::AoEDamage,   1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 2.0f, 60.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 25) &&
                       (bot->GetHealthPct() >= 50.0f);
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
