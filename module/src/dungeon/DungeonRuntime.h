/*
 * mod-coa-playerbots -- dungeon automation
 *
 * What the group's tank does between fights in an auto-dungeon run: perform the scripted events that are due, then walk to the next anchor of
 * the roster (a boss, or an objective whose event it performs on arrival). The pulls themselves stay with BotAI::TryAutoPullInInstance.
 */

#ifndef COA_PLAYERBOTS_DUNGEON_RUNTIME_H
#define COA_PLAYERBOTS_DUNGEON_RUNTIME_H

#include "Define.h"
#include <functional>
#include <string>

class Creature;
class Group;
class Map;
class Player;

namespace DungeonRuntime
{
    // Whether the roster-driven dungeon logic is switched on (CoaBots.Dungeon.UseRoster, default on).
    bool Enabled();

    // Whether the map has a roster at all (a map without encounter data falls back to the older boss scan).
    bool HasRoster(Map* map, Player* ref);

    // Every anchor of the roster is done.
    bool IsCleared(Map* map, Player* ref);

    // One step of the run: due events first, then the next anchor. Returns false only when nothing is left to do.
    bool Advance(Player* tank, Group* group, std::function<void(char const*)> const& note);

    // Hostile creatures standing in the room of the next room-aggro boss that should be cleared before it is pulled (for the room-clear event).
    bool RoomTrashRemaining(Player* tank);

    // The events that are driven during a fight (continuous wave encounters such as the Violet Hold). True when one took the tick.
    bool AdvanceInCombat(Player* tank, Group* group);

    // Walks the tank to a creature and attacks it; the ordinary combat logic takes over once it is engaged. False when it cannot get there.
    bool EngageCreature(Player* tank, Creature* target, uint64 goalId);

    // One line for the training probe: the anchor the group is heading for, how far it is, what is left and what is given up on.
    std::string Describe(Player* tank, Group* group);

    // Drops everything the run remembers about this group.
    void Forget(Group* group);
}

#endif
