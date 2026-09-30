/*
 * mod-coa-playerbots
 *
 * Real encounter-completion tracking and kill order, sourced from the same DungeonEncounter/DBC
 * data the client and core already track -- no bot-side "did we kill this yet" bookkeeping needed
 * for any boss covered here. Replaces guessing a DBC entry directly from a creature entry: the
 * correct lookup goes through ObjectMgr::GetDungeonEncounterList(mapId, difficulty), which returns
 * each DungeonEncounter's real creditEntry (the creature/GO that actually credits the kill) and
 * its DBC encounterIndex (the GetCompletedEncounterMask() bit).
 *
 * Not every boss has a DungeonEncounter row (some are pure trash-flagged CREATURE_FLAG_EXTRA_
 * DUNGEON_BOSS with no formal encounter, or content this server added without one) -- callers
 * should fall back to their own bookkeeping (BotMgr's existing _clearedBosses map) for any
 * creature entry HasEncounterData reports false for, not assume every dungeon boss is covered.
 */

#ifndef COA_PLAYERBOTS_BOT_DUNGEON_ENCOUNTERS_H
#define COA_PLAYERBOTS_BOT_DUNGEON_ENCOUNTERS_H

#include "Define.h"

class Map;

namespace BotDungeonEncounters
{
    // Whether `creatureEntry` has a real DungeonEncounter row for `map`'s id/difficulty at all --
    // lets a caller decide whether to trust IsEncounterCompleted's answer or fall back to its own
    // bookkeeping for a boss this data simply doesn't cover.
    bool HasEncounterData(Map* map, uint32 creatureEntry);

    // True only when `creatureEntry` maps to a real encounter AND that encounter's bit is already
    // set in the instance's own InstanceScript::GetCompletedEncounterMask() -- the same signal the
    // client itself uses. False both for "not a tracked encounter at all" and "tracked but not
    // done yet"; use HasEncounterData first to tell those two apart.
    bool IsEncounterCompleted(Map* map, uint32 creatureEntry);

    // The DBC encounterIndex for `creatureEntry`, or -1 if it has no DungeonEncounter row. The
    // natural kill-order key: dungeons where the walking route backtracks or doesn't match
    // spawn-proximity order need this instead of raw nearest-distance to route correctly.
    int32 GetEncounterOrderIndex(Map* map, uint32 creatureEntry);
}

#endif // COA_PLAYERBOTS_BOT_DUNGEON_ENCOUNTERS_H
