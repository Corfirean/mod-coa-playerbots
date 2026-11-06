/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Rogue (Class 4)
 */

#include "profiles/ProfileRogue.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterRogueProfiles()
    {
        for (uint32 specId : { 1u, 2u, 3u })
        {
            CombatProfile p;
            p.classId = 4;
            p.specId = specId;
            p.role = BotRole::Dps;
            p.profileName = specId == 1 ? "Rogue_Assassination_Dps" : specId == 2 ? "Rogue_Combat_Dps" : "Rogue_Subtlety_Dps";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Evasion (Defensive < 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Evasion";
                d.rootSpellId = 5277;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.missingAuraOnCaster = 5277;
                d.internalThrottleMs = 60000;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // Kick (Interrupt)
            {
                AbilityDescriptor d;
                d.name = "Kick";
                d.rootSpellId = 1766;
                d.tags = AbilityTag::Interrupt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 400.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (ctx.victim && ctx.victim->IsNonMeleeSpellCast(false, false, true))
                        return 100.0f;
                    return -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Slice and Dice (Buff upkeep with combo points)
            {
                AbilityDescriptor d;
                d.name = "Slice and Dice";
                d.rootSpellId = 5171;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 5171;
                d.baseScore = 310.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetComboPoints() >= 2 ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Eviscerate (Finisher at 4-5 CP)
            {
                AbilityDescriptor d;
                d.name = "Eviscerate";
                d.rootSpellId = 2098;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 340.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetComboPoints() >= 4 ? 50.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Rupture (Finisher DoT)
            {
                AbilityDescriptor d;
                d.name = "Rupture";
                d.rootSpellId = 1943;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.bot->GetComboPoints() >= 4 ? 30.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Sinister Strike / Mutilate (Builder)
            {
                AbilityDescriptor d;
                d.name = "Sinister Strike";
                d.rootSpellId = 1752;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Mutilate
            {
                AbilityDescriptor d;
                d.name = "Mutilate";
                d.rootSpellId = 1329;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
