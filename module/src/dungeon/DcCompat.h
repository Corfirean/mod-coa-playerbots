/*
 * mod-coa-playerbots -- dungeon automation
 *
 * The small part of the engine the imported dungeon data (tools/dungeon-import/import_dc.py) was written against, re-created on top of our
 * own engine: the context object the event predicates and hooks receive, the run-state throttles, a few targeting helpers. The data files
 * compile unchanged against this; anything bigger than this (raid musters, flight legs, the chess driver...) stays stubbed by the importer.
 */

#ifndef COA_PLAYERBOTS_DUNGEON_DC_COMPAT_H
#define COA_PLAYERBOTS_DUNGEON_DC_COMPAT_H

#include "Common.h"
#include "Define.h"
#include "ObjectGuid.h"
#include <functional>
#include <optional>
#include <unordered_map>

class Player;
class Creature;
class GameObject;
class Map;
class Unit;
class InstanceScript;
class PlayerbotAI;
struct DungeonBossInfo;

// Outcome of a Custom-step hook (see dungeon/DungeonHooks.h).
enum class ObjectiveArriveResult
{
    Done,     // satisfied: the step is finished
    Running,  // still working: call again next tick
    Blocked,  // cannot complete unaided
    Restart,  // the encounter reset behind the party: rewind the event to its first step
};

// Where the event executor stands for the group (what Custom hooks read of it).
struct DungeonEventProgress
{
    uint32 eventId{0};
    uint32 stepIndex{0};
    uint32 stepStartMs{0};
    uint32 attempts{0};
    uint32 lastDriveMs{0};
    uint32 instanceId{0};
};

namespace DcKey
{
    enum Key : int
    {
        NextDungeonBoss,
        EventProgress,
    };
}

// The context object handed to predicates and hooks. It knows the bot it was made for and answers the few "values" the data asks for.
class AiObjectContext
{
public:
    explicit AiObjectContext(Player* bot = nullptr) : _bot(bot) { }
    Player* Bot() const { return _bot; }

    template <typename T>
    struct Value
    {
        T value{};
        T& Get() { return value; }
    };
    template <typename T>
    struct Value<T&>
    {
        T* ptr{nullptr};
        T& Get() { return *ptr; }
    };

    // Defined for the value types the data uses (see DungeonCompat.cpp).
    template <typename T>
    Value<T>* GetValue(int key);

private:
    Player* _bot;
};

template <>
AiObjectContext::Value<std::optional<DungeonBossInfo>>* AiObjectContext::GetValue<std::optional<DungeonBossInfo>>(int key);
template <>
AiObjectContext::Value<DungeonEventProgress&>* AiObjectContext::GetValue<DungeonEventProgress&>(int key);

namespace DcPoolExit
{
    struct Pool;
}

// Per-bot throttle slots (a slot answers "was this done within the last N ms", and re-arms itself when it was not).
enum class DcThrottle : int
{
    BrdAleTurnIn,
    BrdEventLog,
    BrdKegUse,
    BrdTorchNudge,
    BrdTorchWarn,
    UbrsAltarUse,
    UbrsDoorArm,
    UbrsDoorClick,
    UbrsEventLog,
    UbrsHallRearm,
    UbrsHallRestart,
    UbrsStadiumFire,
    Count,
};

struct DcRunState
{
    bool Throttled(DcThrottle slot, uint32 windowMs);
    void SetTestExtra(char const*, uint32) { }

    uint32 throttleMs[static_cast<int>(DcThrottle::Count)]{};
};

namespace DcRun
{
    DcRunState& Of(AiObjectContext* context);
}

namespace DcTargeting
{
    InstanceScript* GetInstanceScript(Player* bot);
    // A living creature of this entry anywhere on the bot's map (a boss that walks a long lap), else null.
    Creature* FindLiveCreatureOnMap(Player* bot, uint32 entry);
    Creature* GetLiveBoss(Player* bot, AiObjectContext* context, uint32 entry);
}

// Hook tables of the imported dungeons (the Register<Dungeon>Hooks appenders add their rows here).
class ObjectiveHookRegistry
{
public:
    using Hook = std::function<ObjectiveArriveResult(Player*, AiObjectContext*, DungeonBossInfo const&)>;
    using HookTable = std::unordered_map<uint32, Hook>;
    static void AddHook(HookTable& table, uint32 id, Hook hook);
};

// Hook id of the generic raid muster (kept for the imported Karazhan rows; no implementation here).
inline constexpr uint32 DC_HOOK_RAID_MUSTER = 40;

// Ids the imported rows name from helper files that are not imported.
namespace DcStratTrapAlley
{
    constexpr uint32 MAP_STRATHOLME = 329;
    constexpr uint32 EVENT_PIT_RESCUE = 7;
    constexpr uint32 HOOK_PIT_RESCUE = 64;
    constexpr uint32 NPC_THE_UNFORGIVEN = 10516;
}

namespace DcKaraLibraryPit
{
    constexpr uint32 MAP_KARAZHAN = 532;
    constexpr uint32 HOOK_PIT_RESCUE = 65;
}

#endif
