/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Necromancer Profiles & Spec Strategies
 *
 * Specializations:
 *   - Spec 34: Death (Shadow / Disease Caster DPS)
 *   - Spec 35: Animation (Minion Swarm / Pet Master DPS)
 *   - Spec 36: Rime (Frost / Chill Caster DPS)
 */

#include "profiles/ProfileNecromancer.h"
#include "profiles/ProfileRegistry.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterNecromancerProfiles()
    {
        // =========================================================================
        // 1. SPEC 34: DEATH (SHADOW / DISEASE CASTER DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Caster
        // - Resource: Mana
        // - Tactical Policy: Triple disease upkeep, Bone Tithe / Death's Due spenders
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 23; // Necromancer
            p.specId = 34;  // Death
            p.role = BotRole::Dps;
            p.profileName = "Necromancer_Death_Dps";

            // Emergency Defense
            {
                AbilityDescriptor d;
                d.name = "Sacrifice Undead";
                d.rootSpellId = 805027;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.internalThrottleMs = 30000;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // Armor Buff
            {
                AbilityDescriptor d;
                d.name = "Lich Armor";
                d.rootSpellId = 800199;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800199;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Major Burst Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Crypt Plague";
                d.rootSpellId = 92121;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 90000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Ner'zhul's Blessing";
                d.rootSpellId = 704729;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 60000;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // Primary Diseases / DoTs
            {
                AbilityDescriptor d;
                d.name = "Putrefy";
                d.rootSpellId = 804558;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Flesh to Worms";
                d.rootSpellId = 500338;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Tears of Lordaeron";
                d.rootSpellId = 705752;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }

            // Learned disease kit the skeleton never listed (Sidekick guide: Plague of Undeath -> Flesh to Worms -> Lichplague, whose
            // damage grows per Necromancer disease on the target). Plague of Undeath was only cast by the generic fallback and
            // Lichplague / Harvest Plague hardly at all.
            {
                AbilityDescriptor d;
                d.name = "Plague of Undeath";
                d.rootSpellId = 801945;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 262.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Lichplague";
                d.rootSpellId = 802132;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 18000;
                d.baseScore = 256.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Harvest Plague";
                d.rootSpellId = 500968;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 236.0f;
                p.abilities.push_back(d);
            }

            // Heavy Spenders
            {
                AbilityDescriptor d;
                d.name = "Death's Due";
                d.rootSpellId = 807796;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Bone Tithe";
                d.rootSpellId = 802121;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Mass Grave";
                d.rootSpellId = 803741;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 3;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Filler
            {
                AbilityDescriptor d;
                d.name = "Lichfrost";
                d.rootSpellId = 501969;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 23;
            s.specId = 34;
            s.role = BotRole::Dps;
            s.strategyName = "Death_Dps_Strategy";
            s.minResourceToEngage = 40.0f;
            s.recoveryThreshold = 15.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 40;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,    2.0f, 60.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 2. SPEC 35: ANIMATION (MINION SWARM / PET MASTER DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Baseline State: Minion Master
        // - Resource: Mana + Minion army
        // - Pre-Pull: Army summons throttled so they aren't repeatedly recreated
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 23;
            p.specId = 35; // Animation
            p.role = BotRole::Dps;
            p.profileName = "Necromancer_Animation_Dps";

            // Emergency Defense
            {
                AbilityDescriptor d;
                d.name = "Sacrifice Undead";
                d.rootSpellId = 805027;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 40.0f;
                d.internalThrottleMs = 30000;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // Armor Buff
            {
                AbilityDescriptor d;
                d.name = "Lich Armor";
                d.rootSpellId = 800199;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800199;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Minion Summons (Strict throttles and entity tracking to maintain roster without spam)
            {
                AbilityDescriptor d;
                d.name = "Raise: Decaying Colossus";
                d.rootSpellId = 500989;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.trackedEntityType = TrackedEntityType::Minion;
                d.trackedEntityEntry = 50115;
                d.maxActiveEntities = 1;
                d.internalThrottleMs = 60000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Raise: Abomination";
                d.rootSpellId = 500335;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.trackedEntityType = TrackedEntityType::Minion;
                d.trackedEntityEntry = 50068;
                d.maxActiveEntities = 1;
                d.internalThrottleMs = 45000;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Raise: Skeletal Archer";
                d.rootSpellId = 500332;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.trackedEntityType = TrackedEntityType::Minion;
                d.trackedEntityEntry = 50075;
                d.maxActiveEntities = 2;
                d.internalThrottleMs = 30000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Raise: Brittle Skeleton";
                d.rootSpellId = 500970;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.trackedEntityType = TrackedEntityType::Minion;
                d.trackedEntityEntry = 50065;
                d.maxActiveEntities = 3;
                d.internalThrottleMs = 20000;
                d.baseScore = 240.0f;
                p.abilities.push_back(d);
            }

            // Minion Commands
            {
                AbilityDescriptor d;
                d.name = "Foul Bidding: Slaughter";
                d.rootSpellId = 500982;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Unholy Command";
                d.rootSpellId = 802123;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // Direct Damage & DoTs
            {
                AbilityDescriptor d;
                d.name = "Putrefy";
                d.rootSpellId = 804558;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Flesh to Worms";
                d.rootSpellId = 500338;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Lichfrost";
                d.rootSpellId = 501969;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 215.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // instant DoT, 35% of real casts
                d.name = "Crypt Swarm";
                d.rootSpellId = 500965;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 3000;
                d.baseScore = 255.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Harvest Plague";
                d.rootSpellId = 500968;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 225.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 30 s cooldown summon, 15% of real casts
                d.name = "Animate: Skeletal Archer";
                d.rootSpellId = 805040;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 30000;
                d.baseScore = 245.0f;
                p.abilities.push_back(d);
            }
            // Sidekick guide: dump the Runic Power from Crypt Swarm into the Animate summons and March of the Dead. Only the
            // Skeletal Archer was listed, so Bone Wraith / Plaguefather / March of the Dead never fired.
            {
                AbilityDescriptor d;
                d.name = "Animate: Bone Wraith";
                d.rootSpellId = 805032;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 60000;
                d.baseScore = 244.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Animate: Plaguefather";
                d.rootSpellId = 805048;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 120000;
                d.baseScore = 243.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "March of the Dead";
                d.rootSpellId = 707007;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 1;
                d.internalThrottleMs = 10000;
                d.baseScore = 242.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // energize effect: a 12 s cooldown mana refund
                d.name = "Glacial Tap (mana)";
                d.rootSpellId = 805369;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 12000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Unholy Frenzy";
                d.rootSpellId = 805029;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 180000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 23;
            s.specId = 35;
            s.role = BotRole::Dps;
            s.strategyName = "Animation_Dps_Strategy";
            s.minResourceToEngage = 40.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 40;
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f }
            };

            // ResourcePolicy for MinionCapacity (minToEngage = 0 to prevent pull deadlock: minions require hostile target/combat)
            {
                ResourcePolicy pol;
                pol.key = CombatResourceKey{ CombatResourceKind::MinionCapacity, 0, 0 };
                pol.minToEngage = 0;
                s.resourcePolicies.push_back(pol);
            }

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // =========================================================================
        // 3. SPEC 36: RIME (FROST / CHILL CASTER DPS)
        // =========================================================================
        // Contract:
        // - Canonical Role: Dps
        // - Mandatory Baseline State: Lich Form (spell 500981, aura 500981)
        // - Frost Wyrm and Tundra Warriors burst windows
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 23;
            p.specId = 36; // Rime
            p.role = BotRole::Dps;
            p.profileName = "Necromancer_Rime_Dps";

            // Emergency Defense
            {
                AbilityDescriptor d;
                d.name = "Sacrifice Undead";
                d.rootSpellId = 805027;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::DirectHeal;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 35.0f;
                d.internalThrottleMs = 30000;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // Forms & Buffs
            {
                AbilityDescriptor d;
                d.name = "Lich Form";
                d.rootSpellId = 500981;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 500981;
                d.internalThrottleMs = 5000;
                d.baseScore = 480.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Lich Armor";
                d.rootSpellId = 800199;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 800199;
                d.internalThrottleMs = 30000;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Major Frost Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Animate: Frost Wyrm";
                d.rootSpellId = 805428;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 90000;
                d.baseScore = 280.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Tundra Warriors";
                d.rootSpellId = 92122;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 60000;
                d.baseScore = 270.0f;
                p.abilities.push_back(d);
            }

            // Frost Burst Nukes
            {
                AbilityDescriptor d;
                d.name = "Ice Barrage";
                d.rootSpellId = 801760; // Native learned Ice Barrage channel, not its damage trigger
                d.tags = AbilityTag::RangedAttack; // not OffensiveCD: a 12-second-throttle rotational attack; that tag scales the score by x0.15 outside boss/elite/PvP fights
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 12000;
                d.baseScore = 250.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Glacial Tap";
                d.rootSpellId = 805369;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 230.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Winds of Northrend";
                d.rootSpellId = 301333;
                d.tags = AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.minAoETargets = 3;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // Primary DoT
            {
                AbilityDescriptor d;
                d.name = "Putrefy";
                d.rootSpellId = 804558;
                d.tags = AbilityTag::RangedAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 6000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            // Core Frost Nuke
            {
                AbilityDescriptor d;
                d.name = "Lichfrost";
                d.rootSpellId = 501969;
                d.tags = AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 180.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 6 s cooldown nuke, 64% of real casts
                d.name = "Glacial Impact";
                d.rootSpellId = 704355;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 255.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Corpse Explosion";
                d.rootSpellId = 533236;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 60000;
                d.baseScore = 215.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Unholy Frenzy";
                d.rootSpellId = 805029;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 180000;
                d.baseScore = 210.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(std::move(p));

            SpecStrategy s;
            s.classId = 23;
            s.specId = 36;
            s.role = BotRole::Dps;
            s.strategyName = "Rime_Dps_Strategy";
            s.requiredState.formSpellId = 500981; // Lich Form
            s.requiredState.formAuraId = 500981;
            s.minResourceToEngage = 40.0f;

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return bot->HasAura(500981) && (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 40);
            };

            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,    1.8f, 50.0f }
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
