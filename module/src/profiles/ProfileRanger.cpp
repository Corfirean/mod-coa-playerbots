/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Ranger Profiles implementation
 * Supports:
 *   - Spec 29: Farstrider (Support / Ranged DPS) - Guild bot Morenur (GUID 2512)
 *   - Spec 28: Archery (Pure Marksman Ranged DPS)
 *   - Spec 30: Brigand (Melee Agility DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileRanger.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"

namespace BotAI
{
    void RegisterRangerProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Ranger - Spec 29: Farstrider (SUPPORT / RANGED DPS)
        // Main support/ranged spec used by guild bot Morenur (GUID 2512).
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 21;
            p.specId = 29; // Farstrider
            p.role = BotRole::Support;
            p.profileName = "Ranger_Farstrider_Support";

            // 1. Emergency Defensive: Elude (root 801345) (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Elude (Defensive)";
                d.rootSpellId = 801345;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // 2. Crowd Control: Blackjack (root 520568) & Dust Toss (root 807820)
            {
                AbilityDescriptor d;
                d.name = "Blackjack (Stun)";
                d.rootSpellId = 520568;
                d.tags = AbilityTag::CrowdControl | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Dust Toss (Blind/Disorient)";
                d.rootSpellId = 807820;
                d.tags = AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
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
                d.missingAuraOnCaster = 800086;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Horn of Endurance (Party Buff)";
                d.rootSpellId = 806359;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 806359;
                d.baseScore = 175.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Horn of Alacrity (Party Buff)";
                d.rootSpellId = 806360;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 806360;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Command Aura";
                d.rootSpellId = 524600;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 524600;
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
                d.missingAuraOnCaster = 800260;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 5. Burst / Offensive Cooldown: Falconstrike (root 806345)
            {
                AbilityDescriptor d;
                d.name = "Falconstrike (Burst)";
                d.rootSpellId = 806345;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 6. Execute: Skullpiercer (root 802036) (< 35% target HP)
            {
                AbilityDescriptor d;
                d.name = "Skullpiercer (Execute)";
                d.rootSpellId = 802036;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.maxTargetHpPct = 35.0f;
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
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Battle Screech (AoE Debuff)";
                d.rootSpellId = 705070;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
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

            // 1. Defensive: Elude (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Elude (Defensive)";
                d.rootSpellId = 801345;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
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
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Falconstrike (Burst)";
                d.rootSpellId = 806345;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
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
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Aimed Shot (Heavy Shot)";
                d.rootSpellId = 19434;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 195.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Serrated Shot";
                d.rootSpellId = 500073;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 185.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Concussive Shot (Snare)";
                d.rootSpellId = 5116;
                d.tags = AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
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
        }

        // -------------------------------------------------------------
        // Profile 3: Ranger - Spec 30: Brigand (MELEE AGILITY DPS)
        // Close quarters hybrid fighter utilizing dual strikes, bleeds, and stuns.
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 21;
            p.specId = 30; // Brigand
            p.role = BotRole::Dps;
            p.profileName = "Ranger_Brigand_Melee";

            // 1. Defensive: Elude (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Elude (Defensive)";
                d.rootSpellId = 801345;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
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
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Dust Toss (Blind)";
                d.rootSpellId = 807820;
                d.tags = AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
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
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Battle Screech (AoE Debuff)";
                d.rootSpellId = 705070;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
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
        }

    }
}

