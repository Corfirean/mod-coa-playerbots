/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Barbarian Profiles & Spec Strategies
 *
 * Specializations:
 *   - Spec 1: Headhunting (Ranged Thrown / Bleed Hybrid DPS)
 *   - Spec 2: Brutality (Melee Dual-Wield Berserker DPS)
 *   - Spec 3: Ancestry (Support Melee / Shamanic Totemic DPS)
 */

#include "profiles/ProfileBarbarian.h"
#include "profiles/ProfileRegistry.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterBarbarianProfiles()
    {
        // =========================================================================
        // 1. SPEC 1: HEADHUNTING (RANGED THROWN / BLEED HYBRID DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Caster/Physical
        // - Resource: Energy
        // - Tactical Policy: Maintain ranged distance, throw axes/spears, conserve Energy
        // - Anti-Spam: War Cry throttled to 30s
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 12;
            p.specId = 1; // Headhunting
            p.role = BotRole::Dps;
            p.profileName = "Barbarian_Headhunting_Ranged";
            p.useRangedAutoRepeat = true;
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // 1. Party Buff: War Cry
            {
                AbilityDescriptor d;
                d.name = "War Cry";
                d.rootSpellId = 500995;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 500995;
                d.internalThrottleMs = 30000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 2. Burst Offensive CD: Savage Rage
            {
                AbilityDescriptor d;
                d.name = "Savage Rage";
                d.rootSpellId = 800954;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800954;
                d.internalThrottleMs = 45000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 3. Mobility / Leap: Berserker Rush
            {
                AbilityDescriptor d;
                d.name = "Berserker Rush";
                d.rootSpellId = 560518;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 80.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 4. Primary Ranged Thrown Attacks
            {
                AbilityDescriptor d;
                d.name = "Berserker Axe";
                d.rootSpellId = 804138;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Headhunter's Spear";
                d.rootSpellId = 804137;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Throw Weapon";
                d.rootSpellId = 804136;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 5. Melee Fallback
            {
                AbilityDescriptor d;
                d.name = "Ancestral Strike";
                d.rootSpellId = 801576;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Crush";
                d.rootSpellId = 500915;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Whirling Advance";
                d.rootSpellId = 500919;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // Strategy Registration
            SpecStrategy s;
            s.classId = 12;
            s.specId = 1;
            s.role = BotRole::Dps;
            s.strategyName = "Headhunting_Ranged_Strategy";
            s.minResourceToEngage = 60.0f; // Energy

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetPower(POWER_ENERGY) >= 60;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,    1.6f, 40.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 2. SPEC 2: BRUTALITY (MELEE DUAL-WIELD BERSERKER DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Melee
        // - Resource: Energy
        // - Tactical Policy: Maintain Enrage windows, aggressive melee strikes, execute finishers
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 12;
            p.specId = 2; // Brutality
            p.role = BotRole::Dps;
            p.profileName = "Barbarian_Brutality_Melee";

            // 1. Buff: War Cry
            {
                AbilityDescriptor d;
                d.name = "War Cry";
                d.rootSpellId = 500995;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 500995;
                d.internalThrottleMs = 30000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 2. Burst: Savage Rage
            {
                AbilityDescriptor d;
                d.name = "Savage Rage";
                d.rootSpellId = 800954;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800954;
                d.internalThrottleMs = 45000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 3. Gap Closer: Berserker Rush
            {
                AbilityDescriptor d;
                d.name = "Berserker Rush";
                d.rootSpellId = 560518;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 90.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 4. Primary Melee Strikes
            {
                AbilityDescriptor d;
                d.name = "Ancestral Strike";
                d.rootSpellId = 801576;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Crush";
                d.rootSpellId = 500915;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Whirling Advance";
                d.rootSpellId = 500919;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 195.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Berserker Axe";
                d.rootSpellId = 804138;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 5. Gap Filler
            {
                AbilityDescriptor d;
                d.name = "Headhunter's Spear";
                d.rootSpellId = 804137;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Throw Weapon";
                d.rootSpellId = 804136;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 110.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // Strategy Registration
            SpecStrategy s;
            s.classId = 12;
            s.specId = 2;
            s.role = BotRole::Dps;
            s.strategyName = "Brutality_Melee_Strategy";
            s.minResourceToEngage = 50.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetPower(POWER_ENERGY) >= 50;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,   2.0f, 60.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::MeleeAttack, 1.4f, 40.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 3. SPEC 3: ANCESTRY (SUPPORT MELEE / SHAMANIC TOTEMIC DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Support
        // - Baseline State: Melee
        // - Resource: Energy
        // - Tactical Policy: Maintain party buffs/auras, provide melee DPS support
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 12;
            p.specId = 3; // Ancestry
            p.role = BotRole::Support;
            p.profileName = "Barbarian_Ancestry_Support";

            // 1. War Cry (High Priority Support Buff)
            {
                AbilityDescriptor d;
                d.name = "War Cry";
                d.rootSpellId = 500995;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.casterAuraId = 500995;
                d.missingAuraOnCaster = 500995;
                d.internalThrottleMs = 30000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 2. Savage Rage
            {
                AbilityDescriptor d;
                d.name = "Savage Rage";
                d.rootSpellId = 800954;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800954;
                d.internalThrottleMs = 45000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 3. Melee Rotation
            {
                AbilityDescriptor d;
                d.name = "Ancestral Strike";
                d.rootSpellId = 801576;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Crush";
                d.rootSpellId = 500915;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Whirling Advance";
                d.rootSpellId = 500919;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 4. Ranged Support / Pull
            {
                AbilityDescriptor d;
                d.name = "Berserker Rush";
                d.rootSpellId = 560518;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 80.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Headhunter's Spear";
                d.rootSpellId = 804137;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // Strategy Registration
            SpecStrategy s;
            s.classId = 12;
            s.specId = 3;
            s.role = BotRole::Support;
            s.strategyName = "Ancestry_Support_Strategy";
            s.minResourceToEngage = 50.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetPower(POWER_ENERGY) >= 50;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,   1.8f, 50.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
