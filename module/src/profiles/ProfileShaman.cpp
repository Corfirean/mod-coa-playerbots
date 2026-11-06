/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Shaman (Class 7)
 */

#include "profiles/ProfileShaman.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterShamanProfiles()
    {
        // =========================================================================
        // 1. SPEC 4: SPIRIT GUARD (SHAMAN TANK - REBORN)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 7;
            p.specId = 4;
            p.role = BotRole::Tank;
            p.profileName = "Shaman_SpiritGuard_Tank";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Rockbiter Weapon Imbue
            {
                AbilityDescriptor d;
                d.name = "Rockbiter Weapon";
                d.rootSpellId = 8017;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 60000;
                d.baseScore = 480.0f;
                p.abilities.push_back(d);
            }

            // Earth Shock (Taunt & High Threat)
            {
                AbilityDescriptor d;
                d.name = "Earth Shock";
                d.rootSpellId = 8045;
                d.tags = AbilityTag::Taunt | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 420.0f;
                p.abilities.push_back(d);
            }

            // Lightning Shield
            {
                AbilityDescriptor d;
                d.name = "Lightning Shield";
                d.rootSpellId = 324;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 324;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // Stormstrike
            {
                AbilityDescriptor d;
                d.name = "Stormstrike";
                d.rootSpellId = 17364;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Lava Lash
            {
                AbilityDescriptor d;
                d.name = "Lava Lash";
                d.rootSpellId = 60103;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Flame Shock
            {
                AbilityDescriptor d;
                d.name = "Flame Shock";
                d.rootSpellId = 8042;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 2. SPEC 3: RESTORATION (HEALER)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 7;
            p.specId = 3;
            p.role = BotRole::Healer;
            p.profileName = "Shaman_Restoration_Healer";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Earth Shield on Tank
            {
                AbilityDescriptor d;
                d.name = "Earth Shield";
                d.rootSpellId = 974;
                d.tags = AbilityTag::Buff | AbilityTag::PeriodicHeal;
                d.targetType = TargetType::TankAlly;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 420.0f;
                p.abilities.push_back(d);
            }

            // Riptide (< 85% HP)
            {
                AbilityDescriptor d;
                d.name = "Riptide";
                d.rootSpellId = 61295;
                d.tags = AbilityTag::PeriodicHeal | AbilityTag::EmergencyHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 85.0f;
                d.internalThrottleMs = 6000;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // Lesser Healing Wave (< 70% HP)
            {
                AbilityDescriptor d;
                d.name = "Lesser Healing Wave";
                d.rootSpellId = 8004;
                d.tags = AbilityTag::DirectHeal | AbilityTag::EmergencyHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 70.0f;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Healing Wave (< 50% HP)
            {
                AbilityDescriptor d;
                d.name = "Healing Wave";
                d.rootSpellId = 331;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 50.0f;
                d.baseScore = 360.0f;
                p.abilities.push_back(d);
            }

            // Chain Heal (Group)
            {
                AbilityDescriptor d;
                d.name = "Chain Heal";
                d.rootSpellId = 1064;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minInjuredAllies = 2;
                d.injuredAllyHpPctThreshold = 80.0f;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 3. SPEC 1: ELEMENTAL (DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 7;
            p.specId = 1;
            p.role = BotRole::Dps;
            p.profileName = "Shaman_Elemental_Dps";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Flame Shock (DoT)
            {
                AbilityDescriptor d;
                d.name = "Flame Shock";
                d.rootSpellId = 8042;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Lava Burst
            {
                AbilityDescriptor d;
                d.name = "Lava Burst";
                d.rootSpellId = 51505;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 360.0f;
                p.abilities.push_back(d);
            }

            // Chain Lightning
            {
                AbilityDescriptor d;
                d.name = "Chain Lightning";
                d.rootSpellId = 421;
                d.tags = AbilityTag::AoEDamage | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 2;
                d.internalThrottleMs = 6000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Lightning Bolt (Filler)
            {
                AbilityDescriptor d;
                d.name = "Lightning Bolt";
                d.rootSpellId = 403;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 4. SPEC 2: ENHANCEMENT (DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 7;
            p.specId = 2;
            p.role = BotRole::Dps;
            p.profileName = "Shaman_Enhancement_Dps";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Stormstrike
            {
                AbilityDescriptor d;
                d.name = "Stormstrike";
                d.rootSpellId = 17364;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Lava Lash
            {
                AbilityDescriptor d;
                d.name = "Lava Lash";
                d.rootSpellId = 60103;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Earth Shock
            {
                AbilityDescriptor d;
                d.name = "Earth Shock";
                d.rootSpellId = 8045;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Lightning Shield
            {
                AbilityDescriptor d;
                d.name = "Lightning Shield";
                d.rootSpellId = 324;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 324;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
