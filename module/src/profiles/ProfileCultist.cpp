/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Cultist Profiles & Spec Strategies
 *
 * Specializations:
 *   - Spec 96: Dreadnought (Void Plate Heavy Tank)
 *   - Spec 40: Heretic (Dark Mending Healer / Hybrid)
 *   - Spec 41: Corruption (Old God Insanity Caster DPS)
 *   - Spec 42: Godblade (Void Melee Insanity DPS)
 */

#include "profiles/ProfileCultist.h"
#include "profiles/ProfileRegistry.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterCultistProfiles()
    {
        // =========================================================================
        // 1. SPEC 96: DREADNOUGHT (VOID PLATE HEAVY TANK)
        // =========================================================================
        // Contract:
        // - Canonical Role: Tank
        // - Baseline State: Void Armor (buff 804633)
        // - Anti-Spam: Dreadnought & Abyssal Ward throttled to prevent spam bug
        // - TankReady: Void-Enchanted Armor active + HP >= 75%
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 25; // Cultist
            p.specId = 96;  // Dreadnought
            p.role = BotRole::Tank;
            p.profileName = "Cultist_Dreadnought_Tank";

            // Primary Taunts
            {
                AbilityDescriptor d;
                d.name = "Taunt";
                d.rootSpellId = 355;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 450.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Grasp of Zek'voz";
                d.rootSpellId = 573028;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 440.0f;
                p.abilities.push_back(d);
            }

            // Active Mitigation (FIXED: Added missingAuraOnCaster & internalThrottleMs to prevent spam)
            {
                AbilityDescriptor d;
                d.name = "Dreadnought";
                d.rootSpellId = 680750;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 55.0f;
                d.missingAuraOnCaster = 680750;
                d.internalThrottleMs = 15000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Abyssal Ward";
                d.rootSpellId = 804670;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.missingAuraOnCaster = 804670;
                d.internalThrottleMs = 15000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Self-Sustain
            {
                AbilityDescriptor d;
                d.name = "Satiate";
                d.rootSpellId = 804275;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.internalThrottleMs = 6000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Buff: Void-Enchanted Armor (Anti-spam throttle)
            {
                AbilityDescriptor d;
                d.name = "Void-Enchanted Armor";
                d.rootSpellId = 804633;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 804633;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Threat Strikes
            {
                AbilityDescriptor d;
                d.name = "Blade of the Empire";
                d.rootSpellId = 500720;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Eldritch Strike";
                d.rootSpellId = 801964;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Gaze of C'Thun";
                d.rootSpellId = 500110;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 25;
            s.specId = 96;
            s.role = BotRole::Tank;
            s.strategyName = "Dreadnought_Tank_Strategy";

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 75.0f;
            };

            s.phaseModifiers[CombatPhase::Emergency] = {
                { AbilityTag::DefensiveCD, 2.5f, 100.0f },
                { AbilityTag::Shield,      2.0f,  80.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 2. SPEC 40: HERETIC (DARK MENDING HEALER)
        // =========================================================================
        // Contract:
        // - Canonical Role: Healer
        // - Baseline State: Caster
        // - Resource: Mana + Insanity
        // - Anti-Spam: Herald of the Depths throttled to 30s
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 25;
            p.specId = 40; // Heretic
            p.role = BotRole::Healer;
            p.profileName = "Cultist_Heretic_Healer";

            // Critical Direct Heal
            {
                AbilityDescriptor d;
                d.name = "Abyssal Reconstruction";
                d.rootSpellId = 800429;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 40.0f;
                d.internalThrottleMs = 4000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Protective Shield
            {
                AbilityDescriptor d;
                d.name = "Void Shield";
                d.rootSpellId = 500715;
                d.tags = AbilityTag::Shield;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 60.0f;
                d.internalThrottleMs = 8000;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Sustained Direct Heal
            {
                AbilityDescriptor d;
                d.name = "Satiate";
                d.rootSpellId = 804275;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Dark Blessing
            {
                AbilityDescriptor d;
                d.name = "Herald of the Depths";
                d.rootSpellId = 92131;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 92131;
                d.internalThrottleMs = 30000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // Offensive Weaving
            {
                AbilityDescriptor d;
                d.name = "Gaze of C'Thun";
                d.rootSpellId = 500110;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Horrorbolt";
                d.rootSpellId = 800416;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 25;
            s.specId = 40;
            s.role = BotRole::Healer;
            s.strategyName = "Heretic_Healer_Strategy";
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
        // 3. SPEC 41: CORRUPTION (OLD GOD CASTER DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Caster
        // - Resource: Mana + Insanity
        // - Tactical Policy: Channel Gaze of C'Thun, AoE Screams, summon heralds
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 25;
            p.specId = 41; // Corruption
            p.role = BotRole::Dps;
            p.profileName = "Cultist_Corruption_Dps";

            // Emergency Defense
            {
                AbilityDescriptor d;
                d.name = "Void Shield";
                d.rootSpellId = 500715;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // Burst Offensive Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Corrupting Whispers";
                d.rootSpellId = 92130;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 90000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Vision of Doom";
                d.rootSpellId = 520388;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 60000;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // Heralds (Throttled so not spammed)
            {
                AbilityDescriptor d;
                d.name = "Herald of Yogg-Saron";
                d.rootSpellId = 805120;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 60000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Herald of C'thun";
                d.rootSpellId = 805119;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 60000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // DoTs & Channels
            {
                AbilityDescriptor d;
                d.name = "Gaze of C'Thun";
                d.rootSpellId = 500110;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Eldritch Screams";
                d.rootSpellId = 525049;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 3;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // Filler
            {
                AbilityDescriptor d;
                d.name = "Horrorbolt";
                d.rootSpellId = 800416;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 25;
            s.specId = 41;
            s.role = BotRole::Dps;
            s.strategyName = "Corruption_Dps_Strategy";
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
        // 4. SPEC 42: GODBLADE (VOID MELEE DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Melee
        // - Resource: Insanity
        // - Gap Closer: Grasp of Zek'voz
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 25;
            p.specId = 42; // Godblade
            p.role = BotRole::Dps;
            p.profileName = "Cultist_Godblade_Dps";

            // Buff
            {
                AbilityDescriptor d;
                d.name = "Shroud of Pride";
                d.rootSpellId = 805126;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 805126;
                d.internalThrottleMs = 30000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // Burst Cooldown
            {
                AbilityDescriptor d;
                d.name = "Obliteration";
                d.rootSpellId = 92129;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 90000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Gap Closer: Grasp of Zek'voz
            {
                AbilityDescriptor d;
                d.name = "Grasp of Zek'voz";
                d.rootSpellId = 573028;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 100.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Melee Strikes
            {
                AbilityDescriptor d;
                d.name = "Blade of the Empire";
                d.rootSpellId = 500720;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Eldritch Strike";
                d.rootSpellId = 801964;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Devourer";
                d.rootSpellId = 300265;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Spender
            {
                AbilityDescriptor d;
                d.name = "Gaze of C'Thun";
                d.rootSpellId = 500110;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 25;
            s.specId = 42;
            s.role = BotRole::Dps;
            s.strategyName = "Godblade_Dps_Strategy";

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 70.0f;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::MeleeAttack, 1.4f, 40.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
