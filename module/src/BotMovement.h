#ifndef COA_PLAYERBOTS_BOT_MOVEMENT_H
#define COA_PLAYERBOTS_BOT_MOVEMENT_H

#include "BotMovementPrimitives.h"
#include "MotionMaster.h"
#include <optional>
#include <string>

class Player;
class Unit;

struct MovementStats
{
    uint64 issued = 0;          // MovePoint calls actually made
    uint64 redundantSkipped = 0; // MoveTo/Follow/Chase calls that asked for the walk already running
    uint64 stuckEvents = 0;     // progress stalls that started a recovery
    uint64 repaths = 0;
    uint64 detours = 0;
    uint64 backtracks = 0;
    uint64 gaveUp = 0;          // Navigate returned Stuck
    uint64 mountAttempts = 0;
    uint64 mountSuccesses = 0;
    uint64 dismounts = 0;
};

namespace BotMovement
{
    // =========================================================================
    // Controlled Movement Guard
    // =========================================================================

    // Checks whether the bot is undergoing externally-controlled motion (knockback, fear,
    // taxi, flight, jump, stun, vehicle). Bot locomotion must NOT interrupt these.
    bool IsExternallyControlled(Player const* bot);

    // =========================================================================
    // Core Arbitrated Locomotion API (Token-Safe)
    // =========================================================================

    // Move to a specific point with optional navmesh path generation.
    // Returns a generation token. forceDestination is defaulted to false for safety.
    LocomotionToken MoveTo(Player* bot, MoveOwner owner, float x, float y, float z, bool forceDestination = false);

    // Goal-directed multi-leg travel with progress tracking, navmesh-aware detour, and backtrack recovery.
    NavStatus Navigate(Player* bot, MoveOwner owner, uint64 goalId, float x, float y, float z, float acceptRadius, LocomotionToken* outToken = nullptr);

    // Arbitrated following: idempotent with deadzone hysteresis to eliminate generator ping-pong.
    LocomotionToken Follow(Player* bot, MoveOwner owner, Unit* target, float dist = 2.0f, float angle = 0.0f);

    // Arbitrated combat chasing: idempotent range banding without spline jitter.
    LocomotionToken Chase(Player* bot, MoveOwner owner, Unit* target, float minRange = 0.0f, float maxRange = 0.0f, float angle = 0.0f);
    inline LocomotionToken Chase(Player* bot, MoveOwner owner, Unit* target, ChaseRange const& range, float angle = 0.0f)
    {
        return Chase(bot, owner, target, range.MinRange, range.MaxRange, angle);
    }

    // Tactical relative movement (stepping towards or backing away from target)
    LocomotionToken MoveForwards(Player* bot, MoveOwner owner, Unit* target, float dist);
    LocomotionToken MoveBackwards(Player* bot, MoveOwner owner, Unit* target, float dist);

    // Holds position for a given duration
    LocomotionToken Hold(Player* bot, MoveOwner owner, uint32 durationMs);

    // =========================================================================
    // Cancellation & Cleanup
    // =========================================================================

    // Token-safe lifecycle cancellation: cleans up ONLY if active.commandId == token.commandId && active.owner == token.owner.
    // Stale tokens from previous commands or other owners are safely ignored.
    bool Release(Player* bot, LocomotionToken const& token);
    bool Stop(Player* bot, LocomotionToken const& token);

    // Administrative intentional force cancellation: releases or stops whatever command currently belongs to owner.
    void ForceStopOwner(Player* bot, MoveOwner owner);
    void ForceReleaseOwner(Player* bot, MoveOwner owner);

    // Backwards-compatible aliases for administrative force cancellation
    inline void Stop(Player* bot, MoveOwner owner) { ForceStopOwner(bot, owner); }
    inline void Release(Player* bot, MoveOwner owner) { ForceReleaseOwner(bot, owner); }

    // =========================================================================
    // Centralized Mount Controller
    // =========================================================================

    // Requests mounting with distance hysteresis check (>90 yards) and full production spell selection.
    bool RequestMount(Player* bot, MoveOwner owner, float travelDistance = 0.0f, bool wantFlying = false);

    // Requests dismounting with explicit reason tracking and global 6s remount cooldown.
    void RequestDismount(Player* bot, MoveOwner owner, DismountReason reason);

    // Checks whether the bot can legally mount (level, outdoor, cooldown, not controlled).
    bool CanMount(Player const* bot, float travelDistance = 0.0f);

    // Checks whether the bot is currently mounted.
    bool IsMounted(Player const* bot);

    // Mount state machine queries
    MountState GetMountState(Player const* bot);
    uint32 GetPendingMountSpell(Player const* bot);

    // Production mount spell selector: resolves racial, flying, learned, or wrapper mounts.
    uint32 SelectMountSpell(Player* bot, bool wantFlying = false);

    // Sets the group leader's desired mount state (debounced, avoids flapping).
    void SetLeaderMountPreference(Player* bot, DesiredMountState pref);

    // Resolves completed or failed mount cast
    void ResolveMountCast(Player* bot);

    // Spell cast hooks to protect mount casting and handle interruption
    void OnSpellCastStart(Player* bot, uint32 spellId);
    void OnSpellCastSuccess(Player* bot, uint32 spellId);
    void OnSpellCastInterrupt(Player* bot, uint32 spellId);

    // =========================================================================
    // Watchdog, Backtracking, & State Queries
    // =========================================================================

    // Periodic watchdog update: checks airborne safety, mount state, and movement health.
    void Update(Player* bot, uint32 diff);

    // Attempts to navigate back to a confirmed safe position from recent history.
    bool BacktrackToSafePosition(Player* bot, MoveOwner owner);

    // Read current state of a bot's movement slot
    MoveOwner CurrentOwner(Player* bot);
    MoveMode CurrentMode(Player* bot);
    LocomotionState CurrentState(Player* bot);

    // Returns true if owner can claim the slot without being preempted
    bool CanClaim(Player* bot, MoveOwner owner);

    // Returns a safe value-copy snapshot of the active MovementRequest, or std::nullopt if none active.
    std::optional<MovementRequest> GetRequestSnapshot(ObjectGuid botGuid);

    // Clears any active multi-leg navigate request for a bot
    void ResetRequest(ObjectGuid botGuid);

    // Returns a safe value-copy snapshot of aggregated telemetry counters.
    MovementStats Stats();

    // Diagnostics and command inspection
    std::string Describe(Player* bot);
    std::string DescribeHistory(Player* bot);

    // Cleans up all per-bot movement state when a bot despawns
    void Forget(ObjectGuid botGuid);
}

#endif // COA_PLAYERBOTS_BOT_MOVEMENT_H
