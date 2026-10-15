/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Reaper Profiles implementation
 * Supports:
 *   - Spec 57: Domination (Dark Soul Plate Tank)
 *   - Spec 56: Harvest (Melee Soul Spender DPS)
 *   - Spec 55: Soul (Shadow & Nether Caster DPS)
 */

#include "profiles/ProfileReaper.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "engine/SpecStrategyRegistry.h"
#include "Player.h"

namespace BotAI
{
    void RegisterReaperProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Reaper - Spec 57: Domination (DARK SOUL PLATE TANK)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 30;
            p.specId = 57; // Domination
            p.role = BotRole::Tank;
            p.profileName = "Reaper_Domination_Tank";

            // 1. Personal Defensives
            {
                AbilityDescriptor d;
                d.name = "Underwalk (Shadow Escape)";
                d.rootSpellId = 800797;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.missingAuraOnCaster = 800797;
                d.internalThrottleMs = 30000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bolstered Form (Plate Armor Stance)";
                d.rootSpellId = 680337;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 680337;
                d.internalThrottleMs = 15000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Soul Protector (Barrier)";
                d.rootSpellId = 300553;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 60.0f;
                d.missingAuraOnCaster = 300553;
                d.internalThrottleMs = 15000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 2. Taunt / Threat
            // Real taunt: Spell.dbc effect ATTACK_ME + MOD_TAUNT, 8 s native cooldown. The generic
            // Taunt355 root is not a learned spell for Ascension classes ("not known/resolvable" in
            // every I17 tank decision) and passive ids cannot be cast.
            {
                AbilityDescriptor d;
                d.name = "Writhe (Taunt)";
                d.rootSpellId = 801337;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 455.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Tormentor (Taunt)";
                d.rootSpellId = 92147;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 450.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (!ctx.victimTargetingNonTank)
                        return -1.0f;
                    return 0.0f;
                };
                p.abilities.push_back(d);
            }

            // 3. Gap Closer
            {
                AbilityDescriptor d;
                d.name = "Scythe Rush (Gap Closer)";
                d.rootSpellId = 500359;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 190.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 190.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 4. Melee Attacks & Leech Sustain
            {
                AbilityDescriptor d;
                d.name = "Soul Strike (Leech Attack)";
                d.rootSpellId = 500517;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::DirectHeal;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Murder (Heavy Strike)";
                d.rootSpellId = 500376;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Deathwind (AoE Leech)";
                d.rootSpellId = 800174;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reap (Primary Builder)";
                d.rootSpellId = 500357;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Dirge (Offhand Finisher)";
                d.rootSpellId = 801328;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // SpecStrategy for Domination
            SpecStrategy s;
            s.classId = 30;
            s.specId = 57;
            s.role = BotRole::Tank;
            s.strategyName = "Reaper_Domination_Tank_Strategy";
            s.requiredState = RequiredCombatState{ 680337, 680337, {} };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::Taunt,       2.0f, 80.0f },
                { AbilityTag::MeleeAttack, 1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::MeleeAttack, 1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 2.0f, 60.0f }
            };

            // ResourcePolicy for Reaped Soul (500363) and Runic Power
            {
                ResourcePolicy pol;
                pol.key = CombatResourceKey{ CombatResourceKind::AuraStack, 0, 500363 };
                pol.minToEngage = 0;
                pol.defensiveReserve = 1;
                pol.reserveForDefensive = true;
                pol.allowDumpDuringBurst = true;
                s.resourcePolicies.push_back(pol);
            }

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 65.0f;
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 2: Reaper - Spec 56: Harvest (MELEE SOUL SPENDER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 30;
            p.specId = 56; // Harvest
            p.role = BotRole::Dps;
            p.profileName = "Reaper_Harvest_MeleeDps";

            // Learned native Harvest Time preserves Soul Infusion on spenders.
            // Treat this as a combat window, not routine pre-pull buff maintenance.
            {
                AbilityDescriptor d;
                d.name = "Harvest Time";
                d.rootSpellId = 803995;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 803995;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Slaughter (Extinction proc)";
                d.rootSpellId = 500373;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    // Native Extinction560414 is consumed by Slaughter's rank chain.
                    // All costs, range and aura requirements remain native checks.
                    return ctx.bot->HasAura(560414) ? 120.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }


            // 1. Personal Defense
            {
                AbilityDescriptor d;
                d.name = "Underwalk (Shadow Escape)";
                d.rootSpellId = 800797;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.missingAuraOnCaster = 800797;
                d.internalThrottleMs = 30000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bolstered Form";
                d.rootSpellId = 680337;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.missingAuraOnCaster = 680337;
                d.internalThrottleMs = 20000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 2. Gap Closer
            {
                AbilityDescriptor d;
                d.name = "Scythe Rush (Gap Closer)";
                d.rootSpellId = 500359;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 210.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 210.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 3. Melee Slashes & Finishers
            {
                AbilityDescriptor d;
                d.name = "Murder (Execution Strike)";
                d.rootSpellId = 500376;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Soul Strike (Weapon Attack)";
                d.rootSpellId = 500517;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Shudder Scythe (Cleave)";
                d.rootSpellId = 801321;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 165.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Dirge (Finisher)";
                d.rootSpellId = 801328;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 155.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Deathwind (Area Leech)";
                d.rootSpellId = 800174;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reap (Primary Generator)";
                d.rootSpellId = 500357;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // SpecStrategy for Harvest
            SpecStrategy s;
            s.classId = 30;
            s.specId = 56;
            s.role = BotRole::Dps;
            s.strategyName = "Reaper_Harvest_MeleeDps_Strategy";

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::MeleeAttack, 1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::Execute,     1.8f, 50.0f },
                { AbilityTag::MeleeAttack, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,   1.8f, 50.0f },
                { AbilityTag::MeleeAttack, 1.2f, 20.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 50.0f;
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 3: Reaper - Spec 55: Soul (MELEE RUNIC-POWER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 30;
            p.specId = 55; // Soul
            p.role = BotRole::Dps;
            p.profileName = "Reaper_Soul_MeleeDps";
            // Site role is Melee DPS (Strength, Runic Power). Reap/Soul Strike/Murder are melee
            // swings and Reap is the only free builder: native Reap hits generate Soul Fragments
            // (805077) -> Reaped Soul (500363) -> Soul Infusion (803031), which gates Withering
            // Touch, Soulslam and Soulrend. The previous caster tags voted this spec to 22 yards,
            // where Reap was permanently out of range and none of that chain could start.
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // 1. Personal Defense & Mana/Soul Management
            {
                AbilityDescriptor d;
                d.name = "Underwalk (Defensive)";
                d.rootSpellId = 800797;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.missingAuraOnCaster = 800797;
                d.internalThrottleMs = 30000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Soul Tap (Resource Regen)";
                d.rootSpellId = 807397;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::Self;
                d.minSelfHpPct = 40.0f;
                d.internalThrottleMs = 8000;
                d.baseScore = 210.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (ctx.botPowerPct < 40.0f && ctx.botHpPct > 50.0f)
                        return 210.0f;
                    return -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 2. Melee builders, spenders and Soul Infusion DoTs
            {
                AbilityDescriptor d;
                d.name = "Tormented Souls (Shadow DoT)";
                d.rootSpellId = 500483;
                d.tags = AbilityTag::PeriodicDamage | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Withering Touch (Curse DoT)";
                d.rootSpellId = 573071;
                d.tags = AbilityTag::PeriodicDamage | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Ghastly Screech (AoE Shadow)";
                d.rootSpellId = 806146;
                d.tags = AbilityTag::AoEDamage | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Deathwind (AoE Leech)";
                d.rootSpellId = 800174;
                d.tags = AbilityTag::AoEDamage | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Soulslam (Infusion Burst)";
                d.rootSpellId = 504014;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 20000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Wraithblade (Weapon Spender)";
                d.rootSpellId = 806823;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Murder (Execution Strike)";
                d.rootSpellId = 500376;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Soul Strike (Weapon Attack)";
                d.rootSpellId = 500517;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reap (Primary Generator)";
                d.rootSpellId = 500357;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 100.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // SpecStrategy for Soul
            SpecStrategy s;
            s.classId = 30;
            s.specId = 55;
            s.role = BotRole::Dps;
            s.strategyName = "Reaper_Soul_MeleeDps_Strategy";

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::PeriodicDamage, 1.5f, 40.0f },
                { AbilityTag::MeleeAttack,    1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::MeleeAttack,  1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 2.0f, 60.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 30) &&
                       (bot->GetHealthPct() >= 50.0f);
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
