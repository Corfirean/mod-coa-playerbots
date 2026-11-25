/*
 * mod-coa-playerbots -- dungeon automation
 *
 * Activation predicates shared by several dungeons' event rows (the imported rows name them).
 */

#include "dungeon/DungeonEventTables.h"
#include "dungeon/DungeonRuntime.h"

// Due while the room of the next room-aggro boss (Scarlet Cathedral, Scholomance, ...) still holds hostile creatures that should go first.
bool DcRoomAggroPreClearCondition(Player* bot, AiObjectContext* /*context*/)
{
    return DungeonRuntime::RoomTrashRemaining(bot);
}
