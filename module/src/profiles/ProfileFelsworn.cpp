/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Felsworn Profiles & Spec Strategies
 *
 * Specializations:
 *   - Spec 8: Slayer (Melee Havoc / Dual-Wield DPS)
 *   - Spec 9: Tyrant (Demon Metamorphosis Tank)
 *   - Spec 7: Infernal (Chaos / Fire Caster DPS)
 */

#include "profiles/ProfileFelsworn.h"
#include "profiles/ProfileRegistry.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"
#include "SpellAuras.h"

namespace BotAI
{
    void RegisterFelswornProfiles()
    {
        // =========================================================================
        // 1. SPEC 8: SLAYER (MELEE HAVOC / DUAL-WIELD DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Melee
        // - Resource: Energy + Felfury
        // - Tactical Policy: The Demon Within is a burst window (not spammed)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 14;
            p.specId = 8; // Slayer
            p.role = BotRole::Dps;
            p.profileName = "Felsworn_Slayer_Melee";

            // Burst Metamorphosis Window
            {
                AbilityDescriptor d;
                d.name = "Inner Demon";
                d.rootSpellId = 804216;
                d.missingAuraOnCaster = 804216;
                // Native duration is five seconds per consumed Felfury;
                // six stacks also unlock the native full-Fury talent effect.
                uint8 minimumFury = p.role == BotRole::Tank ? 4 : 6;
                d.customScorer = [minimumFury](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    Aura const* fury = ctx.bot->GetAura(800058);
                    if (!fury || !fury->GetStackAmount())
                        return -1.0f;
                    // Six/four is a preferred burst size, not a native eligibility
                    // floor. Existing spenders can prevent that stack count entirely.
                    return fury->GetStackAmount() >= minimumFury ? 80.0f : 0.0f;
                };
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 90000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Crowd Control
            {
                AbilityDescriptor d;
                d.name = "Chaos Nova";
                d.rootSpellId = 802025;
                d.tags = AbilityTag::CrowdControl | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 45000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // Mobility / Gap Closer: Chaos Rush
            {
                AbilityDescriptor d;
                d.name = "Chaos Rush";
                d.rootSpellId = 500028;
                d.tags = AbilityTag::MeleeAttack;
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

            // Immolation Aura
            {
                AbilityDescriptor d;
                d.name = "Immolation Aura";
                d.rootSpellId = 800207;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800207;
                d.internalThrottleMs = 15000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // DoT: Demonfire Pact
            {
                AbilityDescriptor d;
                d.name = "Demonfire Pact";
                d.rootSpellId = 800031;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 195.0f;
                p.abilities.push_back(d);
            }

            // Spender: Felrend
            {
                AbilityDescriptor d;
                d.name = "Felrend";
                d.rootSpellId = 800210;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Builder: Twin Slice
            // Slayer's one-handed weapon attacks (Spell.dbc EquippedItemSubClassMask 0xa091 = 1H axe/mace/sword/
            // dagger/fist; Twin Slice also needs an off-hand weapon). Zyth45 ranks Annihilan Strike and
            // Azzinoth's Assault first; the profile listed neither. Native weapon checks stay in force.
            {
                AbilityDescriptor d;
                d.name = "Annihilan Strike";
                d.rootSpellId = 572804;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 255.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Azzinoth's Assault";
                d.rootSpellId = 803475;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 245.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Twin Slice";
                d.rootSpellId = 801901;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // Buff: Illidari Intuition
            {
                AbilityDescriptor d;
                d.name = "Illidari Intuition";
                d.rootSpellId = 800212;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800212;
                d.internalThrottleMs = 30000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 14;
            s.specId = 8;
            s.role = BotRole::Dps;
            s.strategyName = "Slayer_Melee_Strategy";
            s.minResourceToEngage = 50.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetPower(POWER_ENERGY) >= 50;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,   1.6f, 40.0f }
            };

            // ResourcePolicy for Felfury (800058)
            {
                ResourcePolicy pol;
                pol.key = CombatResourceKey{ CombatResourceKind::AuraStack, 0, 800058 };
                pol.overcapThreshold = 4;
                s.resourcePolicies.push_back(pol);
            }

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 2. SPEC 9: TYRANT (DEMON METAMORPHOSIS TANK)
        // =========================================================================
        // Contract:
        // - Canonical Role: Tank
        // - Baseline State: Tank Melee
        // - Emergency Window: The Demon Within (< 65% HP)
        // - AoE Threat: Immolation Aura + Infernal Strike
        // - TankReady: HP >= 75%
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 14;
            p.specId = 9; // Tyrant
            p.role = BotRole::Tank;
            p.profileName = "Felsworn_Tyrant_Tank";

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

            // Metamorphosis Defensive Window
            {
                AbilityDescriptor d;
                d.name = "Inner Demon";
                d.rootSpellId = 804216;
                d.missingAuraOnCaster = 804216;
                // Native duration is five seconds per consumed Felfury;
                // six stacks also unlock the native full-Fury talent effect.
                uint8 minimumFury = p.role == BotRole::Tank ? 4 : 6;
                d.customScorer = [minimumFury](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    Aura const* fury = ctx.bot->GetAura(800058);
                    if (!fury || !fury->GetStackAmount())
                        return -1.0f;
                    // Six/four is a preferred burst size, not a native eligibility
                    // floor. Existing spenders can prevent that stack count entirely.
                    return fury->GetStackAmount() >= minimumFury ? 80.0f : 0.0f;
                };
                d.tags = AbilityTag::DefensiveCD | AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.internalThrottleMs = 90000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // AoE Control
            {
                AbilityDescriptor d;
                d.name = "Chaos Nova";
                d.rootSpellId = 802025;
                d.tags = AbilityTag::CrowdControl | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 45000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // AoE Threat
            {
                AbilityDescriptor d;
                d.name = "Immolation Aura";
                d.rootSpellId = 800207;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800207;
                d.internalThrottleMs = 15000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Infernal Strike";
                d.rootSpellId = 801016;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 10000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // Threat Spender
            {
                AbilityDescriptor d;
                d.name = "Felrend";
                d.rootSpellId = 800210;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // DoT Debuff
            {
                AbilityDescriptor d;
                d.name = "Demonfire Pact";
                d.rootSpellId = 800031;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // Generator
            {
                AbilityDescriptor d;
                d.name = "Twin Slice";
                d.rootSpellId = 801901;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 14;
            s.specId = 9;
            s.role = BotRole::Tank;
            s.strategyName = "Tyrant_Tank_Strategy";

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
        // 3. SPEC 7: INFERNAL (CHAOS / FIRE CASTER DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Caster
        // - Resource: Mana / Felfury
        // - Nuke: Tormentor (Chaos Bolt), DoT: Demonfire Pact
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 14;
            p.specId = 7; // Infernal
            p.role = BotRole::Dps;
            p.profileName = "Felsworn_Infernal_Caster";

            // Burst Cooldown
            {
                AbilityDescriptor d;
                d.name = "Inner Demon";
                d.rootSpellId = 804216;
                d.missingAuraOnCaster = 804216;
                // Native duration is five seconds per consumed Felfury;
                // six stacks also unlock the native full-Fury talent effect.
                uint8 minimumFury = p.role == BotRole::Tank ? 4 : 6;
                d.customScorer = [minimumFury](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    Aura const* fury = ctx.bot->GetAura(800058);
                    if (!fury || !fury->GetStackAmount())
                        return -1.0f;
                    // Six/four is a preferred burst size, not a native eligibility
                    // floor. Existing spenders can prevent that stack count entirely.
                    return fury->GetStackAmount() >= minimumFury ? 80.0f : 0.0f;
                };
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 90000;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // CC
            {
                AbilityDescriptor d;
                d.name = "Chaos Nova";
                d.rootSpellId = 802025;
                d.tags = AbilityTag::CrowdControl | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 45000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // Primary Nuke: Tormentor
            {
                AbilityDescriptor d;
                d.name = "Tormentor";
                d.rootSpellId = 800162;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // DoT: Demonfire Pact
            {
                AbilityDescriptor d;
                d.name = "Demonfire Pact";
                d.rootSpellId = 800031;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // AoE: Immolation Aura
            {
                AbilityDescriptor d;
                d.name = "Immolation Aura";
                d.rootSpellId = 800207;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800207;
                d.internalThrottleMs = 15000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // Melee Fallbacks
            {
                AbilityDescriptor d;
                d.name = "Felrend";
                d.rootSpellId = 800210;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Twin Slice";
                d.rootSpellId = 801901;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // energy-free filler that needs the native caster aura; 52% of real casts
                d.name = "Sargeron Smite";
                d.rootSpellId = 802060;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f; // free filler: paid spenders go first (a free filler at 225 starved the energy spenders and cost Infernal 63% of its damage)
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bane of Fire";
                d.rootSpellId = 707901;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 215.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Hateforged Barrier";
                d.rootSpellId = 705129;
                d.tags = AbilityTag::Buff | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 30 energy, needs the native caster aura 803468; the legacy rotation cast it 36 times in the baseline and it vanished once the profile always found a filler
                d.name = "Ruin";
                d.rootSpellId = 801895;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 35 energy; 26 casts in the baseline
                d.name = "Fel Fireball";
                d.rootSpellId = 801312;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 235.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 14;
            s.specId = 7;
            s.role = BotRole::Dps;
            s.strategyName = "Infernal_Caster_Strategy";
            s.minResourceToEngage = 40.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 70.0f;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.3f, 30.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
