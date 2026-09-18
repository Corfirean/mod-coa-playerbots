/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Pyromancer Profiles implementation
 * Supports:
 *   - Spec 37: Flameweaving (Fire & Cauterize Healer)
 *   - Spec 38: Incineration (Pure Fire Caster DPS)
 *   - Spec 39: Draconic (Dragon Scales / Hybrid Fire DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfilePyromancer.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterPyromancerProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Pyromancer - Spec 37: Flameweaving (FIRE HEALER)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 24; // Pyromancer
            p.specId = 37; // Flameweaving
            p.role = BotRole::Healer;
            p.profileName = "Pyromancer_Flameweaving_Healer";

            // 1. Emergency Critical Heal: Phoenix Blessing (< 35% HP)
            {
                AbilityDescriptor d;
                d.name = "Phoenix Blessing";
                d.rootSpellId = 800196;
                d.tags = AbilityTag::DirectHeal | AbilityTag::DefensiveCD;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 35.0f;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // 2. Shield / Barrier: Inferno Barrier (< 55% HP)
            {
                AbilityDescriptor d;
                d.name = "Inferno Barrier";
                d.rootSpellId = 504380;
                d.tags = AbilityTag::Shield;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 55.0f;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // 3. AoE Heal: Circle of Fire (>= 2 injured allies)
            {
                AbilityDescriptor d;
                d.name = "Circle of Fire";
                d.rootSpellId = 800807;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::Self;
                d.minInjuredAllies = 2;
                d.injuredAllyHpPctThreshold = 80.0f;
                d.baseScore = 310.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Direct Heal: Blessing of the Firelands (< 80% HP)
            {
                AbilityDescriptor d;
                d.name = "Blessing of the Firelands";
                d.rootSpellId = 504397;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 5. Cauterize HoT: Ember Touch (< 90% HP)
            {
                AbilityDescriptor d;
                d.name = "Ember Touch (Cauterize HoT)";
                d.rootSpellId = 800818;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 6. Offensive Weaving
            {
                AbilityDescriptor d;
                d.name = "Ignite (Fire DoT)";
                d.rootSpellId = 800791;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Scorching Ray";
                d.rootSpellId = 800790;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Pyromancer - Spec 38: Incineration (PURE FIRE CASTER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 24;
            p.specId = 38; // Incineration
            p.role = BotRole::Dps;
            p.profileName = "Pyromancer_Incineration_Dps";

            // 1. Emergency Defense: Inferno Barrier (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Inferno Barrier";
                d.rootSpellId = 504380;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // 2. Buff: Flamecasting
            {
                AbilityDescriptor d;
                d.name = "Flamecasting";
                d.rootSpellId = 804300;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 804300;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 3. Major Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Infernus";
                d.rootSpellId = 92124;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Cataclysm";
                d.rootSpellId = 520218;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Meteor";
                d.rootSpellId = 500135;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 4. Fire DoT: Ignite (Always Maintain)
            {
                AbilityDescriptor d;
                d.name = "Ignite";
                d.rootSpellId = 800791;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 5. Heavy Spenders / Nukes
            {
                AbilityDescriptor d;
                d.name = "Molten Storm";
                d.rootSpellId = 805483;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Slagstone";
                d.rootSpellId = 803950;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Overheat";
                d.rootSpellId = 800408;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 6. Filler: Scorching Ray
            {
                AbilityDescriptor d;
                d.name = "Scorching Ray";
                d.rootSpellId = 800790;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Pyromancer - Spec 39: Draconic (DRAGON HYBRID DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 24;
            p.specId = 39; // Draconic
            p.role = BotRole::Dps;
            p.profileName = "Pyromancer_Draconic_Dps";

            // 1. Stance / Form: Dragonscales
            {
                AbilityDescriptor d;
                d.name = "Dragonscales";
                d.rootSpellId = 524818;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 524818;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 2. Gap Closer: Dragon Leap (> 8 yards)
            {
                AbilityDescriptor d;
                d.name = "Dragon Leap";
                d.rootSpellId = 806611;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 100.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 3. Heavy Strikes
            {
                AbilityDescriptor d;
                d.name = "Dragon's Edge";
                d.rootSpellId = 300755;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Ignis Ultimatus";
                d.rootSpellId = 680369;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 4. DoTs & Spenders
            {
                AbilityDescriptor d;
                d.name = "Ignite";
                d.rootSpellId = 800791;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Slagstone";
                d.rootSpellId = 803950;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 5. Filler: Scorching Ray
            {
                AbilityDescriptor d;
                d.name = "Scorching Ray";
                d.rootSpellId = 800790;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

