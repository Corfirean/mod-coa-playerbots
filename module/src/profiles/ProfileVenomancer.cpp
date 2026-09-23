/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Venomancer Profiles & Spec Strategies
 *
 * Specializations:
 *   - Spec 52:  Fortitude (Beetle Form Heavy Tank) -- HIGHEST-PRIORITY REGRESSION SPEC
 *   - Spec 101: Vizier (Antidote & Venom HoT Healer)
 *   - Spec 53:  Stalking (Spider Form Melee Stalker DPS)
 *   - Spec 54:  Rot (Ranged Poison Caster DPS)
 */

#include "profiles/ProfileVenomancer.h"
#include "profiles/ProfileRegistry.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterVenomancerProfiles()
    {
        // =========================================================================
        // 1. SPEC 52: FORTITUDE (BEETLE FORM TANK) -- HIGHEST-PRIORITY REGRESSION
        // =========================================================================
        // Contract:
        // - Canonical Role: Tank
        // - Mandatory Baseline State: Beetle Form (spell 803183, aura 803183)
        // - Alternative States: Spider Form (800841) for mobility; must return to Beetle
        // - Resource: Rage in Beetle Form (via Catalyst 800895 / 803640)
        // - Anti-Spam: internalThrottleMs on all self-buffs and defensives
        // - TankReady: Beetle Form active + HP > 75%
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 29; // Venomancer
            p.specId = 52;  // Fortitude
            p.role = BotRole::Tank;
            p.profileName = "Venomancer_Fortitude_Tank";

            // Form: Beetle Form (MANDATORY TANK FORM)
            {
                AbilityDescriptor d;
                d.name = "Beetle Form";
                d.rootSpellId = 803183;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 803183;
                d.internalThrottleMs = 4000;
                d.baseScore = 500.0f; // Utmost priority when dropped
                p.abilities.push_back(d);
            }

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
                d.name = "Pinch (Beetle Threat Strike)";
                d.rootSpellId = 704235;
                d.tags = AbilityTag::Taunt | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 440.0f;
                p.abilities.push_back(d);
            }

            // Active Mitigation & Defensives (Strict internal throttles to eliminate OOM bug)
            {
                AbilityDescriptor d;
                d.name = "Reinforced Shell";
                d.rootSpellId = 705966;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.missingAuraOnCaster = 705966;
                d.internalThrottleMs = 15000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fury of Anub'Rekhan";
                d.rootSpellId = 800894;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.missingAuraOnCaster = 800894;
                d.internalThrottleMs = 30000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Weapon / Defense Buff (Anti-spam throttle)
            {
                AbilityDescriptor d;
                d.name = "Beetle Pheromone";
                d.rootSpellId = 803651;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 803651;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Threat Strikes & Resource
            {
                AbilityDescriptor d;
                d.name = "Scorpid Claw";
                d.rootSpellId = 803198;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Venom Fang";
                d.rootSpellId = 800880;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Weakening Venom";
                d.rootSpellId = 805778;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 8000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // Strategy Registration
            SpecStrategy s;
            s.classId = 29;
            s.specId = 52;
            s.role = BotRole::Tank;
            s.strategyName = "Fortitude_Tank_Strategy";
            s.requiredState.formSpellId = 803183; // Beetle Form
            s.requiredState.formAuraId = 803183;
            s.requiredState.alternateFormAuras = { 800841 }; // Spider Form mobility allowed

            // Out-of-combat mobility rule: allows Spider Form only for movement, returns before pull
            s.requiredState.transitionRules.push_back({
                800841, // Spider Form
                800841,
                4000,
                [](Player* bot, CombatContext const& ctx) -> bool {
                    return !bot->IsInCombat() && !ctx.victim && bot->isMoving();
                },
                [](Player* bot, CombatContext const& ctx) -> bool {
                    return bot->IsInCombat() || ctx.victim != nullptr || !bot->isMoving();
                }
            });

            s.minResourceToEngage = 0.0f; // Rage starts at 0

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                // Never allow a pull in caster/default form once Beetle Form is available
                return bot->HasAura(803183) && bot->GetHealthPct() >= 75.0f;
            };

            s.phaseModifiers[CombatPhase::Emergency] = {
                { AbilityTag::DefensiveCD, 2.5f, 100.0f },
                { AbilityTag::Shield,      2.0f,  80.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::MeleeAttack, 1.3f, 30.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 2. SPEC 101: VIZIER (HEALER)
        // =========================================================================
        // Contract:
        // - Canonical Role: Healer
        // - Baseline State: Caster Form (heals naturally from caster)
        // - Emergency Defensive Weaving: Beetle Form allowed when focused < 35% HP
        // - Resources: Mana HoT healer; strict refresh guards on Green Salve
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 29;
            p.specId = 101;
            p.role = BotRole::Healer;
            p.profileName = "Venomancer_Vizier_Healer";

            // Form: Vizier Form (Baseline Healer Form)
            {
                AbilityDescriptor d;
                d.name = "Vizier Form";
                d.rootSpellId = 800912;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800912;
                d.internalThrottleMs = 4000;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

            // Critical Emergency Direct Heal
            {
                AbilityDescriptor d;
                d.name = "Lifeblood";
                d.rootSpellId = 804963;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 40.0f;
                d.internalThrottleMs = 4000;
                d.baseScore = 450.0f;
                p.abilities.push_back(d);
            }

            // Primary Direct Heal
            {
                AbilityDescriptor d;
                d.name = "Shadra's Vigil";
                d.rootSpellId = 800870;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 290.0f;
                p.abilities.push_back(d);
            }

            // HoT: Green Salve (Strict refresh guard)
            {
                AbilityDescriptor d;
                d.name = "Green Salve";
                d.rootSpellId = 800902;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // Cleanse: Antidote / Cure
            {
                AbilityDescriptor d;
                d.name = "Antidote";
                d.rootSpellId = 803529;
                d.tags = AbilityTag::Cleanse;
                d.targetType = TargetType::AnyInjuredAlly;
                d.internalThrottleMs = 3000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Offensive Cooldown
            {
                AbilityDescriptor d;
                d.name = "Spawn (Broodlings)";
                d.rootSpellId = 805568;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // Filler Damage
            {
                AbilityDescriptor d;
                d.name = "Venom Bolt";
                d.rootSpellId = 800869;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // Strategy Registration
            SpecStrategy s;
            s.classId = 29;
            s.specId = 101;
            s.role = BotRole::Healer;
            s.strategyName = "Vizier_Healer_Strategy";
            s.requiredState.formSpellId = 800912; // Vizier Form
            s.requiredState.formAuraId = 800912;
            s.requiredState.alternateFormAuras = { 803183 }; // Beetle Form emergency defense allowed

            // Emergency defensive weaving: Beetle Form when low HP or aggro
            s.requiredState.transitionRules.push_back({
                803183, // Beetle Form
                803183,
                4000,
                [](Player* bot, CombatContext const& ctx) -> bool {
                    return bot->IsInCombat() && (bot->GetHealthPct() < 35.0f || (ctx.victim && ctx.victim->GetVictim() == bot));
                },
                [](Player* bot, CombatContext const& ctx) -> bool {
                    return bot->GetHealthPct() > 55.0f && (!ctx.victim || ctx.victim->GetVictim() != bot);
                }
            });

            s.minResourceToEngage = 50.0f; // Needs >= 50% mana before starting pull
            s.recoveryThreshold = 20.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->HasAura(800912) && (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 50);
            };

            s.phaseModifiers[CombatPhase::Emergency] = {
                { AbilityTag::EmergencyHeal, 2.5f, 100.0f },
                { AbilityTag::DirectHeal,    1.8f,  50.0f }
            };
            s.phaseModifiers[CombatPhase::Recovery] = {
                { AbilityTag::Filler,        0.1f, -50.0f } // Conserve mana in recovery
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 3. SPEC 53: STALKING (MELEE SPIDER STALKER DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Mandatory Baseline State: Spider Form (spell 800841, aura 800841)
        // - Alternative State: Skulk (aura 800843) for stealth opener
        // - Resource: Energy in Spider Form
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 29;
            p.specId = 53;
            p.role = BotRole::Dps;
            p.profileName = "Venomancer_Stalking_Dps";

            // Form: Spider Form (MANDATORY MELEE FORM)
            {
                AbilityDescriptor d;
                d.name = "Spider Form";
                d.rootSpellId = 800841;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800841;
                d.internalThrottleMs = 4000;
                d.baseScore = 480.0f;
                p.abilities.push_back(d);
            }

            // Weapon Buff
            {
                AbilityDescriptor d;
                d.name = "Envenom Weapons";
                d.rootSpellId = 803177;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 803177;
                d.internalThrottleMs = 30000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Gap Closer: Toxic Stride
            {
                AbilityDescriptor d;
                d.name = "Toxic Stride";
                d.rootSpellId = 504347;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 100.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Major Burst Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Avatar of Shadra";
                d.rootSpellId = 503856;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 60000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Adrenal Venom";
                d.rootSpellId = 805775;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 45000;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // Primary Strikes
            {
                AbilityDescriptor d;
                d.name = "Ambush Predator";
                d.rootSpellId = 800878;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Venom Fang";
                d.rootSpellId = 800880;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spindlebind";
                d.rootSpellId = 800887;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spawn";
                d.rootSpellId = 805568;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // Strategy Registration
            SpecStrategy s;
            s.classId = 29;
            s.specId = 53;
            s.role = BotRole::Dps;
            s.strategyName = "Stalking_Dps_Strategy";
            s.requiredState.formSpellId = 800841; // Spider Form
            s.requiredState.formAuraId = 800841;
            s.requiredState.alternateFormAuras = { 800843 }; // Skulk state allowed

            // Skulk stealth opener: only out of combat, must exit on combat/victim
            s.requiredState.transitionRules.push_back({
                800843, // Skulk
                800843,
                3000,
                [](Player* bot, CombatContext const& ctx) -> bool {
                    return !bot->IsInCombat() && !ctx.victim;
                },
                [](Player* bot, CombatContext const& ctx) -> bool {
                    return bot->IsInCombat() || ctx.victim != nullptr;
                }
            });

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return (bot->HasAura(800841) || bot->HasAura(800843)) && bot->GetHealthPct() >= 70.0f;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::MeleeAttack, 1.4f, 40.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 4. SPEC 54: ROT (RANGED POISON CASTER DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Caster (Ranged caster)
        // - Resources: Mana
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 29;
            p.specId = 54;
            p.role = BotRole::Dps;
            p.profileName = "Venomancer_Rot_Dps";

            // Weapon / Poison Buff
            {
                AbilityDescriptor d;
                d.name = "Toxic Pheromone";
                d.rootSpellId = 707689;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 707689;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Emergency Defense
            {
                AbilityDescriptor d;
                d.name = "Reinforced Shell";
                d.rootSpellId = 705966;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.missingAuraOnCaster = 705966;
                d.internalThrottleMs = 20000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // Burst Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Rot Lich";
                d.rootSpellId = 92142;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 90000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Master of Venoms";
                d.rootSpellId = 504326;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 60000;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // Poisons & DoTs
            {
                AbilityDescriptor d;
                d.name = "Weakening Venom";
                d.rootSpellId = 805778;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spindlebind";
                d.rootSpellId = 800887;
                d.tags = AbilityTag::RangedAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spawn";
                d.rootSpellId = 805568;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Filler
            {
                AbilityDescriptor d;
                d.name = "Venom Bolt";
                d.rootSpellId = 800869;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // Strategy Registration
            SpecStrategy s;
            s.classId = 29;
            s.specId = 54;
            s.role = BotRole::Dps;
            s.strategyName = "Rot_Dps_Strategy";
            s.minResourceToEngage = 40.0f;
            s.recoveryThreshold = 15.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 40;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f }
            };
            s.phaseModifiers[CombatPhase::Recovery] = {
                { AbilityTag::Filler,      0.3f, -30.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
