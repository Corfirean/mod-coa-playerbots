/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Bloodmage Profiles & Spec Strategies
 *
 * Specializations:
 *   - Spec 99: Eternal (Blood Protection Heavy Tank)
 *   - Spec 25: Fleshweaver (Blood & Vitality Support / Hybrid Healer)
 *   - Spec 26: Sanguine (Ranged Blood Caster DPS)
 *   - Spec 27: Accursed (Accursed Werewolf Melee DPS)
 */

#include "profiles/ProfileBloodmage.h"
#include "profiles/ProfileRegistry.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterBloodmageProfiles()
    {
        // =========================================================================
        // 1. SPEC 99: ETERNAL (BLOOD PROTECTION HEAVY TANK)
        // =========================================================================
        // Contract:
        // - Canonical Role: Tank
        // - Baseline State: Caster/Physical
        // - Resource: Health + Rage economy (prevent self-damage suicide)
        // - Defensives: Liquify (<25% HP), Bloody Sacrifice (<45% HP), Fleshcraft (<65% HP)
        // - Buffs: Aortic Aegis & Blood Veil throttled to 30s
        // - TankReady: Aortic Aegis active + HP > 75%
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 20; // Bloodmage
            p.specId = 99;  // Eternal
            p.role = BotRole::Tank;
            p.profileName = "Bloodmage_Eternal_Tank";

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

            // Emergency Survival
            {
                AbilityDescriptor d;
                d.name = "Liquify";
                d.rootSpellId = 806310;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 25.0f;
                d.internalThrottleMs = 90000;
                d.baseScore = 420.0f;
                p.abilities.push_back(d);
            }

            // Active Mitigation
            {
                AbilityDescriptor d;
                d.name = "Bloody Sacrifice";
                d.rootSpellId = 705742;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.minSelfHpPct = 20.0f; // Prevent suicide
                d.internalThrottleMs = 30000;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fleshcraft";
                d.rootSpellId = 801952;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.missingAuraOnCaster = 801952;
                d.internalThrottleMs = 15000;
                d.baseScore = 310.0f;
                p.abilities.push_back(d);
            }

            // Stance / Auras
            {
                AbilityDescriptor d;
                d.name = "Aortic Aegis";
                d.rootSpellId = 806274;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 806274;
                d.internalThrottleMs = 30000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Blood Veil";
                d.rootSpellId = 504263;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 504263;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Threat / Leech
            {
                AbilityDescriptor d;
                d.name = "Vampyr's Kiss";
                d.rootSpellId = 504275;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Crimson Tide";
                d.rootSpellId = 504282;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // Heavy Threat Spenders (Health-aware)
            {
                AbilityDescriptor d;
                d.name = "Heartbreak";
                d.rootSpellId = 520314;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minSelfHpPct = 25.0f;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Veinburst";
                d.rootSpellId = 504260;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minSelfHpPct = 20.0f;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Vampiric Fang";
                d.rootSpellId = 804726;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // Builders
            {
                AbilityDescriptor d;
                d.name = "Ravenous Bite";
                d.rootSpellId = 500123;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bloodbolt";
                d.rootSpellId = 804685;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // Strategy Registration
            SpecStrategy s;
            s.classId = 20;
            s.specId = 99;
            s.role = BotRole::Tank;
            s.strategyName = "Eternal_Tank_Strategy";

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 75.0f;
            };

            s.phaseModifiers[CombatPhase::Emergency] = {
                { AbilityTag::DefensiveCD, 2.5f, 100.0f },
                { AbilityTag::Shield,      2.0f,  80.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,   1.8f,  50.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 2. SPEC 25: FLESHWEAVER (SUPPORT / HYBRID HEALER)
        // =========================================================================
        // Contract:
        // - Canonical Role: Support (registered for Support & Healer lookup)
        // - Baseline State: Caster
        // - Resource: Vitality pooling through damage weaving
        // =========================================================================
        auto registerFleshweaver = [](BotRole role)
        {
            CombatProfile p;
            p.classId = 20;
            p.specId = 25;
            p.role = role;
            p.profileName = (role == BotRole::Support) ? "Bloodmage_Fleshweaver_Support" : "Bloodmage_Fleshweaver_Healer";

            // Emergency Direct Heal
            {
                AbilityDescriptor d;
                d.name = "Transfusion";
                d.rootSpellId = 705734;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 40.0f;
                d.internalThrottleMs = 3000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Shield
            {
                AbilityDescriptor d;
                d.name = "Fleshcraft";
                d.rootSpellId = 801952;
                d.tags = AbilityTag::Shield;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 50.0f;
                d.missingAuraOnCaster = 801952;
                d.internalThrottleMs = 12000;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // AoE Blood Wave
            {
                AbilityDescriptor d;
                d.name = "Waves of Blood";
                d.rootSpellId = 681427;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::Self;
                d.minInjuredAllies = 2;
                d.injuredAllyHpPctThreshold = 80.0f;
                d.internalThrottleMs = 6000;
                d.baseScore = 310.0f;
                p.abilities.push_back(d);
            }

            // Primary Heal
            {
                AbilityDescriptor d;
                d.name = "Sanguine Mend";
                d.rootSpellId = 802310;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Blood Redistribution";
                d.rootSpellId = 706256;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 85.0f;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // Support Buff
            {
                AbilityDescriptor d;
                d.name = "Universal Donor";
                d.rootSpellId = 806428;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 806428;
                d.internalThrottleMs = 30000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // Offensive Vitality Weaving
            {
                AbilityDescriptor d;
                d.name = "Vampyr's Kiss";
                d.rootSpellId = 504275;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Crimson Tide";
                d.rootSpellId = 504282;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bloodbolt";
                d.rootSpellId = 804685;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 20;
            s.specId = 25;
            s.role = role;
            s.strategyName = (role == BotRole::Support) ? "Fleshweaver_Support_Strategy" : "Fleshweaver_Healer_Strategy";
            s.minResourceToEngage = 50.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 70.0f;
            };

            s.phaseModifiers[CombatPhase::Emergency] = {
                { AbilityTag::EmergencyHeal, 2.5f, 100.0f },
                { AbilityTag::Shield,        2.0f,  80.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        };

        registerFleshweaver(BotRole::Support);
        registerFleshweaver(BotRole::Healer);

        // =========================================================================
        // 3. SPEC 26: SANGUINE (RANGED BLOOD CASTER DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Caster
        // - Resource: Health + Rage + Thirst (minSelfHpPct on health-spending nukes)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 20;
            p.specId = 26; // Sanguine
            p.role = BotRole::Dps;
            p.profileName = "Bloodmage_Sanguine_Dps";

            // Emergency Defense
            {
                AbilityDescriptor d;
                d.name = "Liquify";
                d.rootSpellId = 806310;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 20.0f;
                d.internalThrottleMs = 90000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fleshcraft";
                d.rootSpellId = 801952;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.missingAuraOnCaster = 801952;
                d.internalThrottleMs = 15000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Major Burst Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Apotheosis";
                d.rootSpellId = 804195;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 90000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Blood Moon";
                d.rootSpellId = 804199;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 60000;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // Core Blood DoTs
            {
                AbilityDescriptor d;
                d.name = "Crimson Tide";
                d.rootSpellId = 504282;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Vampyr's Kiss";
                d.rootSpellId = 504275;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Heavy Spenders (Health-protected)
            {
                AbilityDescriptor d;
                d.name = "Heartbreak";
                d.rootSpellId = 520314;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.minSelfHpPct = 25.0f;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Veinburst";
                d.rootSpellId = 504260;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minSelfHpPct = 20.0f;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Hemoburst";
                d.rootSpellId = 572855;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minSelfHpPct = 20.0f;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // Pet / Summon
            {
                AbilityDescriptor d;
                d.name = "Animated Blood";
                d.rootSpellId = 573299;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 60000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Fillers
            {
                AbilityDescriptor d;
                d.name = "Bloodbolt";
                d.rootSpellId = 804685;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bloodmoon Blast";
                d.rootSpellId = 500125;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 20;
            s.specId = 26;
            s.role = BotRole::Dps;
            s.strategyName = "Sanguine_Dps_Strategy";

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 70.0f;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::Execute,      1.8f, 60.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 4. SPEC 27: ACCURSED (ACCURSED WEREWOLF MELEE DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Mandatory Baseline State: Accursed Form (spell 562572, aura 562572)
        // - Resource: Rage / Ferocity in Werewolf Form
        // - Tactical Policy: Close gap with Bloodleaper, howl buffs, claw and bite
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 20;
            p.specId = 27; // Accursed
            p.role = BotRole::Dps;
            p.profileName = "Bloodmage_Accursed_Dps";

            // Mandatory Form: Accursed Form
            {
                AbilityDescriptor d;
                d.name = "Accursed Form";
                d.rootSpellId = 562572;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 562572;
                d.internalThrottleMs = 4000;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

            // Gap Closer: Bloodleaper
            {
                AbilityDescriptor d;
                d.name = "Bloodleaper";
                d.rootSpellId = 806423;
                d.tags = AbilityTag::MeleeAttack;
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

            // Offensive Howls
            {
                AbilityDescriptor d;
                d.name = "Night Hunter's Howl";
                d.rootSpellId = 500124;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 45000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Wicked Howl";
                d.rootSpellId = 804207;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 45000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // Execute Strike: Reave
            {
                AbilityDescriptor d;
                d.name = "Reave";
                d.rootSpellId = 800490;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Primary Claw Strikes
            {
                AbilityDescriptor d;
                d.name = "Rotclaw";
                d.rootSpellId = 804197;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Vampiric Fang";
                d.rootSpellId = 804726;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Ravenous Bite";
                d.rootSpellId = 500123;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // Filler / Spender
            {
                AbilityDescriptor d;
                d.name = "Blood Shards";
                d.rootSpellId = 804849;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 20;
            s.specId = 27;
            s.role = BotRole::Dps;
            s.strategyName = "Accursed_Dps_Strategy";
            s.requiredState.formSpellId = 562572; // Accursed Form
            s.requiredState.formAuraId = 562572;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->HasAura(562572) && bot->GetHealthPct() >= 70.0f;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::Execute,     2.0f, 60.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
