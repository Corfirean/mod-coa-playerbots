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

#include <unordered_map>

class Player;

namespace DungeonHooks
{
    // The hooks of the wave dungeons (DungeonHooksWaves.cpp): Violet Hold, Culling of Stratholme, Halls of Lightning.
    void RegisterWaveHooks(std::unordered_map<uint32, ObjectiveArriveResult (*)(Player*)>& table);

    // Runs hook `hookId` for the group's tank. An id without an implementation reads Blocked (and is logged once).
    ObjectiveArriveResult Run(uint32 hookId, Player* tank, DungeonBossInfo const* info);

    // Whether the id has an implementation.
    bool Has(uint32 hookId);

    // Makes sure the bot carries the item (a quest item the bots never looted). When the bags are full the least valuable loose item is destroyed
    // to make room (never a quest item, a bag or a consumable unless nothing else is left). Soak test: a tank with full bags stood at the first
    // crate of the Culling of Stratholme for good, because the Arcane Disruptor had no room.
    bool GiveItem(Player* bot, uint32 itemId);
}

#endif
