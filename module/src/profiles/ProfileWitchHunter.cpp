/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Witch Hunter Profiles implementation
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

namespace BotAI
{
    void RegisterWitchHunterProfiles()
    {
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
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 2. Heavy Channel / Anti-Magic: Witchbane
            {
                AbilityDescriptor d;
                d.name = "Witchbane (Devastating Volley)";
                d.rootSpellId = 800165;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
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

            // 4. Mobility / Escape: Vault
            {
                AbilityDescriptor d;
                d.name = "Vault (Mobility)";
                d.rootSpellId = 500085;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 5. Melee Fallback: Saber Slash
            {
                AbilityDescriptor d;
                d.name = "Saber Slash (Melee Fallback)";
                d.rootSpellId = 982349;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
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

            // 1. Pet Summon: Houndmaster's Whistle
            {
                AbilityDescriptor d;
                d.name = "Houndmaster's Whistle (Summon Hound)";
                d.rootSpellId = 801343;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 801343;
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
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 3. Burst Cooldown: Repeater
            {
                AbilityDescriptor d;
                d.name = "Repeater (Barrage)";
                d.rootSpellId = 805903;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 4. Heavy Shot: Witchbane
            {
                AbilityDescriptor d;
                d.name = "Witchbane";
                d.rootSpellId = 800165;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
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
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 7. Melee Fallback: Saber Slash
            {
                AbilityDescriptor d;
                d.name = "Saber Slash";
                d.rootSpellId = 982349;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
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

            // 1. Blade Stance
            {
                AbilityDescriptor d;
                d.name = "Blade Stance (Buff)";
                d.rootSpellId = 802002;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 802002;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 2. Heavy Anti-Magic Burst: Witchbane
            {
                AbilityDescriptor d;
                d.name = "Witchbane (Burst)";
                d.rootSpellId = 800165;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 3. Crossbow Barrage: Repeater
            {
                AbilityDescriptor d;
                d.name = "Repeater";
                d.rootSpellId = 805903;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 4. Melee Strike / Bleed: Saber Slash
            {
                AbilityDescriptor d;
                d.name = "Saber Slash (Strike)";
                d.rootSpellId = 982349;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 5. Ranged Builder: Coiling Shot
            {
                AbilityDescriptor d;
                d.name = "Coiling Shot";
                d.rootSpellId = 500082;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 6. Mobility: Vault
            {
                AbilityDescriptor d;
                d.name = "Vault";
                d.rootSpellId = 500085;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.baseScore = 170.0f;
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

            // 1. Taunt & Dark Shield: Gaze of the Black Knight
            {
                AbilityDescriptor d;
                d.name = "Gaze of the Black Knight (Shield/Taunt)";
                d.rootSpellId = 802138;
                d.tags = AbilityTag::Taunt | AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // 2. Standard Taunt
            {
                AbilityDescriptor d;
                d.name = "Taunt";
                d.rootSpellId = 355;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 450.0f;
                p.abilities.push_back(d);
            }

            // 3. Stance / Defense: Blade Stance
            {
                AbilityDescriptor d;
                d.name = "Blade Stance (Parry/Threat)";
                d.rootSpellId = 802002;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 802002;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Melee Strike: Saber Slash
            {
                AbilityDescriptor d;
                d.name = "Saber Slash (Threat Strike)";
                d.rootSpellId = 982349;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 5. Burst Strike: Witchbane
            {
                AbilityDescriptor d;
                d.name = "Witchbane (Burst Threat)";
                d.rootSpellId = 800165;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 6. Ranged Pull: Coiling Shot
            {
                AbilityDescriptor d;
                d.name = "Coiling Shot (Pull)";
                d.rootSpellId = 500082;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            // 7. Gap Closer / Reposition: Vault
            {
                AbilityDescriptor d;
                d.name = "Vault";
                d.rootSpellId = 500085;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

