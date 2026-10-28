/*
 * mod-coa-playerbots -- dungeon automation
 *
 * The "Custom" steps of the dungeon events: pieces of dungeon-specific work that a plain step list cannot express (firing the Deadmines cannon,
 * waking a boss that only a client area trigger would wake, poking Warden Mellichar...). Each hook is identified by the numeric id the imported
 * event rows carry; the ids are the upstream ones (tools/dungeon-import), the implementations are ours.
 *
 * Ported from the objective hooks of jrad7/mod-dungeon-clear (AGPL-3.0-or-later), adapted to our engine.
 */

#ifndef COA_PLAYERBOTS_DUNGEON_HOOKS_H
#define COA_PLAYERBOTS_DUNGEON_HOOKS_H

#include "dungeon/DcCompat.h"
#include "dungeon/DungeonBossInfo.h"

class Player;

namespace DungeonHooks
{
    // Runs hook `hookId` for the group's tank. An id without an implementation reads Blocked (and is logged once).
    ObjectiveArriveResult Run(uint32 hookId, Player* tank, DungeonBossInfo const* info);

    // Whether the id has an implementation.
    bool Has(uint32 hookId);
}

#endif
