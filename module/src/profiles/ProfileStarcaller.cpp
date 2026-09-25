/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Starcaller Profiles implementation
 * Supports:
 *   - Spec 100: Moon Guard (Astral Protection Tank)
 *   - Spec 43: Moon Priest (Lunar & Tide Healer)
 *   - Spec 44: Sentinel (Ranged Astral Bow DPS)
 *   - Spec 45: Warden (Melee Umbral Moonblade DPS)
 */

#include "profiles/ProfileStarcaller.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "engine/SpecStrategyRegistry.h"
#include "Player.h"

namespace BotAI
{
    void RegisterStarcallerProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Starcaller - Spec 100: Moon Guard (ASTRAL TANK)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 26; // Starcaller
            p.specId = 100; // Moon Guard
            p.role = BotRole::Tank;
            p.profileName = "Starcaller_MoonGuard_Tank";

            // 1. Primary Taunt
            {
                AbilityDescriptor d;
                d.name = "Taunt";
                d.rootSpellId = 355;
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

            // 2. Active Mitigation: Constellation Barrier / Astral Aegis
            {
                AbilityDescriptor d;
                d.name = "Constellation Barrier";
                d.rootSpellId = 300259;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.missingAuraOnCaster = 300259;
                d.internalThrottleMs = 20000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Astral Aegis";
                d.rootSpellId = 806155;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.missingAuraOnCaster = 806155;
                d.internalThrottleMs = 15000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 3. Stance / Aspect: Aspect of the Stars
            {
                AbilityDescriptor d;
                d.name = "Aspect of the Stars";
                d.rootSpellId = 800510;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800510;
                d.internalThrottleMs = 20000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 4. Melee Threat Strikes
            {
                AbilityDescriptor d;
                d.name = "Starsunder (Sunder Threat)";
                d.rootSpellId = 801127;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Celestial Strike (Generate Stars)";
                d.rootSpellId = 800496;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Celestial Cleave (AoE Threat)";
                d.rootSpellId = 801181;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Shooting Star";
                d.rootSpellId = 800505;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // SpecStrategy for Moon Guard
            SpecStrategy s;
            s.classId = 26;
            s.specId = 100;
            s.role = BotRole::Tank;
            s.strategyName = "Starcaller_MoonGuard_Tank_Strategy";
            s.requiredState = RequiredCombatState{ 800510, 800510, {} };

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

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetHealthPct() >= 65.0f;
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 2: Starcaller - Spec 43: Moon Priest (LUNAR HEALER)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 26;
            p.specId = 43; // Moon Priest
            p.role = BotRole::Healer;
            p.profileName = "Starcaller_MoonPriest_Healer";

            // 1. Emergency Critical Direct Heal: Vial of Moonwell Water (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Vial of Moonwell Water";
                d.rootSpellId = 804652;
                d.tags = AbilityTag::DirectHeal | AbilityTag::EmergencyHeal | AbilityTag::DefensiveCD;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 40.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // 2. Protective Barrier: Astral Aegis (< 55% HP)
            {
                AbilityDescriptor d;
                d.name = "Astral Aegis";
                d.rootSpellId = 806155;
                d.tags = AbilityTag::Shield;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 55.0f;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 10000;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // 3. Ground / Area Heal: Moonwell
            {
                AbilityDescriptor d;
                d.name = "Moonwell";
                d.rootSpellId = 804739;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::Self;
                d.minInjuredAllies = 2;
                d.injuredAllyHpPctThreshold = 80.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 310.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Sustained Heal: Touch of Moonlight (< 80% HP)
            {
                AbilityDescriptor d;
                d.name = "Touch of Moonlight";
                d.rootSpellId = 574328;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.internalThrottleMs = 2500;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 5. Stance / Aspect
            {
                AbilityDescriptor d;
                d.name = "Aspect of the Stars";
                d.rootSpellId = 800510;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800510;
                d.internalThrottleMs = 20000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 6. Offensive Weaving
            {
                AbilityDescriptor d;
                d.name = "Shooting Star";
                d.rootSpellId = 800505;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (ctx.lowestAllyHpPct > 80.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Moonwell Splash";
                d.rootSpellId = 800370;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 140.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (ctx.lowestAllyHpPct > 85.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // SpecStrategy for Moon Priest
            SpecStrategy s;
            s.classId = 26;
            s.specId = 43;
            s.role = BotRole::Healer;
            s.strategyName = "Starcaller_MoonPriest_Healer_Strategy";
            s.requiredState = RequiredCombatState{ 800510, 800510, {} };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::DirectHeal, 1.5f, 40.0f },
                { AbilityTag::Shield,     1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::EmergencyHeal, 2.0f, 80.0f },
                { AbilityTag::DirectHeal,    1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEHeal,    2.0f, 60.0f },
                { AbilityTag::DirectHeal, 1.2f, 20.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 40) &&
                       (bot->GetHealthPct() >= 50.0f);
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 3: Starcaller - Spec 44: Sentinel (RANGED ASTRAL BOW DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 26;
            p.specId = 44; // Sentinel
            p.role = BotRole::Dps;
            p.profileName = "Starcaller_Sentinel_Dps";

            // 1. Emergency Defense: Constellation Barrier (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Constellation Barrier";
                d.rootSpellId = 300259;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.missingAuraOnCaster = 300259;
                d.internalThrottleMs = 20000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // 2. Aspect: Aspect of the Huntress
            {
                AbilityDescriptor d;
                d.name = "Aspect of the Huntress";
                d.rootSpellId = 805356;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 805356;
                d.internalThrottleMs = 20000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 3. Ranged Attacks & Shots
            {
                AbilityDescriptor d;
                d.name = "Moon Arrow";
                d.rootSpellId = 801972;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 20000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Huntress Shot";
                d.rootSpellId = 680220;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Lunar Lance";
                d.rootSpellId = 801132;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Moonwell Splash";
                d.rootSpellId = 800370;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 4. Astral Builder: Shooting Star
            {
                AbilityDescriptor d;
                d.name = "Shooting Star";
                d.rootSpellId = 800505;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // SpecStrategy for Sentinel
            SpecStrategy s;
            s.classId = 26;
            s.specId = 44;
            s.role = BotRole::Dps;
            s.strategyName = "Starcaller_Sentinel_Dps_Strategy";
            s.requiredState = RequiredCombatState{ 805356, 805356, {} };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::RangedAttack, 1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 2.0f, 60.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 25) &&
                       (bot->GetHealthPct() >= 50.0f);
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 4: Starcaller - Spec 45: Warden (MELEE UMBRAL MOONBLADE DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 26;
            p.specId = 45; // Warden
            p.role = BotRole::Dps;
            p.profileName = "Starcaller_Warden_Dps";

            // 1. Emergency Defense: Constellation Barrier (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Constellation Barrier";
                d.rootSpellId = 300259;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.missingAuraOnCaster = 300259;
                d.internalThrottleMs = 20000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // 2. Aspect: Aspect of the Warden
            {
                AbilityDescriptor d;
                d.name = "Aspect of the Warden";
                d.rootSpellId = 801128;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 801128;
                d.internalThrottleMs = 20000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 3. Heavy Strikes
            {
                AbilityDescriptor d;
                d.name = "Warden's Blade (Multi-Strike)";
                d.rootSpellId = 805508;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 20000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Celestial Strike";
                d.rootSpellId = 800496;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Starsunder";
                d.rootSpellId = 801127;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Starsweep";
                d.rootSpellId = 805550;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Starshatter";
                d.rootSpellId = 801135;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 4. Filler: Shooting Star
            {
                AbilityDescriptor d;
                d.name = "Shooting Star";
                d.rootSpellId = 800505;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // SpecStrategy for Warden
            SpecStrategy s;
            s.classId = 26;
            s.specId = 45;
            s.role = BotRole::Dps;
            s.strategyName = "Starcaller_Warden_Dps_Strategy";
            s.requiredState = RequiredCombatState{ 801128, 801128, {} };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::MeleeAttack, 1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f },
                { AbilityTag::MeleeAttack, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,   1.8f, 50.0f },
                { AbilityTag::MeleeAttack, 1.2f, 20.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 25) &&
                       (bot->GetHealthPct() >= 50.0f);
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
