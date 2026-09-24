/*
 * mod-coa-playerbots
 *
 * The open-world task model. A WorldTask is what a bot is doing *and why*, kept across ticks:
 * which quest, which objective, how far along it is, which place the bot chose for it, which live
 * mob it has claimed, what phase of the job it is in, and what went wrong last time. The old
 * quest tracker kept only a coordinate ("walk to this Position"), so on arrival nothing knew what
 * the trip had been for -- the bot finished its "task" by standing on a spawn point.
 *
 * Every field here survives from one tick to the next. The planner decides rarely and on purpose;
 * the executor advances the task a phase at a time; nothing re-decides everything from scratch
 * every tick.
 */

#ifndef COA_PLAYERBOTS_WORLD_TASK_H
#define COA_PLAYERBOTS_WORLD_TASK_H

#include "Define.h"
#include "ObjectGuid.h"
#include <vector>

// The bot's longer-term intention -- what kind of player it is being right now.
enum class WorldGoal : uint8
{
    None,
    Questing,
    Gathering,
    Fishing,
    Grinding,
    Exploring,
    Traveling,
    Break,
};

enum class WorldTaskType : uint8
{
    None,
    QuestAccept,
    QuestObjective,
    QuestTurnIn,
    Gather,
    Grind,
    Explore,
    Vendor,
    Repair,
    Travel,
    Social,
};

enum class TaskPhase : uint8
{
    Planning,
    TravelToArea,
    Search,
    Approach,
    Execute,
    Combat,
    Loot,
    Verify,
    Recover,
    Completed,
    Failed,
};

enum class ObjectiveType : uint8
{
    None,
    KillCreature,     // RequiredNpcOrGo > 0, hostile target (kill credit, incl. KillCredit proxies)
    UseGameObject,    // RequiredNpcOrGo < 0, a goober/button the bot clicks
    CollectItem,      // RequiredItemId dropped by creatures (kill + loot)
    LootGameObject,   // RequiredItemId held by a chest-type object (open + loot)
    UseItemSource,    // RequiredItemId produced by clicking an object (goober spell creates it)
    Explore,          // an area trigger the bot must walk into
    CastOnCreature,   // QUEST_SPECIAL_FLAGS_CAST: use a quest item on a creature
    CastOnGameObject, // QUEST_SPECIAL_FLAGS_CAST: use a quest item on an object
    TalkTo,           // friendly NPC credit via gossip/script -- not reliably automatable
    Escort,           // script-driven escort/event -- not automatable yet
    Other,
};

enum class FailureReason : uint8
{
    None,
    Timeout,
    Unreachable,
    NoTargets,
    TargetLost,
    Evaded,
    TargetTaken,
    CastFailed,
    InteractFailed,
    Unsupported,
    QuestGone,
    MapChanged,
    Died,
    Replaced,
    Suspended,
    NoProgress,
    Relocated,
};

enum class SuspendReason : uint8
{
    None,
    Grouped,
    ManualCommand,
    Battleground,
    Instance,
    Disabled,
};

// An objective of a quest, by position in that quest's QuestKnowledge::objectives list.
struct ObjectiveRef
{
    uint32 questId = 0;
    uint8 objectiveIndex = 0;

    bool operator==(ObjectiveRef const& other) const
    {
        return questId == other.questId && objectiveIndex == other.objectiveIndex;
    }
};

struct QuestTaskData
{
    uint32 questId = 0;
    uint8 objectiveIndex = 0;
    ObjectiveType objectiveType = ObjectiveType::None;

    uint32 targetEntry = 0;       // primary creature/object entry (0 when several sources)
    uint32 requiredItemId = 0;
    uint32 requiredCount = 0;
    uint32 currentCount = 0;

    uint32 selectedClusterId = 0;
    ObjectGuid targetGuid;        // the live creature/object currently claimed
    uint32 areaTriggerId = 0;     // explore objectives

    uint32 retryCount = 0;        // failed attempts on this objective (unreachable, evaded...)
    uint32 badClusters = 0;       // areas given up on for this objective
    uint32 attempts = 0;          // engagements / uses started
    uint32 dryAttempts = 0;       // finished attempts that advanced nothing
    uint32 progressMark = 0;      // summed progress when the current attempt started
    uint8 deaths = 0;             // deaths while on this task
    bool useItemMode = false;     // killing gives no credit: use the quest item instead
    bool requireDeadTarget = false; // the quest item only works on corpses
    FailureReason lastFailure = FailureReason::None;

    // Other objectives this trip can progress at the same time ("kill kobolds" and "loot candles
    // from kobolds" in one camp). Their targets join the live search; the task itself completes
    // when the primary objective does.
    std::vector<ObjectiveRef> bundle;
};

struct WorldTask
{
    WorldTaskType type = WorldTaskType::None;
    TaskPhase phase = TaskPhase::Planning;
    uint32 id = 0;                // unique per bot; movement goal ids derive from it

    uint32 mapId = 0;
    float x = 0.0f;               // the area (or NPC spawn) the task happens at
    float y = 0.0f;
    float z = 0.0f;
    float areaRadius = 0.0f;

    uint32 startedMs = 0;
    uint32 phaseStartedMs = 0;
    uint32 deadlineMs = 0;        // hard ceiling for the whole task
    uint32 nextScanMs = 0;        // next live-target scan
    uint32 waitUntilMs = 0;       // a short human pause (linger after a kill, reaction delay)

    QuestTaskData quest;

    // Quest giver / ender for accept and turn-in tasks.
    uint32 npcEntry = 0;
    uint32 npcSpawnId = 0;
    bool npcIsGameObject = false;
    ObjectGuid npcGuid;

    // Search-phase wandering between spawn points of the chosen area.
    uint32 wanderIndex = 0;
    bool wandering = false;
    uint32 corpsesSeen = 0;       // wanted corpses around at the last scan: respawn is coming

    // Stopped by something outside the task (an ambient errand, a group, a manual command). The
    // task's clocks stand still from pausedAtMs until Resume(): an interruption that is not the
    // task's own failure must never look like time spent searching, approaching or travelling.
    bool paused = false;
    uint32 pausedAtMs = 0;

    float utility = 0.0f;
    char const* why = "";         // short planner reason, for .botcmd brain

    bool IsValid() const { return type != WorldTaskType::None; }

    // Moves every armed clock forward by `ms`: the task carries on as if those milliseconds never
    // happened. For external interruptions (see Pause/Resume).
    void ShiftClocks(uint32 ms)
    {
        ShiftArmed(startedMs, ms);
        ShiftArmed(phaseStartedMs, ms);
        ShiftArmed(deadlineMs, ms);
        ShiftArmed(nextScanMs, ms);
        ShiftArmed(waitUntilMs, ms);
    }

    // Time the bot spent on the task's own business while the brain was not ticking -- a fight,
    // resting, looting, a corpse run -- counts toward the task as a whole (the deadline is an
    // anti-loop guard) but never toward the phase it interrupted: a 40-second fight with an add is
    // not 40 seconds of failed searching.
    void ShiftPhaseClock(uint32 ms)
    {
        ShiftArmed(phaseStartedMs, ms);
    }

    // The task's own notion of "now": the real time, or the moment it was paused while it is.
    uint32 ClockNow(uint32 now) const
    {
        return paused ? pausedAtMs : now;
    }

    void Pause(uint32 now)
    {
        if (paused)
            return;
        paused = true;
        pausedAtMs = now;
    }

    // Ends a pause, shifting the clocks by its length. Returns that length (0 when not paused).
    uint32 Resume(uint32 now)
    {
        if (!paused)
            return 0;
        uint32 length = now - pausedAtMs;
        paused = false;
        pausedAtMs = 0;
        ShiftClocks(length);
        return length;
    }

    // "Incoming" in the population heatmap means exactly this: the bot is on its way to the task's
    // destination. Once it is there (searching, fighting, talking) it is resident, counted by its
    // presence; while paused it is not heading anywhere for the task at all.
    bool CountsAsIncoming() const
    {
        return IsValid() && !paused && phase == TaskPhase::TravelToArea;
    }

    // Movement goal ids: one per kind of destination so a new target is a new goal (and a new
    // stuck budget), while chasing the same target across ticks stays the same goal.
    uint64 GoalId(uint8 sub) const { return (uint64(id) << 8) | sub; }

private:
    static void ShiftArmed(uint32& clock, uint32 ms)
    {
        if (clock)
            clock += ms;
    }
};

// Goal-id sub-keys for WorldTask::GoalId.
namespace WorldGoalSub
{
    constexpr uint8 Area = 1;
    constexpr uint8 Wander = 2;
    constexpr uint8 Target = 3;
    constexpr uint8 Npc = 4;
    constexpr uint8 Trigger = 5;
    constexpr uint8 Hub = 6;
}

// The slice of a bot's persistent personality the world layer decides with. Filled by BotAI,
// which owns the personality storage.
struct WorldPersona
{
    uint32 seed = 0;
    uint8 questing = 50;
    uint8 gathering = 50;
    uint8 fishing = 25;
    uint8 grinding = 50;
    uint8 patience = 50;
    uint8 sociability = 50;
    // Current half-hour lean from BotAI's SoloIntent (None, Quest, Gather, Fish, Grind, Explore).
    uint8 lean = 0;
};

// What the brain wants BotAI to do with this tick, for the activities that still live in BotAI.cpp
// (gathering, fishing, grinding) and the ambient layer. Exactly one runs per tick, and only the one
// the brain chose -- that is what stops them from competing for the bot.
enum class WorldDirective : uint8
{
    Busy,     // the brain acted this tick; BotAI does nothing else
    Gather,
    Fish,
    Grind,
    Ambient,  // errands, wandering, city life (BotWorldBehavior)
    Idle,
};

char const* WorldGoalName(WorldGoal goal);
char const* WorldTaskTypeName(WorldTaskType type);
char const* TaskPhaseName(TaskPhase phase);
char const* ObjectiveTypeName(ObjectiveType type);
char const* FailureReasonName(FailureReason reason);
char const* WorldDirectiveName(WorldDirective directive);
char const* SuspendReasonName(SuspendReason reason);

#endif // COA_PLAYERBOTS_WORLD_TASK_H
