#ifndef COA_PLAYERBOTS_BOT_MOVEMENT_PRIMITIVES_H
#define COA_PLAYERBOTS_BOT_MOVEMENT_PRIMITIVES_H

#include "BotNavProgress.h"
#include "Define.h"
#include "ObjectGuid.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>

// =============================================================================
// Enums & Core Primitives
// =============================================================================

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
    Count
};

inline char const* MoveOwnerName(MoveOwner owner)
{
    switch (owner)
    {
        case MoveOwner::None:         return "None";
        case MoveOwner::Ambient:      return "Ambient";
        case MoveOwner::Grind:        return "Grind";
        case MoveOwner::Gather:       return "Gather";
        case MoveOwner::Fish:         return "Fish";
        case MoveOwner::Travel:       return "Travel";
        case MoveOwner::Quest:        return "Quest";
        case MoveOwner::Loot:         return "Loot";
        case MoveOwner::Corpse:       return "Corpse";
        case MoveOwner::AutoDungeon:  return "AutoDungeon";
        case MoveOwner::Battleground: return "Battleground";
        case MoveOwner::Combat:       return "Combat";
        case MoveOwner::Avoidance:    return "Avoidance";
        default:                      return "Unknown";
    }
}

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

inline char const* MoveModeName(MoveMode mode)
{
    switch (mode)
    {
        case MoveMode::Idle:          return "Idle";
        case MoveMode::Point:         return "Point";
        case MoveMode::Navigate:      return "Navigate";
        case MoveMode::Follow:        return "Follow";
        case MoveMode::Chase:         return "Chase";
        case MoveMode::MaintainRange: return "MaintainRange";
        case MoveMode::Retreat:       return "Retreat";
        case MoveMode::MoveForwards:  return "MoveForwards";
        case MoveMode::MoveBackwards: return "MoveBackwards";
        case MoveMode::Hold:          return "Hold";
        case MoveMode::Stop:          return "Stop";
        default:                      return "Unknown";
    }
}

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

inline char const* LocomotionStateName(LocomotionState state)
{
    switch (state)
    {
        case LocomotionState::Idle:        return "Idle";
        case LocomotionState::Planning:    return "Planning";
        case LocomotionState::Moving:      return "Moving";
        case LocomotionState::Following:   return "Following";
        case LocomotionState::Chasing:     return "Chasing";
        case LocomotionState::Holding:     return "Holding";
        case LocomotionState::CastingHold: return "CastingHold";
        case LocomotionState::Blocked:     return "Blocked";
        case LocomotionState::Recovering:  return "Recovering";
        case LocomotionState::Arrived:     return "Arrived";
        case LocomotionState::Failed:      return "Failed";
        default:                           return "Unknown";
    }
}

enum class MountState : uint8
{
    Unmounted = 0,
    MountCasting,
    MountedGround,
    MountedFlying,
    DismountRequested,
    Cooldown,
};

inline char const* MountStateName(MountState s)
{
    switch (s)
    {
        case MountState::Unmounted:         return "Unmounted";
        case MountState::MountCasting:       return "MountCasting";
        case MountState::MountedGround:      return "MountedGround";
        case MountState::MountedFlying:      return "MountedFlying";
        case MountState::DismountRequested:  return "DismountRequested";
        case MountState::Cooldown:           return "Cooldown";
        default:                            return "Unknown";
    }
}

enum class DesiredMountState : uint8
{
    None = 0,
    PreferGround,
    PreferFlying,
    PreferUnmounted,
};

enum class NavStatus : uint8
{
    Moving,   // on the way (or waiting to be allowed to start the next leg)
    Arrived,  // within the acceptance radius; point movement released
    Blocked,  // a higher-priority owner is moving the bot right now
    Stuck,    // the recovery ladder is exhausted; the caller must pick something else
};

inline char const* NavStatusName(NavStatus s)
{
    switch (s)
    {
        case NavStatus::Moving:  return "Moving";
        case NavStatus::Arrived: return "Arrived";
        case NavStatus::Blocked: return "Blocked";
        case NavStatus::Stuck:   return "Stuck";
        default:                 return "Unknown";
    }
}

enum class DismountReason : uint8
{
    Manual = 0,
    Combat,
    Arrival,
    CastInterrupted,
    ActionForbidden,
    Obstacle,
    Taxi,
    LeaderState,
    SpellCastStarted,
    IndoorEntered,
    FlightForbidden,
    WaterEntered,
};

inline char const* DismountReasonName(DismountReason r)
{
    switch (r)
    {
        case DismountReason::Manual:            return "Manual";
        case DismountReason::Combat:            return "Combat";
        case DismountReason::Arrival:           return "Arrival";
        case DismountReason::CastInterrupted:   return "CastInterrupted";
        case DismountReason::ActionForbidden:   return "ActionForbidden";
        case DismountReason::Obstacle:          return "Obstacle";
        case DismountReason::Taxi:              return "Taxi";
        case DismountReason::LeaderState:       return "LeaderState";
        case DismountReason::SpellCastStarted:  return "SpellCastStarted";
        case DismountReason::IndoorEntered:     return "IndoorEntered";
        case DismountReason::FlightForbidden:   return "FlightForbidden";
        case DismountReason::WaterEntered:      return "WaterEntered";
        default:                                return "Unknown";
    }
}

enum class RecoveryMode : uint8
{
    None = 0,
    Repath,
    Detour,
    Backtrack,
};

// =============================================================================
// Command Generation Token
// =============================================================================

struct LocomotionToken
{
    ObjectGuid botGuid;
    MoveOwner owner = MoveOwner::None;
    uint64 commandId = 0;

    bool IsValid() const { return !botGuid.IsEmpty() && owner != MoveOwner::None && commandId != 0; }
    operator bool() const { return IsValid(); }
};

inline bool operator==(LocomotionToken const& a, LocomotionToken const& b)
{
    return a.botGuid == b.botGuid && a.owner == b.owner && a.commandId == b.commandId;
}

inline bool operator!=(LocomotionToken const& a, LocomotionToken const& b)
{
    return !(a == b);
}

// =============================================================================
// Telemetry & Records
// =============================================================================

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

class HistoryBuffer
{
public:
    static constexpr size_t CAPACITY = 32;

    void Record(TransitionRecord const& rec)
    {
        _entries[_head] = rec;
        _head = (_head + 1) % CAPACITY;
        if (_count < CAPACITY)
            ++_count;
    }

    size_t GetCount() const { return _count; }

    TransitionRecord const* GetRecent(size_t offset) const
    {
        if (offset >= _count)
            return nullptr;
        size_t idx = (_head + CAPACITY - 1 - offset) % CAPACITY;
        return &_entries[idx];
    }

private:
    std::array<TransitionRecord, CAPACITY> _entries{};
    size_t _head = 0;
    size_t _count = 0;
};

struct SafePosition
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32 timeMs = 0;
};

class SafePositionHistory
{
public:
    static constexpr size_t CAPACITY = 16;

    void Push(float x, float y, float z, uint32 now)
    {
        if (_count > 0)
        {
            size_t lastIdx = (_head + CAPACITY - 1) % CAPACITY;
            float dx = x - _entries[lastIdx].x;
            float dy = y - _entries[lastIdx].y;
            float dz = z - _entries[lastIdx].z;
            if ((dx * dx + dy * dy + dz * dz) < 4.0f) // < 2 yards delta -> skip redundant
                return;
        }

        _entries[_head] = SafePosition{x, y, z, now};
        _head = (_head + 1) % CAPACITY;
        if (_count < CAPACITY)
            ++_count;
    }

    SafePosition const* FindBacktrackTarget(float curX, float curY, float curZ, size_t skipCount = 0) const
    {
        if (_count == 0)
            return nullptr;

        size_t matchesSeen = 0;
        for (size_t i = 0; i < _count; ++i)
        {
            size_t idx = (_head + CAPACITY - 1 - i) % CAPACITY;
            SafePosition const& sp = _entries[idx];
            float dx = curX - sp.x;
            float dy = curY - sp.y;
            float dz = curZ - sp.z;
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 >= 9.0f && d2 <= 3600.0f) // between 3.0yd and 60.0yd
            {
                if (matchesSeen == skipCount)
                    return &sp;
                ++matchesSeen;
            }
        }
        return nullptr;
    }

    size_t GetCount() const { return _count; }
    void Clear() { _head = 0; _count = 0; }

private:
    std::array<SafePosition, CAPACITY> _entries{};
    size_t _head = 0;
    size_t _count = 0;
};

struct NopathCacheEntry
{
    MoveOwner owner = MoveOwner::None;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32 expiresAtMs = 0;
};

struct MovementRequest
{
    ObjectGuid botGuid;
    MoveOwner owner = MoveOwner::None;
    uint64 goalId = 0;
    uint64 commandId = 0;
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
    RecoveryMode recoveryMode = RecoveryMode::None;
    float backtrackX = 0.0f;
    float backtrackY = 0.0f;
    float backtrackZ = 0.0f;
    uint64 backtrackCommandId = 0;
    uint32 backtrackAttempts = 0;
};

struct BotLocomotionRecord
{
    MoveOwner owner = MoveOwner::None;
    LocomotionState state = LocomotionState::Idle;
    MoveMode mode = MoveMode::Idle;
    uint64 commandId = 0;
    float destX = 0.0f;
    float destY = 0.0f;
    float destZ = 0.0f;
    ObjectGuid targetGuid;
    float targetDist = 0.0f;
    float targetAngle = 0.0f;
    float chaseMinRange = 0.0f;
    float chaseMaxRange = 0.0f;
    float chaseAngle = 0.0f;
    uint32 issuedAtMs = 0;
    uint32 holdUntilMs = 0;
    uint32 lastUpdateMs = 0;
};

struct BotMountRecord
{
    MountState state = MountState::Unmounted;
    uint32 lastDismountMs = 0;
    uint32 remountCooldownUntilMs = 0;
    uint32 pendingMountSpellId = 0;
    uint32 mountCastStartedAt = 0;
    MoveOwner mountOwner = MoveOwner::None;
    DismountReason lastDismountReason = DismountReason::Manual;
    std::unordered_set<uint32> knownBadMountSpells;
    DesiredMountState leaderDesiredState = DesiredMountState::None;
    uint32 leaderStateObservedAt = 0;
};

// =============================================================================
// Pure Logic / Arbitration Components
// =============================================================================

class LocomotionArbiter
{
public:
    static int PriorityOf(MoveOwner owner)
    {
        switch (owner)
        {
            case MoveOwner::Avoidance:    return 120;
            case MoveOwner::Combat:       return 110;
            case MoveOwner::Battleground: return 100;
            case MoveOwner::AutoDungeon:  return 90;
            case MoveOwner::Corpse:       return 80;
            case MoveOwner::Loot:         return 70;
            case MoveOwner::Quest:        return 60;
            case MoveOwner::Travel:       return 50;
            case MoveOwner::Fish:         return 40;
            case MoveOwner::Gather:       return 30;
            case MoveOwner::Grind:        return 20;
            case MoveOwner::Ambient:      return 10;
            default:                      return 0;
        }
    }

    static bool CanClaim(MoveOwner activeOwner, MoveOwner requesterOwner, bool isExternallyControlled)
    {
        if (isExternallyControlled)
            return false;

        if (activeOwner == MoveOwner::None || activeOwner == requesterOwner)
            return true;

        return PriorityOf(requesterOwner) > PriorityOf(activeOwner);
    }
};

class ChaseCommandComparator
{
public:
    static constexpr float DIST_TOLERANCE = 0.5f;
    static constexpr float ANGLE_TOLERANCE = 0.1f;

    static bool IsIdempotent(ObjectGuid activeTarget, float activeMin, float activeMax, float activeAngle,
                             ObjectGuid newTarget, float newMin, float newMax, float newAngle)
    {
        if (activeTarget != newTarget)
            return false;
        if (std::abs(activeMin - newMin) > DIST_TOLERANCE)
            return false;
        if (std::abs(activeMax - newMax) > DIST_TOLERANCE)
            return false;
        if (std::abs(activeAngle - newAngle) > ANGLE_TOLERANCE)
            return false;
        return true;
    }
};

class MountStateMachine
{
public:
    static constexpr float MOUNT_HYSTERESIS_DIST = 90.0f;
    static constexpr uint32 REMOUNT_COOLDOWN_MS = 6000;
    static constexpr uint32 LEADER_DEBOUNCE_MS = 1000;

    static bool CanMount(MountState state, float travelDist, uint32 remountCooldownUntilMs, uint32 now,
                         bool isOutdoors, bool inCombat, bool casting, uint8 level, bool isControlled)
    {
        if (isControlled || inCombat || casting || !isOutdoors)
            return false;

        if (level < 20)
            return false;

        if (state == MountState::MountedGround || state == MountState::MountedFlying || state == MountState::MountCasting)
            return false;

        if (travelDist > 0.0f && travelDist < MOUNT_HYSTERESIS_DIST)
            return false;

        if (state == MountState::Cooldown && now < remountCooldownUntilMs)
            return false;

        return true;
    }

    static void TransitionDismount(BotMountRecord& mRec, uint32 now)
    {
        mRec.state = MountState::Cooldown;
        mRec.pendingMountSpellId = 0;
        mRec.lastDismountMs = now;
        mRec.remountCooldownUntilMs = now + REMOUNT_COOLDOWN_MS;
    }

    static void UpdateCooldown(BotMountRecord& mRec, uint32 now)
    {
        if (mRec.state == MountState::Cooldown && now >= mRec.remountCooldownUntilMs)
        {
            mRec.state = MountState::Unmounted;
        }
    }
};

#endif // COA_PLAYERBOTS_BOT_MOVEMENT_PRIMITIVES_H
