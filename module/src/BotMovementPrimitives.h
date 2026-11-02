#ifndef COA_PLAYERBOTS_BOT_MOVEMENT_PRIMITIVES_H
#define COA_PLAYERBOTS_BOT_MOVEMENT_PRIMITIVES_H

#include "BotNavProgress.h"
#include "Define.h"
#include "MotionMaster.h"
#include "ObjectGuid.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>

struct MovementDispatchGuard
{
    uint64 requestGeneration = 0;
    uint64 goalId = 0;
};

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
    Encounter = 7,
    Loot = 8,
    Corpse = 9,
    AutoDungeon = 10,
    Battleground = 11,
    Combat = 12,
    Avoidance = 13,
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
        case MoveOwner::Encounter:    return "Encounter";
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
    Superseded, // goal reached by obsolete/stale command after replacement; no-op
};

inline char const* NavStatusName(NavStatus s)
{
    switch (s)
    {
        case NavStatus::Moving:     return "Moving";
        case NavStatus::Arrived:    return "Arrived";
        case NavStatus::Blocked:    return "Blocked";
        case NavStatus::Stuck:      return "Stuck";
        case NavStatus::Superseded: return "Superseded";
        default:                    return "Unknown";
    }
}

enum class NavFailureKind : uint8
{
    None = 0,
    BlockedByOwner,
    ExternallyControlled,
    NoPath,
    VerticallyDisconnected,
    StallExhausted,
};

inline char const* NavFailureKindName(NavFailureKind k)
{
    switch (k)
    {
        case NavFailureKind::None:                   return "None";
        case NavFailureKind::BlockedByOwner:         return "BlockedByOwner";
        case NavFailureKind::ExternallyControlled:   return "ExternallyControlled";
        case NavFailureKind::NoPath:                 return "NoPath";
        case NavFailureKind::VerticallyDisconnected: return "VerticallyDisconnected";
        case NavFailureKind::StallExhausted:         return "StallExhausted";
        default:                                     return "Unknown";
    }
}

enum class NavHoldReason : uint8
{
    None = 0,
    ExternalControl,
    Rooted,
    Casting,
    MountCasting,
    HigherPriorityOwner,
};

inline char const* NavHoldReasonName(NavHoldReason r)
{
    switch (r)
    {
        case NavHoldReason::None:                return "None";
        case NavHoldReason::ExternalControl:     return "ExternalControl";
        case NavHoldReason::Rooted:              return "Rooted";
        case NavHoldReason::Casting:             return "Casting";
        case NavHoldReason::MountCasting:        return "MountCasting";
        case NavHoldReason::HigherPriorityOwner: return "HigherPriorityOwner";
        default:                                 return "Unknown";
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
    DetourLeft,
    DetourRight,
    Backtrack,
    Failed,
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
    uint32 mapId = 0;
    uint32 timeMs = 0;
};

class SafePositionHistory
{
public:
    static constexpr size_t CAPACITY = 16;

    void Push(float x, float y, float z, uint32 mapId, uint32 now)
    {
        if (_count > 0)
        {
            size_t lastIdx = (_head + CAPACITY - 1) % CAPACITY;
            if (_entries[lastIdx].mapId == mapId)
            {
                float dx = x - _entries[lastIdx].x;
                float dy = y - _entries[lastIdx].y;
                float dz = z - _entries[lastIdx].z;
                if ((dx * dx + dy * dy + dz * dz) < 4.0f) // < 2 yards delta -> skip redundant
                    return;
            }
        }

        _entries[_head] = SafePosition{x, y, z, mapId, now};
        _head = (_head + 1) % CAPACITY;
        if (_count < CAPACITY)
            ++_count;
    }

    std::optional<SafePosition> FindBacktrackTarget(float curX, float curY, float curZ, uint32 mapId, size_t skipCount = 0) const
    {
        if (_count == 0)
            return std::nullopt;

        size_t matchesSeen = 0;
        for (size_t i = 0; i < _count; ++i)
        {
            size_t idx = (_head + CAPACITY - 1 - i) % CAPACITY;
            SafePosition const& sp = _entries[idx];
            if (sp.mapId != mapId)
                continue;

            float dx = curX - sp.x;
            float dy = curY - sp.y;
            float dz = curZ - sp.z;
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 >= 9.0f && d2 <= 3600.0f) // between 3.0yd and 60.0yd
            {
                if (matchesSeen == skipCount)
                    return sp;
                ++matchesSeen;
            }
        }
        return std::nullopt;
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

struct SharedNoPathContext
{
    uint32 mapId = 0;
    uint32 instanceId = 0;
    uint32 phaseMask = 1;
    uint8 capabilityFlags = 0; // bit 0: canFly, bit 1: inWater, bit 2: isGhost, bit 3: isMounted
    uint64 startPolyRef = 1;   // Detour polygon ref of start position (isolates thin walls / disconnected mesh regions)
    uint64 botGuid = 0;        // Reporter bot GUID raw value (prevents single-bot self-confirmation when != 0)
    bool ambiguous = false;    // True when start navmesh polygon could not be resolved: conservative fallback skips cache
};

struct SharedNoPathEntry
{
    uint64 key = 0;
    uint32 strikes = 0;
    uint32 expiresAtMs = 0;
    uint64 lastBotGuid = 0;
};

class SharedNoPathCache
{
public:
    static constexpr size_t CAPACITY = 256;
    static constexpr uint32 DEFAULT_TTL_MS = 20000;
    static constexpr uint32 STRIKE_THRESHOLD = 2;
    static constexpr float START_CELL_YARDS = 4.0f;
    static constexpr float DEST_CELL_YARDS = 4.0f;

    static uint8 PackCapabilities(bool canFly, bool inWater, bool isGhost, bool isMounted = false)
    {
        return static_cast<uint8>((canFly ? 1u : 0u) |
                                  (inWater ? 2u : 0u) |
                                  (isGhost ? 4u : 0u) |
                                  (isMounted ? 8u : 0u));
    }

    static uint64 MakeCorridorKey(SharedNoPathContext const& ctx, float sx, float sy, float sz, float dx, float dy, float dz)
    {
        auto qs = [](float v) -> int64_t { return static_cast<int64_t>(std::floor(v / START_CELL_YARDS)); };
        auto qd = [](float v) -> int64_t { return static_cast<int64_t>(std::floor(v / DEST_CELL_YARDS)); };
        uint64 h = 1469598103934665603ULL;
        auto mix = [&](uint64 v) {
            h ^= v;
            h *= 1099511628211ULL;
        };
        mix(ctx.mapId);
        mix(ctx.instanceId);
        mix(ctx.phaseMask);
        mix(ctx.capabilityFlags);
        mix(ctx.startPolyRef);
        mix(static_cast<uint64>(qs(sx)));
        mix(static_cast<uint64>(qs(sy)));
        mix(static_cast<uint64>(qs(sz)));
        mix(static_cast<uint64>(qd(dx)));
        mix(static_cast<uint64>(qd(dy)));
        mix(static_cast<uint64>(qd(dz)));
        return h ? h : 1ULL;
    }

    static uint64 MakeCorridorKey(uint32 mapId, float sx, float sy, float sz, float dx, float dy, float dz)
    {
        SharedNoPathContext ctx;
        ctx.mapId = mapId;
        return MakeCorridorKey(ctx, sx, sy, sz, dx, dy, dz);
    }

    bool IsBlocked(SharedNoPathContext const& ctx, float sx, float sy, float sz, float dx, float dy, float dz, uint32 nowMs) const
    {
        if (ctx.ambiguous || ctx.startPolyRef == 0)
            return false;

        uint64 const k = MakeCorridorKey(ctx, sx, sy, sz, dx, dy, dz);
        for (size_t i = 0; i < _count; ++i)
        {
            SharedNoPathEntry const& e = _entries[i];
            if (e.key == k)
                return nowMs < e.expiresAtMs && e.strikes >= STRIKE_THRESHOLD;
        }
        return false;
    }

    bool IsBlocked(uint32 mapId, float sx, float sy, float sz, float dx, float dy, float dz, uint32 nowMs) const
    {
        SharedNoPathContext ctx;
        ctx.mapId = mapId;
        return IsBlocked(ctx, sx, sy, sz, dx, dy, dz, nowMs);
    }

    uint32 RecordFailure(SharedNoPathContext const& ctx, float sx, float sy, float sz, float dx, float dy, float dz, uint32 nowMs, uint32 ttlMs = DEFAULT_TTL_MS)
    {
        if (ctx.ambiguous || ctx.startPolyRef == 0)
            return 0;

        uint64 const k = MakeCorridorKey(ctx, sx, sy, sz, dx, dy, dz);
        for (size_t i = 0; i < _count; ++i)
        {
            if (_entries[i].key == k)
            {
                if (nowMs >= _entries[i].expiresAtMs)
                {
                    _entries[i].strikes = 1;
                    _entries[i].lastBotGuid = ctx.botGuid;
                }
                else if (ctx.botGuid == 0 || ctx.botGuid != _entries[i].lastBotGuid)
                {
                    ++_entries[i].strikes;
                    _entries[i].lastBotGuid = ctx.botGuid;
                }
                _entries[i].expiresAtMs = nowMs + ttlMs;
                return _entries[i].strikes;
            }
        }
        size_t idx = _head;
        _entries[idx] = SharedNoPathEntry{k, 1, nowMs + ttlMs, ctx.botGuid};
        _head = (_head + 1) % CAPACITY;
        if (_count < CAPACITY)
            ++_count;
        return 1;
    }

    uint32 RecordFailure(uint32 mapId, float sx, float sy, float sz, float dx, float dy, float dz, uint32 nowMs, uint32 ttlMs = DEFAULT_TTL_MS)
    {
        SharedNoPathContext ctx;
        ctx.mapId = mapId;
        return RecordFailure(ctx, sx, sy, sz, dx, dy, dz, nowMs, ttlMs);
    }

    void RecordSuccess(SharedNoPathContext const& ctx, float sx, float sy, float sz, float dx, float dy, float dz)
    {
        if (ctx.ambiguous || ctx.startPolyRef == 0)
            return;

        uint64 const k = MakeCorridorKey(ctx, sx, sy, sz, dx, dy, dz);
        for (size_t i = 0; i < _count; ++i)
        {
            if (_entries[i].key == k)
            {
                _entries[i].strikes = 0;
                _entries[i].expiresAtMs = 0;
                _entries[i].lastBotGuid = 0;
                return;
            }
        }
    }

    void RecordSuccess(uint32 mapId, float sx, float sy, float sz, float dx, float dy, float dz)
    {
        SharedNoPathContext ctx;
        ctx.mapId = mapId;
        RecordSuccess(ctx, sx, sy, sz, dx, dy, dz);
    }

    size_t Size() const { return _count; }

    void Clear()
    {
        _head = 0;
        _count = 0;
    }

private:
    std::array<SharedNoPathEntry, CAPACITY> _entries{};
    size_t _head = 0;
    size_t _count = 0;
};

struct MovementRequest
{
    ObjectGuid botGuid;
    MoveOwner owner = MoveOwner::None;
    uint64 goalId = 0;
    uint64 requestGeneration = 0;   // Navigate transaction identity
    uint64 movementCommandId = 0;   // actual committed locomotion token commandId
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
    float legZ = 0.0f;
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
    bool allowMount = true;
    uint8 consecutiveNoPath = 0;
    NavFailureKind lastFailureKind = NavFailureKind::None;
    NavHoldReason holdReason = NavHoldReason::None;
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
    uint64 mountGeneration = 0;
    uint64 castingGeneration = 0;
    uint32 lastDismountMs = 0;
    uint32 remountCooldownUntilMs = 0;
    uint32 pendingMountSpellId = 0;
    uint32 mountCastStartedAt = 0;
    MoveOwner mountOwner = MoveOwner::None;
    DismountReason lastDismountReason = DismountReason::Manual;
    std::unordered_set<uint32> knownBadMountSpells;
    DesiredMountState leaderDesiredState = DesiredMountState::None;
    uint32 leaderStateObservedAt = 0;
    uint32 noMountAvailableUntilMs = 0;
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
            case MoveOwner::Encounter:    return 65;
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

struct MountTransitionResult
{
    uint32 interruptedSpellId = 0;
    bool shouldDismount = false;
};

class MountStateMachine
{
public:
    static constexpr float MOUNT_HYSTERESIS_DIST = 90.0f;
    static constexpr uint32 REMOUNT_COOLDOWN_MS = 6000;
    static constexpr uint32 LEADER_DEBOUNCE_MS = 1000;

    static bool CanMount(MountState state, float travelDist, uint32 remountCooldownUntilMs, uint32 now,
                         bool isOutdoors, bool inCombat, bool casting, uint8 level, bool isControlled,
                         bool isDungeon = false, bool isRaid = false, bool isInWater = false, bool isSwimming = false)
    {
        if (isControlled || inCombat || casting || !isOutdoors || isDungeon || isRaid || isInWater || isSwimming)
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

    static MountTransitionResult TransitionDismount(BotMountRecord& mRec, uint32 now)
    {
        MountTransitionResult res;
        res.interruptedSpellId = mRec.pendingMountSpellId;
        res.shouldDismount = (mRec.state == MountState::MountedGround || mRec.state == MountState::MountedFlying || mRec.state == MountState::MountCasting);

        mRec.state = MountState::Cooldown;
        mRec.pendingMountSpellId = 0;
        mRec.mountGeneration++;
        mRec.lastDismountMs = now;
        mRec.remountCooldownUntilMs = now + REMOUNT_COOLDOWN_MS;
        return res;
    }

    static void TransitionCastFailed(BotMountRecord& mRec, uint32 now, uint32 backoffMs = 1500)
    {
        mRec.state = MountState::Cooldown;
        mRec.pendingMountSpellId = 0;
        mRec.mountGeneration++;
        mRec.lastDismountMs = now;
        mRec.remountCooldownUntilMs = now + backoffMs;
    }

    static void UpdateCooldown(BotMountRecord& mRec, uint32 now)
    {
        if (mRec.state == MountState::Cooldown && now >= mRec.remountCooldownUntilMs)
        {
            mRec.state = MountState::Unmounted;
        }
    }
};

enum class MountAction : uint8
{
    None = 0,
    MountGround,
    MountFlying,
    Dismount,
};

struct MountPolicyDecision
{
    MountAction action = MountAction::None;
    DismountReason dismountReason = DismountReason::Manual;
    bool wantFlying = false;
};

[[nodiscard]] constexpr bool CanUseFlyingMount(uint32 mapId, uint8 level, uint8 unlockLevel, uint16 ridingSkill)
{
    return (mapId == 530 || mapId == 571) && level >= unlockLevel && ridingSkill >= 225;
}

class MountPolicyResolver
{
public:
    static MountPolicyDecision Evaluate(
        MoveOwner activeOwner,
        MoveMode activeMode,
        float travelDist,
        bool isFollowingLeader,
        DesiredMountState leaderPref,
        bool leaderPrefDebounced,
        bool isOutdoors,
        bool inCombat,
        bool casting,
        uint8 level,
        bool isControlled,
        MountState currentState,
        uint32 remountCooldownUntilMs,
        uint32 now,
        bool canFly,
        bool isCurrentlyMounted,
        bool isDungeon = false,
        bool isRaid = false,
        bool isInWater = false,
        bool isSwimming = false,
        bool allowMount = true)
    {
        MountPolicyDecision dec;

        // 1. Invariants: combat, indoors, casting, controlled, dungeon, raid, water require dismount
        if (isControlled || inCombat || !isOutdoors || isDungeon || isRaid || isInWater || isSwimming)
        {
            if (isCurrentlyMounted || currentState == MountState::MountCasting || currentState == MountState::MountedGround || currentState == MountState::MountedFlying)
            {
                dec.action = MountAction::Dismount;
                if (inCombat)
                    dec.dismountReason = DismountReason::Combat;
                else if (isControlled)
                    dec.dismountReason = DismountReason::ActionForbidden;
                else if (isInWater || isSwimming)
                    dec.dismountReason = DismountReason::WaterEntered;
                else
                    dec.dismountReason = DismountReason::IndoorEntered;
            }
            return dec;
        }

        // 2. Group follow context: leader mount preference has strict authority when following
        if (isFollowingLeader && leaderPrefDebounced)
        {
            if (leaderPref == DesiredMountState::PreferUnmounted)
            {
                if (isCurrentlyMounted || currentState == MountState::MountedGround || currentState == MountState::MountedFlying)
                {
                    dec.action = MountAction::Dismount;
                    dec.dismountReason = DismountReason::LeaderState;
                }
                return dec;
            }
            else if (leaderPref == DesiredMountState::PreferFlying && canFly)
            {
                if (isCurrentlyMounted && currentState == MountState::MountedGround)
                {
                    dec.action = MountAction::Dismount;
                    dec.dismountReason = DismountReason::LeaderState;
                    return dec;
                }
                if (!isCurrentlyMounted && currentState != MountState::MountCasting)
                {
                    if (MountStateMachine::CanMount(currentState, 0.0f, remountCooldownUntilMs, now, isOutdoors, inCombat, casting, level, isControlled, isDungeon, isRaid, isInWater, isSwimming))
                    {
                        dec.action = MountAction::MountFlying;
                        dec.wantFlying = true;
                    }
                }
                return dec;
            }
            else if (leaderPref == DesiredMountState::PreferGround)
            {
                if (!isCurrentlyMounted && currentState != MountState::MountCasting)
                {
                    if (MountStateMachine::CanMount(currentState, 0.0f, remountCooldownUntilMs, now, isOutdoors, inCombat, casting, level, isControlled, isDungeon, isRaid, isInWater, isSwimming))
                    {
                        dec.action = MountAction::MountGround;
                    }
                }
                return dec;
            }
        }

        // 3. Autonomous Travel / Navigate context
        // If the bot is moving autonomously (Quest, Travel, Ambient), leader's PreferUnmounted is ignored
        // to prevent flapping between autonomous mounting (>90yd) and leader dismount.
        // REQUIRES allowMount == true AND activeMode == MoveMode::Navigate (never mounts on Hold / Idle / Point)!
        bool isAutonomousTravel = allowMount && (activeMode == MoveMode::Navigate && (activeOwner == MoveOwner::Travel || activeOwner == MoveOwner::Quest || activeOwner == MoveOwner::Ambient || activeOwner == MoveOwner::Encounter));
        if (isAutonomousTravel && travelDist >= MountStateMachine::MOUNT_HYSTERESIS_DIST)
        {
            if (!isCurrentlyMounted && currentState != MountState::MountCasting)
            {
                if (MountStateMachine::CanMount(currentState, travelDist, remountCooldownUntilMs, now, isOutdoors, inCombat, casting, level, isControlled, isDungeon, isRaid, isInWater, isSwimming))
                {
                    dec.action = canFly ? MountAction::MountFlying : MountAction::MountGround;
                    dec.wantFlying = canFly;
                }
            }
            return dec;
        }

        return dec;
    }
};

// =============================================================================
// Pure Command Logic & Reconciliation Components (Unit-tested)
// =============================================================================

class LocomotionCommandValidator
{
public:
    static bool IsTokenMatch(BotLocomotionRecord const& rec, LocomotionToken const& token)
    {
        if (!token.IsValid())
            return false;
        return rec.owner == token.owner && rec.commandId == token.commandId;
    }

    static bool ValidateDeferredAction(BotLocomotionRecord const& cur, uint64 expectedCommandId,
                                      MoveOwner expectedOwner, MoveMode expectedMode, LocomotionState expectedState)
    {
        return cur.commandId == expectedCommandId &&
               cur.owner == expectedOwner &&
               cur.mode == expectedMode &&
               cur.state == expectedState;
    }
};

class GeneratorReconciler
{
public:
    static bool ReconcilePoint(BotLocomotionRecord& rec, MovementGeneratorType currentMotionType, bool isMoving, uint32 now)
    {
        if (rec.state == LocomotionState::Moving && rec.mode == MoveMode::Point)
        {
            if (currentMotionType != POINT_MOTION_TYPE && !isMoving && (now - rec.issuedAtMs > 500))
            {
                rec.state = LocomotionState::Idle;
                rec.owner = MoveOwner::None;
                rec.mode = MoveMode::Idle;
                return true;
            }
        }
        return false;
    }

    static bool ReconcileFollow(BotLocomotionRecord& rec, MovementGeneratorType currentMotionType, bool isMoving, uint32 now)
    {
        if (rec.state == LocomotionState::Following && rec.mode == MoveMode::Follow)
        {
            if (currentMotionType != FOLLOW_MOTION_TYPE && !isMoving && (now - rec.issuedAtMs > 500))
            {
                rec.state = LocomotionState::Idle;
                rec.owner = MoveOwner::None;
                rec.mode = MoveMode::Idle;
                return true;
            }
        }
        return false;
    }

    static bool ReconcileChase(BotLocomotionRecord& rec, MovementGeneratorType currentMotionType, bool isMoving, uint32 now)
    {
        if (rec.state == LocomotionState::Chasing && rec.mode == MoveMode::Chase)
        {
            if (currentMotionType != CHASE_MOTION_TYPE && !isMoving && (now - rec.issuedAtMs > 500))
            {
                rec.state = LocomotionState::Idle;
                rec.owner = MoveOwner::None;
                rec.mode = MoveMode::Idle;
                return true;
            }
        }
        return false;
    }
};

class NavigateTransaction
{
public:
    static constexpr float DEFAULT_MAX_Z_ARRIVAL = 6.0f;

    static float MaxArrivalZ(float acceptRadius)
    {
        return std::min(12.0f, std::max(DEFAULT_MAX_Z_ARRIVAL, acceptRadius * 1.5f));
    }

    static bool IsWithin3dArrival(float distToGoal2d, float acceptRadius, float distZ = 0.0f)
    {
        return distToGoal2d <= acceptRadius && distZ <= MaxArrivalZ(acceptRadius);
    }

    static bool ValidateArrival(MovementRequest const& req, MoveOwner owner, uint64 goalId,
                                float distToGoal, float acceptRadius, LocomotionToken& outTokenToRelease,
                                float distZ = 0.0f)
    {
        if (IsWithin3dArrival(distToGoal, acceptRadius, distZ))
        {
            if (req.owner == owner && req.goalId == goalId && req.movementCommandId != 0)
            {
                outTokenToRelease = LocomotionToken{req.botGuid, owner, req.movementCommandId};
                return true;
            }
        }
        return false;
    }

    static bool IsRequestCurrent(MovementRequest const& req, uint64 expectedGeneration, uint64 goalId)
    {
        return req.requestGeneration == expectedGeneration && req.goalId == goalId;
    }

    static void CommitLeg(MovementRequest& req, LocomotionToken const& token, float legX, float legY, uint32 now, float legZ = 0.0f)
    {
        if (!token.IsValid())
            return;
        req.movementCommandId = token.commandId;
        req.legs++;
        req.legX = legX;
        req.legY = legY;
        req.legZ = legZ;
        req.lastIssueAt = now;
        req.consecutiveNoPath = 0;
        req.lastFailureKind = NavFailureKind::None;
    }
};

enum class WaypointStepAction : uint8
{
    ContinueMoving = 0, // Keep current waypoint, stay in Moving state
    HoldBlocked,        // Temporary preemption/control: shift task phase clock, do NOT mark Stuck or record NavCorridor
    AdvanceArrived,     // Waypoint reached: ++route.next and continue
    SkipStuckWaypoint,  // Genuine navigation failure (NavStatus::Stuck): record FailKind::NavCorridor, ++route.next
    SupersededNoOp,     // Request superseded by a newer goal: do not record failure or mutate route
};

class TravelWaypointPolicy
{
public:
    static WaypointStepAction EvaluateStep(NavStatus status, NavFailureKind failureKind, uint32& blockedSinceMs,
                                           uint32 nowMs, uint32& outPhaseShiftMs)
    {
        outPhaseShiftMs = 0;
        if (status == NavStatus::Blocked)
        {
            if (!blockedSinceMs)
                blockedSinceMs = nowMs;
            else if (nowMs > blockedSinceMs)
            {
                outPhaseShiftMs = nowMs - blockedSinceMs;
                blockedSinceMs = nowMs;
            }
            return WaypointStepAction::HoldBlocked;
        }

        blockedSinceMs = 0;
        if (status == NavStatus::Arrived)
            return WaypointStepAction::AdvanceArrived;
        if (status == NavStatus::Superseded)
            return WaypointStepAction::SupersededNoOp;
        if (status == NavStatus::Stuck)
        {
            if (failureKind == NavFailureKind::BlockedByOwner || failureKind == NavFailureKind::ExternallyControlled)
                return WaypointStepAction::HoldBlocked;
            return WaypointStepAction::SkipStuckWaypoint;
        }
        return WaypointStepAction::ContinueMoving;
    }
};

#endif // COA_PLAYERBOTS_BOT_MOVEMENT_PRIMITIVES_H
