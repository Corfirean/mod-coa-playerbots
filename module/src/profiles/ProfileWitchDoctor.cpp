/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Witch Doctor Profiles & Strategies implementation
 * Supports:
 *   - Spec 6: Brewing (Cauldron & Alchemy Healer)
 *   - Spec 4: Shadowhunting (Spirits & Wards Ranged DPS)
 *   - Spec 5: Voodoo (Puppets & Hexes DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileWitchDoctor.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "engine/SpecStrategyRegistry.h"
#include "Player.h"

namespace BotAI
{
    static void RegisterWitchDoctorStrategies()
    {
        // -------------------------------------------------------------
        // Strategy 1: Witch Doctor - Spec 6: Brewing (Healer)
        // -------------------------------------------------------------
        {
            SpecStrategy s;
            s.classId = 13; // Witch Doctor
            s.specId = 6;   // Brewing
            s.role = BotRole::Healer;

            // Pre-pull readiness requires healthy mana for alchemy
            s.isReadyToPull = [](Player* /*bot*/, CombatContext const& ctx) -> bool
            {
                return ctx.botPowerPct >= 50.0f && ctx.botHpPct >= 65.0f;
            };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::TotemOrWard, 1.8f, 40.0f },
                { AbilityTag::PeriodicHeal, 1.4f, 25.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::EmergencyHeal, 2.2f, 70.0f },
                { AbilityTag::DirectHeal, 1.5f, 35.0f },
                { AbilityTag::Shield, 1.3f, 20.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::TotemOrWard, 1.8f, 40.0f },
                { AbilityTag::PeriodicHeal, 1.5f, 30.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Strategy 2: Witch Doctor - Spec 4: Shadowhunting (DPS)
        // -------------------------------------------------------------
        {
            SpecStrategy s;
            s.classId = 13; // Witch Doctor
            s.specId = 4;   // Shadowhunting
            s.role = BotRole::Dps;

            // Shadow Avatar is temporary burst, not mandatory baseline form
            s.isReadyToPull = [](Player* /*bot*/, CombatContext const& ctx) -> bool
            {
                return ctx.botPowerPct >= 35.0f && ctx.botHpPct >= 60.0f;
            };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f },
                { AbilityTag::PeriodicDamage, 1.6f, 35.0f },
                { AbilityTag::TotemOrWard, 1.4f, 25.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 2.2f, 70.0f },
                { AbilityTag::RangedAttack, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 2.0f, 50.0f },
                { AbilityTag::OffensiveCD, 1.4f, 25.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::RangedAttack, 1.5f, 35.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Strategy 3: Witch Doctor - Spec 5: Voodoo (DPS)
        // -------------------------------------------------------------
        {
            SpecStrategy s;
            s.classId = 13; // Witch Doctor
            s.specId = 5;   // Voodoo (Puppeteer)
            s.role = BotRole::Dps;

            s.isReadyToPull = [](Player* /*bot*/, CombatContext const& ctx) -> bool
            {
                return ctx.botPowerPct >= 35.0f && ctx.botHpPct >= 60.0f;
            };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::PeriodicDamage, 1.8f, 45.0f },
                { AbilityTag::OffensiveCD, 1.6f, 35.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 2.2f, 65.0f },
                { AbilityTag::RangedAttack, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 2.0f, 55.0f },
                { AbilityTag::PeriodicDamage, 1.4f, 25.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::RangedAttack, 1.5f, 35.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }

    void RegisterWitchDoctorProfiles()
    {
        RegisterWitchDoctorStrategies();

        // -------------------------------------------------------------
        // Profile 1: Witch Doctor Healer (Spec 6: Brewing)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 13;
            p.specId = 6; // Brewing Spec
            p.role = BotRole::Healer;
            p.profileName = "WitchDoctor_Brewing_Healer";

            // 1. Emergency Heals (target < 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Loa's Brew (Emergency)";
                d.rootSpellId = 801670;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 40.0f;
                d.internalThrottleMs = 4000;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spirit in a Bottle (Emergency)";
                d.rootSpellId = 801696;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 40.0f;
                d.internalThrottleMs = 5000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reclaim Soul (Emergency)";
                d.rootSpellId = 801796;
                d.tags = AbilityTag::EmergencyHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 35.0f;
                d.internalThrottleMs = 15000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 2. Direct Heals / Triage (target < 75% HP)
            {
                AbilityDescriptor d;
                d.name = "Loa's Brew (Triage)";
                d.rootSpellId = 801670;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 75.0f;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spirit in a Bottle (Triage)";
                d.rootSpellId = 801696;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 75.0f;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 3. Totem / Ward Support & Potions
            {
                AbilityDescriptor d;
                d.name = "Healing Ward";
                d.rootSpellId = 500957;
                d.tags = AbilityTag::TotemOrWard | AbilityTag::PeriodicHeal;
                d.targetType = TargetType::Self;
                d.trackedEntityType = TrackedEntityType::Ward;
                d.trackedEntityEntry = 50104;
                d.maxTargetHpPct = 85.0f;
                d.internalThrottleMs = 25000;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Splash Potion";
                d.rootSpellId = 802710;
                d.tags = AbilityTag::DirectHeal | AbilityTag::AoEDamage;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.internalThrottleMs = 8000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            // 4. Cauldron Ingredient Pre-Setup (Finite transaction: apply once if missing)
            {
                AbilityDescriptor d;
                d.name = "Shrooms (Cauldron Base)";
                d.rootSpellId = 801660;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 801660;
                d.internalThrottleMs = 60000;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            // 5. Maintenance / Top-off (target < 92% HP)
            {
                AbilityDescriptor d;
                d.name = "Loa's Brew (Maintenance)";
                d.rootSpellId = 801670;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 92.0f;
                d.baseScore = 100.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spirit in a Bottle (Maintenance)";
                d.rootSpellId = 801696;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 92.0f;
                d.baseScore = 85.0f;
                p.abilities.push_back(d);
            }

            // 6. Offensive contribution when party is safe (lowest ally > 80% HP)
            {
                AbilityDescriptor d;
                d.name = "Hex of Malice";
                d.rootSpellId = 801693;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 50.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return (ctx.lowestAllyHpPct > 80.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Shadowflare";
                d.rootSpellId = 801669;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 25.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return (ctx.lowestAllyHpPct > 80.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Witch Doctor - Spec 4: Shadowhunting (RANGED DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 13;
            p.specId = 4; // Shadowhunting
            p.role = BotRole::Dps;
            p.profileName = "WitchDoctor_Shadowhunting_Dps";

            // 1. Major Self Buffs & Burst (Shadow Avatar is burst, not permanent form)
            {
                AbilityDescriptor d;
                d.name = "Shadow Avatar";
                d.rootSpellId = 705943;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 705943;
                d.internalThrottleMs = 60000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 2. Guardians & Major Summons
            {
                AbilityDescriptor d;
                d.name = "Big Voodoo";
                d.rootSpellId = 802719;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 45000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "War Golem";
                d.rootSpellId = 800330;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 45000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 3. Wards & Totems (Do not endlessly recast active wards)
            {
                AbilityDescriptor d;
                d.name = "Healing Ward (Self Defense)";
                d.rootSpellId = 500957;
                d.tags = AbilityTag::TotemOrWard | AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.trackedEntityType = TrackedEntityType::Ward;
                d.trackedEntityEntry = 50104;
                d.maxSelfHpPct = 50.0f;
                d.internalThrottleMs = 25000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Serpent Ward";
                d.rootSpellId = 500960;
                d.tags = AbilityTag::TotemOrWard;
                d.targetType = TargetType::CurrentTarget;
                d.trackedEntityType = TrackedEntityType::Ward;
                d.trackedEntityEntry = 50105;
                d.internalThrottleMs = 25000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 4. DoTs & Curses
            {
                AbilityDescriptor d;
                d.name = "Hex of Malice";
                d.rootSpellId = 801693;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Shrinking Jinx";
                d.rootSpellId = 806285;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 8000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 5. Heavy Offensive Nukes
            {
                AbilityDescriptor d;
                d.name = "Malefic Wrath";
                d.rootSpellId = 807037;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 3000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Mojo Beam";
                d.rootSpellId = 500950;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Potion Toss";
                d.rootSpellId = 801661;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            // 6. Spammer / Filler
            {
                AbilityDescriptor d;
                d.name = "Shadowflare";
                d.rootSpellId = 801669;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 60.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Witch Doctor - Spec 5: Voodoo (PUPPETEER DPS)
        // Truly independent from Shadowhunting
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 13;
            p.specId = 5; // Voodoo (Puppeteer)
            p.role = BotRole::Dps;
            p.profileName = "WitchDoctor_Voodoo_Dps";

            // 1. Major Burst Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Dark Incantation";
                d.rootSpellId = 525377;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 45000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bad Juju";
                d.rootSpellId = 802087;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Glaive of the Gods";
                d.rootSpellId = 806289;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 2. Self Defense
            {
                AbilityDescriptor d;
                d.name = "Healing Ward (Self Defense)";
                d.rootSpellId = 500957;
                d.tags = AbilityTag::TotemOrWard | AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.trackedEntityType = TrackedEntityType::Ward;
                d.trackedEntityEntry = 50104;
                d.maxSelfHpPct = 50.0f;
                d.internalThrottleMs = 25000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 3. Voodoo Curses / Strings / Threads
            {
                AbilityDescriptor d;
                d.name = "Puppet Strings";
                d.rootSpellId = 705910;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 8000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Severing Threads";
                d.rootSpellId = 572836;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Hex of Malice";
                d.rootSpellId = 801693;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            // 4. Active Strikes & Attacks
            {
                AbilityDescriptor d;
                d.name = "Puppeteer's Grasp";
                d.rootSpellId = 707209;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Dancing Puppets";
                d.rootSpellId = 500015;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Eclipse";
                d.rootSpellId = 801607;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            // 5. Spammer / Filler
            {
                AbilityDescriptor d;
                d.name = "Shadowflare";
                d.rootSpellId = 801669;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 60.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }
    }
}
