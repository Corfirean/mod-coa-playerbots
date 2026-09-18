/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Necromancer Profiles implementation
 * Supports:
 *   - Spec 34: Death (Shadow / Disease Caster DPS)
 *   - Spec 35: Animation (Minion Swarm / Pet Master DPS)
 *   - Spec 36: Rime (Frost / Chill Caster DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileNecromancer.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterNecromancerProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Necromancer - Spec 34: Death (SHADOW / DISEASE DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 23; // Necromancer
            p.specId = 34; // Death
            p.role = BotRole::Dps;
            p.profileName = "Necromancer_Death_Dps";

            // 1. Emergency Defense: Sacrifice Undead (< 35% HP)
            {
                AbilityDescriptor d;
                d.name = "Sacrifice Undead";
                d.rootSpellId = 805027;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // 2. Armor Buff: Lich Armor
            {
                AbilityDescriptor d;
                d.name = "Lich Armor";
                d.rootSpellId = 800199;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800199;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 3. Major Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Crypt Plague";
                d.rootSpellId = 92121;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Ner'zhul's Blessing";
                d.rootSpellId = 704729;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Diseases / DoTs (Always Maintain)
            {
                AbilityDescriptor d;
                d.name = "Putrefy (Primary Disease)";
                d.rootSpellId = 804558;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Flesh to Worms (Affliction DoT)";
                d.rootSpellId = 500338;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Tears of Lordaeron";
                d.rootSpellId = 705752;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 5. Heavy Spenders / Nukes
            {
                AbilityDescriptor d;
                d.name = "Death's Due";
                d.rootSpellId = 807796;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bone Tithe";
                d.rootSpellId = 802121;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Mass Grave";
                d.rootSpellId = 803741;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 6. Primary Direct Nuke: Lichfrost
            {
                AbilityDescriptor d;
                d.name = "Lichfrost";
                d.rootSpellId = 501969;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Necromancer - Spec 35: Animation (MINION SWARM DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 23;
            p.specId = 35; // Animation
            p.role = BotRole::Dps;
            p.profileName = "Necromancer_Animation_Dps";

            // 1. Emergency Defense: Sacrifice Undead (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Sacrifice Undead";
                d.rootSpellId = 805027;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // 2. Armor Buff: Lich Armor
            {
                AbilityDescriptor d;
                d.name = "Lich Armor";
                d.rootSpellId = 800199;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800199;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 3. Minion Summons
            {
                AbilityDescriptor d;
                d.name = "Raise: Decaying Colossus";
                d.rootSpellId = 500989;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Raise: Abomination";
                d.rootSpellId = 500335;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Raise: Skeletal Archer";
                d.rootSpellId = 500332;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Raise: Brittle Skeleton";
                d.rootSpellId = 500970;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 4. Pet Bidding / Commands
            {
                AbilityDescriptor d;
                d.name = "Foul Bidding: Slaughter";
                d.rootSpellId = 500982;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Unholy Command";
                d.rootSpellId = 802123;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 5. Direct Damage & DoTs
            {
                AbilityDescriptor d;
                d.name = "Putrefy";
                d.rootSpellId = 804558;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Flesh to Worms";
                d.rootSpellId = 500338;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Lichfrost";
                d.rootSpellId = 501969;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Necromancer - Spec 36: Rime (FROST / CHILL DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 23;
            p.specId = 36; // Rime
            p.role = BotRole::Dps;
            p.profileName = "Necromancer_Rime_Dps";

            // 1. Emergency Defense: Sacrifice Undead (< 35% HP)
            {
                AbilityDescriptor d;
                d.name = "Sacrifice Undead";
                d.rootSpellId = 805027;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // 2. Forms & Buffs
            {
                AbilityDescriptor d;
                d.name = "Lich Form";
                d.rootSpellId = 500981;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 500981;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Lich Armor";
                d.rootSpellId = 800199;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800199;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 3. Major Frost Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Animate: Frost Wyrm";
                d.rootSpellId = 805428;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Tundra Warriors";
                d.rootSpellId = 92122;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // 4. Frost Burst Nukes
            {
                AbilityDescriptor d;
                d.name = "Ice Barrage";
                d.rootSpellId = 803779;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Glacial Tap";
                d.rootSpellId = 805369;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Winds of Northrend";
                d.rootSpellId = 301333;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 5. Primary DoT: Putrefy
            {
                AbilityDescriptor d;
                d.name = "Putrefy";
                d.rootSpellId = 804558;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 6. Core Frost Nuke: Lichfrost
            {
                AbilityDescriptor d;
                d.name = "Lichfrost";
                d.rootSpellId = 501969;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

