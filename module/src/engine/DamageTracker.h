/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: DamageTracker
 *
 * Cheap, rolling "how fast is this unit's health dropping right now" estimate -- item 10/#19 of
 * the combat-engine rework explicitly asks for exactly this: no real damage-prediction
 * simulation, just a previous-HP snapshot compared against the current one. Shared by
 * HealEvaluator (heal urgency needs "how much danger is this ally in") and ActionEvaluator's
 * DefensiveCD scoring (does the bot's own current situation actually justify a defensive
 * cooldown, not just "is HP below a fixed threshold").
 */

#ifndef COA_PLAYERBOTS_DAMAGE_TRACKER_H
#define COA_PLAYERBOTS_DAMAGE_TRACKER_H

#include "Define.h"
#include "ObjectGuid.h"

class Unit;

namespace BotAI
{
    class DamageTracker
    {
    public:
        // Samples `unit`'s current health, compares it against the last sample taken for this
        // guid, and returns a smoothed incoming-damage-per-second estimate (0 if health rose or
        // this is the first sample). Cheap -- one hash-map lookup/update, no scanning. Meant to
        // be called at most once per decision tick per unit of interest (a heal-urgency pass over
        // the group, or a bot checking its own situation), not every world tick for every unit.
        static float SampleIncomingDps(Unit* unit);

        // Drops the stored sample for this guid -- called on despawn/death so the map doesn't
        // grow across repeated spawn/despawn cycles, and so a freshly-respawned unit doesn't
        // read a stale huge "incoming dps" from its death.
        static void Forget(ObjectGuid guid);
    };
}

#endif // COA_PLAYERBOTS_DAMAGE_TRACKER_H
