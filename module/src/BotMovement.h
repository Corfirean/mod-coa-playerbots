/*
 * mod-coa-playerbots
 *
 * Production-grade unified locomotion and navigation arbitration layer for all bot subsystems.
 *
 * Fail Safe, Never Cheat Through Geometry:
 * Under this architecture, NO gameplay AI subsystem (combat, avoidance, follow, flee,
 * ambient, questing, dungeon, bg, etc.) may ever call MotionMaster directly, nor may any
 * subsystem issue direct MoveSpline or RemoveAurasByType(SPELL_AURA_MOUNTED).
 *
 * All movement intents (Navigate, MoveTo, Follow, Chase, MoveForwards, MoveBackwards, Hold, Stop)
 * and mount operations flow through this centralized arbitration layer.
 */

#ifndef COA_PLAYERBOTS_BOT_MOVEMENT_H
#define COA_PLAYERBOTS_BOT_MOVEMENT_H

#include "BotNavProgress.h"
#include "Define.h"
#include "MotionMaster.h"
#include "ObjectGuid.h"
#include <string>

class Player;
class Unit;

// Who a bot's current locomotion belongs to.
// Strictly ordered low to high priority: higher priority preempts lower priority claims.
enum class MoveOwner : uint8
{
    None = 0,
    Ambient = 1,
    Grind = 2,
    Gather = 3,
    Fish = 4,
    Travel = 5,
    Quest = 6,
    Loot = 7,
    Corpse = 8,
    AutoDungeon = 9,
    Battleground = 10,
    Combat = 11,
    Avoidance = 12,
};

enum class MoveMode : uint8
{
    Idle,
    Point,
    Navigate,
    Follow,
    Chase,
    MaintainRange,
    Retreat,
    MoveForwards,
    MoveBackwards,
    Hold,
    Stop,
};

enum class LocomotionState : uint8
{
    Idle,
    Planning,
    Moving,
    Following,
    Chasing,
    Holding,
    CastingHold,
    Blocked,
    Recovering,
    Arrived,
    Failed,
};

enum class NavStatus : uint8
{
    Moving,   // on the way (or waiting to be allowed to start the next leg)
    Arrived,  // within the acceptance radius; point movement released
    Blocked,  // a higher-priority owner is moving the bot right now
    Stuck,    // the recovery ladder is exhausted; the caller must pick something else
};

enum class DismountReason : uint8
{
    Combat,
    Arrival,
    CastInterrupted,
    ActionForbidden,
    Obstacle,
    Taxi,
    Manual,
};

enum class MountState : uint8
{
    Grounded,
    MountRequested,
    MountCasting,
    MountedGround,
    MountedFlying,
    DismountRequested,
    Cooldown,
};

// Command generation token to prevent stale cancellations and ensure deterministic preemption.
struct LocomotionToken
{
    uint64 commandId = 0;
    MoveOwner owner = MoveOwner::None;
    MoveMode mode = MoveMode::Idle;
};

// 32-entry circular ring buffer record for debugging and telemetry.
struct TransitionRecord
{
    uint32 timeMs = 0;
    LocomotionState oldState = LocomotionState::Idle;
    LocomotionState newState = LocomotionState::Idle;
    MoveOwner owner = MoveOwner::None;
    MoveMode mode = MoveMode::Idle;
    char reason[32] = {0};
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct SafePosition
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32 timeMs = 0;
};

// The persistent half of a Navigate() call: what the bot is walking to and how it is going.
struct MovementRequest
{
    MoveOwner owner = MoveOwner::None;
    uint64 goalId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float acceptRadius = 0.0f;
    uint32 startedAt = 0;
    uint32 lastCallAt = 0;
    NavProgress progress;         // best distances, last progress, recovery stage (BotNavProgress.h)
    uint32 legs = 0;
    uint32 lastIssueAt = 0;
    float legX = 0.0f;
    float legY = 0.0f;
    bool detour = false;
    float detourX = 0.0f;
    float detourY = 0.0f;
    float detourZ = 0.0f;
};

struct MovementStats
{
    uint64 issued = 0;          // MovePoint calls actually made
    uint64 redundantSkipped = 0; // MoveTo/Follow/Chase calls that asked for the walk already running
    uint64 stuckEvents = 0;     // progress stalls that started a recovery
    uint64 repaths = 0;
    uint64 detours = 0;
    uint64 gaveUp = 0;          // Navigate returned Stuck
    uint64 mountAttempts = 0;
    uint64 mountSuccesses = 0;
    uint64 dismounts = 0;
};

namespace BotMovement
{
    // =========================================================================
    // Core Arbitrated Locomotion API
    // =========================================================================

    // Move to a specific point with optional navmesh path generation.
    // forceDestination is defaulted to false for safety (fail safe, never cheat geometry).
    bool MoveTo(Player* bot, MoveOwner owner, float x, float y, float z, bool forceDestination = false);

    // Goal-directed multi-leg travel with progress tracking and navmesh-aware detour recovery.
    NavStatus Navigate(Player* bot, MoveOwner owner, uint64 goalId, float x, float y, float z, float acceptRadius);

    // Arbitrated following: idempotent with deadzone hysteresis to eliminate generator ping-pong.
    bool Follow(Player* bot, MoveOwner owner, Unit* target, float dist = 2.0f, float angle = 0.0f);

    // Arbitrated combat chasing: idempotent range banding without spline jitter.
    bool Chase(Player* bot, MoveOwner owner, Unit* target, float minRange = 0.0f, float maxRange = 0.0f, float angle = 0.0f);
    inline bool Chase(Player* bot, MoveOwner owner, Unit* target, ChaseRange const& range, float angle = 0.0f)
    {
        return Chase(bot, owner, target, range.MinRange, range.MaxRange, angle);
    }

    // Tactical relative movement (stepping towards or backing away from target)
    bool MoveForwards(Player* bot, MoveOwner owner, Unit* target, float dist);
    bool MoveBackwards(Player* bot, MoveOwner owner, Unit* target, float dist);

    // Holds position for a given duration
    bool Hold(Player* bot, MoveOwner owner, uint32 durationMs);

    // Safe stop: only stops if owner holds the active claim or outranks it.
    void Stop(Player* bot, MoveOwner owner);

    // Releases ownership without forcing abrupt stop if caller simply finished its duty.
    void Release(Player* bot, MoveOwner owner);

    // =========================================================================
    // Centralized Mount Controller
    // =========================================================================

    // Requests mounting with distance hysteresis check (>90 yards).
    bool RequestMount(Player* bot, MoveOwner owner, float travelDistance = 0.0f);

    // Requests dismounting with explicit reason tracking and remount cooldown.
    void RequestDismount(Player* bot, MoveOwner owner, DismountReason reason);

    // Checks whether the bot can legally mount (level, outdoor, spell, cooldown).
    bool CanMount(Player const* bot, float travelDistance = 0.0f);

    // Checks whether the bot is currently mounted.
    bool IsMounted(Player const* bot);

    // Mount state machine queries
    MountState GetMountState(Player const* bot);

    // Spell cast hooks to protect mount casting and handle interruption
    void OnSpellCastStart(Player* bot, uint32 spellId);
    void OnSpellCastSuccess(Player* bot, uint32 spellId);
    void OnSpellCastInterrupt(Player* bot, uint32 spellId);

    // =========================================================================
    // Watchdog, Backtracking, & State Queries
    // =========================================================================

    // Periodic watchdog update: checks airborne safety, mount state, and movement health.
    void Update(Player* bot, uint32 diff);

    // Attempts to navigate back to the bot's last known safe position.
    bool BacktrackToSafePosition(Player* bot, MoveOwner owner);

    // State inspection
    MoveOwner CurrentOwner(Player* bot);
    MoveMode CurrentMode(Player* bot);
    LocomotionState CurrentState(Player* bot);
    bool CanClaim(Player* bot, MoveOwner owner);
    bool IsCommandActive(Player* bot, MoveOwner owner, uint64 commandId);
    uint64 GetActiveCommandId(Player* bot);

    // Request lifecycle
    void ResetRequest(ObjectGuid botGuid);
    MovementRequest const* GetRequest(ObjectGuid botGuid);
    void Forget(ObjectGuid botGuid);

    // Telemetry & Formatting
    char const* OwnerName(MoveOwner owner);
    char const* ModeName(MoveMode mode);
    char const* StateName(LocomotionState state);
    char const* MountStateName(MountState state);
    char const* DismountReasonName(DismountReason reason);

    std::string Describe(Player* bot);
    std::string DescribeHistory(Player* bot);
    MovementStats const& Stats();
}

#endif // COA_PLAYERBOTS_BOT_MOVEMENT_H
