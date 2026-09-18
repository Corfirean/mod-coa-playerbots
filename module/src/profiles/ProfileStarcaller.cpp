/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Starcaller Profiles implementation
 * Supports:
 *   - Spec 100: Moon Guard (Astral Protection Tank)
 *   - Spec 43: Moon Priest (Lunar & Tide Healer)
 *   - Spec 44: Sentinel (Ranged Astral Bow DPS)
 *   - Spec 45: Warden (Melee Umbral Moonblade DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileStarcaller.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
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
                d.baseScore = 450.0f;
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
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Celestial Strike (Generate Stars)";
                d.rootSpellId = 800496;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Celestial Cleave (AoE Threat)";
                d.rootSpellId = 801181;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
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
                d.tags = AbilityTag::DirectHeal | AbilityTag::DefensiveCD;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 40.0f;
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
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Moonwell Splash";
                d.rootSpellId = 800370;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
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
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Huntress Shot";
                d.rootSpellId = 680220;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Lunar Lance";
                d.rootSpellId = 801132;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Moonwell Splash";
                d.rootSpellId = 800370;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
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
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Celestial Strike";
                d.rootSpellId = 800496;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Starsunder";
                d.rootSpellId = 801127;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Starsweep";
                d.rootSpellId = 805550;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Starshatter";
                d.rootSpellId = 801135;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
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
        }

    }
}

