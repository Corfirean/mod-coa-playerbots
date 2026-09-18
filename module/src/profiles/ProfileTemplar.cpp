/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Templar Profiles implementation
 * Supports:
 *   - Spec 22: Oathkeeper (Holy Protection Shield Tank)
 *   - Spec 23: Zealot (Holy Melee Burst / Single-Target DPS)
 *   - Spec 24: Crusader (Holy Melee AoE / Tempest / Cleave DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileTemplar.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterTemplarProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Templar - Spec 22: Oathkeeper (HOLY SHIELD TANK)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 19;
            p.specId = 22; // Oathkeeper
            p.role = BotRole::Tank;
            p.profileName = "Templar_Oathkeeper_Tank";

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
            {
                AbilityDescriptor d;
                d.name = "Courage (Templar Taunt)";
                d.rootSpellId = 707754;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 440.0f;
                p.abilities.push_back(d);
            }

            // 2. Emergency Survival: Final Prayer (< 30% HP)
            {
                AbilityDescriptor d;
                d.name = "Final Prayer (Last Stand)";
                d.rootSpellId = 705268;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 30.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // 3. Active Mitigation: Tyr's Guard (< 55% HP)
            {
                AbilityDescriptor d;
                d.name = "Tyr's Guard";
                d.rootSpellId = 560649;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 55.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 4. Defensive CD: Martyr (< 45% HP)
            {
                AbilityDescriptor d;
                d.name = "Martyr";
                d.rootSpellId = 300530;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 5. Stance / Aura Buffs
            {
                AbilityDescriptor d;
                d.name = "Tenacious Defender";
                d.rootSpellId = 707385;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 707385;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Libram of Tenacity";
                d.rootSpellId = 801461;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 801461;
                d.baseScore = 205.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Gift of Fervor";
                d.rootSpellId = 572629;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 572629;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 6. Interrupt: Interdict
            {
                AbilityDescriptor d;
                d.name = "Interdict";
                d.rootSpellId = 560116;
                d.tags = AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 350.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return ctx.victimIsCastingInterruptible ? 200.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 7. Gap Closer: Divine Charge (> 8 yards)
            {
                AbilityDescriptor d;
                d.name = "Divine Charge";
                d.rootSpellId = 527023;
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

            // 8. Breakers / Spenders (Oath spenders)
            {
                AbilityDescriptor d;
                d.name = "Chastise (Stun & Threat)";
                d.rootSpellId = 803157;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Righteous Tempest (AoE Threat Spender)";
                d.rootSpellId = 805409;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Blade of Faith (Holy Spender)";
                d.rootSpellId = 803872;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Benediction (Holy Burst)";
                d.rootSpellId = 801448;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 9. Debuff: Crusader's Brand
            {
                AbilityDescriptor d;
                d.name = "Crusader's Brand";
                d.rootSpellId = 300513;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 10. Threat Builders
            {
                AbilityDescriptor d;
                d.name = "Holy Cleave (Sweeping Kick)";
                d.rootSpellId = 801445;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Righteous Lunge (Builder Strike)";
                d.rootSpellId = 801443;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Condemn (Judgment Builder)";
                d.rootSpellId = 804906;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Templar - Spec 23: Zealot (HOLY MELEE BURST DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 19;
            p.specId = 23; // Zealot
            p.role = BotRole::Dps;
            p.profileName = "Templar_Zealot_Dps";

            // 1. Emergency Defense: Final Prayer (< 25% HP)
            {
                AbilityDescriptor d;
                d.name = "Final Prayer";
                d.rootSpellId = 705268;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 25.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // 2. Interrupt: Interdict
            {
                AbilityDescriptor d;
                d.name = "Interdict";
                d.rootSpellId = 560116;
                d.tags = AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 350.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return ctx.victimIsCastingInterruptible ? 200.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 3. Stance / Buffs
            {
                AbilityDescriptor d;
                d.name = "Gift of Zeal";
                d.rootSpellId = 706634;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 706634;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Libram of Fervor";
                d.rootSpellId = 805423;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 805423;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 4. Major Offensive Cooldown: Zealotry
            {
                AbilityDescriptor d;
                d.name = "Zealotry";
                d.rootSpellId = 92108;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 5. Gap Closer: Divine Charge (> 8 yards)
            {
                AbilityDescriptor d;
                d.name = "Divine Charge";
                d.rootSpellId = 527023;
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

            // 6. Heavy Burst: Titanstrike & Divine Force
            {
                AbilityDescriptor d;
                d.name = "Titanstrike (2H Holy Weapon Strike)";
                d.rootSpellId = 806521;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Divine Force (Holy Force Burst)";
                d.rootSpellId = 806153;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 7. Primary Spenders / Breakers: Blade of Faith
            {
                AbilityDescriptor d;
                d.name = "Blade of Faith (Primary Single-Target Spender)";
                d.rootSpellId = 803872;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Chastise (Holy Stun Strike)";
                d.rootSpellId = 803157;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Benediction";
                d.rootSpellId = 801448;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 8. Target Debuff
            {
                AbilityDescriptor d;
                d.name = "Crusader's Brand";
                d.rootSpellId = 300513;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 195.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Mark of Aggramar";
                d.rootSpellId = 804928;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 9. Builders (Generate Oaths)
            {
                AbilityDescriptor d;
                d.name = "Righteous Lunge";
                d.rootSpellId = 801443;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Condemn";
                d.rootSpellId = 804906;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Holy Cleave";
                d.rootSpellId = 801445;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Templar - Spec 24: Crusader (HOLY MELEE AOE / TEMPEST DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 19;
            p.specId = 24; // Crusader
            p.role = BotRole::Dps;
            p.profileName = "Templar_Crusader_Dps";

            // 1. Emergency Defense: Final Prayer (< 25% HP)
            {
                AbilityDescriptor d;
                d.name = "Final Prayer";
                d.rootSpellId = 705268;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 25.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // 2. Interrupt: Interdict
            {
                AbilityDescriptor d;
                d.name = "Interdict";
                d.rootSpellId = 560116;
                d.tags = AbilityTag::CrowdControl;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 350.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return ctx.victimIsCastingInterruptible ? 200.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 3. Stance / Auras
            {
                AbilityDescriptor d;
                d.name = "Gift of Fervor";
                d.rootSpellId = 572629;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 572629;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Scourgebane";
                d.rootSpellId = 92111;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 92111;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 4. Ground AoE: Libram of Consecration (melee distance)
            {
                AbilityDescriptor d;
                d.name = "Libram of Consecration";
                d.rootSpellId = 801441;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::Self;
                d.baseScore = 240.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (!ctx.victim) return -1.0f;
                    return (ctx.bot->GetDistance(ctx.victim) <= 8.0f) ? 50.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 5. Gap Closer: Divine Charge (> 8 yards)
            {
                AbilityDescriptor d;
                d.name = "Divine Charge";
                d.rootSpellId = 527023;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    if (!ctx.victim) return -1.0f;
                    float dist = ctx.bot->GetDistance(ctx.victim);
                    return (dist >= 8.0f && dist <= 25.0f) ? 100.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // 6. Heavy Cooldown: Titanstrike & Divine Force
            {
                AbilityDescriptor d;
                d.name = "Titanstrike";
                d.rootSpellId = 806521;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Divine Force";
                d.rootSpellId = 806153;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 7. Core AoE Spender: Righteous Tempest
            {
                AbilityDescriptor d;
                d.name = "Righteous Tempest (Whirlwind Spender)";
                d.rootSpellId = 805409;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 235.0f;
                p.abilities.push_back(d);
            }

            // 8. Secondary Spenders
            {
                AbilityDescriptor d;
                d.name = "Blade of Faith";
                d.rootSpellId = 803872;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Benediction";
                d.rootSpellId = 801448;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // 9. Debuff: Crusader's Brand
            {
                AbilityDescriptor d;
                d.name = "Crusader's Brand";
                d.rootSpellId = 300513;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 195.0f;
                p.abilities.push_back(d);
            }

            // 10. Cleave / Builders
            {
                AbilityDescriptor d;
                d.name = "Holy Cleave (Sweeping Kick)";
                d.rootSpellId = 801445;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 185.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Righteous Lunge";
                d.rootSpellId = 801443;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 175.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Condemn";
                d.rootSpellId = 804906;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 165.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

