/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Ranger Profiles implementation
 * Supports:
 *   - Spec 29: Farstrider (Support / Ranged DPS)
 *   - Spec 28: Archery (Pure Marksman Ranged DPS)
 *   - Spec 30: Brigand (Melee Agility DPS)
 */

#include "profiles/ProfileRanger.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "engine/SpecStrategyRegistry.h"
#include "Player.h"

namespace BotAI
{
    void RegisterRangerProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Ranger - Spec 29: Farstrider (SUPPORT / RANGED DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 21;
            p.specId = 29; // Farstrider
            p.role = BotRole::Support;
            p.profileName = "Ranger_Farstrider_Support";
            p.useRangedAutoRepeat = true;
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // 1. Emergency Defensive: Elude (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Elude (Defensive)";
                d.rootSpellId = 801345;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.missingAuraOnCaster = 801345;
                d.internalThrottleMs = 15000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // 2. Crowd Control: Blackjack & Dust Toss
            {
                AbilityDescriptor d;
                d.name = "Blackjack (Stun)";
                d.rootSpellId = 520568;
                d.tags = AbilityTag::CrowdControl | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 20000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Dust Toss (Blind/Disorient)";
                d.rootSpellId = 807820;
                d.tags = AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 20000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 3. Horn & Aura Party Buffs: Horn of War, Horn of Endurance, Horn of Alacrity, Command Aura
            {
                AbilityDescriptor d;
                d.name = "Horn of War (Party Buff)";
                d.rootSpellId = 800086;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.casterAuraId = 800086;
                d.missingAuraOnCaster = 800086;
                d.internalThrottleMs = 20000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Horn of Endurance (Party Buff)";
                d.rootSpellId = 806359;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.casterAuraId = 806359;
                d.missingAuraOnCaster = 806359;
                d.internalThrottleMs = 20000;
                d.baseScore = 175.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Horn of Alacrity (Party Buff)";
                d.rootSpellId = 806360;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.casterAuraId = 806360;
                d.missingAuraOnCaster = 806360;
                d.internalThrottleMs = 20000;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Command Aura";
                d.rootSpellId = 524600;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.casterAuraId = 524600;
                d.missingAuraOnCaster = 524600;
                d.internalThrottleMs = 20000;
                d.baseScore = 165.0f;
                p.abilities.push_back(d);
            }

            // 4. Quiver Weapon Imbue
            {
                AbilityDescriptor d;
                d.name = "Poison Quiver";
                d.rootSpellId = 800260;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.casterAuraId = 800260;
                d.missingAuraOnCaster = 800260;
                d.internalThrottleMs = 20000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 5. Burst / Offensive Cooldown: Falconstrike
            {
                AbilityDescriptor d;
                d.name = "Falconstrike (Burst)";
                d.rootSpellId = 806345;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 6. Execute: Skullpiercer (< 35% target HP)
            {
                AbilityDescriptor d;
                d.name = "Skullpiercer (Execute)";
                d.rootSpellId = 802036;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.maxTargetHpPct = 35.0f;
                d.internalThrottleMs = 5000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 7. Ranged Rotational: Toxic Dart (DoT)
            {
                AbilityDescriptor d;
                d.name = "Toxic Dart (DoT)";
                d.rootSpellId = 807237;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 8. Ranged Rotational: Serrated Shot
            {
                AbilityDescriptor d;
                d.name = "Serrated Shot";
                d.rootSpellId = 500073;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 9. Ranged Builder / Filler: Quick Shot
            {
                AbilityDescriptor d;
                d.name = "Quick Shot";
                d.rootSpellId = 500074;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 10. Melee Fallback: Flank, Rusty Shiv, Wild Strike, Battle Screech
            {
                AbilityDescriptor d;
                d.name = "Flank (Melee Strike)";
                d.rootSpellId = 804940;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Rusty Shiv (Melee Debuff)";
                d.rootSpellId = 561315;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Battle Screech (AoE Debuff)";
                d.rootSpellId = 705070;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Wild Strike (Melee Filler)";
                d.rootSpellId = 800083;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // SpecStrategy for Farstrider
            SpecStrategy s;
            s.classId = 21;
            s.specId = 29;
            s.role = BotRole::Support;
            s.strategyName = "Ranger_Farstrider_Support_Strategy";

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

            // ResourcePolicy for Ranger Advantage (804329)
            {
                ResourcePolicy pol;
                pol.key = CombatResourceKey{ CombatResourceKind::AuraStack, 0, 804329 };
                pol.overcapThreshold = 4;
                s.resourcePolicies.push_back(pol);
            }

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 2: Ranger - Spec 28: Archery (PURE MARKSMAN RANGED DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 21;
            p.specId = 28; // Archery
            p.role = BotRole::Dps;
            p.profileName = "Ranger_Archery_Marksman";
            p.useRangedAutoRepeat = true;
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // 1. Defensive: Elude (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Elude (Defensive)";
                d.rootSpellId = 801345;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.missingAuraOnCaster = 801345;
                d.internalThrottleMs = 15000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // 2. Buffs: Command Aura & Searing/Poison Quiver
            {
                AbilityDescriptor d;
                d.name = "Command Aura";
                d.rootSpellId = 524600;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 524600;
                d.internalThrottleMs = 20000;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Searing Quiver";
                d.rootSpellId = 500103;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 500103;
                d.internalThrottleMs = 20000;
                d.baseScore = 165.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Poison Quiver";
                d.rootSpellId = 800260;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800260;
                d.internalThrottleMs = 20000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 3. Cooldowns: Rapid Fire & Falconstrike
            {
                AbilityDescriptor d;
                d.name = "Rapid Fire (Burst)";
                d.rootSpellId = 3045;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Falconstrike (Burst)";
                d.rootSpellId = 806345;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 4. Execute: Skullpiercer (< 35% target HP)
            {
                AbilityDescriptor d;
                d.name = "Skullpiercer (Execute)";
                d.rootSpellId = 802036;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.maxTargetHpPct = 35.0f;
                d.internalThrottleMs = 5000;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // 5. AoE: Multi-Shot
            {
                AbilityDescriptor d;
                d.name = "Multi-Shot (AoE)";
                d.rootSpellId = 2643;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 205.0f;
                p.abilities.push_back(d);
            }

            // 6. Ranged Rotation: Toxic Dart, Aimed Shot, Serrated Shot, Concussive Shot
            {
                AbilityDescriptor d;
                d.name = "Toxic Dart (DoT)";
                d.rootSpellId = 807237;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Aimed Shot (Heavy Shot)";
                d.rootSpellId = 19434;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 195.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Serrated Shot";
                d.rootSpellId = 500073;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 5000;
                d.baseScore = 185.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Concussive Shot (Snare)";
                d.rootSpellId = 5116;
                d.tags = AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Quick Shot (Filler)";
                d.rootSpellId = 500074;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 7. Melee Fallback: Flank & Wild Strike
            {
                AbilityDescriptor d;
                d.name = "Flank";
                d.rootSpellId = 804940;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Wild Strike";
                d.rootSpellId = 800083;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // SpecStrategy for Archery
            SpecStrategy s;
            s.classId = 21;
            s.specId = 28;
            s.role = BotRole::Dps;
            s.strategyName = "Ranger_Archery_Marksman_Strategy";

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::PeriodicDamage, 1.5f, 40.0f },
                { AbilityTag::RangedAttack,   1.3f, 30.0f }
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
        // Profile 3: Ranger - Spec 30: Brigand (MELEE AGILITY DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 21;
            p.specId = 30; // Brigand
            p.role = BotRole::Dps;
            p.profileName = "Ranger_Brigand_Melee";
            p.useRangedAutoRepeat = false;
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // 1. Defensive: Elude (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Elude (Defensive)";
                d.rootSpellId = 801345;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.missingAuraOnCaster = 801345;
                d.internalThrottleMs = 15000;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // 2. Crowd Control: Blackjack & Dust Toss
            {
                AbilityDescriptor d;
                d.name = "Blackjack (Stun)";
                d.rootSpellId = 520568;
                d.tags = AbilityTag::CrowdControl | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 20000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Dust Toss (Blind)";
                d.rootSpellId = 807820;
                d.tags = AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 20000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 3. Self Buffs: Poison Quiver & Command Aura
            {
                AbilityDescriptor d;
                d.name = "Poison Quiver";
                d.rootSpellId = 800260;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800260;
                d.internalThrottleMs = 20000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Command Aura";
                d.rootSpellId = 524600;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 524600;
                d.internalThrottleMs = 20000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            // 4. Execute: Skullpiercer (< 35% target HP)
            {
                AbilityDescriptor d;
                d.name = "Skullpiercer (Execute)";
                d.rootSpellId = 802036;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.maxTargetHpPct = 35.0f;
                d.internalThrottleMs = 5000;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 5. Primary Melee Strike Rotation
            {
                AbilityDescriptor d;
                d.name = "Flank (Primary Strike)";
                d.rootSpellId = 804940;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Rusty Shiv (Debuff Strike)";
                d.rootSpellId = 561315;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Battle Screech (AoE Debuff)";
                d.rootSpellId = 705070;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Wild Strike (Primary Builder)";
                d.rootSpellId = 800083;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 175.0f;
                p.abilities.push_back(d);
            }

            // 6. Ranged Pull / Gap Opener
            {
                AbilityDescriptor d;
                d.name = "Toxic Dart (Opener DoT)";
                d.rootSpellId = 807237;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Quick Shot (Opener Shot)";
                d.rootSpellId = 500074;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            // SpecStrategy for Brigand
            SpecStrategy s;
            s.classId = 21;
            s.specId = 30;
            s.role = BotRole::Dps;
            s.strategyName = "Ranger_Brigand_Melee_Strategy";

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::MeleeAttack,    1.5f, 40.0f },
                { AbilityTag::PeriodicDamage, 1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::MeleeAttack, 1.6f, 50.0f }
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
