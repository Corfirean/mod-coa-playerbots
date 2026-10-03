#ifndef COA_PLAYERBOTS_BOT_MOVEMENT_STATE_STORE_H
#define COA_PLAYERBOTS_BOT_MOVEMENT_STATE_STORE_H

#include "BotMovementPrimitives.h"
#include <array>
#include <mutex>
#include <optional>
#include <unordered_map>

constexpr size_t LOCOMOTION_SHARDS = 64;
constexpr size_t DISPATCH_GATES = 256;

struct LocomotionShard
{
    std::mutex mutex;

    std::unordered_map<ObjectGuid, BotLocomotionRecord> locomotion;
    std::unordered_map<ObjectGuid, BotMountRecord> mounts;
    std::unordered_map<ObjectGuid, MovementRequest> requests;
    std::unordered_map<ObjectGuid, SafePositionHistory> safeHistories;
    std::unordered_map<ObjectGuid, uint32> lastSafeCheck;
    std::unordered_map<ObjectGuid, uint32> lastRecordedMap;
    std::unordered_map<ObjectGuid, uint32> airborneMs;
    std::unordered_map<ObjectGuid, HistoryBuffer> histories;
    std::unordered_map<ObjectGuid, NopathCacheEntry> nopathCache;
    std::unordered_map<ObjectGuid, uint64> nextRequestGen;

    struct PlanningState
    {
        uint64 ticket = 0;
        MoveOwner owner = MoveOwner::None;
    };
    std::unordered_map<ObjectGuid, PlanningState> activePlanning;
    std::unordered_map<ObjectGuid, uint64> nextPlanTicket;
};

class LocomotionStateStore
{
public:
    static size_t GetShardIndex(ObjectGuid guid)
    {
        return static_cast<size_t>(guid.GetCounter() % LOCOMOTION_SHARDS);
    }

    static LocomotionShard& GetShard(ObjectGuid guid)
    {
        return _shards[GetShardIndex(guid)];
    }

    static std::array<LocomotionShard, LOCOMOTION_SHARDS>& GetAllShards()
    {
        return _shards;
    }

    // Physical dispatch serialization gate per bot
    static std::mutex& GetDispatchGate(ObjectGuid guid)
    {
        return _dispatchGates[guid.GetCounter() % DISPATCH_GATES];
    }

    // Monotonic request generation allocator (caller MUST hold shard.mutex)
    static uint64 AllocateRequestGenerationLocked(LocomotionShard& shard, ObjectGuid guid)
    {
        return ++shard.nextRequestGen[guid];
    }

    // Monotonic plan ticket / intent epoch allocator (caller MUST hold shard.mutex)
    static uint64 AdvanceIntentEpochLocked(LocomotionShard& shard, ObjectGuid guid, MoveOwner owner)
    {
        uint64 ticket = ++shard.nextPlanTicket[guid];
        shard.activePlanning[guid] = LocomotionShard::PlanningState{ticket, owner};
        return ticket;
    }

    static uint64 AllocatePlanTicketLocked(LocomotionShard& shard, ObjectGuid guid, MoveOwner owner)
    {
        return AdvanceIntentEpochLocked(shard, guid, owner);
    }

    // Plan currency check (caller MUST hold shard.mutex)
    static bool IsPlanCurrentLocked(LocomotionShard const& shard, ObjectGuid guid, MoveOwner owner, uint64 ticket)
    {
        auto it = shard.activePlanning.find(guid);
        if (it == shard.activePlanning.end())
            return true;
        // Same owner: if a newer ticket was allocated, this older plan is superseded!
        if (it->second.owner == owner && it->second.ticket > ticket)
            return false;
        // Administrative stop for all owners (MoveOwner::None):
        if (it->second.owner == MoveOwner::None && it->second.ticket > ticket)
            return false;
        // Priority check: if a higher-priority plan started, this lower-priority plan cannot commit!
        if (LocomotionArbiter::PriorityOf(it->second.owner) > LocomotionArbiter::PriorityOf(owner))
            return false;
        return true;
    }

    // Compare-and-swap style Navigate leg commit (caller MUST hold shard.mutex)
    static bool CommitNavigateLegLocked(
        LocomotionShard& shard,
        ObjectGuid botGuid,
        uint64 expectedRequestGeneration,
        uint64 expectedGoalId,
        uint64 expectedPreviousMovementCommandId,
        MovementRequest const& updatedRequest,
        LocomotionToken const& issuedToken)
    {
        if (!issuedToken.IsValid())
            return false;

        auto rItr = shard.requests.find(botGuid);
        if (rItr == shard.requests.end())
            return false;

        // 1. Validate request identity
        if (rItr->second.requestGeneration != expectedRequestGeneration)
            return false;
        if (rItr->second.goalId != expectedGoalId)
            return false;
        if (rItr->second.owner != issuedToken.owner)
            return false;

        // 2. Validate active physical locomotion
        auto lItr = shard.locomotion.find(botGuid);
        if (lItr == shard.locomotion.end())
            return false;
        if (lItr->second.commandId != issuedToken.commandId)
            return false;
        if (lItr->second.owner != issuedToken.owner)
            return false;
        if (lItr->second.mode != MoveMode::Navigate)
            return false;

        // 3. Command-level CAS: verify no newer leg has already committed
        if (rItr->second.movementCommandId != expectedPreviousMovementCommandId)
            return false;

        // All checks passed -- atomically commit updated request
        rItr->second = updatedRequest;
        rItr->second.movementCommandId = issuedToken.commandId;
        return true;
    }

    // Atomic request erase only if generation and commandId match (caller MUST hold shard.mutex)
    static bool EraseRequestIfCurrentLocked(
        LocomotionShard& shard,
        ObjectGuid botGuid,
        uint64 expectedRequestGeneration,
        uint64 expectedMovementCommandId)
    {
        auto rItr = shard.requests.find(botGuid);
        if (rItr != shard.requests.end() &&
            rItr->second.requestGeneration == expectedRequestGeneration &&
            rItr->second.movementCommandId == expectedMovementCommandId)
        {
            shard.requests.erase(rItr);
            return true;
        }
        return false;
    }

    // Lock-free helpers (caller MUST hold shard.mutex)
    static bool CanClaimLocked(LocomotionShard const& shard, ObjectGuid guid, MoveOwner owner, bool isControlled)
    {
        auto it = shard.locomotion.find(guid);
        MoveOwner activeOwner = (it != shard.locomotion.end()) ? it->second.owner : MoveOwner::None;
        return LocomotionArbiter::CanClaim(activeOwner, owner, isControlled);
    }

    static MoveOwner CurrentOwnerLocked(LocomotionShard const& shard, ObjectGuid guid)
    {
        auto it = shard.locomotion.find(guid);
        return (it != shard.locomotion.end()) ? it->second.owner : MoveOwner::None;
    }

    static MoveMode CurrentModeLocked(LocomotionShard const& shard, ObjectGuid guid)
    {
        auto it = shard.locomotion.find(guid);
        return (it != shard.locomotion.end()) ? it->second.mode : MoveMode::Idle;
    }

    static LocomotionState CurrentStateLocked(LocomotionShard const& shard, ObjectGuid guid)
    {
        auto it = shard.locomotion.find(guid);
        return (it != shard.locomotion.end()) ? it->second.state : LocomotionState::Idle;
    }

    static bool CanMountLocked(LocomotionShard const& shard, ObjectGuid guid, float travelDist, uint32 now,
                               bool isOutdoors, bool inCombat, bool casting, uint8 level, bool isControlled,
                               bool isDungeon = false, bool isRaid = false, bool isInWater = false, bool isSwimming = false)
    {
        auto it = shard.mounts.find(guid);
        MountState state = (it != shard.mounts.end()) ? it->second.state : MountState::Unmounted;
        uint32 cd = (it != shard.mounts.end()) ? it->second.remountCooldownUntilMs : 0;
        return MountStateMachine::CanMount(state, travelDist, cd, now, isOutdoors, inCombat, casting, level, isControlled,
                                           isDungeon, isRaid, isInWater, isSwimming);
    }

    static std::optional<MovementRequest> GetRequestSnapshotLocked(LocomotionShard const& shard, ObjectGuid guid)
    {
        auto it = shard.requests.find(guid);
        if (it == shard.requests.end())
            return std::nullopt;
        return it->second;
    }

    static void ResetAllForTest()
    {
        for (auto& s : _shards)
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            s.locomotion.clear();
            s.mounts.clear();
            s.requests.clear();
            s.safeHistories.clear();
            s.lastSafeCheck.clear();
            s.lastRecordedMap.clear();
            s.airborneMs.clear();
            s.histories.clear();
            s.nopathCache.clear();
            s.nextRequestGen.clear();
            s.activePlanning.clear();
            s.nextPlanTicket.clear();
        }
    }


private:
    static inline std::array<LocomotionShard, LOCOMOTION_SHARDS> _shards;
    static inline std::array<std::mutex, DISPATCH_GATES> _dispatchGates;
};

#endif // COA_PLAYERBOTS_BOT_MOVEMENT_STATE_STORE_H
