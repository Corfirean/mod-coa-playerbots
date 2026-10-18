/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: Runemaster Profiles implementation
 * Supports:
 *   - Spec 61: Engravement (Melee Rune Weaving DPS)
 *   - Spec 62: Glyphic (Ranged Runic Caster DPS)
 *   - Spec 63: Riftblade (Melee Mobility DPS)
 */

#include "profiles/ProfileRunemaster.h"
#include "profiles/ProfileRegistry.h"
#include "engine/CombatContext.h"
#include "engine/SpecStrategyRegistry.h"
#include "Player.h"

namespace BotAI
{
    void RegisterRunemasterProfiles()
    {
        // -------------------------------------------------------------
        // Profile 1: Runemaster - Spec 61: Engravement (MELEE DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 32;
            p.specId = 61; // Engravement
            p.role = BotRole::Dps;
            p.profileName = "Runemaster_Engravement_MeleeDps";

            // Water tattoo: restores mana every 5 s and cuts spell costs. Engravement's nukes (Primordial Blast, Elemental Burst,
            // Fist of the Ancients) run on mana; with the Earth tattoo the bot sat at ~100/2590 mana from second 40 and rejected
            // ~40% of its decisions as "can't afford" (the Sidekick guide: "Water if your own mana margin is thin").
            {
                AbilityDescriptor d;
                d.name = "Runic Tattoos: Water";
                d.rootSpellId = 801107;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 801107;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 1. Personal Defense: Runeshroud (< 50% HP)
            {
                AbilityDescriptor d;
                d.name = "Runeshroud (Shield)";
                d.rootSpellId = 500288;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.missingAuraOnCaster = 500288;
                d.internalThrottleMs = 15000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 2. Major Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Zenith (Burst)";
                d.rootSpellId = 712389;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fist of the Ancients";
                d.rootSpellId = 712326;
                d.tags = AbilityTag::MeleeAttack; // not OffensiveCD: a 15-second-throttle rotational attack; that tag scales the score by x0.15 outside boss/elite/PvP fights
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 3. Melee Strikes & Brands
            {
                AbilityDescriptor d;
                d.name = "Runic Brand";
                d.rootSpellId = 712299;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 170.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Runeblade";
                d.rootSpellId = 707141;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Palm Sigil: Arcane";
                d.rootSpellId = 805380;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fracture";
                d.rootSpellId = 803018;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 135.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Elemental Burst";
                d.rootSpellId = 802202;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 4. DoT Maintenance
            {
                AbilityDescriptor d;
                d.name = "Hoarfrost";
                d.rootSpellId = 801104;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 110.0f;
                p.abilities.push_back(d);
            }

            // 5. Ranged Gap Filler
            {
                AbilityDescriptor d;
                d.name = "Frigid Blast";
                d.rootSpellId = 500118;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 50.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                // 8 s cooldown, 15% of real casts
                d.name = "Primordial Blast";
                d.rootSpellId = 800732;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 8000;
                d.baseScore = 215.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);

            // SpecStrategy for Engravement
            SpecStrategy s;
            s.classId = 32;
            s.specId = 61;
            s.role = BotRole::Dps;
            s.strategyName = "Runemaster_Engravement_MeleeDps_Strategy";

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::MeleeAttack, 1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f },
                { AbilityTag::MeleeAttack, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,   1.8f, 50.0f },
                { AbilityTag::MeleeAttack, 1.2f, 20.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 25) &&
                       (bot->GetHealthPct() >= 50.0f);
            };

            // ResourcePolicy for Arcane Sigil (500282)
            {
                ResourcePolicy pol;
                pol.key = CombatResourceKey{ CombatResourceKind::AuraStack, 0, 500282 };
                pol.overcapThreshold = 3;
                s.resourcePolicies.push_back(pol);
            }

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 2: Runemaster - Spec 62: Glyphic (RANGED CASTER DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 32;
            p.specId = 62; // Glyphic
            p.role = BotRole::Dps;
            p.profileName = "Runemaster_Glyphic_RangedDps";

            // Elemental tattoos are active self buffs, not the passive Runic
            // Tattoos spell. Water also enables the native Unleashed Water
            // helper when that talent is present; keep one stable baseline.
            {
                AbilityDescriptor d;
                d.name = "Runic Tattoos: Water";
                d.rootSpellId = 801107;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 801107;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 1. Personal Defense
            {
                AbilityDescriptor d;
                d.name = "Runeshroud (Shield)";
                d.rootSpellId = 500288;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.missingAuraOnCaster = 500288;
                d.internalThrottleMs = 15000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 2. Major Burst Cooldown
            {
                AbilityDescriptor d;
                d.name = "Zenith (Burst)";
                d.rootSpellId = 712325;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }

            // 3. DoT
            {
                AbilityDescriptor d;
                d.name = "Hoarfrost";
                d.rootSpellId = 801104;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }

            // 4. Heavy Ranged Nukes
            {
                AbilityDescriptor d;
                d.name = "Glyphic Ruin";
                d.rootSpellId = 801179;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Thaumaturgy";
                d.rootSpellId = 804550;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Primordial Blast";
                d.rootSpellId = 800732;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 120.0f;
                p.abilities.push_back(d);
            }

            // 5. Ranged Spammer / Builder
            {
                AbilityDescriptor d;
                d.name = "Frigid Blast";
                d.rootSpellId = 500118;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 70.0f;
                p.abilities.push_back(d);
            }

            // Native Elemental Burst is also a glyph generator. Primordial Blast has
            // an eight-second native cooldown; it alone cannot build three stages
            // inside Frost's fifteen-second duration. Prefer this learned generator
            // over the unrelated Frigid Blast filler while native cooldowns run.
            {
                AbilityDescriptor d;
                d.name = "Elemental Burst (Glyph Generator)";
                d.rootSpellId = 802202;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 100.0f;
                p.abilities.push_back(d);
            }

            // 6. Close-Range Defense
            {
                AbilityDescriptor d;
                d.name = "Fist of the Ancients";
                d.rootSpellId = 712326;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 60.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Primordial Pulse";
                d.rootSpellId = 300578;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 20000;
                d.baseScore = 235.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Glyphic Overload";
                d.rootSpellId = 520099;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.internalThrottleMs = 45000;
                d.baseScore = 225.0f;
                p.abilities.push_back(d);
            }

            // Native generators build Frost -> Flame -> Arcane. Releasers consume
            // every active glyph on hit, so do not discard the cycle after Frost.
            for (auto& ability : p.abilities)
                if (ability.rootSpellId == 801179 || ability.rootSpellId == 804550)
                    ability.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float
                    {
                        uint32 readyGlyph = ctx.bot->HasAura(805727) ? 520092 :
                            ctx.bot->HasAura(705625) ? 520091 :
                            ctx.bot->HasAura(92152) ? 520090 : 0;
                        return !readyGlyph || ctx.bot->HasAura(readyGlyph, ctx.bot->GetGUID()) ? 0.0f : -1.0f;
                    };
            ProfileRegistry::RegisterProfile(p);

            // SpecStrategy for Glyphic
            SpecStrategy s;
            s.classId = 32;
            s.specId = 62;
            s.role = BotRole::Dps;
            s.strategyName = "Runemaster_Glyphic_RangedDps_Strategy";
            s.ownsCombatRotation = true; // Legacy nukes must not bypass the native glyph-cycle scorer.

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::PeriodicDamage, 1.5f, 40.0f },
                { AbilityTag::RangedAttack,   1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD,  1.8f, 50.0f },
                { AbilityTag::RangedAttack, 1.3f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage, 1.8f, 50.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 30) &&
                       (bot->GetHealthPct() >= 50.0f);
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }

        // -------------------------------------------------------------
        // Profile 3: Runemaster - Spec 63: Riftblade (MELEE MOBILITY DPS)
        // -------------------------------------------------------------
        {
            CombatProfile p;
            p.classId = 32;
            p.specId = 63; // Riftblade
            p.role = BotRole::Dps;
            p.profileName = "Runemaster_Riftblade_MeleeDps";

            // Stable native tattoo baseline; its associated native talent
            // remains responsible for any triggered defensive or movement effect.
            {
                AbilityDescriptor d;
                d.name = "Runic Tattoos: Air";
                d.rootSpellId = 802630;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 802630;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 1. Defense
            {
                AbilityDescriptor d;
                d.name = "Runeshroud";
                d.rootSpellId = 500288;
                d.tags = AbilityTag::DefensiveCD | AbilityTag::Shield;
                d.targetType = TargetType::Self;
                d.maxSelfHpPct = 50.0f;
                d.missingAuraOnCaster = 500288;
                d.internalThrottleMs = 15000;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // 2. Cooldowns
            {
                AbilityDescriptor d;
                d.name = "Zenith";
                d.rootSpellId = 712325;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 30000;
                d.baseScore = 220.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fist of the Ancients";
                d.rootSpellId = 712326;
                d.tags = AbilityTag::MeleeAttack; // not OffensiveCD: a 15-second-throttle rotational attack; that tag scales the score by x0.15 outside boss/elite/PvP fights
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 15000;
                d.baseScore = 190.0f;
                p.abilities.push_back(d);
            }

            // 3. Fast Blade Strikes
            {
                AbilityDescriptor d;
                d.name = "Runeblade";
                d.rootSpellId = 707141;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 160.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Fracture";
                d.rootSpellId = 803018;
                d.tags = AbilityTag::MeleeAttack;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 4000;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Runic Brand";
                d.rootSpellId = 712299;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 140.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Elemental Burst";
                d.rootSpellId = 802202;
                d.tags = AbilityTag::MeleeAttack | AbilityTag::AoEDamage;
                d.targetType = TargetType::CurrentTarget;
                d.internalThrottleMs = 6000;
                d.baseScore = 130.0f;
                p.abilities.push_back(d);
            }

            // 4. DoT & Ranged Filler
            {
                AbilityDescriptor d;
                d.name = "Hoarfrost";
                d.rootSpellId = 801104;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.internalThrottleMs = 4000;
                d.baseScore = 100.0f;
                p.abilities.push_back(d);
            }
            {
                AbilityDescriptor d;
                d.name = "Frigid Blast";
                d.rootSpellId = 500118;
                d.tags = AbilityTag::RangedAttack | AbilityTag::Filler;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 50.0f;
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);

            // SpecStrategy for Riftblade
            SpecStrategy s;
            s.classId = 32;
            s.specId = 63;
            s.role = BotRole::Dps;
            s.strategyName = "Runemaster_Riftblade_MeleeDps_Strategy";

            s.phaseModifiers[CombatPhase::Opener] = {
                { AbilityTag::MeleeAttack, 1.5f, 40.0f }
            };
            s.phaseModifiers[CombatPhase::Burst] = {
                { AbilityTag::OffensiveCD, 1.8f, 50.0f },
                { AbilityTag::MeleeAttack, 1.4f, 30.0f }
            };
            s.phaseModifiers[CombatPhase::AoE] = {
                { AbilityTag::AoEDamage,   1.8f, 50.0f },
                { AbilityTag::MeleeAttack, 1.2f, 20.0f }
            };

            s.isReadyToPull = [](Player* bot, CombatContext const&) -> bool
            {
                return (bot->GetPower(POWER_MANA) * 100 / std::max(1u, bot->GetMaxPower(POWER_MANA)) >= 25) &&
                       (bot->GetHealthPct() >= 50.0f);
            };

            SpecStrategyRegistry::RegisterStrategy(std::move(s));
        }
    }
}
