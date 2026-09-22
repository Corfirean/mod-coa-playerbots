/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatResult
 *
 * Replaces the old bool return from DpsEngine/HealerEngine/TankEngine::Execute -- see item 2 of
 * the combat-engine rework. The old contract collapsed "a profile exists and handled this tick"
 * and "a profile exists but found nothing to cast" into the same `true`, which made every legacy
 * fallback below it (taunt/interrupt/AoE/burst/old rotation/generic offensive) permanently
 * unreachable for any class that has a profile at all. NoAction is the one new, meaningful case.
 */

#ifndef COA_PLAYERBOTS_COMBAT_RESULT_H
#define COA_PLAYERBOTS_COMBAT_RESULT_H

namespace BotAI
{
    enum class CombatResult
    {
        Cast,     // Successfully cast a profile ability this tick -- consume the tick.
        Busy,     // On the shared reaction gate, mid-cast, or waiting out a cast just started --
                  // also consume the tick (this is not "nothing to do," it's "already doing it").
        NoAction, // A profile exists but found nothing castable right now (or no profile exists
                  // at all for this class/spec/role) -- caller should fall through to the next
                  // combat layer instead of treating this as a handled tick.
    };
}

#endif // COA_PLAYERBOTS_COMBAT_RESULT_H
