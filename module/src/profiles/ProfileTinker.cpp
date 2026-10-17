/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Tinker Profiles & Strategies implementation
 * Supports:
 *   - Spec 50: Mechanics (Ranged DPS)
 *   - Spec 51: Invention (Nanobot & Medical Dispenser Healer)
 *   - Spec 49: Demolition (Explosive Guns & Artillery DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileTinker.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/SpellResolver.h"
#include "Player.h"

namespace BotAI
{
    static void RegisterTinkerStrategies()
    {
        // -------------------------------------------------------------
        // Strategy 1: Tinker - Spec 50: Mechanics (Ranged DPS)
        // -------------------------------------------------------------
        {
            SpecStrategy s;
            s.classId = 28; // Tinker
            s.specId = 50;  // Mechanics
            s.ownsCombatRotation = true;
            s.role = BotRole::Dps;

            // Mechanics can fight outside Mechsuit. Native Scrap/form requirements remain
            // enforced by the action evaluator; construction must not own every decision
            // while mana is depleted or the temporary form is unavailable.

            // ResourcePolicy for Scrap (801816)
            {
                ResourcePolicy pol;
                pol.key = CombatResourceKey{ CombatResourceKind::AuraStack, 0, 801816 };
                pol.minToEngage = 0;
                pol.defensiveReserve = 10; // Keep at least 10 Scrap for Mechsuit rebuilding
                pol.reserveForDefensive = true;
                pol.allowDumpDuringBurst = true;
                s.resourcePolicies.push_back(pol);
            }

            // Allow the controlled opener while building the forty-Scrap stockpile.
            s.isReadyToPull = [](Player* bot, CombatContext const& ctx) -> bool
            {
                if (bot->HasAura(801384))
                    return ctx.botHpPct >= 70.0f;

                // Out of combat Scrap generation is impossible in Core.
                // Allow pull to acquire Scrap upon first damaging hit!
                Aura const* scrapAura = bot->GetAura(801816);
                int32 scrap = scrapAura ? scrapAura->GetStackAmount() : 0;
                if (scrap < 40)
                    return ctx.botHpPct >= 70.0f;

                return false; // Has enough Scrap, should cast Mechsuit before pull
            };

            // Phase modifiers
            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::RangedAttack, 1.4f, 30.0f },
                { AbilityTag::OffensiveCD, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.6f, 40.0f },
                { AbilityTag::AoEDamage, 1.3f, 25.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 2.0f, 50.0f },
                { AbilityTag::RangedAttack, 1.5f, 30.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Strategy 2: Tinker - Spec 51: Invention (Healer)
        // -------------------------------------------------------------
        {
            SpecStrategy s;
            s.classId = 28; // Tinker
            s.specId = 51;  // Invention
            s.role = BotRole::Healer;

            // Invention is gadget healer
            s.isReadyToPull = [](Player* /*bot*/, CombatContext const& ctx) -> bool
            {
                return ctx.botPowerPct >= 50.0f && ctx.botHpPct >= 65.0f;
            };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::Buff, 1.6f, 30.0f },
                { AbilityTag::PeriodicHeal, 1.4f, 25.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::EmergencyHeal, 2.0f, 60.0f },
                { AbilityTag::DefensiveCD, 1.6f, 40.0f },
                { AbilityTag::DirectHeal, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::Shield, 1.5f, 35.0f },
                { AbilityTag::PeriodicHeal, 1.4f, 30.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Strategy 3: Tinker - Spec 49: Demolition (DPS)
        // -------------------------------------------------------------
        {
            SpecStrategy s;
            s.classId = 28; // Tinker
            s.specId = 49;  // Demolition
            s.role = BotRole::Dps;

            s.isReadyToPull = [](Player* /*bot*/, CombatContext const& ctx) -> bool
            {
                return ctx.botPowerPct >= 30.0f && ctx.botHpPct >= 60.0f;
            };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.3f, 25.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 2.2f, 70.0f },
                { AbilityTag::RangedAttack, 1.4f, 35.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 2.2f, 60.0f },
                { AbilityTag::OffensiveCD, 1.5f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::RangedAttack, 1.5f, 35.0f },
                { AbilityTag::OffensiveCD, 1.3f, 20.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }

    void RegisterTinkerProfiles()
    {
        RegisterTinkerStrategies();

        // -------------------------------------------------------------
        // Profile 1: Tinker - Spec 50: Mechanics (RANGED DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 28; // Tinker
            p.specId = 50;  // Mechanics
            p.role = BotRole::Dps;
            p.profileName = "Tinker_Mechanics_Dps";
            p.preferredEngageDistance = 20.0f;

            // Native Replenishment Beacon restores mana to nearby allies, including its owner.
            // Place it before the mana floor prevents all builders and Mechsuit attacks.
            {
                AbilityDescriptor d;
                d.name = "Build: Replenishment Beacon";
                d.rootSpellId = 560750;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 440.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return ctx.botPowerPct < 70.0f ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 2. Active Mitigation: Kinetic Shield / Scrapshielding
            {
                AbilityDescriptor d;
                d.name = "Kinetic Shield";
                d.rootSpellId = 806224;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Scrapshielding";
                d.rootSpellId = 806627;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 3. Stance / Mechsuit Form (Do not spam rebuild if already active)
            {
                AbilityDescriptor d;
                d.name = "Build: Mechsuit";
                d.rootSpellId = 801384;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 801384;
                d.internalThrottleMs = 30000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 4. Native ranged attacks and Mechsuit damage
            // Native Mechsuit action bar (AscensionTinker::FillMechsuitBar).
            // The legacy profile omitted these and attempted unmounted Shotgun.
            {
                AbilityDescriptor d;
                d.name = "Mechsuit: Combustion";
                d.rootSpellId = 801387;
                d.tags = AbilityTag::AoEDamage;
                d.minAoETargets = 1; // Native Mechsuit attack is also valid against one boss
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                d.requiredAuraOnCaster = 801384; // native Mechsuit aura: casting these outside the suit fails with ONLY_MOUNTED
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Mechsuit: Laser Beam";
                d.rootSpellId = 805372;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                d.requiredAuraOnCaster = 801384; // native Mechsuit aura: casting these outside the suit fails with ONLY_MOUNTED
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Gatling Gun";
                d.rootSpellId = 500213;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                d.requiredAuraOnCaster = 801384; // native Mechsuit aura: casting these outside the suit fails with ONLY_MOUNTED
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Deathball";
                d.rootSpellId = 500236;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Scrap Shot";
                d.rootSpellId = 500549; // Native Shotgun rank chain, not the legacy alias
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 3000;
                d.baseScore = 220.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return ctx.bot->IsInTinkerMechsuit() ? -1.0f : 0.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Sticky Bomb";
                d.rootSpellId = 500232;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reload";
                d.rootSpellId = 500237;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 6000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 20% of real casts
                d.name = "Build: Sentry Turret";
                d.rootSpellId = 500239;
                d.tags = AbilityTag::TotemOrWard;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Build: Shield Beacon";
                d.rootSpellId = 801799;
                d.tags = AbilityTag::TotemOrWard;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 6 s cooldown weapon strike
                d.name = "Spanner Smash";
                d.rootSpellId = 805351;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 205.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Tinker - Spec 51: Invention (GADGET HEALER)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 28;
            p.specId = 51; // Invention
            p.role = BotRole::Healer;
            p.profileName = "Tinker_Invention_Healer";

            // Defibrillate is native area resurrection, handled by combat utility.
            // It is not a direct heal for a living ally.

            // 2. Protective Barrier: Nanobot Reconstruction (< 60% HP)
            {
                AbilityDescriptor d;
                d.name = "Nanobot Reconstruction";
                d.rootSpellId = 801809;
                d.tags = AbilityTag::Shield;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 60.0f;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 8000;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // 3. Primary Direct Heal: Repair Shot (< 80% HP)
            {
                AbilityDescriptor d;
                d.name = "Repair Shot";
                d.rootSpellId = 801707;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 4. HoT / Pack: Med Pack (< 90% HP)
            {
                AbilityDescriptor d;
                d.name = "Med Pack";
                d.rootSpellId = 800347;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 5000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 5. Medical Turret: Build: ZIGGI-6K (Deploy once, long throttle)
            {
                AbilityDescriptor d;
                d.name = "Build: ZIGGI-6K (Healing Turret)";
                d.rootSpellId = 92140;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.trackedEntityType = TrackedEntityType::Turret;
                d.trackedEntityEntry = 500481;
                d.internalThrottleMs = 30000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 6. Offensive Weaving (Only when all party members are healthy)
            {
                AbilityDescriptor d;
                d.name = "Sticky Bomb";
                d.rootSpellId = 500232;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 160.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return (ctx.lowestAllyHpPct > 80.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Blackpowder Shot";
                d.rootSpellId = 500234;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return (ctx.lowestAllyHpPct > 80.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reload";
                d.rootSpellId = 500237;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 6000;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 17% of real casts
                d.name = "Build: Replenishment Beacon";
                d.rootSpellId = 560750;
                d.tags = AbilityTag::TotemOrWard;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Build: Restorative Beacon";
                d.rootSpellId = 801801;
                d.tags = AbilityTag::TotemOrWard;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 225.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Build: Shield Beacon";
                d.rootSpellId = 801799;
                d.tags = AbilityTag::TotemOrWard;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Nanobot Recharger";
                d.rootSpellId = 803552;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::LowestHealthAlly;
                d.internalThrottleMs = 15000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Tinker - Spec 49: Demolition (EXPLOSIVES & GUNS DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 28;
            p.specId = 49; // Demolition
            p.role = BotRole::Dps;
            p.profileName = "Tinker_Demolition_Dps";
            p.useRangedAutoRepeat = true;
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // 1. Emergency Defense: Kinetic Shield (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Kinetic Shield";
                d.rootSpellId = 806224;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.internalThrottleMs = 20000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // 2. Major Artillery Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Rocket Bombardment";
                d.rootSpellId = 706695;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 45000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Air Strike";
                d.rootSpellId = 801744;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 60000;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }
            // Build: Destructo-Bot (804673) is deliberately not cast. It is a 45 s channel that
            // hands the caster control of a remote-controlled puppet (summon properties category
            // PUPPET); the AI never drives that puppet, so I65 telemetry showed the bot channelling
            // for 12.4 s of a 15 s window next to an inert entry 50300 (no victim, never in
            // combat) and the 45 s cooldown equals the channel, which would keep it frozen for a
            // whole 180 s fight.

            // 3. Heavy Ordnance
            {
                AbilityDescriptor d;
                d.name = "Rocket Launcher";
                d.rootSpellId = 500235;
                d.tags = AbilityTag::RangedAttack; // not OffensiveCD: a 12-second-throttle rotational attack; that tag scales the score by x0.15 outside boss/elite/PvP fights
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Napalm";
                d.rootSpellId = 92138;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 8000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Sticky Bomb";
                d.rootSpellId = 500232;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Deathball";
                d.rootSpellId = 500236;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Guns & Reload
            {
                AbilityDescriptor d;
                d.name = "Rifle (Gunsling)";
                d.rootSpellId = 801648;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 2500;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Blackpowder Shot";
                d.rootSpellId = 500234;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reload";
                d.rootSpellId = 500237;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 5000;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 8 s cooldown bomb, 51% of real casts
                d.name = "Bomb Toss";
                d.rootSpellId = 801005;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Build: Sentry Turret";
                d.rootSpellId = 500239;
                d.tags = AbilityTag::TotemOrWard;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }
    }
}
