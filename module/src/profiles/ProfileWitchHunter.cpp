/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Witch Hunter Profiles & Strategies implementation
 * Supports:
 *   - Spec 10: Boltslinger (Dual Crossbow Ranged DPS)
 *   - Spec 11: Houndmaster (Shadowhound / Beast Ranged DPS)
 *   - Spec 12: Inquisition (Holy Fire / Witchbane Hybrid DPS)
 *   - Spec 97: Black Knight (Dark Plate Melee Tank)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileWitchHunter.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "engine/SpecStrategyRegistry.h"
#include "Player.h"

namespace BotAI
{
    static void RegisterWitchHunterStrategies()
    {
        // -------------------------------------------------------------
        // Strategy 1: Witch Hunter - Spec 10: Boltslinger (DPS)
        // -------------------------------------------------------------
        {
            SpecStrategy s;
            s.classId = 15; // Witch Hunter
            s.specId = 10;  // Boltslinger
            s.role = BotRole::Dps;

            s.isReadyToPull = [](Player* /*bot*/, CombatContext const& ctx) -> bool
            {
                return ctx.botPowerPct >= 30.0f && ctx.botHpPct >= 60.0f;
            };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 2.2f, 70.0f },
                { AbilityTag::RangedAttack, 1.5f, 35.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::RangedAttack, 1.6f, 40.0f },
                { AbilityTag::OffensiveCD, 1.4f, 25.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::RangedAttack, 1.5f, 35.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Strategy 2: Witch Hunter - Spec 11: Houndmaster (DPS)
        // -------------------------------------------------------------
        {
            SpecStrategy s;
            s.classId = 15; // Witch Hunter
            s.specId = 11;  // Houndmaster
            s.role = BotRole::Dps;

            // Readiness requires pet/hound summoned
            s.isPetReady = [](Player* bot) -> bool
            {
                return bot->HasAura(801343) || bot->GetPet() != nullptr;
            };

            s.isReadyToPull = [](Player* bot, CombatContext const& ctx) -> bool
            {
                if (!bot->HasAura(801343) && !bot->GetPet())
                    return false;
                return ctx.botPowerPct >= 30.0f && ctx.botHpPct >= 60.0f;
            };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::Buff, 1.8f, 45.0f },
                { AbilityTag::OffensiveCD, 1.6f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 2.2f, 70.0f },
                { AbilityTag::RangedAttack, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::RangedAttack, 1.5f, 35.0f },
                { AbilityTag::OffensiveCD, 1.3f, 20.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::OffensiveCD, 1.6f, 40.0f },
                { AbilityTag::RangedAttack, 1.4f, 30.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Strategy 3: Witch Hunter - Spec 12: Inquisition (DPS)
        // -------------------------------------------------------------
        {
            SpecStrategy s;
            s.classId = 15; // Witch Hunter
            s.specId = 12;  // Inquisition
            s.role = BotRole::Dps;

            s.isReadyToPull = [](Player* /*bot*/, CombatContext const& ctx) -> bool
            {
                return ctx.botPowerPct >= 30.0f && ctx.botHpPct >= 60.0f;
            };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.4f, 30.0f },
                { AbilityTag::MeleeAttack, 1.2f, 20.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 2.2f, 70.0f },
                { AbilityTag::RangedAttack, 1.5f, 35.0f },
                { AbilityTag::MeleeAttack, 1.3f, 25.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::RangedAttack, 1.6f, 40.0f },
                { AbilityTag::MeleeAttack, 1.3f, 25.0f }
            };
            s.phaseModifiers[CombatPhase::Execute] = {
                { AbilityTag::MeleeAttack, 1.5f, 35.0f },
                { AbilityTag::RangedAttack, 1.4f, 30.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Strategy 4: Witch Hunter - Spec 97: Black Knight (Tank)
        // -------------------------------------------------------------
        {
            SpecStrategy s;
            s.classId = 15; // Witch Hunter
            s.specId = 97;  // Black Knight
            s.role = BotRole::Tank;

            // Blade Stance (spell 802002, aura 802002) is mandatory tank stance
            s.requiredState = RequiredCombatState{ 802002, 802002, {} };

            // Readiness requires Blade Stance active
            s.isReadyToPull = [](Player* bot, CombatContext const& ctx) -> bool
            {
                if (!bot->HasAura(802002))
                    return false;
                return ctx.botHpPct >= 70.0f;
            };

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::Taunt, 2.0f, 60.0f },
                { AbilityTag::MeleeAttack, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::DefensiveCD, 1.8f, 50.0f },
                { AbilityTag::MeleeAttack, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::MeleeAttack, 1.6f, 40.0f },
                { AbilityTag::Taunt, 1.5f, 30.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }

    void RegisterWitchHunterProfiles()
    {
        RegisterWitchHunterStrategies();

        // -------------------------------------------------------------
        // Profile 1: Witch Hunter - Spec 10: Boltslinger (DUAL CROSSBOW RANGED DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 15;
            p.specId = 10; // Boltslinger
            p.role = BotRole::Dps;
            p.profileName = "WitchHunter_Boltslinger_Ranged";

            // 1. Burst Cooldown: Repeater (Rapid Crossbow Barrage)
            {
                AbilityDescriptor d;
                d.name = "Repeater (Crossbow Barrage)";
                d.rootSpellId = 805903;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 2. Heavy Channel / Anti-Magic: Witchbane
            {
                AbilityDescriptor d;
                d.name = "Witchbane (Devastating Volley)";
                d.rootSpellId = 800165;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 3. Primary Shot Builder: Coiling Shot
            {
                AbilityDescriptor d;
                d.name = "Coiling Shot (Builder)";
                d.rootSpellId = 500082;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            // 4. Mobility / Escape: Vault (< 50% HP)
            {
                AbilityDescriptor d;
                d.name = "Vault (Mobility)";
                d.rootSpellId = 500085;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.internalThrottleMs = 20000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 5. Melee Fallback: Saber Slash (Low base score so ranged shots are preferred)
            {
                AbilityDescriptor d;
                d.name = "Saber Slash (Melee Fallback)";
                d.rootSpellId = 982349;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 80.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Witch Hunter - Spec 11: Houndmaster (PET RANGED DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 15;
            p.specId = 11; // Houndmaster
            p.role = BotRole::Dps;
            p.profileName = "WitchHunter_Houndmaster_Ranged";

            // 1. Pet Summon: Houndmaster's Whistle (Summon once if missing)
            {
                AbilityDescriptor d;
                d.name = "Houndmaster's Whistle (Summon Hound)";
                d.rootSpellId = 801343;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 801343;
                d.trackedEntityType = TrackedEntityType::Pet;
                d.internalThrottleMs = 30000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 2. Pet Burst Attack: Houndmaster's Call
            {
                AbilityDescriptor d;
                d.name = "Houndmaster's Call (Pet Attack)";
                d.rootSpellId = 802273;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 3. Burst Cooldown: Repeater
            {
                AbilityDescriptor d;
                d.name = "Repeater (Barrage)";
                d.rootSpellId = 805903;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 4. Heavy Shot: Witchbane
            {
                AbilityDescriptor d;
                d.name = "Witchbane";
                d.rootSpellId = 800165;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 5. Primary Shot Builder: Coiling Shot
            {
                AbilityDescriptor d;
                d.name = "Coiling Shot";
                d.rootSpellId = 500082;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            // 6. Mobility: Vault
            {
                AbilityDescriptor d;
                d.name = "Vault";
                d.rootSpellId = 500085;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.internalThrottleMs = 20000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 7. Melee Fallback: Saber Slash
            {
                AbilityDescriptor d;
                d.name = "Saber Slash";
                d.rootSpellId = 982349;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 80.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Witch Hunter - Spec 12: Inquisition (HYBRID CASTER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 15;
            p.specId = 12; // Inquisition
            p.role = BotRole::Dps;
            p.profileName = "WitchHunter_Inquisition_Hybrid";

            // 1. Heavy Anti-Magic Burst: Witchbane
            {
                AbilityDescriptor d;
                d.name = "Witchbane (Burst)";
                d.rootSpellId = 800165;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 2. Crossbow Barrage: Repeater
            {
                AbilityDescriptor d;
                d.name = "Repeater";
                d.rootSpellId = 805903;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 3. Melee Strike: Saber Slash
            {
                AbilityDescriptor d;
                d.name = "Saber Slash (Strike)";
                d.rootSpellId = 982349;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 2500;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 4. Ranged Builder: Coiling Shot
            {
                AbilityDescriptor d;
                d.name = "Coiling Shot";
                d.rootSpellId = 500082;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 5. Mobility: Vault
            {
                AbilityDescriptor d;
                d.name = "Vault";
                d.rootSpellId = 500085;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.internalThrottleMs = 20000;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 4: Witch Hunter - Spec 97: Black Knight (DARK TANK)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 15;
            p.specId = 97; // Black Knight
            p.role = BotRole::Tank;
            p.profileName = "WitchHunter_BlackKnight_Tank";

            // 1. Primary Taunts
            {
                AbilityDescriptor d;
                d.name = "Taunt";
                d.rootSpellId = 355;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 450.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return ctx.victimTargetingNonTank ? 100.0f : 0.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Gaze of the Black Knight (Shield/Taunt)";
                d.rootSpellId = 802138;
                d.tags = AbilityTag::Taunt | AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 15000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // 2. Stance / Defense: Blade Stance (Do not spam if already present)
            {
                AbilityDescriptor d;
                d.name = "Blade Stance (Parry/Threat)";
                d.rootSpellId = 802002;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 802002;
                d.internalThrottleMs = 30000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 3. Primary Melee Strike: Saber Slash
            {
                AbilityDescriptor d;
                d.name = "Saber Slash (Threat Strike)";
                d.rootSpellId = 982349;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 2500;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 4. Burst Strike: Witchbane
            {
                AbilityDescriptor d;
                d.name = "Witchbane (Burst Threat)";
                d.rootSpellId = 800165;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 5. Ranged Pull: Coiling Shot
            {
                AbilityDescriptor d;
                d.name = "Coiling Shot (Pull)";
                d.rootSpellId = 500082;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            // 6. Gap Closer / Reposition: Vault (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Vault";
                d.rootSpellId = 500085;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.internalThrottleMs = 20000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }
    }
}
