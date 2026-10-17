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
                d.maxTargetHpPct = 100.0f; // free instant 15 s heal over time (49% of real healing): keep it rolling on every ally
                d.requireAuraMissingOnTarget = true;
                d.refreshBelowMs = 4000;
                d.internalThrottleMs = 1500;
                d.baseScore = 330.0f;
                p.abilities.push_back(d);
            }

            // Buff
            {
                AbilityDescriptor d;
                d.name = "Fortify Timeline";
                d.rootSpellId = 807458; // instant heal of the whole party (0.4 spell power) that also extends Accelerated Recovery
                d.tags = AbilityTag::AoEHeal | AbilityTag::DirectHeal;
                d.minInjuredAllies = 2;
                d.targetType = TargetType::Self;
                // 60 s cooldown: use on every cooldown while the party is hurt
                d.internalThrottleMs = 60000;
                d.baseScore = 350.0f;
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
            {
                AbilityDescriptor d;
                // 5 s cooldown self shield, 21% of real casts
                d.name = "Aeon of Protection";
                d.rootSpellId = 806291; // Aeon of Resilience: Epoch casts faster and cheaper and bounces Accelerated Recovery (the throughput Aeon; Protection only adds an absorb)
                d.missingAuraOnCaster = 806291;
                d.tags = AbilityTag::Buff | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 5000;
                d.baseScore = 450.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Ripple";
                d.rootSpellId = 806296;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Timeguard";
                d.rootSpellId = 804441;
                d.tags = AbilityTag::Shield;
                d.targetType = TargetType::LowestHealthAlly;
                d.internalThrottleMs = 120000;
                d.baseScore = 215.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 2 s heal worth half the spell power that also grants Sands of Time, the resource Reverse Wound spends; it was cast only by the generic fallback, which the heal-over-time upkeep starved
                d.name = "Epoch";
                d.rootSpellId = 501779;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 95.0f;
                d.baseScore = 295.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 2.5 s party heal of 0.42 bonus healing, no cooldown
                d.name = "Correct the Mistake";
                d.rootSpellId = 572360;
                d.tags = AbilityTag::AoEHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 85.0f;
                d.minInjuredAllies = 2;
                d.baseScore = 305.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // stores half of the damage taken for 10 s and heals it back: use when several allies are hurt
                d.name = "Temporal Anomaly";
                d.rootSpellId = 806315;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 120000;
                d.minInjuredAllies = 3;
                d.baseScore = 390.0f;
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
                d.internalThrottleMs = 3000; // real players keep it rolling: 34% of casts
                d.baseScore = 250.0f;
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
            {
                AbilityDescriptor d;
                // 15% of real casts; the DBC names a stance (mask 2^30), the native check decides
                d.name = "Discordance";
                d.rootSpellId = 503875;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f; // 580 damage per cast
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Accelerated Recovery";
                d.rootSpellId = 800857;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f; // a heal-over-time cast on a healthy party only wasted the global cooldown
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 8000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // about 1,750 damage per cast and no cooldown: the main damage spell, previously cast only by the generic fallback that the other descriptors starved
                d.name = "Unmake";
                d.rootSpellId = 574345;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
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
                d.tags = AbilityTag::RangedAttack; // not OffensiveCD: a 3-second-throttle rotational attack; that tag scales the score by x0.15 outside boss/elite/PvP fights
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 3000; // native cooldown of the learned rank 572417
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
                d.baseScore = 180.0f;
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
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Echo Fragment (804455) builders. Native CustomResourceData grants a fragment on each
            // damaging Wand of Time hit and on each Artificer's Wand cast; Shatter Echo / Arc
            // Collision need that aura, so without a builder the whole spender chain stayed
            // blocked (I17: 1586 damage in 15 s, both spenders "missing spell-defined caster aura").
            {
                AbilityDescriptor d;
                d.name = "Wand of Time (Echo builder)";
                d.rootSpellId = 520175;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 215.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Artificer's Wand (Echo filler)";
                d.rootSpellId = 561357;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Discordance";
                d.rootSpellId = 503875;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 205.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Unmake";
                d.rootSpellId = 503784;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
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
            {
                AbilityDescriptor d;
                d.name = "Singularity Core";
                d.rootSpellId = 804438;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 15000;
                d.baseScore = 215.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Accelerated Recovery";
                d.rootSpellId = 800857;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.internalThrottleMs = 8000;
                d.baseScore = 150.0f;
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
