/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Sun Cleric Profiles implementation
 */

#include "profiles/ProfileSunCleric.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"

namespace BotAI
{
    void RegisterSunClericProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Sun Cleric - Spec 48: Seraphim (TANK)
        // Main Tank spec used by guild bot Truthethioth (GUID 376).
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 27;
            p.specId = 48; // Seraphim
            p.role = BotRole::Tank;
            p.profileName = "SunCleric_Seraphim_Tank";

            // 1. Taunt: Glare (root 805583) when target attacks a non-tank
            {
                AbilityDescriptor d;
                d.name = "Glare (Taunt)";
                d.rootSpellId = 805583;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 450.0f;
                p.abilities.push_back(d);
            }

            // 2. Emergency Survival: Sol Invictus (root 807732) (< 35% HP)
            {
                AbilityDescriptor d;
                d.name = "Sol Invictus (Defensive)";
                d.rootSpellId = 807732;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // 3. Stun / Crowd Control: Hammer of Kings (root 804751)
            {
                AbilityDescriptor d;
                d.name = "Hammer of Kings (Stun)";
                d.rootSpellId = 804751;
                d.tags = AbilityTag::CrowdControl | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            // 4. Self Buffs: Consecrated Weapons & Solar Conquest
            {
                AbilityDescriptor d;
                d.name = "Consecrated Weapons";
                d.rootSpellId = 704395;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 704395;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Solar Invocation: Conquest";
                d.rootSpellId = 800764;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800764;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            // 5. Emergency Self-Heal: Illumination (< 45% HP)
            {
                AbilityDescriptor d;
                d.name = "Illumination (Self Heal)";
                d.rootSpellId = 500143;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 6. Self HoT Sustain: Revivify (< 75% HP)
            {
                AbilityDescriptor d;
                d.name = "Revivify (Self HoT)";
                d.rootSpellId = 801790;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 75.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            // 7. Primary Melee Weapon Strikes (Hold Threat)
            {
                AbilityDescriptor d;
                d.name = "Gavel of Light";
                d.rootSpellId = 800611;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Dawnbreak";
                d.rootSpellId = 800654;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 115.0f;
                p.abilities.push_back(d);
            }

            // 8. Ranged Pull / Gap Closer
            {
                AbilityDescriptor d;
                d.name = "Chains of Light";
                d.rootSpellId = 801524;
                d.tags = AbilityTag::CrowdControl | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 90.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Sunflare (Pull)";
                d.rootSpellId = 800231;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 50.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // -------------------------------------------------------------
        // Profile 2: Sun Cleric - Spec 98: Blessings (HEALER)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 27;
            p.specId = 98; // Blessings
            p.role = BotRole::Healer;
            p.profileName = "SunCleric_Blessings_Healer";

            // 1. Emergency Direct Heal (< 45% HP)
            {
                AbilityDescriptor d;
                d.name = "Illumination (Emergency)";
                d.rootSpellId = 500143;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 45.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 2. Maintain Revivify HoT on Tank
            {
                AbilityDescriptor d;
                d.name = "Revivify (Tank HoT)";
                d.rootSpellId = 801790;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::TankAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 3. Triage Direct Heal (< 80% HP)
            {
                AbilityDescriptor d;
                d.name = "Illumination (Triage)";
                d.rootSpellId = 500143;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 4. Spread Revivify HoT on Damaged Allies
            {
                AbilityDescriptor d;
                d.name = "Revivify (Party HoT)";
                d.rootSpellId = 801790;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::AnyInjuredAlly;
                d.maxTargetHpPct = 85.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            // 5. Self Buff
            {
                AbilityDescriptor d;
                d.name = "Consecrated Weapons";
                d.rootSpellId = 704395;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 704395;
                d.baseScore = 100.0f;
                p.abilities.push_back(d);
            }

            // 6. Offensive support when party is safe
            {
                AbilityDescriptor d;
                d.name = "Horusath Blast";
                d.rootSpellId = 500154;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 40.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return (ctx.lowestAllyHpPct > 85.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Sunflare";
                d.rootSpellId = 800231;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 20.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return (ctx.lowestAllyHpPct > 85.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // -------------------------------------------------------------
        // Profile 3: Sun Cleric - Spec 47: Valkyrie (MELEE DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 27;
            p.specId = 47; // Valkyrie
            p.role = BotRole::Dps;
            p.profileName = "SunCleric_Valkyrie_MeleeDps";

            // 1. Self Buffs
            {
                AbilityDescriptor d;
                d.name = "Consecrated Weapons";
                d.rootSpellId = 704395;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 704395;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Solar Invocation: Conquest";
                d.rootSpellId = 800764;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800764;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 2. Heavy Stun / Strike
            {
                AbilityDescriptor d;
                d.name = "Hammer of Kings";
                d.rootSpellId = 804751;
                d.tags = AbilityTag::CrowdControl | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 3. Primary Melee Weapon Strikes
            {
                AbilityDescriptor d;
                d.name = "Gavel of Light";
                d.rootSpellId = 800611;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Dawnbreak";
                d.rootSpellId = 800654;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 4. DoT & Burst
            {
                AbilityDescriptor d;
                d.name = "Sunset";
                d.rootSpellId = 804584;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 100.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Horusath Blast";
                d.rootSpellId = 500154;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 80.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Sunflare (Filler)";
                d.rootSpellId = 800231;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 40.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // -------------------------------------------------------------
        // Profile 4: Sun Cleric - Spec 46: Piety (RANGED CASTER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 27;
            p.specId = 46; // Piety
            p.role = BotRole::Dps;
            p.profileName = "SunCleric_Piety_RangedDps";

            // 1. Self Buff
            {
                AbilityDescriptor d;
                d.name = "Consecrated Weapons";
                d.rootSpellId = 704395;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 704395;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 2. Heavy Holy Nukes
            {
                AbilityDescriptor d;
                d.name = "Horusath Blast";
                d.rootSpellId = 500154;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Sunset";
                d.rootSpellId = 804584;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            // 3. Ranged Spammer / Builder
            {
                AbilityDescriptor d;
                d.name = "Sunflare";
                d.rootSpellId = 800231;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 100.0f;
                p.abilities.push_back(d);
            }

            // 4. Melee Emergency Strike
            {
                AbilityDescriptor d;
                d.name = "Gavel of Light";
                d.rootSpellId = 800611;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 60.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
