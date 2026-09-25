/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Runemaster Profiles implementation
 * Supports:
 *   - Spec 61: Engravement (Melee Rune Weaving DPS)
 *   - Spec 62: Glyphic (Ranged Runic Caster DPS)
 *   - Spec 63: Riftblade (Melee Mobility DPS)
 */

#include "profiles/ProfileRunemaster.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "engine/SpecStrategyRegistry.h"
#include "Player.h"

namespace BotAI
{
    void RegisterRunemasterProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Runemaster - Spec 61: Engravement (MELEE DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 32;
            p.specId = 61; // Engravement
            p.role = BotRole::Dps;
            p.profileName = "Runemaster_Engravement_MeleeDps";

            // 1. Personal Defense: Runeshroud (< 50% HP)
            {
                AbilityDescriptor d;
                d.name = "Runeshroud (Shield)";
                d.rootSpellId = 500288;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.missingAuraOnCaster = 500288;
                d.internalThrottleMs = 15000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 2. Major Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Zenith (Burst)";
                d.rootSpellId = 712325;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fist of the Ancients";
                d.rootSpellId = 712326;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 3. Melee Strikes & Brands
            {
                AbilityDescriptor d;
                d.name = "Runic Brand";
                d.rootSpellId = 712299;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Runeblade";
                d.rootSpellId = 707141;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Palm Sigil: Arcane";
                d.rootSpellId = 805380;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fracture";
                d.rootSpellId = 803018;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 135.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Elemental Burst";
                d.rootSpellId = 802202;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 4. DoT Maintenance
            {
                AbilityDescriptor d;
                d.name = "Hoarfrost";
                d.rootSpellId = 801104;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 110.0f;
                p.abilities.push_back(d);
            }

            // 5. Ranged Gap Filler
            {
                AbilityDescriptor d;
                d.name = "Frigid Blast";
                d.rootSpellId = 500118;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 50.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);

            // SpecStrategy for Engravement
            SpecStrategy s;
            s.classId = 32;
            s.specId = 61;
            s.role = BotRole::Dps;
            s.strategyName = "Runemaster_Engravement_MeleeDps_Strategy";

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

            // ResourcePolicy for Arcane Sigil (500282)
            {
                ResourcePolicy pol;
                pol.key = CombatResourceKey{ CombatResourceKind::AuraStack, 0, 500282 };
                pol.overcapThreshold = 3;
                s.resourcePolicies.push_back(pol);
            }

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 2: Runemaster - Spec 62: Glyphic (RANGED CASTER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 32;
            p.specId = 62; // Glyphic
            p.role = BotRole::Dps;
            p.profileName = "Runemaster_Glyphic_RangedDps";

            // 1. Personal Defense
            {
                AbilityDescriptor d;
                d.name = "Runeshroud (Shield)";
                d.rootSpellId = 500288;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.missingAuraOnCaster = 500288;
                d.internalThrottleMs = 15000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 2. Major Burst Cooldown
            {
                AbilityDescriptor d;
                d.name = "Zenith (Burst)";
                d.rootSpellId = 712325;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 3. DoT
            {
                AbilityDescriptor d;
                d.name = "Hoarfrost";
                d.rootSpellId = 801104;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 4. Heavy Ranged Nukes
            {
                AbilityDescriptor d;
                d.name = "Glyphic Ruin";
                d.rootSpellId = 801179;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Thaumaturgy";
                d.rootSpellId = 804550;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Primordial Blast";
                d.rootSpellId = 800732;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            // 5. Ranged Spammer / Builder
            {
                AbilityDescriptor d;
                d.name = "Frigid Blast";
                d.rootSpellId = 500118;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 70.0f;
                p.abilities.push_back(d);
            }

            // 6. Close-Range Defense
            {
                AbilityDescriptor d;
                d.name = "Fist of the Ancients";
                d.rootSpellId = 712326;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 60.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);

            // SpecStrategy for Glyphic
            SpecStrategy s;
            s.classId = 32;
            s.specId = 62;
            s.role = BotRole::Dps;
            s.strategyName = "Runemaster_Glyphic_RangedDps_Strategy";

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::PeriodicDamage, 1.5f, 40.0f },
                { AbilityTag::RangedAttack,   1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 1.8f, 50.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 30) &&
                       (bot->GetHealthPct() >= 50.0f);
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 3: Runemaster - Spec 63: Riftblade (MELEE MOBILITY DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 32;
            p.specId = 63; // Riftblade
            p.role = BotRole::Dps;
            p.profileName = "Runemaster_Riftblade_MeleeDps";

            // 1. Defense
            {
                AbilityDescriptor d;
                d.name = "Runeshroud";
                d.rootSpellId = 500288;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.missingAuraOnCaster = 500288;
                d.internalThrottleMs = 15000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 2. Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Zenith";
                d.rootSpellId = 712325;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fist of the Ancients";
                d.rootSpellId = 712326;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 3. Fast Blade Strikes
            {
                AbilityDescriptor d;
                d.name = "Runeblade";
                d.rootSpellId = 707141;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fracture";
                d.rootSpellId = 803018;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Runic Brand";
                d.rootSpellId = 712299;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Elemental Burst";
                d.rootSpellId = 802202;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 4. DoT & Ranged Filler
            {
                AbilityDescriptor d;
                d.name = "Hoarfrost";
                d.rootSpellId = 801104;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 100.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Frigid Blast";
                d.rootSpellId = 500118;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 50.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);

            // SpecStrategy for Riftblade
            SpecStrategy s;
            s.classId = 32;
            s.specId = 63;
            s.role = BotRole::Dps;
            s.strategyName = "Runemaster_Riftblade_MeleeDps_Strategy";

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
