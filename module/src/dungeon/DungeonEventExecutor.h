/*
 * mod-coa-playerbots -- dungeon automation
 *
 * Runs the scripted events of a dungeon (pull a lever, ring a gong, talk to an NPC, fire a cannon, free a prisoner...) on the group's tank,
 * step by step. The events are the imported data rows (dungeon/DungeonEventRegistry.h); this is the engine that performs them with our
 * movement layer. One run per group; a persistent event keeps its place across fights, any other starts over when it was not driven for
 * a moment.
 */

#ifndef COA_PLAYERBOTS_DUNGEON_EVENT_EXECUTOR_H
#define COA_PLAYERBOTS_DUNGEON_EVENT_EXECUTOR_H

#include "ObjectGuid.h"
#include "dungeon/DcCompat.h"
#include "dungeon/DungeonEventRegistry.h"

class Group;
class Player;

enum class EventResult : uint8
{
    Running,  // still working: call again next tick
    Done,     // every step finished
    Blocked,  // a step timed out or cannot be done unaided
};

namespace DungeonEventExecutor
{
    // Performs the event for the group; call once per tick while it is due and the party is out of combat (a fight in between is fine for a
    // persistent event).
    EventResult Drive(Player* tank, Group* group, DungeonEvent const& ev);

    // Whether the group is in the middle of this event (driven within the last few seconds).
    bool Active(ObjectGuid groupKey, uint32 mapId, uint32 eventId);

    // Fills in where the group's run stands (for hooks that read the step's start time). False when there is none.
    bool Progress(ObjectGuid groupKey, DungeonEventProgress& out);

    // The current step index of the group's run (for logs), or -1 when none.
    int32 CurrentStep(ObjectGuid groupKey);

    // Drops the group's run and the latches of conditional events.
    void Forget(ObjectGuid groupKey);

    // A conditional event that has finished is not offered again in this instance (unless it is repeatable).
    bool IsLatched(uint32 instanceId, uint32 mapId, uint32 eventId);
    void Latch(uint32 instanceId, uint32 mapId, uint32 eventId);
}

#endif
