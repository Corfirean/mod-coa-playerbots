/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Guardian Profiles & Spec Strategies
 *
 * Specializations:
 *   - Spec 21: Vanguard (Tower Formation Heavy Shield Tank)
 *   - Spec 19: Gladiator (Line Formation 1H + Shield Physical DPS)
 *   - Spec 20: Inspiration (Line Formation Aura & Support DPS)
 */

#include "profiles/ProfileGuardian.h"
#include "profiles/ProfileRegistry.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

namespace BotAI
{
    unsigned int VanguardBaselineFormation()
    {
        SpellInfo const* stance = sSpellMgr->GetSpellInfo(803907);
        SpellInfo const* ram = sSpellMgr->GetSpellInfo(573211);
        if (stance && ram)
            for (auto const& effect : stance->Effects)
                if (effect.IsAura(SPELL_AURA_MOD_SHAPESHIFT))
                    return ram->CheckShapeshift(effect.MiscValue) == SPELL_CAST_OK ? 800317u : 803130u;
        return 803130u;
    }

    void RegisterGuardianProfiles()
    {
        // =========================================================================
        // 1. SPEC 21: VANGUARD (HEAVY SHIELD TANK)
        // =========================================================================
        // Contract:
        // - Canonical Role: Tank
        // - Mandatory Baseline State: Tower Formation (spell 800317, aura 800317)
        // - Active Mitigation: Raise Shield (< 65% HP, throttled)
        // - TankReady: Tower Formation active + HP >= 75%
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 18; // Guardian
            p.specId = 21;  // Vanguard
            p.role = BotRole::Tank;
            p.profileName = "Guardian_Vanguard_Tank";

            // Native Tower Formation also casts stance 803907 (MOD_SHAPESHIFT, form 36). Ram,
            // Pulverize, Raise Shield and Shield Challenge carry SPELL_ATTR0_NOT_SHAPESHIFTED, so
            // whenever form 36 is not a STANCE-flagged form (this world's spellshapeshiftform_dbc
            // row 36 has Flags 0) the core rejects all of them in Tower: I17/I49 Vanguard cast only
            // Reprisal (4 casts in 60 s) and held aggro 3 s of 15. Ask the core itself whether Tower
            // allows the tank's strikes and fall back to Line Formation (which Gladiator and
            // Inspiration use) when it does not; no cast guard is bypassed either way.
            uint32 const vanguardFormation = VanguardBaselineFormation();

            // Mandatory Stance: Tower Formation (or Line Formation when Tower blocks the kit)
            {
                AbilityDescriptor d;
                d.name = vanguardFormation == 800317 ? "Tower Formation" : "Line Formation";
                d.rootSpellId = vanguardFormation;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = vanguardFormation;
                d.internalThrottleMs = 4000;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

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

            // Active Mitigation
            {
                AbilityDescriptor d;
                d.name = "Raise Shield";
                d.rootSpellId = 500168;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Gap Closer / Stun: Ram
            {
                AbilityDescriptor d;
                d.name = "Ram";
                d.rootSpellId = 802284;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 80.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Threat Strikes
            {
                AbilityDescriptor d;
                d.name = "Pulverize";
                d.rootSpellId = 800311;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reprisal";
                d.rootSpellId = 800316;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Broad Sweep";
                d.rootSpellId = 805150;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 18;
            s.specId = 21;
            s.role = BotRole::Tank;
            s.strategyName = "Vanguard_Tank_Strategy";
            s.requiredState.formSpellId = vanguardFormation;
            s.requiredState.formAuraId = vanguardFormation;

            s.isReadyToPull = [vanguardFormation](Player* bot, CombatContext const&) -> bool
            {
                return bot->HasAura(vanguardFormation) && bot->GetHealthPct() >= 75.0f;
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
        // 2. SPEC 19: GLADIATOR (1H + SHIELD PHYSICAL DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Mandatory Baseline State: Line Formation (spell 803130, aura 803130)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 18;
            p.specId = 19; // Gladiator
            p.role = BotRole::Dps;
            p.profileName = "Guardian_Gladiator_Dps";

            // Mandatory Formation: Line Formation
            {
                AbilityDescriptor d;
                d.name = "Line Formation";
                d.rootSpellId = 803130;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 803130;
                d.internalThrottleMs = 5000;
                d.baseScore = 480.0f;
                p.abilities.push_back(d);
            }

            // Defensive
            {
                AbilityDescriptor d;
                d.name = "Raise Shield";
                d.rootSpellId = 500168;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Gap Closer: Ram
            {
                AbilityDescriptor d;
                d.name = "Ram";
                d.rootSpellId = 802284;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Primary Strikes
            {
                AbilityDescriptor d;
                d.name = "Pulverize";
                d.rootSpellId = 800311;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reprisal";
                d.rootSpellId = 800316;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Broad Sweep";
                d.rootSpellId = 805150;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 1; // cheap cleave, real players spend energy on it even on one target (28-31% of casts)
                d.baseScore = 235.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 15 s cooldown weapon strike
                d.name = "Centurion Strike";
                d.rootSpellId = 802286;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Standard of Valiance";
                d.rootSpellId = 800319;
                d.tags = AbilityTag::TotemOrWard | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 18;
            s.specId = 19;
            s.role = BotRole::Dps;
            s.strategyName = "Gladiator_Dps_Strategy";
            s.requiredState.formSpellId = 803130; // Line Formation
            s.requiredState.formAuraId = 803130;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->HasAura(803130) && bot->GetHealthPct() >= 70.0f;
            };

            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,   1.8f, 50.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::MeleeAttack, 1.4f, 40.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 3. SPEC 20: INSPIRATION (AURA & FORMATION SUPPORT DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Support
        // - Mandatory Baseline State: Line Formation (spell 803130, aura 803130)
        // - Party Support: Broad Sweep AP debuff, Ram stun support
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 18;
            p.specId = 20; // Inspiration
            p.role = BotRole::Support;
            p.profileName = "Guardian_Inspiration_Support";

            // Mandatory Support Formation
            {
                AbilityDescriptor d;
                d.name = "Line Formation";
                d.rootSpellId = 803130;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.casterAuraId = 803130;
                d.missingAuraOnCaster = 803130;
                d.internalThrottleMs = 5000;
                d.baseScore = 480.0f;
                p.abilities.push_back(d);
            }

            // Defensive
            {
                AbilityDescriptor d;
                d.name = "Raise Shield";
                d.rootSpellId = 500168;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Support Debuff / Party Protection
            {
                AbilityDescriptor d;
                d.name = "Broad Sweep";
                d.rootSpellId = 805150;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                
                d.minAoETargets = 1;
                d.baseScore = 235.0f;
                p.abilities.push_back(d);
            }

            // Support Stun: Ram
            {
                AbilityDescriptor d;
                d.name = "Ram";
                d.rootSpellId = 802284;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 245.0f;
                p.abilities.push_back(d);
            }

            // Strikes
            {
                AbilityDescriptor d;
                d.name = "Pulverize";
                d.rootSpellId = 800311;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reprisal";
                d.rootSpellId = 800316;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 175.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // energy spender around the caster, 28% of real casts
                d.name = "Ballad of the Dragonslayer";
                d.rootSpellId = 574364;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 1;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Standard of Rallying";
                d.rootSpellId = 500263;
                d.tags = AbilityTag::TotemOrWard | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // about 1,600 damage per cast against 720 for Ballad of the Dragonslayer; it was only cast by the generic fallback
                d.name = "Ballad of the Conqueror";
                d.rootSpellId = 574340;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 18;
            s.specId = 20;
            s.role = BotRole::Support;
            s.strategyName = "Inspiration_Support_Strategy";
            s.requiredState.formSpellId = 803130; // Line Formation
            s.requiredState.formAuraId = 803130;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->HasAura(803130) && bot->GetHealthPct() >= 70.0f;
            };

            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 1.8f, 50.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
