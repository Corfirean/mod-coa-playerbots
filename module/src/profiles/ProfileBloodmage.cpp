/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Bloodmage Profiles implementation
 * Supports:
 *   - Spec 99: Eternal (Blood Protection Tank)
 *   - Spec 25: Fleshweaver (Blood & Vitality Healer / Support)
 *   - Spec 26: Sanguine (Ranged Blood Caster DPS)
 *   - Spec 27: Accursed (Melee Ferocity / Werewolf DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileBloodmage.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterBloodmageProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Bloodmage - Spec 99: Eternal (BLOOD TANK)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 20; // Son of Arugal / Bloodmage
            p.specId = 99; // Eternal
            p.role = BotRole::Tank;
            p.profileName = "Bloodmage_Eternal_Tank";

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

            // 2. Emergency Survival: Liquify (< 25% HP immunity)
            {
                AbilityDescriptor d;
                d.name = "Liquify (Emergency Immunity)";
                d.rootSpellId = 806310;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 25.0f;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // 3. Active Mitigation: Bloody Sacrifice (< 45% HP)
            {
                AbilityDescriptor d;
                d.name = "Bloody Sacrifice";
                d.rootSpellId = 705742;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.minSelfHpPct = 15.0f; // Prevent suicide
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 4. Blood Shield: Fleshcraft (< 65% HP)
            {
                AbilityDescriptor d;
                d.name = "Fleshcraft (Shield)";
                d.rootSpellId = 801952;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 5. Stance / Auras: Aortic Aegis / Blood Veil
            {
                AbilityDescriptor d;
                d.name = "Aortic Aegis";
                d.rootSpellId = 806274;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 806274;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Blood Veil";
                d.rootSpellId = 504263;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 504263;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 6. Threat / Leech DoTs
            {
                AbilityDescriptor d;
                d.name = "Vampyr's Kiss (Leech Threat)";
                d.rootSpellId = 504275;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Crimson Tide (AoE Blood Wave)";
                d.rootSpellId = 504282;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 7. Heavy Threat Spenders
            {
                AbilityDescriptor d;
                d.name = "Heartbreak";
                d.rootSpellId = 520314;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minSelfHpPct = 25.0f;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Veinburst";
                d.rootSpellId = 504260;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minSelfHpPct = 20.0f;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Vampiric Fang";
                d.rootSpellId = 804726;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 8. Builders / Self-Heal Strike
            {
                AbilityDescriptor d;
                d.name = "Ravenous Bite";
                d.rootSpellId = 500123;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bloodbolt";
                d.rootSpellId = 804685;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Bloodmage - Spec 25: Fleshweaver (HEALER / SUPPORT)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 20;
            p.specId = 25; // Fleshweaver
            p.role = BotRole::Healer;
            p.profileName = "Bloodmage_Fleshweaver_Healer";

            // 1. Emergency Critical Heal: Transfusion (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Transfusion (Critical Direct Heal)";
                d.rootSpellId = 705734;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 40.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // 2. Emergency Shield: Fleshcraft (< 50% HP)
            {
                AbilityDescriptor d;
                d.name = "Fleshcraft (Protective Shield)";
                d.rootSpellId = 801952;
                d.tags = AbilityTag::Shield;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 50.0f;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // 3. AoE Blood Wave Heal: Waves of Blood (>= 2 injured allies)
            {
                AbilityDescriptor d;
                d.name = "Waves of Blood (Group Heal)";
                d.rootSpellId = 681427;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::Self;
                d.minInjuredAllies = 2;
                d.injuredAllyHpPctThreshold = 80.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Sustained Heal: Transfusion / Sanguine Mend (< 80% HP)
            {
                AbilityDescriptor d;
                d.name = "Sanguine Mend (Primary Heal)";
                d.rootSpellId = 802310;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 5. Blood Redistribution (< 85% HP)
            {
                AbilityDescriptor d;
                d.name = "Blood Redistribution";
                d.rootSpellId = 706256;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 85.0f;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 6. Support Buff: Universal Donor / Blood Pact
            {
                AbilityDescriptor d;
                d.name = "Universal Donor";
                d.rootSpellId = 806428;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 806428;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 7. Offensive Vitality Weaving (generates vitality for healing)
            {
                AbilityDescriptor d;
                d.name = "Vampyr's Kiss (Leech Vitality)";
                d.rootSpellId = 504275;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Crimson Tide";
                d.rootSpellId = 504282;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bloodbolt (Vitality Builder)";
                d.rootSpellId = 804685;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Bloodmage - Spec 26: Sanguine (RANGED BLOOD CASTER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 20;
            p.specId = 26; // Sanguine
            p.role = BotRole::Dps;
            p.profileName = "Bloodmage_Sanguine_Dps";

            // 1. Emergency Defense: Liquify (< 20% HP)
            {
                AbilityDescriptor d;
                d.name = "Liquify";
                d.rootSpellId = 806310;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 20.0f;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fleshcraft (< 35% HP)";
                d.rootSpellId = 801952;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 2. Major Cooldown: Apotheosis / Blood Moon
            {
                AbilityDescriptor d;
                d.name = "Apotheosis";
                d.rootSpellId = 804195;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Blood Moon";
                d.rootSpellId = 804199;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // 3. Core Blood DoTs (Always Maintain)
            {
                AbilityDescriptor d;
                d.name = "Crimson Tide (Primary DoT)";
                d.rootSpellId = 504282;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Vampyr's Kiss (Leech DoT)";
                d.rootSpellId = 504275;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 4. Heavy Spenders / Finishers
            {
                AbilityDescriptor d;
                d.name = "Heartbreak (Execute Spender)";
                d.rootSpellId = 520314;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.minSelfHpPct = 25.0f;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Veinburst (Burst Spender)";
                d.rootSpellId = 504260;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minSelfHpPct = 20.0f;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Hemoburst";
                d.rootSpellId = 572855;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.minSelfHpPct = 20.0f;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 5. Summon / Pet: Animated Blood
            {
                AbilityDescriptor d;
                d.name = "Animated Blood";
                d.rootSpellId = 573299;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 6. Fillers
            {
                AbilityDescriptor d;
                d.name = "Bloodbolt";
                d.rootSpellId = 804685;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bloodmoon Blast";
                d.rootSpellId = 500125;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 4: Bloodmage - Spec 27: Accursed (MELEE FEROCITY / WEREWOLF DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 20;
            p.specId = 27; // Accursed
            p.role = BotRole::Dps;
            p.profileName = "Bloodmage_Accursed_Dps";

            // 1. Maintain Accursed / Werewolf Form
            {
                AbilityDescriptor d;
                d.name = "Accursed Form";
                d.rootSpellId = 562572;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 562572;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 2. Gap Closer: Bloodleaper (> 8 yards)
            {
                AbilityDescriptor d;
                d.name = "Bloodleaper";
                d.rootSpellId = 806423;
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

            // 3. Major Offensive Howls
            {
                AbilityDescriptor d;
                d.name = "Night Hunter's Howl";
                d.rootSpellId = 500124;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Wicked Howl";
                d.rootSpellId = 804207;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 4. Primary Execute Strike: Reave
            {
                AbilityDescriptor d;
                d.name = "Reave (Execute Bleed)";
                d.rootSpellId = 800490;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::Execute;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 5. Claw Attacks
            {
                AbilityDescriptor d;
                d.name = "Rotclaw";
                d.rootSpellId = 804197;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Vampiric Fang";
                d.rootSpellId = 804726;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Ravenous Bite";
                d.rootSpellId = 500123;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 6. Blood Shards (Filler / Spender)
            {
                AbilityDescriptor d;
                d.name = "Blood Shards";
                d.rootSpellId = 804849;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

