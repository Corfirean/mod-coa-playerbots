#include "BotMovement.h"
#include "AscensionCollectibleSpellData.h"
#include "Optional.h"
#include "GridTerrainData.h"
#include "GameTime.h"
#include "Log.h"
#include "MotionMaster.h"
#include "PathGenerator.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "StringFormat.h"
#include "UnitDefines.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    // =========================================================================
    // True Sharded Synchronization & Containers (64 Independent Shards)
    // =========================================================================
    constexpr size_t LOCOMOTION_SHARDS = 64;

    struct LocomotionShard
    {
        std::mutex mutex;

        std::unordered_map<ObjectGuid, BotLocomotionRecord> locomotion;
        std::unordered_map<ObjectGuid, BotMountRecord> mounts;
        std::unordered_map<ObjectGuid, MovementRequest> requests;
        std::unordered_map<ObjectGuid, SafePositionHistory> safeHistories;
        std::unordered_map<ObjectGuid, uint32> lastSafeCheck;
        std::unordered_map<ObjectGuid, uint32> airborneMs;
        std::unordered_map<ObjectGuid, HistoryBuffer> histories;
        std::unordered_map<ObjectGuid, NopathCacheEntry> nopathCache;
    };

    static std::array<LocomotionShard, LOCOMOTION_SHARDS> _shards;

    inline LocomotionShard& GetShard(ObjectGuid guid)
    {
        return _shards[guid.GetCounter() % LOCOMOTION_SHARDS];
    }

    // =========================================================================
    // Atomic Telemetry & Statistics
    // =========================================================================
    struct AtomicMovementStats
    {
        std::atomic<uint64> issued{0};
        std::atomic<uint64> redundantSkipped{0};
        std::atomic<uint64> stuckEvents{0};
        std::atomic<uint64> repaths{0};
        std::atomic<uint64> detours{0};
        std::atomic<uint64> backtracks{0};
        std::atomic<uint64> gaveUp{0};
        std::atomic<uint64> mountAttempts{0};
        std::atomic<uint64> mountSuccesses{0};
        std::atomic<uint64> dismounts{0};

        MovementStats Snapshot() const
        {
            MovementStats s;
            s.issued = issued.load(std::memory_order_relaxed);
            s.redundantSkipped = redundantSkipped.load(std::memory_order_relaxed);
            s.stuckEvents = stuckEvents.load(std::memory_order_relaxed);
            s.repaths = repaths.load(std::memory_order_relaxed);
            s.detours = detours.load(std::memory_order_relaxed);
            s.backtracks = backtracks.load(std::memory_order_relaxed);
            s.gaveUp = gaveUp.load(std::memory_order_relaxed);
            s.mountAttempts = mountAttempts.load(std::memory_order_relaxed);
            s.mountSuccesses = mountSuccesses.load(std::memory_order_relaxed);
            s.dismounts = dismounts.load(std::memory_order_relaxed);
            return s;
        }
    };

    static AtomicMovementStats _stats;

    // Navigation & Hysteresis Constants
    constexpr float SAME_POINT_YARDS = 1.0f;
    constexpr float LEG_YARDS = 120.0f;
    constexpr float GOAL_MOVED_YARDS = 4.0f;
    constexpr uint32 RESUME_GAP_MS = 2500;
    constexpr uint32 EARLY_END_REISSUE_MS = 1500;
    constexpr uint32 NOPATH_RECHECK_COOLDOWN_MS = 4000;
    constexpr float DISMOUNT_ARRIVAL_DISTANCE = 35.0f;
    NavProgressRules const PROGRESS_RULES;

    uint32 NowMs()
    {
        return uint32(GameTime::GetGameTimeMS().count());
    }

    float Dist2d(float ax, float ay, float bx, float by)
    {
        return std::hypot(ax - bx, ay - by);
    }

    float ResolveGroundZ(Player* bot, float x, float y, float z)
    {
        float groundZ = z;
        if (!bot->CanFly())
            bot->UpdateAllowedPositionZ(x, y, groundZ);
        if (groundZ <= INVALID_HEIGHT)
            groundZ = z;
        return groundZ;
    }

    // =========================================================================
    // Internal Lock-Free Helpers (Called ONLY while caller holds shard.mutex)
    // =========================================================================

    bool CanClaimLocked(LocomotionShard const& shard, ObjectGuid guid, MoveOwner owner, bool isControlled)
    {
        if (isControlled)
            return false;

        auto itr = shard.locomotion.find(guid);
        if (itr == shard.locomotion.end())
            return true;

        return LocomotionArbiter::CanClaim(itr->second.owner, owner, false);
    }

    MoveOwner CurrentOwnerLocked(LocomotionShard const& shard, ObjectGuid guid)
    {
        auto itr = shard.locomotion.find(guid);
        return (itr == shard.locomotion.end()) ? MoveOwner::None : itr->second.owner;
    }

    MoveMode CurrentModeLocked(LocomotionShard const& shard, ObjectGuid guid)
    {
        auto itr = shard.locomotion.find(guid);
        return (itr == shard.locomotion.end()) ? MoveMode::Idle : itr->second.mode;
    }

    LocomotionState CurrentStateLocked(LocomotionShard const& shard, ObjectGuid guid)
    {
        auto itr = shard.locomotion.find(guid);
        return (itr == shard.locomotion.end()) ? LocomotionState::Idle : itr->second.state;
    }

    void RecordTransitionLocked(LocomotionShard& shard, ObjectGuid botGuid, LocomotionState oldState, LocomotionState newState,
        MoveOwner owner, MoveMode mode, char const* reason, float x, float y, float z, uint32 now)
    {
        HistoryBuffer& buf = shard.histories[botGuid];
        TransitionRecord rec;
        rec.timeMs = now;
        rec.oldState = oldState;
        rec.newState = newState;
        rec.owner = owner;
        rec.mode = mode;
        rec.x = x;
        rec.y = y;
        rec.z = z;
        std::strncpy(rec.reason, reason ? reason : "", sizeof(rec.reason) - 1);
        rec.reason[sizeof(rec.reason) - 1] = '\0';
        buf.Record(rec);
    }

    bool CanMountLocked(LocomotionShard const& shard, Player const* bot, float travelDistance, uint32 now)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        bool isControlled = BotMovement::IsExternallyControlled(bot);
        auto mItr = shard.mounts.find(bot->GetGUID());
        MountState state = (mItr == shard.mounts.end()) ? MountState::Unmounted : mItr->second.state;
        uint32 remountCooldown = (mItr == shard.mounts.end()) ? 0 : mItr->second.remountCooldownUntilMs;
        bool casting = (mItr != shard.mounts.end() && mItr->second.pendingMountSpellId != 0);

        return MountStateMachine::CanMount(state, travelDistance, remountCooldown, now,
            bot->IsOutdoors(), bot->IsInCombat(), casting || bot->IsNonMeleeSpellCast(false),
            bot->GetLevel(), isControlled);
    }

    // Safely clears only bot-controlled normal locomotion generators without touching controlled motion.
    // NEVER locks shard mutex.
    void ClearAuthorizedMovement(Player* bot)
    {
        if (!bot || BotMovement::IsExternallyControlled(bot))
            return;

        MovementGeneratorType genType = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
        if (genType == POINT_MOTION_TYPE || genType == FOLLOW_MOTION_TYPE || genType == CHASE_MOTION_TYPE)
        {
            bot->StopMoving();
            bot->GetMotionMaster()->Clear();
        }
    }

    uint32 GetRacialGroundMountSpell(uint8 race)
    {
        switch (race)
        {
            case RACE_HUMAN:         return 458;   // Brown Horse
            case RACE_ORC:           return 6654;  // Brown Wolf
            case RACE_DWARF:         return 6777;  // Gray Ram
            case RACE_NIGHTELF:      return 10793; // Striped Nightsaber
            case RACE_UNDEAD_PLAYER: return 17462; // Red Skeletal Horse
            case RACE_TAUREN:        return 18990; // Brown Kodo
            case RACE_GNOME:         return 10873; // Red Mechanostrider
            case RACE_TROLL:         return 8395;  // Emerald Raptor
            case RACE_BLOODELF:      return 34795; // Red Hawkstrider
            case RACE_DRAENEI:       return 34406; // Brown Elekk
            default:                 return 0;
        }
    }

    uint32 GetDefaultFlyingMountSpell(Player const* bot)
    {
        return bot->GetTeamId() == TEAM_ALLIANCE ? 32235 : 32243;
    }

    bool IsMountSpellInfo(SpellInfo const* spellInfo)
    {
        return spellInfo && !spellInfo->IsPassive() && spellInfo->HasAura(SPELL_AURA_MOUNTED) &&
            spellInfo->GetMaxDuration() == -1;
    }

    bool IsFlyingMountSpellInfo(SpellInfo const* spellInfo)
    {
        if (!spellInfo || spellInfo->IsPassive() || spellInfo->GetMaxDuration() != -1)
            return false;

        if (spellInfo->HasAura(SPELL_AURA_FLY) ||
            spellInfo->HasAura(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) ||
            spellInfo->HasAura(SPELL_AURA_MOD_FLIGHT_SPEED_ALWAYS))
            return true;

        auto const& entries = AscensionCollectibles::MountWrappers;
        auto itr = std::lower_bound(entries.begin(), entries.end(), spellInfo->Id,
            [](AscensionCollectibles::MountWrapper const& entry, uint32 id) { return entry.SpellId < id; });
        if (itr != entries.end() && itr->SpellId == spellInfo->Id)
        {
            return (itr->Flying150 != 0 || itr->Flying280 != 0 || itr->Flying310 != 0);
        }

        return false;
    }

    bool IssueLeg(Player* bot, MovementRequest& req, bool force)
    {
        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();

        float tx = req.x;
        float ty = req.y;
        float tz = req.z;

        if (req.recoveryMode == RecoveryMode::Detour)
        {
            tx = req.detourX;
            ty = req.detourY;
            tz = req.detourZ;
        }
        else if (req.recoveryMode == RecoveryMode::Backtrack)
        {
            tx = req.backtrackX;
            ty = req.backtrackY;
            tz = req.backtrackZ;
        }
        else
        {
            float dist = Dist2d(bx, by, req.x, req.y);
            if (dist > LEG_YARDS)
            {
                float t = LEG_YARDS / dist;
                tx = bx + (req.x - bx) * t;
                ty = by + (req.y - by) * t;
                tz = bot->GetPositionZ();
            }
        }

        if (force)
        {
            ClearAuthorizedMovement(bot);
        }

        LocomotionToken token = BotMovement::MoveTo(bot, req.owner, tx, ty, tz, /*forceDestination=*/false);
        if (!token)
            return false;

        req.commandId = token.commandId;
        if (req.recoveryMode == RecoveryMode::Backtrack)
            req.backtrackCommandId = token.commandId;

        req.legs++;
        req.legX = tx;
        req.legY = ty;
        req.lastIssueAt = NowMs();
        return true;
    }
}

namespace BotMovement
{
    // =========================================================================
    // Controlled Movement Guard
    // =========================================================================

    bool IsExternallyControlled(Player const* bot)
    {
        if (!bot)
            return false;

        if (bot->IsInFlight() || bot->GetVehicle() || ((Unit const*)bot->m_mover != (Unit const*)bot))
            return true;

        if (bot->HasUnitState(UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING | UNIT_STATE_CHARGING |
                              UNIT_STATE_JUMPING | UNIT_STATE_IN_FLIGHT | UNIT_STATE_POSSESSED |
                              UNIT_STATE_STUNNED | UNIT_STATE_DISTRACTED))
            return true;

        MovementGeneratorType genType = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
        if (genType == EFFECT_MOTION_TYPE || genType == FLEEING_MOTION_TYPE ||
            genType == TIMED_FLEEING_MOTION_TYPE || genType == CONFUSED_MOTION_TYPE ||
            genType == FLIGHT_MOTION_TYPE)
            return true;

        return false;
    }

    MoveOwner CurrentOwner(Player* bot)
    {
        if (!bot)
            return MoveOwner::None;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        return CurrentOwnerLocked(shard, bot->GetGUID());
    }

    MoveMode CurrentMode(Player* bot)
    {
        if (!bot)
            return MoveMode::Idle;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        return CurrentModeLocked(shard, bot->GetGUID());
    }

    LocomotionState CurrentState(Player* bot)
    {
        if (!bot)
            return LocomotionState::Idle;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        return CurrentStateLocked(shard, bot->GetGUID());
    }

    bool CanClaim(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return false;

        bool isControlled = IsExternallyControlled(bot);
        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        return CanClaimLocked(shard, bot->GetGUID(), owner, isControlled);
    }

    std::optional<MovementRequest> GetRequestSnapshot(ObjectGuid botGuid)
    {
        LocomotionShard& shard = GetShard(botGuid);
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto itr = shard.requests.find(botGuid);
        if (itr == shard.requests.end())
            return std::nullopt;
        return itr->second;
    }

    void ResetRequest(ObjectGuid botGuid)
    {
        LocomotionShard& shard = GetShard(botGuid);
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.requests.erase(botGuid);
    }

    MovementStats Stats()
    {
        return _stats.Snapshot();
    }

    // =========================================================================
    // Arbitrated Locomotion Primitives (Token-Safe & Sharded)
    // =========================================================================

    LocomotionToken MoveTo(Player* bot, MoveOwner owner, float x, float y, float z, bool forceDestination)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return {};

        if (bot->IsNonMeleeSpellCast(false))
            return {};

        if (IsExternallyControlled(bot))
            return {};

        uint32 now = NowMs();
        float groundZ = ResolveGroundZ(bot, x, y, z);
        LocomotionToken token;

        LocomotionShard& shard = GetShard(bot->GetGUID());

        // Step 1: Claim validation, redundancy check, reserve commandId under shard lock
        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return {};

            BotLocomotionRecord& rec = shard.locomotion[bot->GetGUID()];

            // Redundancy check: if already walking to essentially the same point
            if (rec.owner == owner && rec.mode == MoveMode::Point &&
                bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE &&
                std::fabs(rec.destX - x) <= SAME_POINT_YARDS &&
                std::fabs(rec.destY - y) <= SAME_POINT_YARDS &&
                std::fabs(rec.destZ - groundZ) <= SAME_POINT_YARDS * 3.0f)
            {
                _stats.redundantSkipped.fetch_add(1, std::memory_order_relaxed);
                return LocomotionToken{bot->GetGUID(), owner, rec.commandId};
            }

            // No-path cache check: avoid flooding navmesh queries for known unreachable points
            auto cached = shard.nopathCache.find(bot->GetGUID());
            if (cached != shard.nopathCache.end() && cached->second.owner == owner && now < cached->second.expiresAtMs &&
                std::fabs(cached->second.x - x) <= SAME_POINT_YARDS &&
                std::fabs(cached->second.y - y) <= SAME_POINT_YARDS &&
                std::fabs(cached->second.z - groundZ) <= SAME_POINT_YARDS * 3.0f)
            {
                return {};
            }

            rec.commandId++;
            rec.owner = owner;
            rec.state = LocomotionState::Planning;
            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
        }

        // Step 2: OUTSIDE LOCK: PathGenerator computation (Never serializes global movement!)
        bool pathValid = true;
        if (!bot->CanFly())
        {
            PathGenerator path(bot);
            path.CalculatePath(x, y, groundZ, forceDestination);
            if (path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH))
            {
                pathValid = false;
            }
        }

        // Step 3: Sharded Lock: verify generation still current before committing
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotLocomotionRecord& rec = shard.locomotion[bot->GetGUID()];

            if (rec.commandId != token.commandId || rec.owner != owner)
            {
                return {};
            }

            if (!pathValid)
            {
                RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Planning, LocomotionState::Failed,
                    owner, MoveMode::Point, "NoPathFound", x, y, groundZ, now);
                rec.state = LocomotionState::Failed;

                NopathCacheEntry& entry = shard.nopathCache[bot->GetGUID()];
                entry.owner = owner;
                entry.x = x;
                entry.y = y;
                entry.z = groundZ;
                entry.expiresAtMs = now + NOPATH_RECHECK_COOLDOWN_MS;
                return {};
            }

            rec.destX = x;
            rec.destY = y;
            rec.destZ = groundZ;
            rec.mode = MoveMode::Point;
            rec.state = LocomotionState::Moving;
            rec.issuedAtMs = now;
            rec.lastUpdateMs = now;

            RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Planning, LocomotionState::Moving,
                owner, MoveMode::Point, "MovePointIssued", x, y, groundZ, now);
        }

        // Step 4: OUTSIDE LOCK: Dispatch MotionMaster MovePoint
        ClearAuthorizedMovement(bot);
        bot->GetMotionMaster()->MovePoint(uint32(owner), x, y, groundZ, FORCED_MOVEMENT_NONE, 0.0f, 0.0f,
            /*generatePath=*/true, forceDestination);
        _stats.issued.fetch_add(1, std::memory_order_relaxed);

        return token;
    }

    NavStatus Navigate(Player* bot, MoveOwner owner, uint64 goalId, float x, float y, float z, float acceptRadius, LocomotionToken* outToken)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return NavStatus::Blocked;

        if (IsExternallyControlled(bot))
            return NavStatus::Blocked;

        uint32 now = NowMs();
        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();
        float bz = bot->GetPositionZ();
        float distToGoal = Dist2d(bx, by, x, y);

        // Arrival Check
        if (distToGoal <= acceptRadius)
        {
            LocomotionShard& shard = GetShard(bot->GetGUID());
            LocomotionToken tokenToRelease;
            {
                std::lock_guard<std::mutex> lock(shard.mutex);
                auto rItr = shard.requests.find(bot->GetGUID());
                if (rItr != shard.requests.end() && rItr->second.owner == owner && rItr->second.goalId == goalId)
                {
                    tokenToRelease = LocomotionToken{bot->GetGUID(), owner, rItr->second.commandId};
                }
            }
            if (tokenToRelease.IsValid())
                Release(bot, tokenToRelease);
            else
                ForceReleaseOwner(bot, owner);

            return NavStatus::Arrived;
        }

        LocomotionShard& shard = GetShard(bot->GetGUID());

        // Step 1: Snapshot and prepare request under shard lock
        MovementRequest reqCopy;
        uint64 expectedCommandId = 0;
        bool isNewRequest = false;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return NavStatus::Blocked;

            MovementRequest& req = shard.requests[bot->GetGUID()];
            if (req.goalId != goalId || req.owner != owner || Dist2d(req.x, req.y, x, y) > GOAL_MOVED_YARDS)
            {
                isNewRequest = true;
                req.botGuid = bot->GetGUID();
                req.owner = owner;
                req.goalId = goalId;
                req.commandId = shard.locomotion[bot->GetGUID()].commandId + 1;
                req.x = x;
                req.y = y;
                req.z = z;
                req.acceptRadius = acceptRadius;
                req.startedAt = now;
                req.lastCallAt = now;
                float distZ = std::abs(bz - z);
                req.progress.Start(distToGoal, distZ, now);
                req.legs = 0;
                req.lastIssueAt = 0;
                req.legX = 0.0f;
                req.legY = 0.0f;
                req.detour = false;
                req.recoveryMode = RecoveryMode::None;
                req.backtrackAttempts = 0;
            }
            else
            {
                req.lastCallAt = now;
                req.acceptRadius = acceptRadius;
            }

            reqCopy = req;
            expectedCommandId = req.commandId;
        }

        // Step 2: Handle First Leg or Recovery
        if (isNewRequest)
        {
            if (IssueLeg(bot, reqCopy, true))
            {
                std::lock_guard<std::mutex> lock(shard.mutex);
                auto rItr = shard.requests.find(bot->GetGUID());
                if (rItr != shard.requests.end() && rItr->second.goalId == goalId)
                {
                    rItr->second = reqCopy;
                    if (outToken)
                        *outToken = LocomotionToken{bot->GetGUID(), owner, reqCopy.commandId};
                }
                return NavStatus::Moving;
            }
            else
            {
                Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.commandId});
                return NavStatus::Blocked;
            }
        }

        // Step 3: Backtrack completion check
        float distZ = std::abs(bz - z);
        if (reqCopy.recoveryMode == RecoveryMode::Backtrack)
        {
            float distToBacktrack = Dist2d(bx, by, reqCopy.backtrackX, reqCopy.backtrackY);
            if (distToBacktrack <= 3.0f || !bot->isMoving())
            {
                // Reached safe position! Rebase progress and resume journey toward original goal
                reqCopy.recoveryMode = RecoveryMode::None;
                reqCopy.progress.Rebase(distToGoal, distZ, now);
                if (IssueLeg(bot, reqCopy, true))
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    auto rItr = shard.requests.find(bot->GetGUID());
                    if (rItr != shard.requests.end() && rItr->second.goalId == goalId)
                    {
                        rItr->second = reqCopy;
                        if (outToken)
                            *outToken = LocomotionToken{bot->GetGUID(), owner, reqCopy.commandId};
                    }
                    return NavStatus::Moving;
                }
            }
        }

        // Step 4: Progress evaluation
        NavRecovery recovery = reqCopy.progress.Update(distToGoal, distZ, now, PROGRESS_RULES);
        if (recovery == NavRecovery::GiveUp)
        {
            _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
            Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.commandId});
            return NavStatus::Stuck;
        }

        if (recovery == NavRecovery::Repath)
        {
            _stats.stuckEvents.fetch_add(1, std::memory_order_relaxed);
            _stats.repaths.fetch_add(1, std::memory_order_relaxed);
            reqCopy.recoveryMode = RecoveryMode::Repath;
            reqCopy.detour = false;
            IssueLeg(bot, reqCopy, true);
        }
        else if (recovery == NavRecovery::DetourLeft || recovery == NavRecovery::DetourRight)
        {
            _stats.stuckEvents.fetch_add(1, std::memory_order_relaxed);
            _stats.detours.fetch_add(1, std::memory_order_relaxed);
            reqCopy.recoveryMode = RecoveryMode::Detour;
            reqCopy.detour = true;
            // Generate lateral detour point
            float angle = bot->GetOrientation() + (recovery == NavRecovery::DetourLeft ? 1.047f : -1.047f);
            reqCopy.detourX = bx + std::cos(angle) * 15.0f;
            reqCopy.detourY = by + std::sin(angle) * 15.0f;
            reqCopy.detourZ = ResolveGroundZ(bot, reqCopy.detourX, reqCopy.detourY, bz);

            if (!IssueLeg(bot, reqCopy, true))
            {
                // Detour failed -> escalate to Backtrack
                recovery = NavRecovery::DetourLeft; // trigger backtrack branch below
            }
        }

        if (reqCopy.recoveryMode == RecoveryMode::Detour && !bot->isMoving())
        {
            // Detour stalled or failed -> attempt Backtrack to safe position
            SafePosition const* safeTarget = nullptr;
            {
                std::lock_guard<std::mutex> lock(shard.mutex);
                safeTarget = shard.safeHistories[bot->GetGUID()].FindBacktrackTarget(bx, by, bz, reqCopy.backtrackAttempts);
            }

            if (safeTarget && reqCopy.backtrackAttempts < 3)
            {
                _stats.backtracks.fetch_add(1, std::memory_order_relaxed);
                reqCopy.backtrackAttempts++;
                reqCopy.recoveryMode = RecoveryMode::Backtrack;
                reqCopy.backtrackX = safeTarget->x;
                reqCopy.backtrackY = safeTarget->y;
                reqCopy.backtrackZ = safeTarget->z;
                IssueLeg(bot, reqCopy, true);
            }
            else
            {
                _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.commandId});
                return NavStatus::Stuck;
            }
        }

        // Check if current leg completed and next leg needed
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE && !bot->isMoving())
        {
            float legDist = Dist2d(bx, by, reqCopy.legX, reqCopy.legY);
            if (legDist <= 4.0f && distToGoal > acceptRadius)
            {
                IssueLeg(bot, reqCopy, false);
            }
        }

        // Commit updated request state under shard lock
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            auto rItr = shard.requests.find(bot->GetGUID());
            if (rItr != shard.requests.end() && rItr->second.commandId == expectedCommandId && rItr->second.goalId == goalId)
            {
                rItr->second = reqCopy;
                if (outToken)
                    *outToken = LocomotionToken{bot->GetGUID(), owner, reqCopy.commandId};
            }
        }

        return NavStatus::Moving;
    }

    LocomotionToken Follow(Player* bot, MoveOwner owner, Unit* target, float dist, float angle)
    {
        if (!bot || !target || !bot->IsInWorld() || !bot->IsAlive())
            return {};

        if (IsExternallyControlled(bot))
            return {};

        LocomotionShard& shard = GetShard(bot->GetGUID());
        LocomotionToken token;
        bool redundant = false;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return {};

            BotLocomotionRecord& rec = shard.locomotion[bot->GetGUID()];

            // Hysteresis deadzone check: if already following same target within 0.5yd and 0.1 rad
            if (rec.owner == owner && rec.mode == MoveMode::Follow && rec.targetGuid == target->GetGUID() &&
                bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE &&
                std::fabs(rec.targetDist - dist) <= 0.5f &&
                std::fabs(rec.targetAngle - angle) <= 0.1f)
            {
                _stats.redundantSkipped.fetch_add(1, std::memory_order_relaxed);
                return LocomotionToken{bot->GetGUID(), owner, rec.commandId};
            }

            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::Follow;
            rec.state = LocomotionState::Following;
            rec.targetGuid = target->GetGUID();
            rec.targetDist = dist;
            rec.targetAngle = angle;
            rec.issuedAtMs = NowMs();
            rec.lastUpdateMs = NowMs();

            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
        }

        ClearAuthorizedMovement(bot);
        bot->GetMotionMaster()->MoveFollow(target, dist, angle);
        _stats.issued.fetch_add(1, std::memory_order_relaxed);

        return token;
    }

    LocomotionToken Chase(Player* bot, MoveOwner owner, Unit* target, float minRange, float maxRange, float angle)
    {
        if (!bot || !target || !bot->IsInWorld() || !bot->IsAlive())
            return {};

        if (IsExternallyControlled(bot))
            return {};

        LocomotionShard& shard = GetShard(bot->GetGUID());
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return {};

            BotLocomotionRecord& rec = shard.locomotion[bot->GetGUID()];

            // Range banding idempotency: check if chase command is effectively identical
            if (rec.owner == owner && rec.mode == MoveMode::Chase &&
                bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE &&
                ChaseCommandComparator::IsIdempotent(rec.targetGuid, rec.chaseMinRange, rec.chaseMaxRange, rec.chaseAngle,
                                                     target->GetGUID(), minRange, maxRange, angle))
            {
                _stats.redundantSkipped.fetch_add(1, std::memory_order_relaxed);
                return LocomotionToken{bot->GetGUID(), owner, rec.commandId};
            }

            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::Chase;
            rec.state = LocomotionState::Chasing;
            rec.targetGuid = target->GetGUID();
            rec.chaseMinRange = minRange;
            rec.chaseMaxRange = maxRange;
            rec.chaseAngle = angle;
            rec.issuedAtMs = NowMs();
            rec.lastUpdateMs = NowMs();

            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
        }

        ClearAuthorizedMovement(bot);

        // Core MotionMaster::MoveChase dispatch:
        // When range band is present, ALWAYS pass ChaseRange(minRange, maxRange) regardless of angle!
        if (minRange > 0.0f || maxRange > 0.0f)
        {
            std::optional<ChaseAngle> chaseAngle;
            if (angle != 0.0f)
                chaseAngle = ChaseAngle(angle, 0.5f);

            bot->GetMotionMaster()->MoveChase(target, ChaseRange(minRange, maxRange), chaseAngle);
        }
        else
        {
            bot->GetMotionMaster()->MoveChase(target);
        }

        _stats.issued.fetch_add(1, std::memory_order_relaxed);
        return token;
    }

    LocomotionToken MoveForwards(Player* bot, MoveOwner owner, Unit* target, float dist)
    {
        if (!bot || !target)
            return {};

        float angle = target->GetRelativeAngle(bot);
        float x = bot->GetPositionX() + std::cos(angle) * dist;
        float y = bot->GetPositionY() + std::sin(angle) * dist;
        float z = ResolveGroundZ(bot, x, y, bot->GetPositionZ());

        return MoveTo(bot, owner, x, y, z);
    }

    LocomotionToken MoveBackwards(Player* bot, MoveOwner owner, Unit* target, float dist)
    {
        if (!bot || !target)
            return {};

        float angle = target->GetRelativeAngle(bot) + M_PI;
        float x = bot->GetPositionX() + std::cos(angle) * dist;
        float y = bot->GetPositionY() + std::sin(angle) * dist;
        float z = ResolveGroundZ(bot, x, y, bot->GetPositionZ());

        return MoveTo(bot, owner, x, y, z);
    }

    LocomotionToken Hold(Player* bot, MoveOwner owner, uint32 durationMs)
    {
        if (!bot)
            return {};

        LocomotionShard& shard = GetShard(bot->GetGUID());
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return {};

            BotLocomotionRecord& rec = shard.locomotion[bot->GetGUID()];
            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::Hold;
            rec.state = LocomotionState::Holding;
            rec.holdUntilMs = NowMs() + durationMs;
            rec.issuedAtMs = NowMs();
            rec.lastUpdateMs = NowMs();

            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
        }

        ClearAuthorizedMovement(bot);
        return token;
    }

    // =========================================================================
    // Cancellation & Cleanup (Token-Safe & Administrative Force)
    // =========================================================================

    bool Release(Player* bot, LocomotionToken const& token)
    {
        if (!bot || !token.IsValid())
            return false;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        bool matched = false;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            auto itr = shard.locomotion.find(bot->GetGUID());
            if (itr != shard.locomotion.end() && itr->second.owner == token.owner && itr->second.commandId == token.commandId)
            {
                RecordTransitionLocked(shard, bot->GetGUID(), itr->second.state, LocomotionState::Idle,
                    token.owner, MoveMode::Idle, "ReleaseToken", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), NowMs());

                itr->second.owner = MoveOwner::None;
                itr->second.mode = MoveMode::Idle;
                itr->second.state = LocomotionState::Idle;
                shard.requests.erase(bot->GetGUID());
                matched = true;
            }
        }

        if (matched)
            ClearAuthorizedMovement(bot);

        return matched;
    }

    bool Stop(Player* bot, LocomotionToken const& token)
    {
        return Release(bot, token);
    }

    void ForceStopOwner(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        bool matched = false;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            auto itr = shard.locomotion.find(bot->GetGUID());
            if (itr != shard.locomotion.end() && (itr->second.owner == owner || owner == MoveOwner::None))
            {
                RecordTransitionLocked(shard, bot->GetGUID(), itr->second.state, LocomotionState::Idle,
                    owner, MoveMode::Idle, "ForceStopOwner", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), NowMs());

                itr->second.owner = MoveOwner::None;
                itr->second.mode = MoveMode::Idle;
                itr->second.state = LocomotionState::Idle;
                shard.requests.erase(bot->GetGUID());
                matched = true;
            }
        }

        if (matched)
            ClearAuthorizedMovement(bot);
    }

    void ForceReleaseOwner(Player* bot, MoveOwner owner)
    {
        ForceStopOwner(bot, owner);
    }

    // =========================================================================
    // Centralized Mount Controller Implementation
    // =========================================================================

    bool CanMount(Player const* bot, float travelDistance)
    {
        if (!bot)
            return false;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        return CanMountLocked(shard, bot, travelDistance, NowMs());
    }

    bool IsMounted(Player const* bot)
    {
        return bot && bot->IsMounted();
    }

    MountState GetMountState(Player const* bot)
    {
        if (!bot)
            return MountState::Unmounted;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto itr = shard.mounts.find(bot->GetGUID());
        return (itr == shard.mounts.end()) ? MountState::Unmounted : itr->second.state;
    }

    uint32 GetPendingMountSpell(Player const* bot)
    {
        if (!bot)
            return 0;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto itr = shard.mounts.find(bot->GetGUID());
        return (itr == shard.mounts.end()) ? 0 : itr->second.pendingMountSpellId;
    }

    uint32 SelectMountSpell(Player* bot, bool wantFlying)
    {
        if (!bot)
            return 0;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot->GetGUID()];

        if (wantFlying)
        {
            uint32 defaultFly = GetDefaultFlyingMountSpell(bot);
            if (defaultFly && bot->HasSpell(defaultFly) && !mRec.knownBadMountSpells.count(defaultFly))
                return defaultFly;

            for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
            {
                if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                    continue;
                if (mRec.knownBadMountSpells.count(spellId))
                    continue;

                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                if (IsMountSpellInfo(spellInfo) && IsFlyingMountSpellInfo(spellInfo))
                    return spellId;
            }

            return GetRacialGroundMountSpell(bot->getRace());
        }
        else
        {
            uint32 racialSpellId = GetRacialGroundMountSpell(bot->getRace());
            if (racialSpellId && bot->HasSpell(racialSpellId) && !mRec.knownBadMountSpells.count(racialSpellId))
                return racialSpellId;

            for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
            {
                if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                    continue;
                if (mRec.knownBadMountSpells.count(spellId))
                    continue;

                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                if (IsMountSpellInfo(spellInfo) && !IsFlyingMountSpellInfo(spellInfo))
                    return spellId;
            }

            return racialSpellId;
        }
    }

    bool RequestMount(Player* bot, MoveOwner owner, float travelDistance, bool wantFlying)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        if (IsExternallyControlled(bot))
            return false;

        uint32 now = NowMs();
        LocomotionShard& shard = GetShard(bot->GetGUID());
        uint32 spellId = 0;

        // Step 1: Pre-validation under shard lock
        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!CanMountLocked(shard, bot, travelDistance, now))
                return false;

            // If active locomotion is owned by a higher-priority owner, mount request cannot preempt it!
            if (!CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return false;
        }

        // Step 2: Stop any active movement under this owner so mount cast does not cancel immediately.
        // If the bot is still moving physically, wait for next tick.
        ForceStopOwner(bot, owner);
        if (bot->isMoving())
            return false;

        // Step 3: Resolve spell and record state under shard lock
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotMountRecord& mRec = shard.mounts[bot->GetGUID()];

            if (wantFlying)
            {
                uint32 mapId = bot->GetMapId();
                if ((mapId != 530 && mapId != 571) || bot->GetLevel() < 60)
                    wantFlying = false;
            }

            if (wantFlying)
            {
                uint32 defaultFly = GetDefaultFlyingMountSpell(bot);
                if (defaultFly && bot->HasSpell(defaultFly) && !mRec.knownBadMountSpells.count(defaultFly))
                    spellId = defaultFly;

                if (!spellId)
                {
                    for (auto const& [sId, pSpell] : bot->GetSpellMap())
                    {
                        if (!pSpell || pSpell->State == PLAYERSPELL_REMOVED || !pSpell->Active || mRec.knownBadMountSpells.count(sId))
                            continue;
                        SpellInfo const* si = sSpellMgr->GetSpellInfo(sId);
                        if (IsMountSpellInfo(si) && IsFlyingMountSpellInfo(si))
                        {
                            spellId = sId;
                            break;
                        }
                    }
                }

                if (!spellId)
                    spellId = GetRacialGroundMountSpell(bot->getRace());
            }
            else
            {
                uint32 racialSpellId = GetRacialGroundMountSpell(bot->getRace());
                if (racialSpellId && bot->HasSpell(racialSpellId) && !mRec.knownBadMountSpells.count(racialSpellId))
                    spellId = racialSpellId;

                if (!spellId)
                {
                    for (auto const& [sId, pSpell] : bot->GetSpellMap())
                    {
                        if (!pSpell || pSpell->State == PLAYERSPELL_REMOVED || !pSpell->Active || mRec.knownBadMountSpells.count(sId))
                            continue;
                        SpellInfo const* si = sSpellMgr->GetSpellInfo(sId);
                        if (IsMountSpellInfo(si) && !IsFlyingMountSpellInfo(si))
                        {
                            spellId = sId;
                            break;
                        }
                    }
                }
            }

            if (!spellId)
                return false;

            mRec.pendingMountSpellId = spellId;
            mRec.state = MountState::MountCasting;
            mRec.mountCastStartedAt = now;
            mRec.mountOwner = owner;
            _stats.mountAttempts.fetch_add(1, std::memory_order_relaxed);
        }

        // Step 4: OUTSIDE LOCK: Cast Spell
        SpellCastResult result = bot->CastSpell(bot, spellId, false);
        if (result == SPELL_CAST_OK)
        {
            return true;
        }

        // Cast failed immediately
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotMountRecord& mRec = shard.mounts[bot->GetGUID()];
            mRec.pendingMountSpellId = 0;
            mRec.state = MountState::Unmounted;
            if (spellId != GetRacialGroundMountSpell(bot->getRace()) && spellId != GetDefaultFlyingMountSpell(bot))
            {
                if (result != SPELL_FAILED_ONLY_OUTDOORS && result != SPELL_FAILED_MOVING &&
                    result != SPELL_FAILED_SPELL_IN_PROGRESS && result != SPELL_FAILED_NOT_HERE)
                {
                    mRec.knownBadMountSpells.insert(spellId);
                }
            }
        }

        return false;
    }

    void RequestDismount(Player* bot, MoveOwner owner, DismountReason reason)
    {
        if (!bot)
            return;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        uint32 pendingSpell = 0;
        bool isMounted = false;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotMountRecord& mRec = shard.mounts[bot->GetGUID()];

            MountStateMachine::TransitionDismount(mRec, NowMs());
            mRec.lastDismountReason = reason;

            pendingSpell = mRec.pendingMountSpellId;
            mRec.pendingMountSpellId = 0;
            isMounted = bot->IsMounted();

            RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Moving, LocomotionState::Idle, owner, MoveMode::Idle,
                DismountReasonName(reason), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), NowMs());
        }

        if (pendingSpell)
            bot->InterruptNonMeleeSpells(false);

        if (isMounted)
        {
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
            _stats.dismounts.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void SetLeaderMountPreference(Player* bot, DesiredMountState pref)
    {
        if (!bot)
            return;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot->GetGUID()];
        if (mRec.leaderDesiredState != pref)
        {
            mRec.leaderDesiredState = pref;
            mRec.leaderStateObservedAt = NowMs();
        }
    }

    void ResolveMountCast(Player* bot)
    {
        if (!bot)
            return;

        uint32 justTried = 0;
        bool castFinished = false;
        bool mounted = bot->IsMounted();

        LocomotionShard& shard = GetShard(bot->GetGUID());
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotMountRecord& mRec = shard.mounts[bot->GetGUID()];

            if (mRec.pendingMountSpellId == 0)
            {
                if (mounted && mRec.state != MountState::MountedGround && mRec.state != MountState::MountedFlying)
                {
                    mRec.state = bot->CanFly() ? MountState::MountedFlying : MountState::MountedGround;
                }
                return;
            }

            // Mount casts take ~1.5s. If bot is still non-melee casting, wait for it!
            if (bot->IsNonMeleeSpellCast(false))
                return;

            justTried = mRec.pendingMountSpellId;
            mRec.pendingMountSpellId = 0;
            castFinished = true;

            if (mounted)
            {
                mRec.state = bot->CanFly() ? MountState::MountedFlying : MountState::MountedGround;
                _stats.mountSuccesses.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                MountStateMachine::TransitionDismount(mRec, NowMs());
                if (!bot->IsInCombat() && !bot->isMoving() &&
                    justTried != GetRacialGroundMountSpell(bot->getRace()) &&
                    justTried != GetDefaultFlyingMountSpell(bot))
                {
                    mRec.knownBadMountSpells.insert(justTried);
                }
            }
        }
    }

    void OnSpellCastStart(Player* bot, uint32 spellId)
    {
        if (!bot)
            return;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot->GetGUID()];
        mRec.pendingMountSpellId = spellId;
        mRec.state = MountState::MountCasting;
        mRec.mountCastStartedAt = NowMs();
    }

    void OnSpellCastSuccess(Player* bot, uint32 spellId)
    {
        if (!bot)
            return;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot->GetGUID()];
        if (mRec.pendingMountSpellId == spellId || spellId == 0)
        {
            mRec.pendingMountSpellId = 0;
            mRec.state = bot->CanFly() ? MountState::MountedFlying : MountState::MountedGround;
            _stats.mountSuccesses.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void OnSpellCastInterrupt(Player* bot, uint32 spellId)
    {
        if (!bot)
            return;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot->GetGUID()];
        if (mRec.pendingMountSpellId == spellId || spellId == 0)
        {
            mRec.pendingMountSpellId = 0;
            MountStateMachine::TransitionDismount(mRec, NowMs());
        }
    }

    // =========================================================================
    // Watchdog, Backtracking, & Periodic Maintenance
    // =========================================================================

    bool BacktrackToSafePosition(Player* bot, MoveOwner owner)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();
        float bz = bot->GetPositionZ();
        SafePosition const* target = nullptr;

        LocomotionShard& shard = GetShard(bot->GetGUID());
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            target = shard.safeHistories[bot->GetGUID()].FindBacktrackTarget(bx, by, bz, 0);
        }

        if (!target)
            return false;

        _stats.backtracks.fetch_add(1, std::memory_order_relaxed);
        LocomotionToken token = MoveTo(bot, owner, target->x, target->y, target->z, false);
        return token.IsValid();
    }

    void Update(Player* bot, uint32 diff)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return;

        uint32 now = NowMs();
        LocomotionShard& shard = GetShard(bot->GetGUID());

        enum class Action { None, DismountLeader, MountLeaderGround, MountLeaderFlying, ClearAuthorized };
        Action action = Action::None;

        // Step 1: Decision Snapshot under Shard Lock (ZERO recursive locking!)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            // 1. Maintain Mount Cooldown: Cooldown -> Unmounted
            BotMountRecord& mRec = shard.mounts[bot->GetGUID()];
            MountStateMachine::UpdateCooldown(mRec, now);

            // 2. Invariant Check: If internal locomotion state is Idle but MotionMaster has active bot generator
            BotLocomotionRecord& rec = shard.locomotion[bot->GetGUID()];
            MovementGeneratorType genType = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
            if (rec.state == LocomotionState::Idle &&
                (genType == POINT_MOTION_TYPE || genType == FOLLOW_MOTION_TYPE || genType == CHASE_MOTION_TYPE))
            {
                action = Action::ClearAuthorized;
            }

            // 3. Leader Mount Preference Debounce (1000ms stability)
            if (action == Action::None && mRec.leaderDesiredState != DesiredMountState::None &&
                (now - mRec.leaderStateObservedAt) >= MountStateMachine::LEADER_DEBOUNCE_MS)
            {
                if (mRec.leaderDesiredState == DesiredMountState::PreferUnmounted && bot->IsMounted())
                {
                    action = Action::DismountLeader;
                }
                else if (mRec.leaderDesiredState == DesiredMountState::PreferGround && !bot->IsMounted())
                {
                    action = Action::MountLeaderGround;
                }
                else if (mRec.leaderDesiredState == DesiredMountState::PreferFlying && !bot->IsMounted())
                {
                    action = Action::MountLeaderFlying;
                }
            }

            // 4. Safe Position Ring Buffer Maintenance (every 500ms when grounded on navmesh)
            uint32& lastSafe = shard.lastSafeCheck[bot->GetGUID()];
            if (now - lastSafe >= 500)
            {
                lastSafe = now;
                if (!bot->IsFlying() && !bot->IsInFlight() && !bot->IsInWater())
                {
                    shard.safeHistories[bot->GetGUID()].Push(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), now);
                }
            }

            // 5. Airborne watchdog
            uint32& airTime = shard.airborneMs[bot->GetGUID()];
            if (bot->IsFalling() && !bot->CanFly() && !bot->IsInFlight())
            {
                airTime += diff;
                if (airTime >= 1500)
                {
                    action = Action::ClearAuthorized;
                }
            }
            else
            {
                airTime = 0;
            }
        }

        // Step 2: Execute Decisions OUTSIDE Shard Lock
        if (action == Action::ClearAuthorized)
        {
            ClearAuthorizedMovement(bot);
        }
        else if (action == Action::DismountLeader)
        {
            RequestDismount(bot, MoveOwner::Travel, DismountReason::LeaderState);
        }
        else if (action == Action::MountLeaderGround)
        {
            RequestMount(bot, MoveOwner::Travel, 0.0f, false);
        }
        else if (action == Action::MountLeaderFlying)
        {
            RequestMount(bot, MoveOwner::Travel, 0.0f, true);
        }

        // Step 3: Resolve any pending mount cast
        ResolveMountCast(bot);
    }

    std::string Describe(Player* bot)
    {
        if (!bot)
            return "Null bot";

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto lItr = shard.locomotion.find(bot->GetGUID());
        auto mItr = shard.mounts.find(bot->GetGUID());
        auto rItr = shard.requests.find(bot->GetGUID());

        uint32 now = static_cast<uint32>(GameTime::GetGameTimeMS().count());
        uint64 cmdId = lItr != shard.locomotion.end() ? lItr->second.commandId : 0;
        MoveOwner owner = lItr != shard.locomotion.end() ? lItr->second.owner : MoveOwner::None;
        MoveMode mode = lItr != shard.locomotion.end() ? lItr->second.mode : MoveMode::Idle;
        LocomotionState state = lItr != shard.locomotion.end() ? lItr->second.state : LocomotionState::Idle;
        MountState mState = mItr != shard.mounts.end() ? mItr->second.state : (bot->IsMounted() ? MountState::MountedGround : MountState::Unmounted);
        uint32 pendingSpell = mItr != shard.mounts.end() ? mItr->second.pendingMountSpellId : 0;
        uint32 cooldownLeft = (mItr != shard.mounts.end() && mItr->second.remountCooldownUntilMs > now) ? (mItr->second.remountCooldownUntilMs - now) : 0;
        DismountReason dReason = mItr != shard.mounts.end() ? mItr->second.lastDismountReason : DismountReason::Manual;
        uint64 navGoal = rItr != shard.requests.end() ? rItr->second.goalId : 0;
        uint8 navStage = rItr != shard.requests.end() ? rItr->second.progress.stage : 0;
        uint32 btAttempts = rItr != shard.requests.end() ? rItr->second.backtrackAttempts : 0;

        return Acore::StringFormat(
            "cmdId={} owner={} mode={} state={} gen={} extCtrl={} target={} ranges=[{:.1f}, {:.1f}, ang={:.2f}] "
            "mountState={} pendingSpell={} cdLeft={}ms dismountReason={} navGoal={} navStage={} btAttempts={}",
            cmdId, MoveOwnerName(owner), MoveModeName(mode), LocomotionStateName(state),
            uint32(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()),
            IsExternallyControlled(bot),
            (lItr != shard.locomotion.end() && !lItr->second.targetGuid.IsEmpty()) ? lItr->second.targetGuid.ToString() : "none",
            lItr != shard.locomotion.end() ? lItr->second.chaseMinRange : 0.0f,
            lItr != shard.locomotion.end() ? lItr->second.chaseMaxRange : 0.0f,
            lItr != shard.locomotion.end() ? lItr->second.chaseAngle : 0.0f,
            MountStateName(mState), pendingSpell, cooldownLeft, DismountReasonName(dReason),
            navGoal, navStage, btAttempts);
    }

    std::string DescribeHistory(Player* bot)
    {
        if (!bot)
            return "Null bot";

        LocomotionShard& shard = GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto itr = shard.histories.find(bot->GetGUID());
        if (itr == shard.histories.end() || itr->second.GetCount() == 0)
            return "No locomotion history";

        std::string out;
        size_t count = std::min<size_t>(itr->second.GetCount(), 8);
        for (size_t i = 0; i < count; ++i)
        {
            TransitionRecord const* rec = itr->second.GetRecent(i);
            if (!rec)
                continue;
            out += Acore::StringFormat("[{}ms] {}->{} owner={} mode={} reason='{}' at ({:.1f}, {:.1f}, {:.1f})\n",
                rec->timeMs, LocomotionStateName(rec->oldState), LocomotionStateName(rec->newState),
                MoveOwnerName(rec->owner), MoveModeName(rec->mode), rec->reason, rec->x, rec->y, rec->z);
        }
        return out;
    }

    void Forget(ObjectGuid botGuid)
    {
        LocomotionShard& shard = GetShard(botGuid);
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.locomotion.erase(botGuid);
        shard.mounts.erase(botGuid);
        shard.safeHistories.erase(botGuid);
        shard.lastSafeCheck.erase(botGuid);
        shard.airborneMs.erase(botGuid);
        shard.histories.erase(botGuid);
        shard.requests.erase(botGuid);
        shard.nopathCache.erase(botGuid);
    }
}

