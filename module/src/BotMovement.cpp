#include "BotMovement.h"
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
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
    std::mutex _locomotionMutex;

    // Command tracking per bot
    struct BotLocomotionRecord
    {
        uint64 commandId = 0;
        MoveOwner owner = MoveOwner::None;
        MoveMode mode = MoveMode::Idle;
        LocomotionState state = LocomotionState::Idle;
        ObjectGuid targetGuid;
        float destX = 0.0f;
        float destY = 0.0f;
        float destZ = 0.0f;
        float targetDist = 0.0f;
        float targetAngle = 0.0f;
        uint32 issuedAtMs = 0;
        uint32 lastUpdateMs = 0;
        uint32 holdUntilMs = 0;
    };

    std::unordered_map<ObjectGuid, BotLocomotionRecord> _botLocomotion;

    // Mount controller state
    struct BotMountRecord
    {
        MountState state = MountState::Grounded;
        uint32 pendingMountSpellId = 0;
        uint32 remountCooldownUntilMs = 0;
        DismountReason lastDismountReason = DismountReason::Manual;
        uint32 lastDismountAtMs = 0;
    };

    std::unordered_map<ObjectGuid, BotMountRecord> _botMounts;

    // Detour & safe position tracking
    std::unordered_map<ObjectGuid, SafePosition> _safePositions;
    std::unordered_map<ObjectGuid, uint32> _lastSafePosCheck;

    // Airborne watchdog timer
    std::unordered_map<ObjectGuid, uint32> _airborneMs;

    // 32-entry circular transition history ring buffer per bot
    constexpr size_t RING_BUFFER_SIZE = 32;
    struct HistoryBuffer
    {
        TransitionRecord records[RING_BUFFER_SIZE];
        size_t head = 0;
        size_t count = 0;
    };
    std::unordered_map<ObjectGuid, HistoryBuffer> _histories;

    // Point movement & navigation persistence
    std::unordered_map<ObjectGuid, MovementRequest> _requests;
    MovementStats _stats;

    struct NopathCacheEntry
    {
        MoveOwner owner = MoveOwner::None;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint32 expiresAtMs = 0;
    };
    std::unordered_map<ObjectGuid, NopathCacheEntry> _nopathCache;
    constexpr uint32 NOPATH_RECHECK_COOLDOWN_MS = 5000;

    constexpr float SAME_POINT_YARDS = 1.0f;
    constexpr float LEG_YARDS = 120.0f;
    NavProgressRules const PROGRESS_RULES;
    constexpr float GOAL_MOVED_YARDS = 4.0f;
    constexpr uint32 RESUME_GAP_MS = 2500;
    constexpr uint32 EARLY_END_REISSUE_MS = 1500;

    // Hysteresis constants
    constexpr float MOUNT_MIN_DISTANCE = 90.0f;
    constexpr float DISMOUNT_ARRIVAL_DISTANCE = 35.0f;
    constexpr uint32 REMOUNT_COOLDOWN_MS = 6000;

    uint8 Priority(MoveOwner owner)
    {
        return uint8(owner);
    }

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

    void RecordTransitionInternal(ObjectGuid botGuid, LocomotionState oldState, LocomotionState newState,
        MoveOwner owner, MoveMode mode, char const* reason, float x, float y, float z)
    {
        HistoryBuffer& buf = _histories[botGuid];
        TransitionRecord& rec = buf.records[buf.head];
        rec.timeMs = NowMs();
        rec.oldState = oldState;
        rec.newState = newState;
        rec.owner = owner;
        rec.mode = mode;
        rec.x = x;
        rec.y = y;
        rec.z = z;
        std::strncpy(rec.reason, reason ? reason : "", sizeof(rec.reason) - 1);
        rec.reason[sizeof(rec.reason) - 1] = '\0';

        buf.head = (buf.head + 1) % RING_BUFFER_SIZE;
        if (buf.count < RING_BUFFER_SIZE)
            ++buf.count;
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

    bool IssueLeg(Player* bot, MovementRequest& req, bool force)
    {
        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();

        float tx = req.x;
        float ty = req.y;
        float tz = req.z;

        if (req.detour)
        {
            tx = req.detourX;
            ty = req.detourY;
            tz = req.detourZ;
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

        if (force && BotMovement::CurrentOwner(bot) == req.owner)
        {
            bot->StopMoving();
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
                bot->GetMotionMaster()->Clear();
        }

        // forceDestination=false: intermediate hop must never clip through walls
        if (!BotMovement::MoveTo(bot, req.owner, tx, ty, tz, /*forceDestination=*/false))
            return false;

        req.legs++;
        req.legX = tx;
        req.legY = ty;
        req.lastIssueAt = NowMs();
        return true;
    }
}

namespace BotMovement
{
    MoveOwner CurrentOwner(Player* bot)
    {
        if (!bot)
            return MoveOwner::None;

        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botLocomotion.find(bot->GetGUID());
        if (itr == _botLocomotion.end())
            return MoveOwner::None;

        // Check if movement generator was cleared externally (e.g. death, stun, tele)
        MovementGeneratorType genType = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
        bool active = false;
        switch (itr->second.mode)
        {
            case MoveMode::Point:
            case MoveMode::Navigate:
                active = (genType == POINT_MOTION_TYPE);
                break;
            case MoveMode::Follow:
                active = (genType == FOLLOW_MOTION_TYPE);
                break;
            case MoveMode::Chase:
                active = (genType == CHASE_MOTION_TYPE);
                break;
            case MoveMode::Hold:
                active = (NowMs() < itr->second.holdUntilMs);
                break;
            case MoveMode::MoveForwards:
            case MoveMode::MoveBackwards:
                active = (genType == POINT_MOTION_TYPE || !bot->IsStopped());
                break;
            default:
                active = false;
                break;
        }

        if (!active && itr->second.mode != MoveMode::Idle)
        {
            itr->second.owner = MoveOwner::None;
            itr->second.mode = MoveMode::Idle;
            itr->second.state = LocomotionState::Idle;
            return MoveOwner::None;
        }

        return itr->second.owner;
    }

    MoveMode CurrentMode(Player* bot)
    {
        if (!bot)
            return MoveMode::Idle;
        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botLocomotion.find(bot->GetGUID());
        return (itr != _botLocomotion.end()) ? itr->second.mode : MoveMode::Idle;
    }

    LocomotionState CurrentState(Player* bot)
    {
        if (!bot)
            return LocomotionState::Idle;
        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botLocomotion.find(bot->GetGUID());
        return (itr != _botLocomotion.end()) ? itr->second.state : LocomotionState::Idle;
    }

    bool CanClaim(Player* bot, MoveOwner owner)
    {
        MoveOwner current = CurrentOwner(bot);
        return current == MoveOwner::None || current == owner || Priority(current) <= Priority(owner);
    }

    bool IsCommandActive(Player* bot, MoveOwner owner, uint64 commandId)
    {
        if (!bot || commandId == 0)
            return false;
        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botLocomotion.find(bot->GetGUID());
        if (itr == _botLocomotion.end())
            return false;
        return itr->second.owner == owner && itr->second.commandId == commandId;
    }

    uint64 GetActiveCommandId(Player* bot)
    {
        if (!bot)
            return 0;
        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botLocomotion.find(bot->GetGUID());
        return (itr != _botLocomotion.end()) ? itr->second.commandId : 0;
    }

    bool MoveTo(Player* bot, MoveOwner owner, float x, float y, float z, bool forceDestination)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        // If actively casting non-melee spell (such as mount or hearth), refuse movement
        if (bot->IsNonMeleeSpellCast(false))
            return false;

        if (!CanClaim(bot, owner))
            return false;

        float groundZ = ResolveGroundZ(bot, x, y, z);

        {
            std::lock_guard<std::mutex> lock(_locomotionMutex);
            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];

            // Idempotency: if already walking to within 1 yard for the same owner, leave spline alone
            if (rec.owner == owner && (rec.mode == MoveMode::Point || rec.mode == MoveMode::Navigate) &&
                bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE &&
                std::fabs(rec.destX - x) <= SAME_POINT_YARDS &&
                std::fabs(rec.destY - y) <= SAME_POINT_YARDS &&
                std::fabs(rec.destZ - groundZ) <= SAME_POINT_YARDS * 3.0f)
            {
                ++_stats.redundantSkipped;
                return true;
            }

            // Nopath cooldown cache: skip repeated expensive CalculatePath failures
            uint32 now = NowMs();
            auto cached = _nopathCache.find(bot->GetGUID());
            if (cached != _nopathCache.end() && cached->second.owner == owner && now < cached->second.expiresAtMs &&
                std::fabs(cached->second.x - x) <= SAME_POINT_YARDS &&
                std::fabs(cached->second.y - y) <= SAME_POINT_YARDS &&
                std::fabs(cached->second.z - groundZ) <= SAME_POINT_YARDS * 3.0f)
            {
                return false;
            }

            // Ground bot navmesh pre-check: FAIL SAFE, NEVER CHEAT THROUGH GEOMETRY
            if (!bot->CanFly())
            {
                PathGenerator path(bot);
                path.CalculatePath(x, y, groundZ, false);
                if (path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH))
                {
                    LOG_DEBUG("module.coa-playerbots.navigation",
                        "MoveTo: bot '{}' has NO navmesh path to ({:.1f}, {:.1f}, {:.1f}) -- refusing straight move.",
                        bot->GetName(), x, y, groundZ);
                    _nopathCache[bot->GetGUID()] = NopathCacheEntry{ owner, x, y, groundZ, now + NOPATH_RECHECK_COOLDOWN_MS };
                    RecordTransitionInternal(bot->GetGUID(), rec.state, LocomotionState::Blocked, owner, MoveMode::Point, "NoNavmeshPath", x, y, groundZ);
                    rec.state = LocomotionState::Blocked;
                    return false;
                }
            }

            // Preemption: clear previous generator if present
            LocomotionState oldState = rec.state;
            MovementGeneratorType genType = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
            if (genType != IDLE_MOTION_TYPE)
                bot->GetMotionMaster()->Clear();

            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::Point;
            rec.state = LocomotionState::Moving;
            rec.destX = x;
            rec.destY = y;
            rec.destZ = groundZ;
            rec.issuedAtMs = now;
            rec.lastUpdateMs = now;

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Moving, owner, MoveMode::Point, "MoveToIssued", x, y, groundZ);
        }

        bot->GetMotionMaster()->MovePoint(uint32(owner), x, y, groundZ, FORCED_MOVEMENT_NONE, 0.0f, 0.0f,
            /*generatePath=*/true, forceDestination);
        ++_stats.issued;
        return true;
    }

    NavStatus Navigate(Player* bot, MoveOwner owner, uint64 goalId, float x, float y, float z, float acceptRadius)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return NavStatus::Blocked;

        uint32 now = NowMs();
        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();
        float dist = Dist2d(bx, by, x, y);
        float dz = std::fabs(bot->GetPositionZ() - z);

        MovementRequest& req = _requests[bot->GetGUID()];
        bool fresh = (req.owner != owner || req.goalId != goalId);
        bool goalMoved = false;

        if (fresh)
        {
            req = MovementRequest();
            req.owner = owner;
            req.goalId = goalId;
            req.startedAt = now;
            req.progress.Start(dist, dz, now);
        }
        else if (Dist2d(req.x, req.y, x, y) > GOAL_MOVED_YARDS)
        {
            goalMoved = true;
            req.progress.Rebase(dist, dz, now);
        }

        if (!fresh && now - req.lastCallAt > RESUME_GAP_MS)
        {
            req.progress.Rebase(dist, dz, now);
            req.detour = false;
        }

        req.x = x;
        req.y = y;
        req.z = z;
        req.acceptRadius = acceptRadius;
        req.lastCallAt = now;

        // Arrival check
        if (dist <= acceptRadius && dz <= std::max(6.0f, acceptRadius))
        {
            Release(bot, owner);
            _requests.erase(bot->GetGUID());
            return NavStatus::Arrived;
        }

        // Dismount hysteresis on arrival proximity
        if (dist <= DISMOUNT_ARRIVAL_DISTANCE && IsMounted(bot))
        {
            RequestDismount(bot, owner, DismountReason::Arrival);
        }

        if (!CanClaim(bot, owner))
        {
            req.progress.Hold(now);
            return NavStatus::Blocked;
        }

        bool forceReissue = goalMoved;

        switch (req.progress.Update(dist, dz, now, PROGRESS_RULES))
        {
            case NavRecovery::None:
                break;
            case NavRecovery::Repath:
                ++_stats.stuckEvents;
                ++_stats.repaths;
                req.detour = false;
                forceReissue = true;
                LOG_DEBUG("module.coa-playerbots.navigation",
                    "Bot '{}' repathing to goal {} ({:.0f} yd left).", bot->GetName(), goalId, dist);
                break;
            case NavRecovery::DetourLeft:
            case NavRecovery::DetourRight:
            {
                // Navmesh-aware detour candidate generation:
                // Test angles: ±30°, ±60°, ±90° at distance 14 yards.
                // Score candidates by progress toward goal and validity on navmesh.
                ++_stats.stuckEvents;
                ++_stats.detours;

                float baseAngle = std::atan2(y - by, x - bx);
                float const candidateAngles[] = {
                    baseAngle + (req.progress.stage == 2 ? 0.52f : -0.52f),  // ±30 deg
                    baseAngle + (req.progress.stage == 2 ? 1.05f : -1.05f),  // ±60 deg
                    baseAngle + (req.progress.stage == 2 ? 1.57f : -1.57f),  // ±90 deg
                    baseAngle + (req.progress.stage == 2 ? -0.52f : 0.52f)   // opposite side fallback
                };

                bool foundValidDetour = false;
                float bestScore = -999999.0f;
                float bestDx = 0.0f, bestDy = 0.0f, bestDz = 0.0f;

                constexpr float DETOUR_STEP_DIST = 14.0f;
                for (float candAngle : candidateAngles)
                {
                    float cx = bx + DETOUR_STEP_DIST * std::cos(candAngle);
                    float cy = by + DETOUR_STEP_DIST * std::sin(candAngle);
                    float cz = ResolveGroundZ(bot, cx, cy, bot->GetPositionZ());

                    PathGenerator detourPath(bot);
                    bool pathOk = detourPath.CalculatePath(cx, cy, cz, false);
                    if (pathOk && !(detourPath.GetPathType() & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH)))
                    {
                        float candDistToGoal = Dist2d(cx, cy, x, y);
                        float score = (dist - candDistToGoal) - 0.2f * std::fabs(candAngle - baseAngle);
                        if (score > bestScore)
                        {
                            bestScore = score;
                            bestDx = cx;
                            bestDy = cy;
                            bestDz = cz;
                            foundValidDetour = true;
                        }
                    }
                }

                if (foundValidDetour)
                {
                    req.detour = true;
                    req.detourX = bestDx;
                    req.detourY = bestDy;
                    req.detourZ = bestDz;
                    forceReissue = true;
                    LOG_DEBUG("module.coa-playerbots.navigation",
                        "Bot '{}' found valid navmesh detour candidate (score {:.1f}).", bot->GetName(), bestScore);
                }
                else
                {
                    // No candidate cleared navmesh: repath directly from current position
                    req.detour = false;
                    forceReissue = true;
                }
                break;
            }
            case NavRecovery::GiveUp:
            default:
                ++_stats.stuckEvents;
                ++_stats.gaveUp;
                LOG_DEBUG("module.coa-playerbots.navigation",
                    "Bot '{}' gave up on goal {} after exhausted recoveries.", bot->GetName(), goalId);
                Release(bot, owner);
                return NavStatus::Stuck;
        }

        if (req.detour && Dist2d(bx, by, req.detourX, req.detourY) <= 3.0f)
        {
            req.detour = false;
            forceReissue = true;
        }

        bool running = (CurrentOwner(bot) == owner && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE);
        if (running && !forceReissue)
            return NavStatus::Moving;

        bool endedEarly = req.legs && Dist2d(bx, by, req.legX, req.legY) > 3.0f;
        if (!forceReissue && endedEarly && now - req.lastIssueAt < EARLY_END_REISSUE_MS)
            return NavStatus::Moving;

        IssueLeg(bot, req, forceReissue);
        return NavStatus::Moving;
    }

    bool Follow(Player* bot, MoveOwner owner, Unit* target, float dist, float angle)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !target || !target->IsInWorld())
            return false;

        if (bot->IsNonMeleeSpellCast(false))
            return false;

        if (!CanClaim(bot, owner))
            return false;

        {
            std::lock_guard<std::mutex> lock(_locomotionMutex);
            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];

            // Idempotency check: if already following this target at this distance and generator is active
            if (rec.owner == owner && rec.mode == MoveMode::Follow && rec.targetGuid == target->GetGUID() &&
                bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE &&
                std::fabs(rec.targetDist - dist) <= 0.5f && std::fabs(rec.targetAngle - angle) <= 0.2f)
            {
                ++_stats.redundantSkipped;
                return true;
            }

            LocomotionState oldState = rec.state;
            MovementGeneratorType genType = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
            if (genType != IDLE_MOTION_TYPE && genType != FOLLOW_MOTION_TYPE)
                bot->GetMotionMaster()->Clear();

            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::Follow;
            rec.state = LocomotionState::Following;
            rec.targetGuid = target->GetGUID();
            rec.targetDist = dist;
            rec.targetAngle = angle;
            rec.issuedAtMs = NowMs();
            rec.lastUpdateMs = NowMs();

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Following, owner, MoveMode::Follow, "FollowIssued",
                target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
        }

        bot->GetMotionMaster()->MoveFollow(target, dist, angle);
        ++_stats.issued;
        return true;
    }

    bool Chase(Player* bot, MoveOwner owner, Unit* target, float minRange, float maxRange, float angle)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !target || !target->IsInWorld())
            return false;

        if (bot->IsNonMeleeSpellCast(false))
            return false;

        if (!CanClaim(bot, owner))
            return false;

        // Dismount immediately on entering combat chase
        if (IsMounted(bot))
            RequestDismount(bot, owner, DismountReason::Combat);

        {
            std::lock_guard<std::mutex> lock(_locomotionMutex);
            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];

            // Idempotency: if already chasing same target with same ranges
            if (rec.owner == owner && rec.mode == MoveMode::Chase && rec.targetGuid == target->GetGUID() &&
                bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE &&
                std::fabs(rec.targetDist - maxRange) <= 0.5f)
            {
                ++_stats.redundantSkipped;
                return true;
            }

            LocomotionState oldState = rec.state;
            MovementGeneratorType genType = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
            if (genType != IDLE_MOTION_TYPE && genType != CHASE_MOTION_TYPE)
                bot->GetMotionMaster()->Clear();

            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::Chase;
            rec.state = LocomotionState::Chasing;
            rec.targetGuid = target->GetGUID();
            rec.targetDist = maxRange;
            rec.targetAngle = angle;
            rec.issuedAtMs = NowMs();
            rec.lastUpdateMs = NowMs();

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Chasing, owner, MoveMode::Chase, "ChaseIssued",
                target->GetPositionX(), target->GetPositionY(), target->GetPositionZ());
        }

        if (maxRange > 0.0f)
        {
            if (minRange > 0.0f)
                bot->GetMotionMaster()->MoveChase(target, ChaseRange(minRange, maxRange), angle ? std::optional<ChaseAngle>(ChaseAngle(angle, 0.5f)) : std::nullopt);
            else
                bot->GetMotionMaster()->MoveChase(target, maxRange, angle);
        }
        else
            bot->GetMotionMaster()->MoveChase(target);

        ++_stats.issued;
        return true;
    }

    bool MoveForwards(Player* bot, MoveOwner owner, Unit* target, float dist)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !target)
            return false;

        if (!CanClaim(bot, owner))
            return false;

        {
            std::lock_guard<std::mutex> lock(_locomotionMutex);
            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];
            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::MoveForwards;
            rec.state = LocomotionState::Moving;
            rec.targetGuid = target->GetGUID();
            rec.targetDist = dist;
            rec.issuedAtMs = NowMs();
            rec.lastUpdateMs = NowMs();
            RecordTransitionInternal(bot->GetGUID(), rec.state, LocomotionState::Moving, owner, MoveMode::MoveForwards, "MoveForwards", 0, 0, 0);
        }

        bot->GetMotionMaster()->MoveForwards(target, dist);
        ++_stats.issued;
        return true;
    }

    bool MoveBackwards(Player* bot, MoveOwner owner, Unit* target, float dist)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !target)
            return false;

        if (!CanClaim(bot, owner))
            return false;

        {
            std::lock_guard<std::mutex> lock(_locomotionMutex);
            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];
            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::MoveBackwards;
            rec.state = LocomotionState::Moving;
            rec.targetGuid = target->GetGUID();
            rec.targetDist = dist;
            rec.issuedAtMs = NowMs();
            rec.lastUpdateMs = NowMs();
            RecordTransitionInternal(bot->GetGUID(), rec.state, LocomotionState::Moving, owner, MoveMode::MoveBackwards, "MoveBackwards", 0, 0, 0);
        }

        bot->GetMotionMaster()->MoveBackwards(target, dist);
        ++_stats.issued;
        return true;
    }

    bool Hold(Player* bot, MoveOwner owner, uint32 durationMs)
    {
        if (!bot || !CanClaim(bot, owner))
            return false;

        bot->StopMoving();
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();

        std::lock_guard<std::mutex> lock(_locomotionMutex);
        BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];
        rec.commandId++;
        rec.owner = owner;
        rec.mode = MoveMode::Hold;
        rec.state = LocomotionState::Holding;
        rec.holdUntilMs = NowMs() + durationMs;
        rec.issuedAtMs = NowMs();
        rec.lastUpdateMs = NowMs();

        RecordTransitionInternal(bot->GetGUID(), rec.state, LocomotionState::Holding, owner, MoveMode::Hold, "HoldPosition",
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        return true;
    }

    void Stop(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return;

        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botLocomotion.find(bot->GetGUID());
        if (itr == _botLocomotion.end())
        {
            bot->StopMoving();
            return;
        }

        // Stop only if owner matches or outranks current claim
        if (itr->second.owner != owner && Priority(owner) < Priority(itr->second.owner))
            return;

        LocomotionState oldState = itr->second.state;
        itr->second.commandId++;
        itr->second.owner = MoveOwner::None;
        itr->second.mode = MoveMode::Idle;
        itr->second.state = LocomotionState::Idle;
        itr->second.lastUpdateMs = NowMs();

        RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Idle, owner, MoveMode::Stop, "StopMoving",
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

        bot->StopMoving();
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();
    }

    void Release(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return;

        _requests.erase(bot->GetGUID());

        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botLocomotion.find(bot->GetGUID());
        if (itr == _botLocomotion.end())
            return;

        if (itr->second.owner != owner)
            return;

        LocomotionState oldState = itr->second.state;
        itr->second.commandId++;
        itr->second.owner = MoveOwner::None;
        itr->second.mode = MoveMode::Idle;
        itr->second.state = LocomotionState::Idle;
        itr->second.lastUpdateMs = NowMs();

        RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Idle, owner, MoveMode::Idle, "ReleaseClaim",
            bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

        MovementGeneratorType genType = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();
        if (genType == POINT_MOTION_TYPE || genType == FOLLOW_MOTION_TYPE)
        {
            bot->StopMoving();
            bot->GetMotionMaster()->Clear();
        }
    }

    // =========================================================================
    // Mount Controller Implementation
    // =========================================================================

    bool CanMount(Player const* bot, float travelDistance)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        if (bot->IsInCombat() || bot->GetVictim())
            return false;

        if (bot->HasUnitMovementFlag(MOVEMENTFLAG_SWIMMING))
            return false;

        if (bot->GetLevel() < 20)
            return false;

        if (!bot->IsOutdoors() || bot->GetMap()->IsDungeon())
            return false;

        if (travelDistance > 0.0f && travelDistance < MOUNT_MIN_DISTANCE)
            return false;

        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botMounts.find(bot->GetGUID());
        if (itr != _botMounts.end())
        {
            if (NowMs() < itr->second.remountCooldownUntilMs)
                return false;
            if (itr->second.state == MountState::MountCasting)
                return false;
        }

        return true;
    }

    bool IsMounted(Player const* bot)
    {
        if (!bot)
            return false;
        return bot->HasAuraType(SPELL_AURA_MOUNTED);
    }

    MountState GetMountState(Player const* bot)
    {
        if (!bot)
            return MountState::Grounded;
        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botMounts.find(bot->GetGUID());
        return (itr != _botMounts.end()) ? itr->second.state : MountState::Grounded;
    }

    bool RequestMount(Player* bot, MoveOwner owner, float travelDistance)
    {
        if (!CanMount(bot, travelDistance))
            return false;

        if (IsMounted(bot))
            return true;

        uint32 mapId = bot->GetMapId();
        bool allowFlying = (mapId == 530 || mapId == 571) && bot->GetLevel() >= 60;

        uint32 spellId = 0;
        if (allowFlying)
        {
            uint32 defaultFly = GetDefaultFlyingMountSpell(bot);
            if (bot->HasSpell(defaultFly))
                spellId = defaultFly;
        }

        if (!spellId)
        {
            uint32 racialSpell = GetRacialGroundMountSpell(bot->getRace());
            if (bot->HasSpell(racialSpell))
                spellId = racialSpell;
        }

        if (!spellId)
            return false;

        // Mounting requires standing still
        bot->StopMoving();

        SpellCastResult result = bot->CastSpell(bot, spellId, false);
        ++_stats.mountAttempts;

        std::lock_guard<std::mutex> lock(_locomotionMutex);
        BotMountRecord& mrec = _botMounts[bot->GetGUID()];

        if (result == SPELL_CAST_OK)
        {
            mrec.state = MountState::MountCasting;
            mrec.pendingMountSpellId = spellId;
            return true;
        }

        mrec.state = MountState::Cooldown;
        mrec.remountCooldownUntilMs = NowMs() + 3000;
        return false;
    }

    void RequestDismount(Player* bot, MoveOwner owner, DismountReason reason)
    {
        if (!bot)
            return;

        std::lock_guard<std::mutex> lock(_locomotionMutex);
        BotMountRecord& mrec = _botMounts[bot->GetGUID()];

        if (mrec.state == MountState::MountCasting)
        {
            bot->InterruptNonMeleeSpells(false);
            mrec.pendingMountSpellId = 0;
        }

        if (bot->HasAuraType(SPELL_AURA_MOUNTED))
        {
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
            ++_stats.dismounts;
        }

        mrec.state = MountState::Cooldown;
        mrec.lastDismountReason = reason;
        mrec.lastDismountAtMs = NowMs();
        mrec.remountCooldownUntilMs = NowMs() + REMOUNT_COOLDOWN_MS;
    }

    void OnSpellCastStart(Player* bot, uint32 spellId)
    {
        if (!bot)
            return;
        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botMounts.find(bot->GetGUID());
        if (itr != _botMounts.end() && itr->second.pendingMountSpellId == spellId)
        {
            itr->second.state = MountState::MountCasting;
        }
    }

    void OnSpellCastSuccess(Player* bot, uint32 spellId)
    {
        if (!bot)
            return;
        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botMounts.find(bot->GetGUID());
        if (itr != _botMounts.end() && itr->second.pendingMountSpellId == spellId)
        {
            itr->second.pendingMountSpellId = 0;
            itr->second.state = bot->CanFly() ? MountState::MountedFlying : MountState::MountedGround;
            ++_stats.mountSuccesses;
        }
    }

    void OnSpellCastInterrupt(Player* bot, uint32 spellId)
    {
        if (!bot)
            return;
        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _botMounts.find(bot->GetGUID());
        if (itr != _botMounts.end() && itr->second.pendingMountSpellId == spellId)
        {
            itr->second.pendingMountSpellId = 0;
            itr->second.state = MountState::Cooldown;
            itr->second.remountCooldownUntilMs = NowMs() + 3000;
        }
    }

    // =========================================================================
    // Watchdog & Safety Updates
    // =========================================================================

    void Update(Player* bot, uint32 diff)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return;

        uint32 now = NowMs();

        // 1. Mount state synchronization
        {
            std::lock_guard<std::mutex> lock(_locomotionMutex);
            BotMountRecord& mrec = _botMounts[bot->GetGUID()];
            bool mounted = bot->HasAuraType(SPELL_AURA_MOUNTED);
            if (mounted)
            {
                mrec.state = bot->CanFly() ? MountState::MountedFlying : MountState::MountedGround;
                mrec.pendingMountSpellId = 0;
            }
            else if (mrec.state == MountState::MountedGround || mrec.state == MountState::MountedFlying)
            {
                mrec.state = MountState::Cooldown;
                mrec.remountCooldownUntilMs = now + REMOUNT_COOLDOWN_MS;
            }
            else if (mrec.state == MountState::Cooldown && now >= mrec.remountCooldownUntilMs)
            {
                mrec.state = MountState::Grounded;
            }
        }

        // 2. Safe position tracking (recorded every 1000ms when grounded on valid navmesh)
        uint32& lastSafeCheck = _lastSafePosCheck[bot->GetGUID()];
        if (now - lastSafeCheck >= 1000)
        {
            lastSafeCheck = now;
            if (!bot->CanFly() && !bot->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_SWIMMING))
            {
                float gz = ResolveGroundZ(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
                if (std::fabs(bot->GetPositionZ() - gz) <= 1.5f)
                {
                    SafePosition& sp = _safePositions[bot->GetGUID()];
                    sp.x = bot->GetPositionX();
                    sp.y = bot->GetPositionY();
                    sp.z = gz;
                    sp.timeMs = now;
                }
            }
        }

        // 3. Airborne safety watchdog (fail safe: bots must not float in air)
        if (!bot->CanFly() && !bot->HasUnitMovementFlag(MOVEMENTFLAG_SWIMMING))
        {
            float gz = ResolveGroundZ(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
            float heightAboveGround = bot->GetPositionZ() - gz;

            if (heightAboveGround > 3.5f && !bot->HasUnitMovementFlag(MOVEMENTFLAG_FALLING))
            {
                uint32& airTime = _airborneMs[bot->GetGUID()];
                airTime += diff;
                if (airTime >= 1500)
                {
                    LOG_WARN("module.coa-playerbots.navigation",
                        "Bot '{}' detected suspended in air ({:.1f}yd above ground) for {}ms -- forcing landing.",
                        bot->GetName(), heightAboveGround, airTime);
                    bot->GetMotionMaster()->MoveFall();
                    airTime = 0;
                }
            }
            else
            {
                _airborneMs[bot->GetGUID()] = 0;
            }
        }
    }

    bool BacktrackToSafePosition(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return false;

        auto itr = _safePositions.find(bot->GetGUID());
        if (itr == _safePositions.end() || itr->second.timeMs == 0)
            return false;

        LOG_INFO("module.coa-playerbots.navigation",
            "Bot '{}' backtracking to last known safe position ({:.1f}, {:.1f}, {:.1f}).",
            bot->GetName(), itr->second.x, itr->second.y, itr->second.z);

        return MoveTo(bot, owner, itr->second.x, itr->second.y, itr->second.z, /*forceDestination=*/false);
    }

    void ResetRequest(ObjectGuid botGuid)
    {
        _requests.erase(botGuid);
    }

    MovementRequest const* GetRequest(ObjectGuid botGuid)
    {
        auto itr = _requests.find(botGuid);
        return itr == _requests.end() ? nullptr : &itr->second;
    }

    void Forget(ObjectGuid botGuid)
    {
        std::lock_guard<std::mutex> lock(_locomotionMutex);
        _botLocomotion.erase(botGuid);
        _botMounts.erase(botGuid);
        _safePositions.erase(botGuid);
        _lastSafePosCheck.erase(botGuid);
        _airborneMs.erase(botGuid);
        _histories.erase(botGuid);
        _requests.erase(botGuid);
        _nopathCache.erase(botGuid);
    }

    char const* OwnerName(MoveOwner owner)
    {
        switch (owner)
        {
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
            default:                      return "None";
        }
    }

    char const* ModeName(MoveMode mode)
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

    char const* StateName(LocomotionState state)
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

    char const* MountStateName(MountState state)
    {
        switch (state)
        {
            case MountState::Grounded:         return "Grounded";
            case MountState::MountRequested:   return "MountRequested";
            case MountState::MountCasting:     return "MountCasting";
            case MountState::MountedGround:    return "MountedGround";
            case MountState::MountedFlying:    return "MountedFlying";
            case MountState::DismountRequested:return "DismountRequested";
            case MountState::Cooldown:         return "Cooldown";
            default:                           return "Unknown";
        }
    }

    char const* DismountReasonName(DismountReason reason)
    {
        switch (reason)
        {
            case DismountReason::Combat:          return "Combat";
            case DismountReason::Arrival:         return "Arrival";
            case DismountReason::CastInterrupted: return "CastInterrupted";
            case DismountReason::ActionForbidden: return "ActionForbidden";
            case DismountReason::Obstacle:        return "Obstacle";
            case DismountReason::Taxi:            return "Taxi";
            case DismountReason::Manual:          return "Manual";
            default:                              return "Unknown";
        }
    }

    std::string Describe(Player* bot)
    {
        if (!bot)
            return "bot is null";

        MoveOwner owner = CurrentOwner(bot);
        MoveMode mode = CurrentMode(bot);
        LocomotionState state = CurrentState(bot);
        MountState mState = GetMountState(bot);

        MovementRequest const* req = GetRequest(bot->GetGUID());
        if (!req)
        {
            return Acore::StringFormat("locomotion: owner {} mode {} state {} mount {}",
                OwnerName(owner), ModeName(mode), StateName(state), MountStateName(mState));
        }

        uint32 now = NowMs();
        NavProgress const& p = req->progress;
        return Acore::StringFormat(
            "locomotion: owner {} mode {} state {} mount {} | goal {} -> ({:.0f}, {:.0f}, {:.0f}) {:.0f}yd left, best {:.0f}, legs {}, stage: {}{}",
            OwnerName(owner), ModeName(mode), StateName(state), MountStateName(mState),
            req->goalId, req->x, req->y, req->z,
            Dist2d(bot->GetPositionX(), bot->GetPositionY(), req->x, req->y),
            p.best2d, req->legs, NavRecoveryStageName(p.stage),
            req->detour ? " (detouring)" : "");
    }

    std::string DescribeHistory(Player* bot)
    {
        if (!bot)
            return "bot is null";

        std::lock_guard<std::mutex> lock(_locomotionMutex);
        auto itr = _histories.find(bot->GetGUID());
        if (itr == _histories.end() || itr->second.count == 0)
            return "No locomotion transitions recorded.";

        HistoryBuffer const& buf = itr->second;
        std::string out = Acore::StringFormat("Last {} locomotion transitions for '{}':\n", buf.count, bot->GetName());

        size_t start = (buf.head + RING_BUFFER_SIZE - buf.count) % RING_BUFFER_SIZE;
        uint32 now = NowMs();
        for (size_t i = 0; i < buf.count; ++i)
        {
            size_t idx = (start + i) % RING_BUFFER_SIZE;
            TransitionRecord const& r = buf.records[idx];
            float ageSec = (now >= r.timeMs) ? float(now - r.timeMs) / 1000.0f : 0.0f;
            out += Acore::StringFormat("  [-{:.1f}s] {} -> {} | owner: {} mode: {} | reason: {} | pos: ({:.1f}, {:.1f}, {:.1f})\n",
                ageSec, StateName(r.oldState), StateName(r.newState), OwnerName(r.owner), ModeName(r.mode),
                r.reason, r.x, r.y, r.z);
        }

        return out;
    }

    MovementStats const& Stats()
    {
        return _stats;
    }
}
