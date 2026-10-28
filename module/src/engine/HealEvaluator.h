/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: HealEvaluator (item 10/12, Phase 2)
 *
 * Healing leaned too hard on lowest-HP%. A tank at 55% taking heavy incoming damage can matter
 * more than a DPS at 30% nobody's still hitting -- this scores every candidate on missing HP,
 * incoming-damage trend (see DamageTracker), role, whether an enemy is actively on them, and
 * already-reserved incoming heals from other healers (see CombatReservations), instead of just
 * picking the lowest percentage. Also answers item 12's "don't force a fixed 25yd approach
 * distance" via BestKnownHealRange, so positioning can use the healer's own actual spell range.
 */

#ifndef COA_PLAYERBOTS_HEAL_EVALUATOR_H
#define COA_PLAYERBOTS_HEAL_EVALUATOR_H

#include "Define.h"
#include "ObjectGuid.h"
#include "engine/CombatDecisions.h"

class Player;
class SpellInfo;
class Unit;

namespace BotAI
{
    class HealEvaluator
    {
    public:
        // Eligibility gate (item 11, Phase 2 fixup), separate from scoring: is `candidate` worth
        // healing *at all* right now? The old code used a magic `score > 5.0` threshold that
        // worked out to "missing >= 2.5% HP," an overly aggressive top-off trigger (a player at
        // 98% HP with nothing hurting them doesn't need attention; one at 98% taking heavy burst
        // does). True when missing HP alone already justifies it (>=5%), or a lesser dip is made
        // urgent by a real incoming-damage trend, a low time-to-die, or a dangerous DoT debuff.
        static bool ShouldConsiderHealing(Player* healer, Player* candidate);

        // Higher is better; -1 means "not a valid heal candidate at all" (dead, out of world,
        // absurdly far away). Does NOT itself gate eligibility -- see ShouldConsiderHealing.
        static float ScoreHealUrgency(Player* healer, Player* candidate);

        // Best heal target among the healer and its group within maxRange that passes
        // ShouldConsiderHealing, or nullptr if nobody currently needs it.
        static Player* SelectBestHealTarget(Player* healer, float maxRange = 60.0f);

        // What a direct heal of `spellInfo` would restore on `target` if all of it landed on missing health (healing done / taken
        // modifiers included, NOT capped at the missing health); 0 for a pure HoT or a script-only heal, whose amount is unknown.
        static uint32 EstimateRawHeal(Player* healer, Unit* target, SpellInfo const* spellInfo);

        // The forecast for casting `spellInfo` on `target` now: raw amount, damage expected before it lands (cast time x incoming damage, less
        // the usable absorb), heals other healers have in flight, and from those the useful part (effective) and the waste (overheal).
        static Decisions::HealForecast ForecastHeal(Player* healer, Unit* target, SpellInfo const* spellInfo);

        // False when casting a direct heal on `target` would be waste: the target does not need healing at all (ShouldConsiderHealing), or
        // the useful part is under 40% of the raw heal and nothing about the target is urgent (see Decisions::IsDirectHealWorthCasting).
        // A heal of unknown size is judged as a 15% heal.
        static bool IsDirectHealWorthCasting(Player* healer, Player* target, SpellInfo const* spellInfo);

        // The absorb shields on `unit` that can be counted on over the next `horizonMs` against damage of `schoolMask` (0 = unknown). An
        // absorb is not health: it lowers what reaches health, it never lowers missing health.
        static uint32 UsableAbsorb(Unit const* unit, uint32 horizonMs, uint32 schoolMask);

        // Max range among the healer's own currently-known, usable heal-shaped spells (falls back
        // to `fallback` if it knows none) -- item 12: a healer with a 40yd heal shouldn't be
        // forced to approach to a fixed 25yd. Cached per-bot for a few seconds since this walks
        // the whole spellbook; cheap to call once per healer decision tick.
        static float BestKnownHealRange(Player* healer, float fallback = 35.0f);

        // Drops this bot's cached heal-range result -- called on despawn (see BotAI::Forget) so
        // the cache doesn't grow across repeated spawn/despawn cycles.
        static void ForgetBot(ObjectGuid botGuid);
    };
}

#endif // COA_PLAYERBOTS_HEAL_EVALUATOR_H
