/*
 * mod-coa-playerbots
 *
 * Wildcard / Classless Hero Combat Profile: Hero (Class 10)
 */

#include "profiles/ProfileHero.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterHeroProfiles()
    {
        // =========================================================================
        // 1. SPEC 1: HERO JUGGERNAUT (TANK)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 10;
            p.specId = 1;
            p.role = BotRole::Tank;
            p.profileName = "Hero_Juggernaut_Tank";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Defensive Stance / Bear Form / Righteous Fury
            {
                AbilityDescriptor d;
                d.name = "Defensive Stance";
                d.rootSpellId = 71;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 71;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Righteous Fury";
                d.rootSpellId = 25780;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 25780;
                d.baseScore = 490.0f;
                p.abilities.push_back(d);
            }

            // Taunt / Growl / Hand of Reckoning
            {
                AbilityDescriptor d;
                d.name = "Taunt";
                d.rootSpellId = 355;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 450.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (!ctx.victimTargetingNonTank)
                        return -1.0f;
                    return 0.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Growl";
                d.rootSpellId = 6795;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 445.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (!ctx.victimTargetingNonTank)
                        return -1.0f;
                    return 0.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Hand of Reckoning";
                d.rootSpellId = 62124;
                d.tags = AbilityTag::Taunt;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 440.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    if (!ctx.victimTargetingNonTank)
                        return -1.0f;
                    return 0.0f;
                };
                p.abilities.push_back(d);
            }

            // Shield Wall / Last Stand / Barkskin (< 35% HP)
            {
                AbilityDescriptor d;
                d.name = "Shield Wall";
                d.rootSpellId = 871;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.missingAuraOnCaster = 871;
                d.internalThrottleMs = 30000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Barkskin";
                d.rootSpellId = 22812;
                d.tags = AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.missingAuraOnCaster = 22812;
                d.internalThrottleMs = 30000;
                d.baseScore = 390.0f;
                p.abilities.push_back(d);
            }

            // Shield Slam / Maul / Revenge
            {
                AbilityDescriptor d;
                d.name = "Shield Slam";
                d.rootSpellId = 23922;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Maul";
                d.rootSpellId = 6807;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Revenge";
                d.rootSpellId = 6572;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 290.0f;
                p.abilities.push_back(d);
            }

            // Thunder Clap / Consecration / Swipe
            {
                AbilityDescriptor d;
                d.name = "Thunder Clap";
                d.rootSpellId = 6343;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 2;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Consecration";
                d.rootSpellId = 26573;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 2;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // Sunder Armor / Heroic Strike
            {
                AbilityDescriptor d;
                d.name = "Sunder Armor";
                d.rootSpellId = 7386;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Heroic Strike";
                d.rootSpellId = 78;
                d.tags = AbilityTag::Filler | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 2. SPEC 2: HERO PRIEST (HEALER)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 10;
            p.specId = 2;
            p.role = BotRole::Healer;
            p.profileName = "Hero_Priest_Healer";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Power Word: Shield (< 85% HP)
            {
                AbilityDescriptor d;
                d.name = "Power Word: Shield";
                d.rootSpellId = 17;
                d.tags = AbilityTag::Shield | AbilityTag::EmergencyHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 85.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 420.0f;
                p.abilities.push_back(d);
            }

            // Flash of Light / Flash Heal / Lesser Healing Wave (< 75% HP)
            {
                AbilityDescriptor d;
                d.name = "Flash Heal";
                d.rootSpellId = 2061;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 75.0f;
                d.baseScore = 360.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Flash of Light";
                d.rootSpellId = 19750;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 75.0f;
                d.baseScore = 355.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Lesser Healing Wave";
                d.rootSpellId = 8004;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 75.0f;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // Rejuvenation / Renew (HoT < 90% HP)
            {
                AbilityDescriptor d;
                d.name = "Renew";
                d.rootSpellId = 139;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Rejuvenation";
                d.rootSpellId = 774;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 315.0f;
                p.abilities.push_back(d);
            }

            // Healing Wave / Holy Light / Greater Heal (< 50% HP)
            {
                AbilityDescriptor d;
                d.name = "Healing Wave";
                d.rootSpellId = 331;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 50.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Holy Light";
                d.rootSpellId = 635;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 50.0f;
                d.baseScore = 375.0f;
                p.abilities.push_back(d);
            }

            // Chain Heal / Prayer of Healing (Group Heal)
            {
                AbilityDescriptor d;
                d.name = "Chain Heal";
                d.rootSpellId = 1064;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minInjuredAllies = 2;
                d.baseScore = 360.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 3. SPEC 3: HERO BERSERKER (MELEE DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 10;
            p.specId = 3;
            p.role = BotRole::Dps;
            p.profileName = "Hero_Berserker_MeleeDps";
            p.preferredEngageDistance = PROFILE_MELEE_ENGAGE_DISTANCE;

            // Battle Shout
            {
                AbilityDescriptor d;
                d.name = "Battle Shout";
                d.rootSpellId = 6673;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 6673;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // Mortal Strike / Bloodthirst / Crusader Strike / Stormstrike
            {
                AbilityDescriptor d;
                d.name = "Mortal Strike";
                d.rootSpellId = 12294;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bloodthirst";
                d.rootSpellId = 23881;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 315.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Crusader Strike";
                d.rootSpellId = 35395;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 310.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Stormstrike";
                d.rootSpellId = 17364;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 305.0f;
                p.abilities.push_back(d);
            }

            // Execute (< 20% HP)
            {
                AbilityDescriptor d;
                d.name = "Execute";
                d.rootSpellId = 5308;
                d.tags = AbilityTag::Execute | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.maxTargetHpPct = 20.0f;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Sinister Strike / Claw (Filler)
            {
                AbilityDescriptor d;
                d.name = "Sinister Strike";
                d.rootSpellId = 1752;
                d.tags = AbilityTag::Filler | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Heroic Strike";
                d.rootSpellId = 78;
                d.tags = AbilityTag::Filler | AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 4. SPEC 4: HERO RANGER (RANGED DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 10;
            p.specId = 4;
            p.role = BotRole::Dps;
            p.profileName = "Hero_Ranger_RangedDps";
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

            // Kill Shot (< 20% HP)
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

            // Serpent Sting
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

            // Aimed Shot / Arcane Shot
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
            {
                AbilityDescriptor d;
                d.name = "Arcane Shot";
                d.rootSpellId = 3044;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 240.0f;
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

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 5. SPEC 5: HERO ELEMENTALIST (CASTER DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 10;
            p.specId = 5;
            p.role = BotRole::Dps;
            p.profileName = "Hero_Elementalist_CasterDps";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Corruption / Shadow Word: Pain / Moonfire / Flame Shock (DoTs)
            {
                AbilityDescriptor d;
                d.name = "Corruption";
                d.rootSpellId = 172;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Shadow Word: Pain";
                d.rootSpellId = 589;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 330.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Moonfire";
                d.rootSpellId = 8921;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Flame Shock";
                d.rootSpellId = 8042;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 310.0f;
                p.abilities.push_back(d);
            }

            // Nukes: Lava Burst / Chaos Bolt / Mind Blast / Fire Blast
            {
                AbilityDescriptor d;
                d.name = "Lava Burst";
                d.rootSpellId = 51505;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Chaos Bolt";
                d.rootSpellId = 50796;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Mind Blast";
                d.rootSpellId = 8092;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Fillers: Frostbolt / Fireball / Shadow Bolt / Lightning Bolt / Wrath
            {
                AbilityDescriptor d;
                d.name = "Frostbolt";
                d.rootSpellId = 116;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fireball";
                d.rootSpellId = 133;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Shadow Bolt";
                d.rootSpellId = 686;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 205.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Lightning Bolt";
                d.rootSpellId = 403;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Wrath";
                d.rootSpellId = 5176;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 195.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
