/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Chronomancer Profiles & Spec Strategies
 *
 * Specializations:
 *   - Spec 31: Time (Temporal Healer / Chrono Shielder)
 *   - Spec 32: Infinite (Arcane / Temporal Caster DPS)
 *   - Spec 33: Artificer (Clockwork / Gadget Temporal DPS)
 */

#include "profiles/ProfileChronomancer.h"
#include "profiles/ProfileRegistry.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterChronomancerProfiles()
    {
        // =========================================================================
        // 1. SPEC 31: TIME (TEMPORAL HEALER)
        // =========================================================================
        // Contract:
        // - Canonical Role: Healer
        // - Baseline State: Caster
        // - Resource: Mana (strict conservation in Recovery)
        // - Triage: Emergency Rewind -> Shields -> Group Waves -> HoT maintenance
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 22; // Chronomancer
            p.specId = 31;  // Time
            p.role = BotRole::Healer;
            p.profileName = "Chronomancer_Time_Healer";

            // Emergency Rewinds
            {
                AbilityDescriptor d;
                d.name = "Do Over";
                d.rootSpellId = 800669;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DefensiveCD;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 30.0f;
                d.internalThrottleMs = 6000;
                d.baseScore = 420.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Rewind";
                d.rootSpellId = 801294;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DefensiveCD;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 35.0f;
                d.internalThrottleMs = 8000;
                d.baseScore = 390.0f;
                p.abilities.push_back(d);
            }

            // Temporal Shield
            {
                AbilityDescriptor d;
                d.name = "Infinite Shield";
                d.rootSpellId = 520457;
                d.tags = AbilityTag::Shield;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 55.0f;
                d.internalThrottleMs = 12000;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Group Temporal Heal
            {
                AbilityDescriptor d;
                d.name = "Waves of Time";
                d.rootSpellId = 801277;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::Self;
                d.minInjuredAllies = 2;
                d.injuredAllyHpPctThreshold = 80.0f;
                d.internalThrottleMs = 6000;
                d.baseScore = 310.0f;
                p.abilities.push_back(d);
            }

            // Primary Direct Heal
            {
                AbilityDescriptor d;
                d.name = "Reverse Wound";
                d.rootSpellId = 801303;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Temporal HoT
            {
                AbilityDescriptor d;
                d.name = "Accelerated Recovery";
                d.rootSpellId = 800857;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // Buff
            {
                AbilityDescriptor d;
                d.name = "Fortify Timeline";
                d.rootSpellId = 804491;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 804491;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Offensive Weaving
            {
                AbilityDescriptor d;
                d.name = "Decomposition";
                d.rootSpellId = 800856;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Sandblast";
                d.rootSpellId = 804464;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // Strategy Registration
            SpecStrategy s;
            s.classId = 22;
            s.specId = 31;
            s.role = BotRole::Healer;
            s.strategyName = "Time_Healer_Strategy";
            s.minResourceToEngage = 50.0f;
            s.recoveryThreshold = 20.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 50;
            };

            s.phaseModifiers[CombatPhase::Emergency] = {
                { AbilityTag::EmergencyHeal, 2.5f, 100.0f },
                { AbilityTag::DirectHeal,    1.8f,  50.0f }
            };
            s.phaseModifiers[CombatPhase::Recovery] = {
                { AbilityTag::Filler,        0.1f, -50.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 2. SPEC 32: INFINITE (ARCANE / TEMPORAL CASTER DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Caster
        // - Resource: Mana
        // - Rotation: DoTs (Melt Reality, Decomposition) -> Gravity Bomb / Chromatic Shard -> Sandblast
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 22;
            p.specId = 32; // Infinite
            p.role = BotRole::Dps;
            p.profileName = "Chronomancer_Infinite_Dps";

            // Emergency Defense
            {
                AbilityDescriptor d;
                d.name = "Rewind";
                d.rootSpellId = 801294;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 30.0f;
                d.internalThrottleMs = 20000;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Infinite Shield";
                d.rootSpellId = 520457;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Burst Offensive Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Infinite Power";
                d.rootSpellId = 92118;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 90000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Temporal Anomaly";
                d.rootSpellId = 806315;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 60000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // DoTs
            {
                AbilityDescriptor d;
                d.name = "Melt Reality";
                d.rootSpellId = 806335;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Decomposition";
                d.rootSpellId = 800856;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // Spenders
            {
                AbilityDescriptor d;
                d.name = "Gravity Bomb";
                d.rootSpellId = 801281;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 3;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Chromatic Shard";
                d.rootSpellId = 801292;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // Filler
            {
                AbilityDescriptor d;
                d.name = "Sandblast";
                d.rootSpellId = 804464;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 22;
            s.specId = 32;
            s.role = BotRole::Dps;
            s.strategyName = "Infinite_Dps_Strategy";
            s.minResourceToEngage = 40.0f;
            s.recoveryThreshold = 15.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 40;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,    2.0f, 60.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 3. SPEC 33: ARTIFICER (CLOCKWORK / GADGET TEMPORAL DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Caster
        // - Resource: Mana
        // - Tactical Policy: Shatter Echo weapon strikes, Arc Collision electrical DoTs
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 22;
            p.specId = 33; // Artificer
            p.role = BotRole::Dps;
            p.profileName = "Chronomancer_Artificer_Dps";

            // Emergency Defense
            {
                AbilityDescriptor d;
                d.name = "Infinite Shield";
                d.rootSpellId = 520457;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Weapon Attacks & Bursts
            {
                AbilityDescriptor d;
                d.name = "Shatter Echo";
                d.rootSpellId = 804503;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 10000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Arc Collision";
                d.rootSpellId = 524853;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // DoTs
            {
                AbilityDescriptor d;
                d.name = "Melt Reality";
                d.rootSpellId = 806335;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Decomposition";
                d.rootSpellId = 800856;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // Spenders & Fillers
            {
                AbilityDescriptor d;
                d.name = "Chromatic Shard";
                d.rootSpellId = 801292;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Sandblast";
                d.rootSpellId = 804464;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 22;
            s.specId = 33;
            s.role = BotRole::Dps;
            s.strategyName = "Artificer_Dps_Strategy";
            s.minResourceToEngage = 40.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 40;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.3f, 30.0f }
            };

            // ResourcePolicy for Chronomancer Echo Fragment (804455)
            {
                ResourcePolicy pol;
                pol.key = CombatResourceKey{ CombatResourceKind::AuraStack, 0, 804455 };
                pol.overcapThreshold = 4;
                s.resourcePolicies.push_back(pol);
            }

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
