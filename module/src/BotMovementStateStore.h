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
        }
    }


private:
    static inline std::array<LocomotionShard, LOCOMOTION_SHARDS> _shards;
    static inline std::array<std::mutex, DISPATCH_GATES> _dispatchGates;
};

#endif // COA_PLAYERBOTS_BOT_MOVEMENT_STATE_STORE_H
