/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Chronomancer Profiles implementation
 * Supports:
 *   - Spec 31: Time (Temporal Healer / Chrono Shielder)
 *   - Spec 32: Infinite (Arcane / Temporal Caster DPS)
 *   - Spec 33: Artificer (Clockwork / Arc Gadget DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileChronomancer.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterChronomancerProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Chronomancer - Spec 31: Time (TEMPORAL HEALER)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 22; // Chronomancer
            p.specId = 31; // Time
            p.role = BotRole::Healer;
            p.profileName = "Chronomancer_Time_Healer";

            // 1. Emergency Rewind: Do Over / Rewind (< 30% HP)
            {
                AbilityDescriptor d;
                d.name = "Do Over (Emergency Rewind)";
                d.rootSpellId = 800669;
                d.tags = AbilityTag::DirectHeal | AbilityTag::DefensiveCD;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 30.0f;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Rewind (Temporal Recovery)";
                d.rootSpellId = 801294;
                d.tags = AbilityTag::DirectHeal | AbilityTag::DefensiveCD;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 35.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // 2. Temporal Shield: Infinite Shield (< 55% HP)
            {
                AbilityDescriptor d;
                d.name = "Infinite Shield";
                d.rootSpellId = 520457;
                d.tags = AbilityTag::Shield;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 55.0f;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // 3. AoE Heal: Waves of Time (>= 2 injured allies)
            {
                AbilityDescriptor d;
                d.name = "Waves of Time (AoE Time Heal)";
                d.rootSpellId = 801277;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::Self;
                d.minInjuredAllies = 2;
                d.injuredAllyHpPctThreshold = 80.0f;
                d.baseScore = 310.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Direct Heal: Reverse Wound (< 80% HP)
            {
                AbilityDescriptor d;
                d.name = "Reverse Wound";
                d.rootSpellId = 801303;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 5. Temporal HoT: Accelerated Recovery (< 90% HP)
            {
                AbilityDescriptor d;
                d.name = "Accelerated Recovery (Time HoT)";
                d.rootSpellId = 800857;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 6. Utility Buff: Fortify Timeline
            {
                AbilityDescriptor d;
                d.name = "Fortify Timeline";
                d.rootSpellId = 804491;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 804491;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 7. Offensive Weaving
            {
                AbilityDescriptor d;
                d.name = "Decomposition (Time Decay DoT)";
                d.rootSpellId = 800856;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Sandblast (Temporal Filler)";
                d.rootSpellId = 804464;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Chronomancer - Spec 32: Infinite (ARCANE / TEMPORAL CASTER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 22;
            p.specId = 32; // Infinite
            p.role = BotRole::Dps;
            p.profileName = "Chronomancer_Infinite_Dps";

            // 1. Emergency Defense: Rewind / Infinite Shield
            {
                AbilityDescriptor d;
                d.name = "Rewind (Self Recovery)";
                d.rootSpellId = 801294;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 30.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Infinite Shield";
                d.rootSpellId = 520457;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 2. Major Cooldown: Infinite Power / Temporal Anomaly
            {
                AbilityDescriptor d;
                d.name = "Infinite Power";
                d.rootSpellId = 92118;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Temporal Anomaly";
                d.rootSpellId = 806315;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 3. DoTs
            {
                AbilityDescriptor d;
                d.name = "Melt Reality (Temporal DoT)";
                d.rootSpellId = 806335;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Decomposition (Entropy Decay)";
                d.rootSpellId = 800856;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // 4. Heavy Spenders / Spells
            {
                AbilityDescriptor d;
                d.name = "Gravity Bomb";
                d.rootSpellId = 801281;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Chromatic Shard";
                d.rootSpellId = 801292;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 5. Filler
            {
                AbilityDescriptor d;
                d.name = "Sandblast";
                d.rootSpellId = 804464;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Chronomancer - Spec 33: Artificer (CLOCKWORK GADGET DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 22;
            p.specId = 33; // Artificer
            p.role = BotRole::Dps;
            p.profileName = "Chronomancer_Artificer_Dps";

            // 1. Emergency Defense
            {
                AbilityDescriptor d;
                d.name = "Infinite Shield";
                d.rootSpellId = 520457;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 2. Artificer Weapon Attacks: Shatter Echo & Arc Collision
            {
                AbilityDescriptor d;
                d.name = "Shatter Echo (Weapon Time Strike)";
                d.rootSpellId = 804503;
                d.tags = AbilityTag::RangedAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Arc Collision (Electrical Time DoT)";
                d.rootSpellId = 524853;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 3. DoTs
            {
                AbilityDescriptor d;
                d.name = "Melt Reality";
                d.rootSpellId = 806335;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Decomposition";
                d.rootSpellId = 800856;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 4. Spenders & Fillers
            {
                AbilityDescriptor d;
                d.name = "Chromatic Shard";
                d.rootSpellId = 801292;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Sandblast";
                d.rootSpellId = 804464;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

