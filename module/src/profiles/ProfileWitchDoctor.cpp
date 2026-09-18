/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Witch Doctor Profile implementation
 */

#include "profiles/ProfileWitchDoctor.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"

namespace BotAI
{
    void RegisterWitchDoctorProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Witch Doctor Healer (Spec 6: Brewing)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 13;
            p.specId = 6; // Brewing Spec
            p.role = BotRole::Healer;
            p.profileName = "WitchDoctor_Brewing_Healer";

            // 1. Emergency Heals (target < 40% HP)
            {
                AbilityDescriptor d;
                d.name = "Loa's Brew (Emergency)";
                d.rootSpellId = 801670;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 40.0f;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spirit in a Bottle (Emergency)";
                d.rootSpellId = 801696;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 40.0f;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Reclaim Soul (Emergency)";
                d.rootSpellId = 801796;
                d.tags = AbilityTag::EmergencyHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 35.0f;
                d.baseScore = 260.0f;
                p.abilities.push_back(d);
            }

            // 2. Direct Heals / Triage (target < 75% HP)
            {
                AbilityDescriptor d;
                d.name = "Loa's Brew (Triage)";
                d.rootSpellId = 801670;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 75.0f;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spirit in a Bottle (Triage)";
                d.rootSpellId = 801696;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 75.0f;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 3. Totem / Ward Support
            {
                AbilityDescriptor d;
                d.name = "Healing Ward";
                d.rootSpellId = 500957;
                d.tags = AbilityTag::TotemOrWard | AbilityTag::PeriodicHeal;
                d.targetType = TargetType::Self;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 85.0f;
                d.internalThrottleMs = 20000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }

            // 4. Maintenance / Top-off (target < 92% HP)
            {
                AbilityDescriptor d;
                d.name = "Loa's Brew (Maintenance)";
                d.rootSpellId = 801670;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 92.0f;
                d.baseScore = 100.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Spirit in a Bottle (Maintenance)";
                d.rootSpellId = 801696;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minTargetHpPct = 0.0f;
                d.maxTargetHpPct = 92.0f;
                d.baseScore = 85.0f;
                p.abilities.push_back(d);
            }

            // 5. Offensive contribution when party is safe (lowest ally > 85% HP)
            {
                AbilityDescriptor d;
                d.name = "Hex of Malice";
                d.rootSpellId = 801693;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 40.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    // Only cast offensive debuff if nobody in party is in danger
                    return (ctx.lowestAllyHpPct > 85.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Shadowflare";
                d.rootSpellId = 801669;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 20.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                {
                    return (ctx.lowestAllyHpPct > 85.0f) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // -------------------------------------------------------------
        // Profile 2: Witch Doctor - Spec 4: Shadowhunting (RANGED DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 13;
            p.specId = 4; // Shadowhunting
            p.role = BotRole::Dps;
            p.profileName = "WitchDoctor_Shadowhunting_Dps";

            // 1. Major Self Buffs
            {
                AbilityDescriptor d;
                d.name = "Shadow Avatar";
                d.rootSpellId = 705943;
                d.tags = AbilityTag::OffensiveCD | AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 705943;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // 2. Guardians & Major Summons
            {
                AbilityDescriptor d;
                d.name = "Big Voodoo";
                d.rootSpellId = 802719;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 45000;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "War Golem";
                d.rootSpellId = 800330;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 45000;
                d.baseScore = 175.0f;
                p.abilities.push_back(d);
            }

            // 3. Wards & Totems
            {
                AbilityDescriptor d;
                d.name = "Healing Ward (Self Defense)";
                d.rootSpellId = 500957;
                d.tags = AbilityTag::TotemOrWard | AbilityTag::DefensiveCD;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 60.0f;
                d.internalThrottleMs = 25000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Serpent Ward";
                d.rootSpellId = 500960;
                d.tags = AbilityTag::TotemOrWard;
                d.targetType = TargetType::CurrentTarget;
                d.minSelfHpPct = 60.0f;
                d.internalThrottleMs = 25000;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 4. DoTs & Curses
            {
                AbilityDescriptor d;
                d.name = "Hex of Malice";
                d.rootSpellId = 801693;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Shrinking Jinx";
                d.rootSpellId = 806285;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 135.0f;
                p.abilities.push_back(d);
            }

            // 5. Heavy Offensive Nukes
            {
                AbilityDescriptor d;
                d.name = "Malefic Wrath";
                d.rootSpellId = 807037;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 110.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Potion Toss";
                d.rootSpellId = 801661;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 105.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Splash Potion";
                d.rootSpellId = 802710;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 100.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Mojo Beam";
                d.rootSpellId = 500950;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 95.0f;
                p.abilities.push_back(d);
            }

            // 6. Spammer / Filler
            {
                AbilityDescriptor d;
                d.name = "Shadowflare";
                d.rootSpellId = 801669;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 50.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);

            // Register also for Spec 5: Voodoo DPS
            p.specId = 5;
            p.profileName = "WitchDoctor_Voodoo_Dps";
            ProfileRegistry::RegisterProfile(p);
        }
    }
}
