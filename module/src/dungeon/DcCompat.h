/*
 * mod-coa-playerbots -- dungeon automation
 *
 * The few types the imported dungeon data (tools/dungeon-import/import_dc.py) still names from the engine it came from. Our engine does not have
 * them; the data only ever passes them around, so empty stand-ins are enough.
 */

#ifndef COA_PLAYERBOTS_DUNGEON_DC_COMPAT_H
#define COA_PLAYERBOTS_DUNGEON_DC_COMPAT_H

#include "Common.h"
#include "Define.h"

class Player;
class Creature;
class GameObject;
class Map;
class Unit;
class PlayerbotAI;

// Placeholder for the engine context the imported predicates are called with; always passed as nullptr.
class AiObjectContext
{
};

// Outcome of a Custom-step hook (see dungeon/DungeonHooks.h).
enum class ObjectiveArriveResult
{
    Done,     // satisfied: the step is finished
    Running,  // still working: call again next tick
    Blocked,  // cannot complete unaided
    Restart,  // the encounter reset behind the party: rewind the event to its first step
};

namespace DcPoolExit
{
    struct Pool;
}

// Stand-ins for engine run-state types that only appear in the signatures of stubbed helpers.
struct DcRunState
{
};
enum class DcThrottle : int
{
};

// Hook id of the generic raid muster (kept for the imported Karazhan rows; no implementation here).
inline constexpr uint32 DC_HOOK_RAID_MUSTER = 40;

// Stratholme trap-alley pit rescue: only the ids the event row names.
namespace DcStratTrapAlley
{
    constexpr uint32 EVENT_PIT_RESCUE = 7;
    constexpr uint32 HOOK_PIT_RESCUE = 64;
    constexpr uint32 NPC_THE_UNFORGIVEN = 10516;
}

#endif
