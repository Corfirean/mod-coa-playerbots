#include "BotMovement.h"
#include "AscensionCollectibleSpellData.h"
#include "Optional.h"
#include "GridTerrainData.h"
#include "GameTime.h"
#include "Group.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
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
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
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
    constexpr uint32 NOPATH_RECHECK_COOLDOWN_MS = 4000;
    constexpr uint32 MOUNT_FAIL_BACKOFF_MS = 1500;
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

    // Safely clears only bot-controlled normal locomotion generators without touching controlled motion.
    // Caller MUST ensure appropriate dispatch gate is held when clearing physical motion.
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

    // Internal force stop executed while caller ALREADY holds dispatch gate.
    void ForceStopOwnerInternal(Player* bot, MoveOwner owner)
    {
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
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

    // Internal release executed while caller ALREADY holds dispatch gate.
    bool ReleaseInternal(Player* bot, LocomotionToken const& token)
    {
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        bool matched = false;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            auto itr = shard.locomotion.find(bot->GetGUID());
            if (itr != shard.locomotion.end() && LocomotionCommandValidator::IsTokenMatch(itr->second, token))
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

    LocomotionToken IssueLeg(Player* bot, MovementRequest& req)
    {
        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();

        float tx = req.x;
        float ty = req.y;
        float tz = req.z;

        if (req.recoveryMode == RecoveryMode::DetourLeft || req.recoveryMode == RecoveryMode::DetourRight)
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

        // Token-safe: Issue leg under explicit Navigate MoveMode. No independent physical clear!
        LocomotionToken token = BotMovement::MoveToInternal(bot, req.owner, tx, ty, tz, /*forceDestination=*/false, MoveMode::Navigate);
        if (!token.IsValid())
            return {};

        NavigateTransaction::CommitLeg(req, token, tx, ty, NowMs());
        if (req.recoveryMode == RecoveryMode::Backtrack)
            req.backtrackCommandId = token.commandId;

        return token;
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

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        return LocomotionStateStore::CurrentOwnerLocked(shard, bot->GetGUID());
    }

    MoveMode CurrentMode(Player* bot)
    {
        if (!bot)
            return MoveMode::Idle;

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        return LocomotionStateStore::CurrentModeLocked(shard, bot->GetGUID());
    }

    LocomotionState CurrentState(Player* bot)
    {
        if (!bot)
            return LocomotionState::Idle;

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        return LocomotionStateStore::CurrentStateLocked(shard, bot->GetGUID());
    }

    bool CanClaim(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return false;

        bool isControlled = IsExternallyControlled(bot);
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        return LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, isControlled);
    }

    std::optional<MovementRequest> GetRequestSnapshot(ObjectGuid botGuid)
    {
        LocomotionShard& shard = LocomotionStateStore::GetShard(botGuid);
        std::lock_guard<std::mutex> lock(shard.mutex);
        return LocomotionStateStore::GetRequestSnapshotLocked(shard, botGuid);
    }

    void ResetRequest(ObjectGuid botGuid)
    {
        LocomotionShard& shard = LocomotionStateStore::GetShard(botGuid);
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.requests.erase(botGuid);
    }

    MovementStats Stats()
    {
        return _stats.Snapshot();
    }

    // =========================================================================
    // Arbitrated Locomotion Primitives (Linearized Physical Dispatch & Token-Safe)
    // =========================================================================

    LocomotionToken MoveToInternal(Player* bot, MoveOwner owner, float x, float y, float z, bool forceDestination, MoveMode executionMode)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return {};

        if (bot->IsNonMeleeSpellCast(false))
            return {};

        if (IsExternallyControlled(bot))
            return {};

        uint32 now = NowMs();
        float groundZ = ResolveGroundZ(bot, x, y, z);
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());

        // Step 1: Pre-validation under shard state lock.
        // TRANSACTIONAL: Do NOT commit owner or increment commandId before PathGenerator succeeds!
        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return {};

            auto it = shard.locomotion.find(bot->GetGUID());
            if (it != shard.locomotion.end())
            {
                BotLocomotionRecord const& rec = it->second;
                // Redundancy check: if already walking to essentially the same point
                if (rec.owner == owner && (rec.mode == executionMode || (rec.mode == MoveMode::Point && executionMode == MoveMode::Navigate)) &&
                    bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE &&
                    std::fabs(rec.destX - x) <= SAME_POINT_YARDS &&
                    std::fabs(rec.destY - y) <= SAME_POINT_YARDS &&
                    std::fabs(rec.destZ - groundZ) <= SAME_POINT_YARDS * 3.0f)
                {
                    _stats.redundantSkipped.fetch_add(1, std::memory_order_relaxed);
                    return LocomotionToken{bot->GetGUID(), owner, rec.commandId};
                }
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
        }

        // Step 2: Outside shard state lock AND outside dispatch gate: compute PathGenerator!
        // This ensures unrelated bots sharing the striped dispatch gate are not blocked during pathfinding.
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

        // Step 3: Handle path result under shard state lock
        if (!pathValid)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            // Record failure in nopath cache WITHOUT touching rec.owner or rec.commandId!
            NopathCacheEntry& entry = shard.nopathCache[bot->GetGUID()];
            entry.owner = owner;
            entry.x = x;
            entry.y = y;
            entry.z = groundZ;
            entry.expiresAtMs = now + NOPATH_RECHECK_COOLDOWN_MS;
            return {};
        }

        // Step 4: Serialize physical MotionMaster dispatch under dispatch gate
        std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            // Revalidate CanClaim in case state changed during PathGenerator calculation
            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return {};

            BotLocomotionRecord& rec = shard.locomotion[bot->GetGUID()];
            rec.commandId++;
            rec.owner = owner;
            rec.destX = x;
            rec.destY = y;
            rec.destZ = groundZ;
            rec.mode = executionMode;
            rec.state = LocomotionState::Moving;
            rec.issuedAtMs = now;
            rec.lastUpdateMs = now;
            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};

            if (executionMode != MoveMode::Navigate)
            {
                shard.requests.erase(bot->GetGUID());
            }

            RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Planning, LocomotionState::Moving,
                owner, executionMode, (executionMode == MoveMode::Navigate ? "NavigateLegIssued" : "MovePointIssued"),
                x, y, groundZ, now);
        }

        // Step 5: Dispatch MotionMaster MovePoint outside shard state lock (under dispatch gate)
        ClearAuthorizedMovement(bot);
        bot->GetMotionMaster()->MovePoint(uint32(owner), x, y, groundZ, FORCED_MOVEMENT_NONE, 0.0f, 0.0f,
            /*generatePath=*/true, forceDestination);
        _stats.issued.fetch_add(1, std::memory_order_relaxed);

        return token;
    }

    LocomotionToken MoveTo(Player* bot, MoveOwner owner, float x, float y, float z, bool forceDestination)
    {
        return MoveToInternal(bot, owner, x, y, z, forceDestination, MoveMode::Point);
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

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());

        // Arrival Check
        if (distToGoal <= acceptRadius)
        {
            LocomotionToken tokenToRelease;
            {
                std::lock_guard<std::mutex> lock(shard.mutex);
                auto rItr = shard.requests.find(bot->GetGUID());
                if (rItr != shard.requests.end())
                {
                    if (NavigateTransaction::ValidateArrival(rItr->second, owner, goalId, distToGoal, acceptRadius, tokenToRelease))
                    {
                        shard.requests.erase(rItr);
                    }
                }
            }

            if (tokenToRelease.IsValid())
            {
                Release(bot, tokenToRelease);
                return NavStatus::Arrived;
            }
            else
            {
                // Stale arrival: request was already superseded by newer goal or cleared.
                // Do NOT touch any state or MotionMaster!
                return NavStatus::Superseded;
            }
        }

        // Step 1: Snapshot and prepare request under shard state lock
        MovementRequest reqCopy;
        uint64 expectedRequestGen = 0;
        bool isNewRequest = false;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return NavStatus::Blocked;

            auto rItr = shard.requests.find(bot->GetGUID());
            if (rItr == shard.requests.end() || rItr->second.goalId != goalId || rItr->second.owner != owner ||
                Dist2d(rItr->second.x, rItr->second.y, x, y) > GOAL_MOVED_YARDS)
            {
                isNewRequest = true;
                expectedRequestGen = LocomotionStateStore::AllocateRequestGenerationLocked(shard, bot->GetGUID());

                MovementRequest& req = shard.requests[bot->GetGUID()];
                req.botGuid = bot->GetGUID();
                req.owner = owner;
                req.goalId = goalId;
                req.requestGeneration = expectedRequestGen;
                req.movementCommandId = 0; // Not issued yet
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

                reqCopy = req;
            }
            else
            {
                rItr->second.lastCallAt = now;
                rItr->second.acceptRadius = acceptRadius;
                expectedRequestGen = rItr->second.requestGeneration;
                reqCopy = rItr->second;
            }
        }

        // Step 2: Handle First Leg
        if (isNewRequest)
        {
            LocomotionToken token = IssueLeg(bot, reqCopy);
            if (token.IsValid())
            {
                bool superseded = false;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    auto rItr = shard.requests.find(bot->GetGUID());
                    if (rItr != shard.requests.end() &&
                        NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId))
                    {
                        rItr->second = reqCopy;
                        rItr->second.movementCommandId = token.commandId;
                        if (outToken)
                            *outToken = token;
                    }
                    else
                    {
                        superseded = true;
                    }
                }

                if (superseded)
                {
                    Release(bot, token);
                    return NavStatus::Superseded;
                }
                return NavStatus::Moving;
            }
            else
            {
                // First-leg failure: DO NOT release anything! No token was issued.
                // Erase own request generation if still current:
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    auto rItr = shard.requests.find(bot->GetGUID());
                    if (rItr != shard.requests.end() && rItr->second.requestGeneration == expectedRequestGen)
                    {
                        shard.requests.erase(rItr);
                    }
                }
                return NavStatus::Blocked;
            }
        }

        // Step 3: Backtrack state machine
        float distZ = std::abs(bz - z);
        if (reqCopy.recoveryMode == RecoveryMode::Backtrack)
        {
            float distToBacktrack = Dist2d(bx, by, reqCopy.backtrackX, reqCopy.backtrackY);
            if (distToBacktrack <= 3.5f)
            {
                // Arrived at backtrack safe position! Rebase progress and resume journey toward original goal
                reqCopy.recoveryMode = RecoveryMode::None;
                reqCopy.backtrackAttempts = 0;
                reqCopy.progress.Rebase(distToGoal, distZ, now);
                LocomotionToken token = IssueLeg(bot, reqCopy);
                if (token.IsValid())
                {
                    bool superseded = false;
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        auto rItr = shard.requests.find(bot->GetGUID());
                        if (rItr != shard.requests.end() &&
                            NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId))
                        {
                            rItr->second = reqCopy;
                            rItr->second.movementCommandId = token.commandId;
                            if (outToken)
                                *outToken = token;
                        }
                        else
                        {
                            superseded = true;
                        }
                    }
                    if (superseded)
                    {
                        Release(bot, token);
                        return NavStatus::Superseded;
                    }
                    return NavStatus::Moving;
                }
            }
            else if (!bot->isMoving() && (now - reqCopy.lastIssueAt > 1500))
            {
                // Stopped early without arriving at backtrack target! Advance candidate.
                reqCopy.backtrackAttempts++;
                std::optional<SafePosition> safeTarget;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    safeTarget = shard.safeHistories[bot->GetGUID()].FindBacktrackTarget(bx, by, bz, bot->GetMapId(), reqCopy.backtrackAttempts);
                }

                if (safeTarget && reqCopy.backtrackAttempts < 3)
                {
                    _stats.backtracks.fetch_add(1, std::memory_order_relaxed);
                    reqCopy.backtrackX = safeTarget->x;
                    reqCopy.backtrackY = safeTarget->y;
                    reqCopy.backtrackZ = safeTarget->z;
                    LocomotionToken token = IssueLeg(bot, reqCopy);
                    if (token.IsValid())
                    {
                        bool superseded = false;
                        {
                            std::lock_guard<std::mutex> lock(shard.mutex);
                            auto rItr = shard.requests.find(bot->GetGUID());
                            if (rItr != shard.requests.end() &&
                                NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId))
                            {
                                rItr->second = reqCopy;
                                rItr->second.movementCommandId = token.commandId;
                                if (outToken)
                                    *outToken = token;
                            }
                            else
                            {
                                superseded = true;
                            }
                        }
                        if (superseded)
                        {
                            Release(bot, token);
                            return NavStatus::Superseded;
                        }
                    }
                }
                else
                {
                    _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                    if (reqCopy.movementCommandId != 0)
                        Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.movementCommandId});
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        auto rItr = shard.requests.find(bot->GetGUID());
                        if (rItr != shard.requests.end() && rItr->second.requestGeneration == expectedRequestGen)
                            shard.requests.erase(rItr);
                    }
                    return NavStatus::Stuck;
                }
            }
        }

        // Step 4: Progress evaluation
        NavRecovery recovery = reqCopy.progress.Update(distToGoal, distZ, now, PROGRESS_RULES);

        // If progress said GiveUp, check if Backtrack candidates exist before giving up!
        if (recovery == NavRecovery::GiveUp)
        {
            if (reqCopy.recoveryMode != RecoveryMode::Backtrack && reqCopy.backtrackAttempts < 3)
            {
                std::optional<SafePosition> safeTarget;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    safeTarget = shard.safeHistories[bot->GetGUID()].FindBacktrackTarget(bx, by, bz, bot->GetMapId(), reqCopy.backtrackAttempts);
                }

                if (safeTarget)
                {
                    _stats.backtracks.fetch_add(1, std::memory_order_relaxed);
                    reqCopy.recoveryMode = RecoveryMode::Backtrack;
                    reqCopy.backtrackAttempts++;
                    reqCopy.backtrackX = safeTarget->x;
                    reqCopy.backtrackY = safeTarget->y;
                    reqCopy.backtrackZ = safeTarget->z;
                    LocomotionToken token = IssueLeg(bot, reqCopy);
                    if (token.IsValid())
                    {
                        bool superseded = false;
                        {
                            std::lock_guard<std::mutex> lock(shard.mutex);
                            auto rItr = shard.requests.find(bot->GetGUID());
                            if (rItr != shard.requests.end() &&
                                NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId))
                            {
                                rItr->second = reqCopy;
                                rItr->second.movementCommandId = token.commandId;
                                if (outToken)
                                    *outToken = token;
                            }
                            else
                            {
                                superseded = true;
                            }
                        }
                        if (superseded)
                        {
                            Release(bot, token);
                            return NavStatus::Superseded;
                        }
                    }
                }
                else
                {
                    _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                    if (reqCopy.movementCommandId != 0)
                        Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.movementCommandId});
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        auto rItr = shard.requests.find(bot->GetGUID());
                        if (rItr != shard.requests.end() && rItr->second.requestGeneration == expectedRequestGen)
                            shard.requests.erase(rItr);
                    }
                    return NavStatus::Stuck;
                }
            }
            else
            {
                _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                if (reqCopy.movementCommandId != 0)
                    Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.movementCommandId});
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    auto rItr = shard.requests.find(bot->GetGUID());
                    if (rItr != shard.requests.end() && rItr->second.requestGeneration == expectedRequestGen)
                        shard.requests.erase(rItr);
                }
                return NavStatus::Stuck;
            }
        }
        else if (recovery == NavRecovery::Repath)
        {
            _stats.stuckEvents.fetch_add(1, std::memory_order_relaxed);
            _stats.repaths.fetch_add(1, std::memory_order_relaxed);
            reqCopy.recoveryMode = RecoveryMode::Repath;
            reqCopy.detour = false;
            LocomotionToken token = IssueLeg(bot, reqCopy);
            if (token.IsValid())
            {
                bool superseded = false;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    auto rItr = shard.requests.find(bot->GetGUID());
                    if (rItr != shard.requests.end() &&
                        NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId))
                    {
                        rItr->second = reqCopy;
                        rItr->second.movementCommandId = token.commandId;
                        if (outToken)
                            *outToken = token;
                    }
                    else
                    {
                        superseded = true;
                    }
                }
                if (superseded)
                {
                    Release(bot, token);
                    return NavStatus::Superseded;
                }
            }
        }
        else if (recovery == NavRecovery::DetourLeft || recovery == NavRecovery::DetourRight)
        {
            _stats.stuckEvents.fetch_add(1, std::memory_order_relaxed);
            _stats.detours.fetch_add(1, std::memory_order_relaxed);
            reqCopy.recoveryMode = (recovery == NavRecovery::DetourLeft) ? RecoveryMode::DetourLeft : RecoveryMode::DetourRight;
            reqCopy.detour = true;
            float angle = bot->GetOrientation() + (recovery == NavRecovery::DetourLeft ? 1.047f : -1.047f);
            reqCopy.detourX = bx + std::cos(angle) * 15.0f;
            reqCopy.detourY = by + std::sin(angle) * 15.0f;
            reqCopy.detourZ = ResolveGroundZ(bot, reqCopy.detourX, reqCopy.detourY, bz);

            LocomotionToken token = IssueLeg(bot, reqCopy);
            if (!token.IsValid())
            {
                // Detour issue failed -> escalate to Backtrack
                std::optional<SafePosition> safeTarget;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    safeTarget = shard.safeHistories[bot->GetGUID()].FindBacktrackTarget(bx, by, bz, bot->GetMapId(), reqCopy.backtrackAttempts);
                }

                if (safeTarget && reqCopy.backtrackAttempts < 3)
                {
                    _stats.backtracks.fetch_add(1, std::memory_order_relaxed);
                    reqCopy.recoveryMode = RecoveryMode::Backtrack;
                    reqCopy.backtrackAttempts++;
                    reqCopy.backtrackX = safeTarget->x;
                    reqCopy.backtrackY = safeTarget->y;
                    reqCopy.backtrackZ = safeTarget->z;
                    LocomotionToken bToken = IssueLeg(bot, reqCopy);
                    if (bToken.IsValid())
                    {
                        bool superseded = false;
                        {
                            std::lock_guard<std::mutex> lock(shard.mutex);
                            auto rItr = shard.requests.find(bot->GetGUID());
                            if (rItr != shard.requests.end() &&
                                NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId))
                            {
                                rItr->second = reqCopy;
                                rItr->second.movementCommandId = bToken.commandId;
                                if (outToken)
                                    *outToken = bToken;
                            }
                            else
                            {
                                superseded = true;
                            }
                        }
                        if (superseded)
                        {
                            Release(bot, bToken);
                            return NavStatus::Superseded;
                        }
                    }
                }
                else
                {
                    _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                    if (reqCopy.movementCommandId != 0)
                        Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.movementCommandId});
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        auto rItr = shard.requests.find(bot->GetGUID());
                        if (rItr != shard.requests.end() && rItr->second.requestGeneration == expectedRequestGen)
                            shard.requests.erase(rItr);
                    }
                    return NavStatus::Stuck;
                }
            }
            else
            {
                bool superseded = false;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    auto rItr = shard.requests.find(bot->GetGUID());
                    if (rItr != shard.requests.end() &&
                        NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId))
                    {
                        rItr->second = reqCopy;
                        rItr->second.movementCommandId = token.commandId;
                        if (outToken)
                            *outToken = token;
                    }
                    else
                    {
                        superseded = true;
                    }
                }
                if (superseded)
                {
                    Release(bot, token);
                    return NavStatus::Superseded;
                }
            }
        }

        // Check if Detour leg stalled
        if ((reqCopy.recoveryMode == RecoveryMode::DetourLeft || reqCopy.recoveryMode == RecoveryMode::DetourRight) &&
            !bot->isMoving() && (now - reqCopy.lastIssueAt > 1500))
        {
            std::optional<SafePosition> safeTarget;
            {
                std::lock_guard<std::mutex> lock(shard.mutex);
                safeTarget = shard.safeHistories[bot->GetGUID()].FindBacktrackTarget(bx, by, bz, bot->GetMapId(), reqCopy.backtrackAttempts);
            }

            if (safeTarget && reqCopy.backtrackAttempts < 3)
            {
                _stats.backtracks.fetch_add(1, std::memory_order_relaxed);
                reqCopy.backtrackAttempts++;
                reqCopy.recoveryMode = RecoveryMode::Backtrack;
                reqCopy.backtrackX = safeTarget->x;
                reqCopy.backtrackY = safeTarget->y;
                reqCopy.backtrackZ = safeTarget->z;
                LocomotionToken token = IssueLeg(bot, reqCopy);
                if (token.IsValid())
                {
                    bool superseded = false;
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        auto rItr = shard.requests.find(bot->GetGUID());
                        if (rItr != shard.requests.end() &&
                            NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId))
                        {
                            rItr->second = reqCopy;
                            rItr->second.movementCommandId = token.commandId;
                            if (outToken)
                                *outToken = token;
                        }
                        else
                        {
                            superseded = true;
                        }
                    }
                    if (superseded)
                    {
                        Release(bot, token);
                        return NavStatus::Superseded;
                    }
                }
            }
            else
            {
                _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                if (reqCopy.movementCommandId != 0)
                    Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.movementCommandId});
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    auto rItr = shard.requests.find(bot->GetGUID());
                    if (rItr != shard.requests.end() && rItr->second.requestGeneration == expectedRequestGen)
                        shard.requests.erase(rItr);
                }
                return NavStatus::Stuck;
            }
        }

        // Check if current normal leg completed and next leg needed
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE && !bot->isMoving())
        {
            float legDist = Dist2d(bx, by, reqCopy.legX, reqCopy.legY);
            if (legDist <= 4.0f && distToGoal > acceptRadius)
            {
                LocomotionToken token = IssueLeg(bot, reqCopy);
                if (token.IsValid())
                {
                    bool superseded = false;
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        auto rItr = shard.requests.find(bot->GetGUID());
                        if (rItr != shard.requests.end() &&
                            NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId))
                        {
                            rItr->second = reqCopy;
                            rItr->second.movementCommandId = token.commandId;
                            if (outToken)
                                *outToken = token;
                        }
                        else
                        {
                            superseded = true;
                        }
                    }
                    if (superseded)
                    {
                        Release(bot, token);
                        return NavStatus::Superseded;
                    }
                    return NavStatus::Moving;
                }
            }
        }

        // Commit updated request state under shard state lock
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            auto rItr = shard.requests.find(bot->GetGUID());
            if (rItr != shard.requests.end() &&
                NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId))
            {
                rItr->second = reqCopy;
                if (outToken && reqCopy.movementCommandId != 0)
                    *outToken = LocomotionToken{bot->GetGUID(), owner, reqCopy.movementCommandId};
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

        std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
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
            shard.requests.erase(bot->GetGUID());

            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
            RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Idle, LocomotionState::Following,
                owner, MoveMode::Follow, "FollowIssued", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), NowMs());
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

        std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
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
            shard.requests.erase(bot->GetGUID());

            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
            RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Idle, LocomotionState::Chasing,
                owner, MoveMode::Chase, "ChaseIssued", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), NowMs());
        }

        ClearAuthorizedMovement(bot);

        // MotionMaster::MoveChase dispatch:
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

        std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return {};

            BotLocomotionRecord& rec = shard.locomotion[bot->GetGUID()];
            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::Hold;
            rec.state = LocomotionState::Holding;
            rec.holdUntilMs = NowMs() + durationMs;
            rec.issuedAtMs = NowMs();
            rec.lastUpdateMs = NowMs();
            shard.requests.erase(bot->GetGUID());

            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
            RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Idle, LocomotionState::Holding,
                owner, MoveMode::Hold, "HoldIssued", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), NowMs());
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

        std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
        return ReleaseInternal(bot, token);
    }

    bool Stop(Player* bot, LocomotionToken const& token)
    {
        return Release(bot, token);
    }

    void ForceStopOwner(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return;

        std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
        ForceStopOwnerInternal(bot, owner);
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
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        bool isDungeon = bot->GetMap() && bot->GetMap()->IsDungeon();
        bool isRaid = bot->GetMap() && bot->GetMap()->IsRaid();
        bool isInWater = bot->IsInWater();
        bool isSwimming = bot->HasUnitMovementFlag(MOVEMENTFLAG_SWIMMING);

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        return LocomotionStateStore::CanMountLocked(shard, bot->GetGUID(), travelDistance, NowMs(),
            bot->IsOutdoors(), bot->IsInCombat(), bot->IsNonMeleeSpellCast(false),
            bot->GetLevel(), IsExternallyControlled(bot), isDungeon, isRaid, isInWater, isSwimming);
    }


    bool IsMounted(Player const* bot)
    {
        return bot && bot->IsMounted();
    }

    MountState GetMountState(Player const* bot)
    {
        if (!bot)
            return MountState::Unmounted;

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto itr = shard.mounts.find(bot->GetGUID());
        return (itr == shard.mounts.end()) ? MountState::Unmounted : itr->second.state;
    }

    uint32 GetPendingMountSpell(Player const* bot)
    {
        if (!bot)
            return 0;

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto itr = shard.mounts.find(bot->GetGUID());
        return (itr == shard.mounts.end()) ? 0 : itr->second.pendingMountSpellId;
    }

    uint32 SelectMountSpell(Player* bot, bool wantFlying)
    {
        if (!bot)
            return 0;

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord const& mRec = shard.mounts[bot->GetGUID()];

        if (wantFlying)
        {
            uint32 defaultFly = GetDefaultFlyingMountSpell(bot);
            if (defaultFly && bot->HasSpell(defaultFly) && !mRec.knownBadMountSpells.count(defaultFly))
                return defaultFly;

            for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
            {
                if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                    continue;
                if (!bot->HasSpell(spellId) || mRec.knownBadMountSpells.count(spellId))
                    continue;

                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                if (IsMountSpellInfo(spellInfo) && IsFlyingMountSpellInfo(spellInfo))
                    return spellId;
            }

            uint32 racialGround = GetRacialGroundMountSpell(bot->getRace());
            if (racialGround && bot->HasSpell(racialGround) && !mRec.knownBadMountSpells.count(racialGround))
                return racialGround;

            return 0;
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
                if (!bot->HasSpell(spellId) || mRec.knownBadMountSpells.count(spellId))
                    continue;

                SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                if (IsMountSpellInfo(spellInfo) && !IsFlyingMountSpellInfo(spellInfo))
                    return spellId;
            }

            if (racialSpellId && bot->HasSpell(racialSpellId))
                return racialSpellId;

            return 0;
        }
    }

    bool RequestMountInternal(Player* bot, MoveOwner owner, float travelDistance, bool wantFlying)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        if (IsExternallyControlled(bot))
            return false;

        uint32 now = NowMs();
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());

        bool isDungeon = bot->GetMap() && bot->GetMap()->IsDungeon();
        bool isRaid = bot->GetMap() && bot->GetMap()->IsRaid();
        bool isInWater = bot->IsInWater();
        bool isSwimming = bot->HasUnitMovementFlag(MOVEMENTFLAG_SWIMMING);

        // Step 1: Pre-validation under shard state lock
        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!LocomotionStateStore::CanMountLocked(shard, bot->GetGUID(), travelDistance, now,
                    bot->IsOutdoors(), bot->IsInCombat(), bot->IsNonMeleeSpellCast(false),
                    bot->GetLevel(), false, isDungeon, isRaid, isInWater, isSwimming))
                return false;

            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return false;
        }

        // Step 2: Stop any active movement under this owner so mount cast does not cancel immediately.
        ForceStopOwnerInternal(bot, owner);
        if (bot->isMoving())
            return false;

        // Step 3: Select mount spell
        if (wantFlying)
        {
            uint32 mapId = bot->GetMapId();
            if ((mapId != 530 && mapId != 571) || bot->GetLevel() < 60)
                wantFlying = false;
        }

        uint32 spellId = SelectMountSpell(bot, wantFlying);
        if (!spellId)
            return false;

        // Step 4: Record state under shard lock
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotMountRecord& mRec = shard.mounts[bot->GetGUID()];
            mRec.pendingMountSpellId = spellId;
            mRec.state = MountState::MountCasting;
            mRec.mountCastStartedAt = now;
            mRec.mountOwner = owner;
            mRec.mountGeneration++;
            _stats.mountAttempts.fetch_add(1, std::memory_order_relaxed);
        }

        // Step 5: OUTSIDE shard lock: Cast Spell
        SpellCastResult result = bot->CastSpell(bot, spellId, false);
        if (result == SPELL_CAST_OK)
        {
            return true;
        }

        // Cast failed immediately: apply failure backoff cooldown (1500ms)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotMountRecord& mRec = shard.mounts[bot->GetGUID()];
            MountStateMachine::TransitionCastFailed(mRec, now, MOUNT_FAIL_BACKOFF_MS);

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

    bool RequestMount(Player* bot, MoveOwner owner, float travelDistance, bool wantFlying)
    {
        if (!bot)
            return false;
        std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
        return RequestMountInternal(bot, owner, travelDistance, wantFlying);
    }

    void RequestDismountInternal(Player* bot, MoveOwner owner, DismountReason reason)
    {
        if (!bot)
            return;

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        MountTransitionResult trans;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotMountRecord& mRec = shard.mounts[bot->GetGUID()];
            trans = MountStateMachine::TransitionDismount(mRec, NowMs());
            mRec.lastDismountReason = reason;

            RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Moving, LocomotionState::Idle, owner, MoveMode::Idle,
                DismountReasonName(reason), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), NowMs());
        }

        if (trans.interruptedSpellId != 0)
            bot->InterruptNonMeleeSpells(false);

        if (trans.shouldDismount || bot->IsMounted())
        {
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
            _stats.dismounts.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void RequestDismount(Player* bot, MoveOwner owner, DismountReason reason)
    {
        if (!bot)
            return;
        std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
        RequestDismountInternal(bot, owner, reason);
    }


    void SetLeaderMountPreference(Player* bot, DesiredMountState pref)
    {
        if (!bot)
            return;

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot->GetGUID()];
        if (mRec.leaderDesiredState != pref)
        {
            mRec.leaderDesiredState = pref;
            mRec.leaderStateObservedAt = NowMs();
        }
    }

    void TryMatchLeaderMountState(Player* bot)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return;

        Group* group = bot->GetGroup();
        if (!group)
        {
            SetLeaderMountPreference(bot, DesiredMountState::None);
            return;
        }

        ObjectGuid leaderGuid = group->GetLeaderGUID();
        if (leaderGuid.IsEmpty() || leaderGuid == bot->GetGUID())
        {
            SetLeaderMountPreference(bot, DesiredMountState::None);
            return;
        }

        Player* leader = ObjectAccessor::FindPlayer(leaderGuid);
        if (!leader || !leader->IsInWorld() || leader->GetMapId() != bot->GetMapId())
        {
            SetLeaderMountPreference(bot, DesiredMountState::None);
            return;
        }

        if (leader->IsFlying())
        {
            SetLeaderMountPreference(bot, DesiredMountState::PreferFlying);
        }
        else if (leader->IsMounted())
        {
            SetLeaderMountPreference(bot, DesiredMountState::PreferGround);
        }
        else
        {
            SetLeaderMountPreference(bot, DesiredMountState::PreferUnmounted);
        }
    }

    void ResolveMountCast(Player* bot)
    {
        if (!bot)
            return;

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        uint32 now = NowMs();

        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot->GetGUID()];

        if (mRec.pendingMountSpellId == 0)
        {
            // If mounted physically but record unmounted, sync only if not in dismount cooldown
            if (bot->IsMounted() && mRec.state != MountState::MountedGround && mRec.state != MountState::MountedFlying &&
                now >= mRec.remountCooldownUntilMs)
            {
                mRec.state = bot->CanFly() ? MountState::MountedFlying : MountState::MountedGround;
            }
            return;
        }

        // Mount casts take ~1.5s. If bot is still non-melee casting, wait for it!
        if (bot->IsNonMeleeSpellCast(false))
            return;

        uint32 justTried = mRec.pendingMountSpellId;
        mRec.pendingMountSpellId = 0;

        if (bot->IsMounted())
        {
            mRec.state = bot->CanFly() ? MountState::MountedFlying : MountState::MountedGround;
            _stats.mountSuccesses.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            // Cast ended without becoming mounted (interrupted or failed)
            MountStateMachine::TransitionCastFailed(mRec, now, MOUNT_FAIL_BACKOFF_MS);
            if (!bot->IsInCombat() && !bot->isMoving() &&
                justTried != GetRacialGroundMountSpell(bot->getRace()) &&
                justTried != GetDefaultFlyingMountSpell(bot))
            {
                mRec.knownBadMountSpells.insert(justTried);
            }
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
        std::optional<SafePosition> target;

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            target = shard.safeHistories[bot->GetGUID()].FindBacktrackTarget(bx, by, bz, bot->GetMapId(), 0);
        }

        if (!target)
            return false;

        _stats.backtracks.fetch_add(1, std::memory_order_relaxed);
        LocomotionToken token = MoveTo(bot, owner, target->x, target->y, target->z, false);
        return token.IsValid();
    }

    void ApplyMountPolicy(Player* bot, uint32 now)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return;

        std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());

        MountPolicyDecision decision;
        MoveOwner currentOwner = MoveOwner::None;
        float travelDist = 0.0f;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotLocomotionRecord const& rec = shard.locomotion[bot->GetGUID()];
            BotMountRecord const& mRec = shard.mounts[bot->GetGUID()];
            currentOwner = rec.owner;

            // Travel distance is strictly tied to an active, matching Navigate request in Navigate mode!
            if (rec.mode == MoveMode::Navigate)
            {
                auto reqIt = shard.requests.find(bot->GetGUID());
                if (reqIt != shard.requests.end() && reqIt->second.movementCommandId == rec.commandId)
                {
                    travelDist = Dist2d(bot->GetPositionX(), bot->GetPositionY(), reqIt->second.x, reqIt->second.y);
                }
            }

            bool isFollowingLeader = (rec.mode == MoveMode::Follow && bot->GetGroup() != nullptr);
            bool leaderPrefDebounced = (now - mRec.leaderStateObservedAt >= MountStateMachine::LEADER_DEBOUNCE_MS);
            bool isDungeon = bot->GetMap() && bot->GetMap()->IsDungeon();
            bool isRaid = bot->GetMap() && bot->GetMap()->IsRaid();
            bool isInWater = bot->IsInWater();
            bool isSwimming = bot->HasUnitMovementFlag(MOVEMENTFLAG_SWIMMING);

            decision = MountPolicyResolver::Evaluate(
                rec.owner,
                rec.mode,
                travelDist,
                isFollowingLeader,
                mRec.leaderDesiredState,
                leaderPrefDebounced,
                bot->IsOutdoors(),
                bot->IsInCombat(),
                bot->IsNonMeleeSpellCast(false),
                bot->GetLevel(),
                BotMovement::IsExternallyControlled(bot),
                mRec.state,
                mRec.remountCooldownUntilMs,
                now,
                bot->CanFly(),
                bot->IsMounted(),
                isDungeon,
                isRaid,
                isInWater,
                isSwimming
            );
        }

        if (decision.action == MountAction::Dismount && (bot->IsMounted() || GetMountState(bot) == MountState::MountCasting))
        {
            RequestDismountInternal(bot, currentOwner, decision.dismountReason);
        }
        else if (decision.action == MountAction::MountGround && !bot->IsMounted() && GetMountState(bot) != MountState::MountCasting)
        {
            RequestMountInternal(bot, currentOwner, travelDist, false);
        }
        else if (decision.action == MountAction::MountFlying && !bot->IsMounted() && GetMountState(bot) != MountState::MountCasting)
        {
            RequestMountInternal(bot, currentOwner, travelDist, true);
        }
    }

    void Update(Player* bot, uint32 diff)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return;

        uint32 now = NowMs();
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());

        struct DeferredAction
        {
            enum class Type { None, ClearAuthorized, AirborneRecover } type = Type::None;
            uint64 expectedCommandId = 0;
            MoveOwner expectedOwner = MoveOwner::None;
            MoveMode expectedMode = MoveMode::Idle;
            LocomotionState expectedState = LocomotionState::Idle;
        } deferred;

        // Update leader preference if in group
        TryMatchLeaderMountState(bot);

        // Step 1: Decision Snapshot under Shard State Lock (ZERO recursive locking!)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            // 1. Maintain Mount Cooldown: Cooldown -> Unmounted
            BotMountRecord& mRec = shard.mounts[bot->GetGUID()];
            MountStateMachine::UpdateCooldown(mRec, now);

            BotLocomotionRecord& rec = shard.locomotion[bot->GetGUID()];

            // 2. Hold Expiration Check
            if (rec.state == LocomotionState::Holding && rec.holdUntilMs > 0 && now >= rec.holdUntilMs)
            {
                RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Holding, LocomotionState::Idle,
                    rec.owner, MoveMode::Idle, "HoldExpired", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), now);
                rec.state = LocomotionState::Idle;
                rec.owner = MoveOwner::None;
                rec.mode = MoveMode::Idle;
                rec.holdUntilMs = 0;
            }

            // 3. Generator Completion Reconciliation (Mode-aware: Point, Follow, Chase)
            MovementGeneratorType genType = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
            bool isMoving = bot->isMoving();

            if (GeneratorReconciler::ReconcilePoint(rec, genType, isMoving, now))
            {
                RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Moving, LocomotionState::Idle,
                    rec.owner, MoveMode::Idle, "PointArrivedReconciled", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), now);
            }
            else if (GeneratorReconciler::ReconcileFollow(rec, genType, isMoving, now))
            {
                RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Following, LocomotionState::Idle,
                    rec.owner, MoveMode::Idle, "FollowEndedReconciled", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), now);
            }
            else if (GeneratorReconciler::ReconcileChase(rec, genType, isMoving, now))
            {
                RecordTransitionLocked(shard, bot->GetGUID(), LocomotionState::Chasing, LocomotionState::Idle,
                    rec.owner, MoveMode::Idle, "ChaseEndedReconciled", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), now);
            }

            // 4. Invariant Check: If internal locomotion state is Idle but MotionMaster has active bot generator
            if (rec.state == LocomotionState::Idle &&
                (genType == POINT_MOTION_TYPE || genType == FOLLOW_MOTION_TYPE || genType == CHASE_MOTION_TYPE))
            {
                deferred.type = DeferredAction::Type::ClearAuthorized;
                deferred.expectedCommandId = rec.commandId;
                deferred.expectedOwner = rec.owner;
                deferred.expectedMode = rec.mode;
                deferred.expectedState = rec.state;
            }
        }

        // Step 2: Safe Position Maintenance & Airborne Watchdog (terrain query OUTSIDE shard lock)
        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();
        float bz = bot->GetPositionZ();
        float groundZ = ResolveGroundZ(bot, bx, by, bz);

        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotLocomotionRecord const& rec = shard.locomotion[bot->GetGUID()];

            // Safe position candidates: ground-confirmed recovery candidates
            uint32& lastSafe = shard.lastSafeCheck[bot->GetGUID()];
            if (now - lastSafe >= 500)
            {
                lastSafe = now;
                uint32 curMap = bot->GetMapId();
                if (shard.lastRecordedMap[bot->GetGUID()] != curMap)
                {
                    shard.safeHistories[bot->GetGUID()].Clear();
                    shard.lastRecordedMap[bot->GetGUID()] = curMap;
                }

                if (!bot->CanFly() && !bot->IsFlying() && !bot->IsInFlight() &&
                    !bot->IsInWater() && !bot->GetVehicle() && !BotMovement::IsExternallyControlled(bot) &&
                    !bot->IsFalling() && !bot->HasUnitState(UNIT_STATE_JUMPING) &&
                    std::abs(bz - groundZ) < 2.0f)
                {
                    shard.safeHistories[bot->GetGUID()].Push(bx, by, bz, curMap, now);
                }
            }

            // Airborne Watchdog: catch suspended ground bots (>5.0yd above ground, stationary)
            uint32& airTime = shard.airborneMs[bot->GetGUID()];
            if (!bot->CanFly() && !bot->IsInFlight() && !bot->IsInWater() && !bot->GetVehicle())
            {
                bool isSuspended = (bz - groundZ > 5.0f);
                if (isSuspended && !bot->isMoving())
                {
                    airTime += diff;
                    if (airTime >= 1500)
                    {
                        deferred.type = DeferredAction::Type::AirborneRecover;
                        deferred.expectedCommandId = rec.commandId;
                    }
                }
                else
                {
                    airTime = 0;
                }
            }
            else
            {
                airTime = 0;
            }
        }

        // Step 3: Execute Deferred Actions under Dispatch Gate with Generation Revalidation
        if (deferred.type == DeferredAction::Type::ClearAuthorized)
        {
            std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
            bool stillStale = false;
            {
                std::lock_guard<std::mutex> lock(shard.mutex);
                BotLocomotionRecord const& cur = shard.locomotion[bot->GetGUID()];
                if (LocomotionCommandValidator::ValidateDeferredAction(cur, deferred.expectedCommandId,
                        deferred.expectedOwner, deferred.expectedMode, deferred.expectedState))
                {
                    stillStale = true;
                }
            }
            if (stillStale)
            {
                ClearAuthorizedMovement(bot);
            }
        }
        else if (deferred.type == DeferredAction::Type::AirborneRecover)
        {
            std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
            bool stillAirborne = false;
            {
                std::lock_guard<std::mutex> lock(shard.mutex);
                BotLocomotionRecord const& cur = shard.locomotion[bot->GetGUID()];
                if (cur.commandId == deferred.expectedCommandId)
                    stillAirborne = true;
            }
            if (stillAirborne)
            {
                ClearAuthorizedMovement(bot);
                bot->GetMotionMaster()->MoveFall();
            }
        }

        // Step 4: Evaluate and apply mount policy atomically under serialized dispatch gate
        ApplyMountPolicy(bot, now);

        // Step 5: Resolve any pending mount cast
        ResolveMountCast(bot);
    }

    std::string Describe(Player* bot)
    {
        if (!bot)
            return "Null bot";

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
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

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
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
        LocomotionShard& shard = LocomotionStateStore::GetShard(botGuid);
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.locomotion.erase(botGuid);
        shard.mounts.erase(botGuid);
        shard.safeHistories.erase(botGuid);
        shard.lastSafeCheck.erase(botGuid);
        shard.lastRecordedMap.erase(botGuid);
        shard.airborneMs.erase(botGuid);
        shard.histories.erase(botGuid);
        shard.requests.erase(botGuid);
        shard.nopathCache.erase(botGuid);
        shard.nextRequestGen.erase(botGuid);
    }

}
