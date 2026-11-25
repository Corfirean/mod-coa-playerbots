/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Shaman (Class 7)
 */

#include "profiles/ProfileShaman.h"
#include "profiles/ProfileRegistry.h"
#include "profiles/ProfileHelpers.h"
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

            // The weapon imbue (Rockbiter) is applied by BotAI's weapon-imbue upkeep: it needs an item target, which a
            // profile entry cannot give.
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
                d.rootSpellId = 8050;   // 8042 is Earth Shock rank 1: this descriptor never cast Flame Shock
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // Wind Shear
            {
                AbilityDescriptor d;
                d.name = "Wind Shear";
                d.rootSpellId = 57994;
                d.tags = AbilityTag::Interrupt | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 400.0f;
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

            // Water Shield
            {
                AbilityDescriptor d;
                d.name = "Water Shield";
                d.rootSpellId = 52127;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 345.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return HasAuraOfAny(ctx.bot, { 52127, 324 }) ? -1.0f : 0.0f;  // shields replace one another
                };
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
                d.rootSpellId = 8050;   // 8042 is Earth Shock rank 1: this descriptor never cast Flame Shock
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

            // Wind Shear
            {
                AbilityDescriptor d;
                d.name = "Wind Shear";
                d.rootSpellId = 57994;
                d.tags = AbilityTag::Interrupt | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Elemental Mastery
            {
                AbilityDescriptor d;
                d.name = "Elemental Mastery";
                d.rootSpellId = 16166;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 330.0f;
                d.missingAuraOnCaster = 16166;
                p.abilities.push_back(d);
            }

            // Bloodlust
            {
                AbilityDescriptor d;
                d.name = "Bloodlust";
                d.rootSpellId = 2825;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 320.0f;
                d.missingAuraOnCaster = 2825;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (ctx.bot->HasAura(57724) || ctx.bot->HasAura(57723)) ? -1.0f : 0.0f;  // Sated / Exhaustion
                };
                p.abilities.push_back(d);
            }

            // Heroism
            {
                AbilityDescriptor d;
                d.name = "Heroism";
                d.rootSpellId = 32182;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 320.0f;
                d.missingAuraOnCaster = 32182;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (ctx.bot->HasAura(57724) || ctx.bot->HasAura(57723)) ? -1.0f : 0.0f;  // Sated / Exhaustion
                };
                p.abilities.push_back(d);
            }

            // Water Shield
            {
                AbilityDescriptor d;
                d.name = "Water Shield";
                d.rootSpellId = 52127;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 340.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return HasAuraOfAny(ctx.bot, { 52127, 324 }) ? -1.0f : 0.0f;  // shields replace one another
                };
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

            // Wind Shear
            {
                AbilityDescriptor d;
                d.name = "Wind Shear";
                d.rootSpellId = 57994;
                d.tags = AbilityTag::Interrupt | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Feral Spirit
            {
                AbilityDescriptor d;
                d.name = "Feral Spirit";
                d.rootSpellId = 51533;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Shamanistic Rage
            {
                AbilityDescriptor d;
                d.name = "Shamanistic Rage";
                d.rootSpellId = 30823;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 350.0f;
                d.missingAuraOnCaster = 30823;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (ctx.botPowerPct < 40.0f || ctx.botHpPct < 50.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Bloodlust
            {
                AbilityDescriptor d;
                d.name = "Bloodlust";
                d.rootSpellId = 2825;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 320.0f;
                d.missingAuraOnCaster = 2825;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (ctx.bot->HasAura(57724) || ctx.bot->HasAura(57723)) ? -1.0f : 0.0f;  // Sated / Exhaustion
                };
                p.abilities.push_back(d);
            }

            // Heroism
            {
                AbilityDescriptor d;
                d.name = "Heroism";
                d.rootSpellId = 32182;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 320.0f;
                d.missingAuraOnCaster = 32182;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (ctx.bot->HasAura(57724) || ctx.bot->HasAura(57723)) ? -1.0f : 0.0f;  // Sated / Exhaustion
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
