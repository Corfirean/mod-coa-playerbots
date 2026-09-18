/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Cultist Profiles implementation
 * Supports:
 *   - Spec 96: Dreadnought (Void Plate Tank)
 *   - Spec 40: Heretic (Dark Mending Healer)
 *   - Spec 41: Corruption (Old God Caster DPS)
 *   - Spec 42: Godblade (Void Melee DPS)
 *   - Spec 0: Default Fallback
 */

#include "profiles/ProfileCultist.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterCultistProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Cultist - Spec 96: Dreadnought (VOID TANK)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 25; // Cultist
            p.specId = 96; // Dreadnought
            p.role = BotRole::Tank;
            p.profileName = "Cultist_Dreadnought_Tank";

            // 1. Primary Taunts
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
                d.name = "Grasp of Zek'voz (Void Grip)";
                d.rootSpellId = 573028;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 440.0f;
                p.abilities.push_back(d);
            }

            // 2. Active Mitigation: Dreadnought / Abyssal Ward (< 55% HP)
            {
                AbilityDescriptor d;
                d.name = "Dreadnought";
                d.rootSpellId = 680750;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 55.0f;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Abyssal Ward";
                d.rootSpellId = 804670;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 65.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // 3. Self-Sustain: Satiate (< 45% HP)
            {
                AbilityDescriptor d;
                d.name = "Satiate";
                d.rootSpellId = 804275;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 45.0f;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 4. Buffs: Void-Enchanted Armor
            {
                AbilityDescriptor d;
                d.name = "Void-Enchanted Armor";
                d.rootSpellId = 804633;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 804633;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 5. Threat Strikes
            {
                AbilityDescriptor d;
                d.name = "Blade of the Empire";
                d.rootSpellId = 500720;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Eldritch Strike";
                d.rootSpellId = 801964;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Gaze of C'Thun";
                d.rootSpellId = 500110;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 2: Cultist - Spec 40: Heretic (DARK MENDING HEALER)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 25;
            p.specId = 40; // Heretic
            p.role = BotRole::Healer;
            p.profileName = "Cultist_Heretic_Healer";

            // 1. Critical Direct Heal: Abyssal Reconstruction (< 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Abyssal Reconstruction";
                d.rootSpellId = 800429;
                d.tags = AbilityTag::DirectHeal | AbilityTag::DefensiveCD;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 40.0f;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // 2. Protective Shield: Void Shield (< 60% HP)
            {
                AbilityDescriptor d;
                d.name = "Void Shield";
                d.rootSpellId = 500715;
                d.tags = AbilityTag::Shield;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 60.0f;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // 3. Sustained Dark Heal: Satiate (< 80% HP)
            {
                AbilityDescriptor d;
                d.name = "Satiate";
                d.rootSpellId = 804275;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 80.0f;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 4. Group Dark Blessing: Herald of the Depths
            {
                AbilityDescriptor d;
                d.name = "Herald of the Depths";
                d.rootSpellId = 92131;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 92131;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 5. Offensive Weaving
            {
                AbilityDescriptor d;
                d.name = "Gaze of C'Thun";
                d.rootSpellId = 500110;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Horrorbolt";
                d.rootSpellId = 800416;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 3: Cultist - Spec 41: Corruption (OLD GOD CASTER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 25;
            p.specId = 41; // Corruption
            p.role = BotRole::Dps;
            p.profileName = "Cultist_Corruption_Dps";

            // 1. Emergency Defense: Void Shield (< 35% HP)
            {
                AbilityDescriptor d;
                d.name = "Void Shield";
                d.rootSpellId = 500715;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // 2. Major Cooldowns: Corrupting Whispers / Vision of Doom
            {
                AbilityDescriptor d;
                d.name = "Corrupting Whispers";
                d.rootSpellId = 92130;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Vision of Doom";
                d.rootSpellId = 520388;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // 3. Heralds
            {
                AbilityDescriptor d;
                d.name = "Herald of Yogg-Saron";
                d.rootSpellId = 805120;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Herald of C'thun";
                d.rootSpellId = 805119;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // 4. DoTs & Channels
            {
                AbilityDescriptor d;
                d.name = "Gaze of C'Thun (Void Channel)";
                d.rootSpellId = 500110;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Eldritch Screams";
                d.rootSpellId = 525049;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 5. Filler: Horrorbolt
            {
                AbilityDescriptor d;
                d.name = "Horrorbolt";
                d.rootSpellId = 800416;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

        // -------------------------------------------------------------
        // Profile 4: Cultist - Spec 42: Godblade (VOID MELEE DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 25;
            p.specId = 42; // Godblade
            p.role = BotRole::Dps;
            p.profileName = "Cultist_Godblade_Dps";

            // 1. Buff: Shroud of Pride
            {
                AbilityDescriptor d;
                d.name = "Shroud of Pride";
                d.rootSpellId = 805126;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 805126;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }

            // 2. Major Cooldown: Obliteration
            {
                AbilityDescriptor d;
                d.name = "Obliteration";
                d.rootSpellId = 92129;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }

            // 3. Gap Closer / Grip: Grasp of Zek'voz (> 8 yards)
            {
                AbilityDescriptor d;
                d.name = "Grasp of Zek'voz";
                d.rootSpellId = 573028;
                d.tags = AbilityTag::RangedAttack;
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

            // 4. Melee Strikes
            {
                AbilityDescriptor d;
                d.name = "Blade of the Empire";
                d.rootSpellId = 500720;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Eldritch Strike";
                d.rootSpellId = 801964;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Devourer";
                d.rootSpellId = 300265;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 5. Spender / Channel: Gaze of C'Thun
            {
                AbilityDescriptor d;
                d.name = "Gaze of C'Thun";
                d.rootSpellId = 500110;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));
        }

    }
}

