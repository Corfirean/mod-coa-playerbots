/*
 * mod-coa-playerbots -- dungeon automation
 *
 * The ordered list of things an auto-dungeon group still has to do on its map: the bosses of the DungeonEncounter data, corrected by the
 * imported roster patches (wrong or event-locked bosses removed, real ones added, travel objectives such as "fire the cannon" slotted in),
 * narrowed to the wing the group stands in on maps that hold several disconnected parts (Scarlet Monastery, Dire Maul, Blackrock Spire/Depths,
 * Maraudon), and cut down to what is not done yet.
 */

#ifndef COA_PLAYERBOTS_DUNGEON_ROSTER_H
#define COA_PLAYERBOTS_DUNGEON_ROSTER_H

#include "dungeon/DungeonBossInfo.h"
#include "ObjectGuid.h"
#include <vector>

class Map;
class Player;

namespace DungeonRoster
{
    // Every anchor of the map in clear order, patches and wing filter applied, done ones included.
    std::vector<DungeonBossInfo> Full(Map* map, Player* ref);

    // Whether an anchor is finished: a boss by its kill bit / boss state / instance data, an objective by its latch (set when its event finished),
    // by the creature that its completion spawns, or by the same instance signals.
    bool IsDone(Map* map, DungeonBossInfo const& anchor);

    // Remembers that a travel objective was completed in this instance (a re-entered instance keeps it, a fresh one starts clean).
    void MarkObjectiveDone(Map* map, uint32 entry);

    // The anchors still to do, in clear order, without those the group gave up on lately (see MarkUnreachable). Never empty when the full list has
    // anything left: if everything left is marked unreachable, the marks are ignored so the group keeps trying.
    std::vector<DungeonBossInfo> Remaining(Map* map, Player* ref, ObjectGuid groupKey);

    // The anchor the group should head for: the one it already committed to while it is still on the list, else the first of Remaining.
    // Returns false when nothing is left.
    bool PickTarget(Map* map, Player* ref, ObjectGuid groupKey, DungeonBossInfo& out);

    // The anchor PickTarget would return, without committing the group to it.
    bool Peek(Map* map, Player* ref, ObjectGuid groupKey, DungeonBossInfo& out);

    // The position to walk to for an anchor: for a boss that may stand at one of several spawn points, the one where it is alive now.
    void ResolvePosition(Map* map, DungeonBossInfo const& anchor, float& x, float& y, float& z);

    // The group could not get to this anchor (no path, no progress): leave it out for a while and head for the next one.
    void MarkUnreachable(ObjectGuid groupKey, uint32 entry, uint32 forMs);

    // Forget the group's commitment and marks (called when the auto-dungeon mode ends).
    void Forget(ObjectGuid groupKey);

    // Short description of the list for logs: "Name (entry) done/todo, ...".
    std::string Describe(Map* map, Player* ref);
}

#endif
