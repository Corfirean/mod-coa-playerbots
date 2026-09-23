/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Knight of Xoroth Profiles & Spec Strategies
 *
 * Specializations:
 *   - Spec 17: Defiance (Chaos & Fire Melee Tank)
 *   - Spec 18: War (2H Chaos Melee DPS)
 *   - Spec 16: Hellfire (Destruction / Fire Hybrid DPS)
 */

#include "profiles/ProfileKnightOfXoroth.h"
#include "profiles/ProfileRegistry.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterKnightOfXorothProfiles()
    {
        // =========================================================================
        // 1. SPEC 17: DEFIANCE (CHAOS & FIRE MELEE TANK)
        // =========================================================================
        // Contract:
        // - Canonical Role: Tank
        // - Baseline State: Melee
        // - Resource: Rage + Demonfire + Demon's Blood
        // - Defensive / Buff: Demon's Blood (throttled to 30s)
        // - TankReady: HP >= 75%
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 17; // Knight of Xoroth
            p.specId = 17;  // Defiance
            p.role = BotRole::Tank;
            p.profileName = "KnightOfXoroth_Defiance_Tank";

            // Primary Taunt
            {
                AbilityDescriptor d;
                d.name = "Taunt";
                d.rootSpellId = 355;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 450.0f;
                p.abilities.push_back(d);
            }

            // Defensive Buff: Demon's Blood
            {
                AbilityDescriptor d;
                d.name = "Demon's Blood";
                d.rootSpellId = 800999;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800999;
                d.internalThrottleMs = 30000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // AoE Threat: Flames of Xoroth
            {
                AbilityDescriptor d;
                d.name = "Flames of Xoroth";
                d.rootSpellId = 801059;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // Primary Threat Strike: Infernal Strike
            {
                AbilityDescriptor d;
                d.name = "Infernal Strike";
                d.rootSpellId = 801016;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // Rage Generator: Sever
            {
                AbilityDescriptor d;
                d.name = "Sever";
                d.rootSpellId = 500904;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            // Ranged Pull: Chaos Bolt
            {
                AbilityDescriptor d;
                d.name = "Chaos Bolt";
                d.rootSpellId = 802057;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 17;
            s.specId = 17;
            s.role = BotRole::Tank;
            s.strategyName = "Defiance_Tank_Strategy";

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 75.0f;
            };

            s.phaseModifiers[CombatPhase::Emergency] = {
                { AbilityTag::DefensiveCD, 2.5f, 100.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,   1.8f,  50.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 2. SPEC 18: WAR (2H CHAOS MELEE DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Melee
        // - Resource: Rage / Demonfire
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 17;
            p.specId = 18; // War
            p.role = BotRole::Dps;
            p.profileName = "KnightOfXoroth_War_Melee";

            // Buff
            {
                AbilityDescriptor d;
                d.name = "Demon's Blood";
                d.rootSpellId = 800999;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800999;
                d.internalThrottleMs = 30000;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            // Primary Heavy Strike
            {
                AbilityDescriptor d;
                d.name = "Infernal Strike";
                d.rootSpellId = 801016;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // AoE Burst
            {
                AbilityDescriptor d;
                d.name = "Flames of Xoroth";
                d.rootSpellId = 801059;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // Generator
            {
                AbilityDescriptor d;
                d.name = "Sever";
                d.rootSpellId = 500904;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // Opener / Nuke
            {
                AbilityDescriptor d;
                d.name = "Chaos Bolt";
                d.rootSpellId = 802057;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 17;
            s.specId = 18;
            s.role = BotRole::Dps;
            s.strategyName = "War_Melee_Strategy";

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 70.0f;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,   1.8f, 50.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 3. SPEC 16: HELLFIRE (HYBRID CASTER / MELEE DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Caster/Hybrid
        // - Resource: Demonfire
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 17;
            p.specId = 16; // Hellfire
            p.role = BotRole::Dps;
            p.profileName = "KnightOfXoroth_Hellfire_Hybrid";

            // Primary Ranged Burst
            {
                AbilityDescriptor d;
                d.name = "Chaos Bolt";
                d.rootSpellId = 802057;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 10000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // AoE Fire
            {
                AbilityDescriptor d;
                d.name = "Flames of Xoroth";
                d.rootSpellId = 801059;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // Melee Strike
            {
                AbilityDescriptor d;
                d.name = "Infernal Strike";
                d.rootSpellId = 801016;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Generator
            {
                AbilityDescriptor d;
                d.name = "Sever";
                d.rootSpellId = 500904;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // Buff
            {
                AbilityDescriptor d;
                d.name = "Demon's Blood";
                d.rootSpellId = 800999;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800999;
                d.internalThrottleMs = 30000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 17;
            s.specId = 16;
            s.role = BotRole::Dps;
            s.strategyName = "Hellfire_Hybrid_Strategy";

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 70.0f;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,    1.8f, 50.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
