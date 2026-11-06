/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Hunter (Class 3)
 */

#include "profiles/ProfileHunter.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterHunterProfiles()
    {
        // Hunter specs (Beast Mastery: 1, Marksmanship: 2, Survival: 3)
        for (uint32 specId : { 1u, 2u, 3u })
        {
            CombatProfile p;
            p.classId = 3;
            p.specId = specId;
            p.role = BotRole::Dps;
            p.profileName = specId == 1 ? "Hunter_BM_Dps" : specId == 2 ? "Hunter_MM_Dps" : "Hunter_Survival_Dps";
            p.useRangedAutoRepeat = true;
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Hunter's Mark
            {
                AbilityDescriptor d;
                d.name = "Hunter's Mark";
                d.rootSpellId = 1130;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Kill Shot (< 20% HP finisher)
            {
                AbilityDescriptor d;
                d.name = "Kill Shot";
                d.rootSpellId = 53351;
                d.tags = AbilityTag::Execute | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.maxTargetHpPct = 20.0f;
                d.baseScore = 420.0f;
                p.abilities.push_back(d);
            }

            // Serpent Sting (DoT)
            {
                AbilityDescriptor d;
                d.name = "Serpent Sting";
                d.rootSpellId = 1978;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // Aimed Shot / Chimera Shot
            {
                AbilityDescriptor d;
                d.name = "Aimed Shot";
                d.rootSpellId = 19434;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 10000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Arcane Shot
            {
                AbilityDescriptor d;
                d.name = "Arcane Shot";
                d.rootSpellId = 3044;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Multi-Shot (AoE)
            {
                AbilityDescriptor d;
                d.name = "Multi-Shot";
                d.rootSpellId = 2643;
                d.tags = AbilityTag::AoEDamage | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 2;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // Steady Shot (Filler)
            {
                AbilityDescriptor d;
                d.name = "Steady Shot";
                d.rootSpellId = 34120;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            // Concussive Shot (Slow)
            {
                AbilityDescriptor d;
                d.name = "Concussive Shot";
                d.rootSpellId = 5116;
                d.tags = AbilityTag::CrowdControl | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
