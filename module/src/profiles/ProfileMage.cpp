/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Mage (Class 8)
 */

#include "profiles/ProfileMage.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterMageProfiles()
    {
        for (uint32 specId : { 1u, 2u, 3u })
        {
            CombatProfile p;
            p.classId = 8;
            p.specId = specId;
            p.role = BotRole::Dps;
            p.profileName = specId == 1 ? "Mage_Arcane_Dps" : specId == 2 ? "Mage_Fire_Dps" : "Mage_Frost_Dps";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Ice Barrier (< 60% HP)
            {
                AbilityDescriptor d;
                d.name = "Ice Barrier";
                d.rootSpellId = 11426;
                d.tags = AbilityTag::Shield | AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 60.0f;
                d.missingAuraOnCaster = 11426;
                d.internalThrottleMs = 30000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Blink (Escape melee range)
            {
                AbilityDescriptor d;
                d.name = "Blink";
                d.rootSpellId = 1953;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 15000;
                d.baseScore = 320.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (ctx.victim && ctx.bot->GetDistance(ctx.victim) < 6.0f)
                        return 80.0f;
                    return -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Frost Nova (Root adds in melee)
            {
                AbilityDescriptor d;
                d.name = "Frost Nova";
                d.rootSpellId = 122;
                d.tags = AbilityTag::CrowdControl | AbilityTag::AoEDamage;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 20000;
                d.baseScore = 310.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (ctx.victim && ctx.bot->GetDistance(ctx.victim) < 8.0f)
                        return 50.0f;
                    return -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Pyroblast
            {
                AbilityDescriptor d;
                d.name = "Pyroblast";
                d.rootSpellId = 11366;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 360.0f;
                p.abilities.push_back(d);
            }

            // Fire Blast
            {
                AbilityDescriptor d;
                d.name = "Fire Blast";
                d.rootSpellId = 2136;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 290.0f;
                p.abilities.push_back(d);
            }

            // Blizzard (AoE)
            {
                AbilityDescriptor d;
                d.name = "Blizzard";
                d.rootSpellId = 10;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 3;
                d.baseScore = 330.0f;
                p.abilities.push_back(d);
            }

            // Arcane Blast
            {
                AbilityDescriptor d;
                d.name = "Arcane Blast";
                d.rootSpellId = 30451;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // Arcane Missiles
            {
                AbilityDescriptor d;
                d.name = "Arcane Missiles";
                d.rootSpellId = 5143;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // Frostbolt (Primary or fallback)
            {
                AbilityDescriptor d;
                d.name = "Frostbolt";
                d.rootSpellId = 116;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // Fireball (Filler)
            {
                AbilityDescriptor d;
                d.name = "Fireball";
                d.rootSpellId = 133;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
