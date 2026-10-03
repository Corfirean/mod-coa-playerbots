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
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    // =========================================================================
    // Sharded Synchronization (Scales to 3,000+ bots across map threads)
    // =========================================================================
    constexpr size_t LOCOMOTION_SHARDS = 64;
    std::mutex _shards[LOCOMOTION_SHARDS];

    inline std::mutex& GetShardMutex(ObjectGuid guid)
    {
        return _shards[guid.GetCounter() % LOCOMOTION_SHARDS];
    }

    // =========================================================================
    // Locomotion Record
    // =========================================================================
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
        float chaseMinRange = 0.0f;
        float chaseMaxRange = 0.0f;
        float chaseAngle = 0.0f;
        uint32 issuedAtMs = 0;
        uint32 lastUpdateMs = 0;
        uint32 holdUntilMs = 0;
        uint32 lastGenChangeMs = 0;
        uint32 genChangesInWindow = 0;
    };

    std::unordered_map<ObjectGuid, BotLocomotionRecord> _botLocomotion;

    // =========================================================================
    // Centralized Mount Controller State
    // =========================================================================
    struct BotMountRecord
    {
        MountState state = MountState::Grounded;
        uint32 pendingMountSpellId = 0;
        uint32 mountCastStartedAt = 0;
        uint32 remountCooldownUntilMs = 0;
        DismountReason lastDismountReason = DismountReason::Manual;
        uint32 lastDismountAtMs = 0;
        std::unordered_set<uint32> knownBadMountSpells;

        // Leader desired state debouncing
        DesiredMountState leaderDesiredState = DesiredMountState::None;
        uint32 leaderStateObservedAt = 0;
    };

    std::unordered_map<ObjectGuid, BotMountRecord> _botMounts;

    // =========================================================================
    // Safe Position History Ring Buffer (16 confirmed positions per bot)
    // =========================================================================
    constexpr size_t SAFE_HISTORY_CAPACITY = 16;
    struct SafePositionHistory
    {
        SafePosition entries[SAFE_HISTORY_CAPACITY];
        size_t head = 0;
        size_t count = 0;
        uint32 lastRecordMs = 0;
        uint32 lastBacktrackMs = 0;
        uint8 backtrackAttempts = 0;
    };

    std::unordered_map<ObjectGuid, SafePositionHistory> _safeHistories;
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
    constexpr uint32 NOPATH_RECHECK_COOLDOWN_MS = 4000;

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
    constexpr uint32 LEADER_DEBOUNCE_MS = 1000;

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

    // Safely clears only bot-controlled normal locomotion generators without touching controlled motion.
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
            ClearAuthorizedMovement(bot);
        }

        LocomotionToken token = BotMovement::MoveTo(bot, req.owner, tx, ty, tz, /*forceDestination=*/false);
        if (!token)
            return false;

        req.commandId = token.commandId;
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

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto itr = _botLocomotion.find(bot->GetGUID());
        if (itr == _botLocomotion.end())
            return MoveOwner::None;

        return itr->second.owner;
    }

    MoveMode CurrentMode(Player* bot)
    {
        if (!bot)
            return MoveMode::Idle;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto itr = _botLocomotion.find(bot->GetGUID());
        return (itr == _botLocomotion.end()) ? MoveMode::Idle : itr->second.mode;
    }

    LocomotionState CurrentState(Player* bot)
    {
        if (!bot)
            return LocomotionState::Idle;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto itr = _botLocomotion.find(bot->GetGUID());
        return (itr == _botLocomotion.end()) ? LocomotionState::Idle : itr->second.state;
    }

    bool CanClaim(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return false;

        if (IsExternallyControlled(bot))
            return false;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto itr = _botLocomotion.find(bot->GetGUID());
        if (itr == _botLocomotion.end())
            return true;

        if (itr->second.owner == owner || itr->second.owner == MoveOwner::None)
            return true;

        return Priority(owner) >= Priority(itr->second.owner);
    }

    bool IsCommandActive(Player* bot, MoveOwner owner, uint64 commandId)
    {
        if (!bot || commandId == 0)
            return false;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto itr = _botLocomotion.find(bot->GetGUID());
        if (itr == _botLocomotion.end())
            return false;

        return itr->second.owner == owner && itr->second.commandId == commandId;
    }

    uint64 GetActiveCommandId(Player* bot)
    {
        if (!bot)
            return 0;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto itr = _botLocomotion.find(bot->GetGUID());
        return (itr == _botLocomotion.end()) ? 0 : itr->second.commandId;
    }

    LocomotionToken GetActiveToken(Player* bot)
    {
        if (!bot)
            return {};

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto itr = _botLocomotion.find(bot->GetGUID());
        if (itr == _botLocomotion.end())
            return {};

        return LocomotionToken{bot->GetGUID(), itr->second.owner, itr->second.commandId};
    }

    // =========================================================================
    // Core Arbitrated Locomotion API (Token-Safe & Non-blocking outside mutex)
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

        // 1. Sharded Lock: claim validation, redundancy check, reserve commandId
        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));

            if (!CanClaim(bot, owner))
                return {};

            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];

            // Redundancy check: if already walking to essentially the same point
            if (rec.owner == owner && rec.mode == MoveMode::Point &&
                bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE &&
                std::fabs(rec.destX - x) <= SAME_POINT_YARDS &&
                std::fabs(rec.destY - y) <= SAME_POINT_YARDS &&
                std::fabs(rec.destZ - groundZ) <= SAME_POINT_YARDS * 3.0f)
            {
                ++_stats.redundantSkipped;
                return LocomotionToken{bot->GetGUID(), owner, rec.commandId};
            }

            // No-path cache check: avoid flooding navmesh queries for known unreachable points
            auto cached = _nopathCache.find(bot->GetGUID());
            if (cached != _nopathCache.end() && cached->second.owner == owner && now < cached->second.expiresAtMs &&
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

        // 2. OUTSIDE LOCK: PathGenerator computation (Never serializes global movement!)
        bool pathValid = true;
        if (!bot->CanFly())
        {
            PathGenerator path(bot);
            path.CalculatePath(x, y, groundZ, false);
            if (path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH))
            {
                pathValid = false;
            }
        }

        // 3. Sharded Lock: verify generation still current before committing
        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];

            // If another command preempted while we were calculating path, discard stale result
            if (rec.commandId != token.commandId || rec.owner != token.owner)
                return {};

            if (!pathValid)
            {
                LOG_DEBUG("module.coa-playerbots.navigation",
                    "MoveTo: bot '{}' has NO navmesh path to ({:.1f}, {:.1f}, {:.1f}) -- refusing straight move.",
                    bot->GetName(), x, y, groundZ);

                _nopathCache[bot->GetGUID()] = NopathCacheEntry{ owner, x, y, groundZ, now + NOPATH_RECHECK_COOLDOWN_MS };
                RecordTransitionInternal(bot->GetGUID(), rec.state, LocomotionState::Blocked, owner, MoveMode::Point, "NoNavmeshPath", x, y, groundZ);
                rec.state = LocomotionState::Blocked;
                return {};
            }

            LocomotionState oldState = rec.state;
            rec.mode = MoveMode::Point;
            rec.state = LocomotionState::Moving;
            rec.destX = x;
            rec.destY = y;
            rec.destZ = groundZ;
            rec.issuedAtMs = now;
            rec.lastUpdateMs = now;

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Moving, owner, MoveMode::Point, "MoveToIssued", x, y, groundZ);
        }

        // 4. OUTSIDE LOCK: Physical MotionMaster dispatch
        ClearAuthorizedMovement(bot);
        bot->GetMotionMaster()->MovePoint(uint32(owner), x, y, groundZ, FORCED_MOVEMENT_NONE, 0.0f, 0.0f,
            /*generatePath=*/true, forceDestination);
        ++_stats.issued;

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
        float dist = Dist2d(bx, by, x, y);
        float dz = std::fabs(bot->GetPositionZ() - z);

        MovementRequest* pReq = nullptr;
        bool fresh = false;
        bool goalMoved = false;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            MovementRequest& req = _requests[bot->GetGUID()];
            pReq = &req;
            req.botGuid = bot->GetGUID();

            fresh = (req.owner != owner || req.goalId != goalId);
            if (fresh)
            {
                req = MovementRequest();
                req.botGuid = bot->GetGUID();
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
                req.backtracking = false;
            }

            req.x = x;
            req.y = y;
            req.z = z;
            req.acceptRadius = acceptRadius;
            req.lastCallAt = now;
        }

        MovementRequest& req = *pReq;

        // Arrival check
        if (dist <= acceptRadius && dz <= std::max(6.0f, acceptRadius))
        {
            Release(bot, owner);
            return NavStatus::Arrived;
        }

        // Dismount hysteresis on arrival proximity
        if (dist <= DISMOUNT_ARRIVAL_DISTANCE && IsMounted(bot))
        {
            RequestDismount(bot, owner, DismountReason::Arrival);
        }

        if (!CanClaim(bot, owner))
        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
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
                req.backtracking = false;
                forceReissue = true;
                LOG_DEBUG("module.coa-playerbots.navigation",
                    "Bot '{}' repathing to goal {} ({:.0f} yd left).", bot->GetName(), goalId, dist);
                break;
            case NavRecovery::DetourLeft:
            case NavRecovery::DetourRight:
            {
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
                    // If detours failed, attempt safe backtrack recovery before giving up
                    if (BacktrackToSafePosition(bot, owner))
                    {
                        req.backtracking = true;
                        req.detour = false;
                        return NavStatus::Moving;
                    }

                    req.detour = false;
                    forceReissue = true;
                }
                break;
            }
            case NavRecovery::GiveUp:
            default:
                // Final recovery step: attempt safe backtrack once before declaring Stuck
                if (!req.backtracking && BacktrackToSafePosition(bot, owner))
                {
                    req.backtracking = true;
                    return NavStatus::Moving;
                }

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
        {
            if (outToken)
                *outToken = GetActiveToken(bot);
            return NavStatus::Moving;
        }

        bool endedEarly = req.legs && Dist2d(bx, by, req.legX, req.legY) > 3.0f;
        if (!forceReissue && endedEarly && now - req.lastIssueAt < EARLY_END_REISSUE_MS)
        {
            if (outToken)
                *outToken = GetActiveToken(bot);
            return NavStatus::Moving;
        }

        IssueLeg(bot, req, forceReissue);
        if (outToken)
            *outToken = GetActiveToken(bot);

        return NavStatus::Moving;
    }

    LocomotionToken Follow(Player* bot, MoveOwner owner, Unit* target, float dist, float angle)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !target || !target->IsInWorld())
            return {};

        if (bot->IsNonMeleeSpellCast(false))
            return {};

        if (IsExternallyControlled(bot))
            return {};

        uint32 now = NowMs();
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));

            if (!CanClaim(bot, owner))
                return {};

            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];

            // Idempotency: if already following this target at this distance/angle and generator is active
            if (rec.owner == owner && rec.mode == MoveMode::Follow && rec.targetGuid == target->GetGUID() &&
                bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE &&
                std::fabs(rec.targetDist - dist) <= 0.5f &&
                std::fabs(rec.targetAngle - angle) <= 0.1f)
            {
                ++_stats.redundantSkipped;
                return LocomotionToken{bot->GetGUID(), owner, rec.commandId};
            }

            LocomotionState oldState = rec.state;
            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::Follow;
            rec.state = LocomotionState::Following;
            rec.targetGuid = target->GetGUID();
            rec.targetDist = dist;
            rec.targetAngle = angle;
            rec.issuedAtMs = now;
            rec.lastUpdateMs = now;

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Following, owner, MoveMode::Follow, "FollowIssued",
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
        }

        ClearAuthorizedMovement(bot);
        bot->GetMotionMaster()->MoveFollow(target, dist, angle);

        return token;
    }

    LocomotionToken Chase(Player* bot, MoveOwner owner, Unit* target, float minRange, float maxRange, float angle)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !target || !target->IsInWorld())
            return {};

        if (IsExternallyControlled(bot))
            return {};

        if (bot->IsMounted())
            RequestDismount(bot, owner, DismountReason::Combat);

        uint32 now = NowMs();
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));

            if (!CanClaim(bot, owner))
                return {};

            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];

            // Full range configuration idempotency check: target, minRange, maxRange, angle
            if (rec.owner == owner && rec.mode == MoveMode::Chase && rec.targetGuid == target->GetGUID() &&
                bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE &&
                std::fabs(rec.chaseMinRange - minRange) <= 0.5f &&
                std::fabs(rec.chaseMaxRange - maxRange) <= 0.5f &&
                std::fabs(rec.chaseAngle - angle) <= 0.1f)
            {
                ++_stats.redundantSkipped;
                return LocomotionToken{bot->GetGUID(), owner, rec.commandId};
            }

            LocomotionState oldState = rec.state;
            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::Chase;
            rec.state = LocomotionState::Chasing;
            rec.targetGuid = target->GetGUID();
            rec.chaseMinRange = minRange;
            rec.chaseMaxRange = maxRange;
            rec.chaseAngle = angle;
            rec.issuedAtMs = now;
            rec.lastUpdateMs = now;

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Chasing, owner, MoveMode::Chase, "ChaseIssued",
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
        }

        ClearAuthorizedMovement(bot);

        if (minRange > 0.0f || maxRange > 0.0f)
        {
            if (angle != 0.0f)
                bot->GetMotionMaster()->MoveChase(target, ChaseRange(minRange, maxRange), std::optional<ChaseAngle>(ChaseAngle(angle, 0.5f)));
            else
                bot->GetMotionMaster()->MoveChase(target, maxRange, angle);
        }
        else
        {
            bot->GetMotionMaster()->MoveChase(target);
        }

        return token;
    }

    LocomotionToken MoveForwards(Player* bot, MoveOwner owner, Unit* target, float dist)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !target || !target->IsInWorld())
            return {};

        if (bot->IsNonMeleeSpellCast(false))
            return {};

        if (IsExternallyControlled(bot))
            return {};

        uint32 now = NowMs();
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            if (!CanClaim(bot, owner))
                return {};

            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];
            LocomotionState oldState = rec.state;
            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::MoveForwards;
            rec.state = LocomotionState::Moving;
            rec.targetGuid = target->GetGUID();
            rec.targetDist = dist;
            rec.issuedAtMs = now;
            rec.lastUpdateMs = now;

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Moving, owner, MoveMode::MoveForwards, "MoveForwards",
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
        }

        ClearAuthorizedMovement(bot);
        bot->GetMotionMaster()->MoveForwards(target, dist);

        return token;
    }

    LocomotionToken MoveBackwards(Player* bot, MoveOwner owner, Unit* target, float dist)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive() || !target || !target->IsInWorld())
            return {};

        if (bot->IsNonMeleeSpellCast(false))
            return {};

        if (IsExternallyControlled(bot))
            return {};

        uint32 now = NowMs();
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            if (!CanClaim(bot, owner))
                return {};

            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];
            LocomotionState oldState = rec.state;
            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::MoveBackwards;
            rec.state = LocomotionState::Moving;
            rec.targetGuid = target->GetGUID();
            rec.targetDist = dist;
            rec.issuedAtMs = now;
            rec.lastUpdateMs = now;

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Moving, owner, MoveMode::MoveBackwards, "MoveBackwards",
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
        }

        ClearAuthorizedMovement(bot);
        bot->GetMotionMaster()->MoveBackwards(target, dist);

        return token;
    }

    LocomotionToken Hold(Player* bot, MoveOwner owner, uint32 durationMs)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return {};

        if (IsExternallyControlled(bot))
            return {};

        uint32 now = NowMs();
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            if (!CanClaim(bot, owner))
                return {};

            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];
            LocomotionState oldState = rec.state;
            rec.commandId++;
            rec.owner = owner;
            rec.mode = MoveMode::Hold;
            rec.state = LocomotionState::Holding;
            rec.holdUntilMs = now + durationMs;
            rec.lastUpdateMs = now;

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Holding, owner, MoveMode::Hold, "HoldIssued",
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());

            token = LocomotionToken{bot->GetGUID(), owner, rec.commandId};
        }

        ClearAuthorizedMovement(bot);

        return token;
    }

    // =========================================================================
    // Token-Safe Cancellation
    // =========================================================================

    bool Release(Player* bot, LocomotionToken const& token)
    {
        if (!bot || !token.IsValid())
            return false;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            auto itr = _botLocomotion.find(bot->GetGUID());
            if (itr == _botLocomotion.end())
                return false;

            // Strict generation check: reject stale releases from old commands
            if (itr->second.owner != token.owner || itr->second.commandId != token.commandId)
                return false;

            // Safely erase request only if it matches current token
            auto reqItr = _requests.find(bot->GetGUID());
            if (reqItr != _requests.end() && reqItr->second.owner == token.owner && reqItr->second.commandId == token.commandId)
                _requests.erase(reqItr);

            LocomotionState oldState = itr->second.state;
            itr->second.commandId++;
            itr->second.owner = MoveOwner::None;
            itr->second.mode = MoveMode::Idle;
            itr->second.state = LocomotionState::Idle;
            itr->second.lastUpdateMs = NowMs();

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Idle, token.owner, MoveMode::Idle, "ReleaseToken",
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        }

        ClearAuthorizedMovement(bot);
        return true;
    }

    bool Stop(Player* bot, LocomotionToken const& token)
    {
        if (!bot || !token.IsValid())
            return false;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            auto itr = _botLocomotion.find(bot->GetGUID());
            if (itr == _botLocomotion.end())
                return false;

            if (itr->second.owner != token.owner || itr->second.commandId != token.commandId)
                return false;

            LocomotionState oldState = itr->second.state;
            itr->second.commandId++;
            itr->second.owner = MoveOwner::None;
            itr->second.mode = MoveMode::Stop;
            itr->second.state = LocomotionState::Idle;
            itr->second.lastUpdateMs = NowMs();

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Idle, token.owner, MoveMode::Stop, "StopToken",
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        }

        ClearAuthorizedMovement(bot);
        return true;
    }

    void Stop(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return;

        if (IsExternallyControlled(bot))
            return;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            if (!CanClaim(bot, owner))
                return;

            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];
            LocomotionState oldState = rec.state;
            rec.commandId++;
            rec.owner = MoveOwner::None;
            rec.mode = MoveMode::Stop;
            rec.state = LocomotionState::Idle;
            rec.lastUpdateMs = NowMs();

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Idle, owner, MoveMode::Stop, "StopAdmin",
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        }

        ClearAuthorizedMovement(bot);
    }

    void Release(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            auto itr = _botLocomotion.find(bot->GetGUID());
            if (itr == _botLocomotion.end())
                return;

            if (itr->second.owner != owner)
                return;

            // Erase request ONLY after verifying owner matches!
            auto reqItr = _requests.find(bot->GetGUID());
            if (reqItr != _requests.end() && reqItr->second.owner == owner)
                _requests.erase(reqItr);

            LocomotionState oldState = itr->second.state;
            itr->second.commandId++;
            itr->second.owner = MoveOwner::None;
            itr->second.mode = MoveMode::Idle;
            itr->second.state = LocomotionState::Idle;
            itr->second.lastUpdateMs = NowMs();

            RecordTransitionInternal(bot->GetGUID(), oldState, LocomotionState::Idle, owner, MoveMode::Idle, "ReleaseAdmin",
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        }

        ClearAuthorizedMovement(bot);
    }

    // =========================================================================
    // Centralized Mount Controller Implementation
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

        if (!bot->IsOutdoors() || bot->GetMap()->IsDungeon() || bot->GetMap()->IsRaid())
            return false;

        if (IsExternallyControlled(bot))
            return false;

        if (travelDistance > 0.0f && travelDistance < MOUNT_MIN_DISTANCE)
            return false;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto itr = _botMounts.find(bot->GetGUID());
        if (itr != _botMounts.end())
        {
            if (NowMs() < itr->second.remountCooldownUntilMs)
                return false;

            if (itr->second.state == MountState::MountCasting || itr->second.pendingMountSpellId != 0)
                return false;
        }

        return true;
    }

    bool IsMounted(Player const* bot)
    {
        return bot && bot->IsMounted();
    }

    MountState GetMountState(Player const* bot)
    {
        if (!bot)
            return MountState::Grounded;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto itr = _botMounts.find(bot->GetGUID());
        if (itr == _botMounts.end())
            return bot->IsMounted() ? MountState::MountedGround : MountState::Grounded;

        return itr->second.state;
    }

    uint32 GetPendingMountSpell(Player const* bot)
    {
        if (!bot)
            return 0;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto itr = _botMounts.find(bot->GetGUID());
        return (itr == _botMounts.end()) ? 0 : itr->second.pendingMountSpellId;
    }

    uint32 SelectMountSpell(Player* bot, bool wantFlying)
    {
        if (!bot)
            return 0;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        BotMountRecord& mRec = _botMounts[bot->GetGUID()];

        if (wantFlying)
        {
            uint32 mapId = bot->GetMapId();
            if ((mapId != 530 && mapId != 571) || bot->GetLevel() < 60)
                wantFlying = false;
        }

        uint32 chosen = 0;

        if (wantFlying)
        {
            uint32 defaultFly = GetDefaultFlyingMountSpell(bot);
            if (defaultFly && bot->HasSpell(defaultFly) && !mRec.knownBadMountSpells.count(defaultFly))
                chosen = defaultFly;

            if (!chosen)
            {
                // Search spellbook for flying mounts
                for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
                {
                    if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                        continue;
                    if (mRec.knownBadMountSpells.count(spellId))
                        continue;

                    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                    if (spellInfo && IsFlyingMountSpellInfo(spellInfo))
                    {
                        chosen = spellId;
                        break;
                    }
                }
            }
        }

        if (!chosen)
        {
            uint32 racialSpellId = GetRacialGroundMountSpell(bot->getRace());
            if (racialSpellId && bot->HasSpell(racialSpellId) && !mRec.knownBadMountSpells.count(racialSpellId))
                chosen = racialSpellId;

            if (!chosen)
            {
                // Search spellbook for ground-only mounts
                for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
                {
                    if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                        continue;
                    if (mRec.knownBadMountSpells.count(spellId))
                        continue;

                    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
                    if (spellInfo && IsMountSpellInfo(spellInfo) && !IsFlyingMountSpellInfo(spellInfo))
                    {
                        chosen = spellId;
                        break;
                    }
                }
            }
        }

        return chosen;
    }

    bool RequestMount(Player* bot, MoveOwner owner, float travelDistance, bool wantFlying)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        if (IsMounted(bot))
            return true;

        if (!CanMount(bot, travelDistance))
            return false;

        uint32 spellId = SelectMountSpell(bot, wantFlying);
        if (!spellId)
            return false;

        uint32 now = NowMs();
        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            BotMountRecord& mRec = _botMounts[bot->GetGUID()];
            mRec.state = MountState::MountRequested;
        }

        // Stop moving before casting mount
        Stop(bot, owner);

        LOG_DEBUG("module.coa-playerbots.navigation",
            "RequestMount: bot '{}' casting mount spell {} (travelDist={:.1f}, flying={}).",
            bot->GetName(), spellId, travelDistance, wantFlying);

        SpellCastResult result = bot->CastSpell(bot, spellId, false);
        ++_stats.mountAttempts;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        BotMountRecord& mRec = _botMounts[bot->GetGUID()];

        if (result == SPELL_CAST_OK)
        {
            mRec.state = MountState::MountCasting;
            mRec.pendingMountSpellId = spellId;
            mRec.mountCastStartedAt = now;
            return true;
        }

        LOG_DEBUG("module.coa-playerbots.navigation",
            "RequestMount: bot '{}' spell {} failed (result {}).",
            bot->GetName(), spellId, uint32(result));

        if (spellId != GetRacialGroundMountSpell(bot->getRace()) && spellId != GetDefaultFlyingMountSpell(bot))
        {
            if (result != SPELL_FAILED_ONLY_OUTDOORS && result != SPELL_FAILED_MOVING &&
                result != SPELL_FAILED_SPELL_IN_PROGRESS && result != SPELL_FAILED_NOT_HERE)
            {
                mRec.knownBadMountSpells.insert(spellId);
            }
        }

        mRec.state = MountState::Grounded;
        mRec.remountCooldownUntilMs = now + 2000;
        return false;
    }

    void RequestDismount(Player* bot, MoveOwner owner, DismountReason reason)
    {
        if (!bot)
            return;

        uint32 now = NowMs();
        uint32 pendingSpell = 0;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            BotMountRecord& mRec = _botMounts[bot->GetGUID()];
            pendingSpell = mRec.pendingMountSpellId;
            mRec.pendingMountSpellId = 0;
            mRec.state = MountState::Grounded;
            mRec.lastDismountAtMs = now;
            mRec.remountCooldownUntilMs = now + REMOUNT_COOLDOWN_MS;
            mRec.lastDismountReason = reason;

            RecordTransitionInternal(bot->GetGUID(), LocomotionState::Moving, LocomotionState::Idle, owner, MoveMode::Idle,
                DismountReasonName(reason), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        }

        if (pendingSpell)
            bot->InterruptNonMeleeSpells(false);

        if (bot->IsMounted())
        {
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
            ++_stats.dismounts;
        }
    }

    void SetLeaderMountPreference(Player* bot, DesiredMountState pref)
    {
        if (!bot)
            return;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        BotMountRecord& mRec = _botMounts[bot->GetGUID()];
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

        uint32 now = NowMs();
        uint32 justTried = 0;
        bool confirmedMounted = false;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            BotMountRecord& mRec = _botMounts[bot->GetGUID()];

            if (bot->IsMounted())
            {
                mRec.state = bot->CanFly() ? MountState::MountedFlying : MountState::MountedGround;
                mRec.pendingMountSpellId = 0;
                return;
            }

            if (!mRec.pendingMountSpellId)
                return;

            // If still casting 1.5s mount spell, do not disturb!
            if (bot->IsNonMeleeSpellCast(false))
                return;

            justTried = mRec.pendingMountSpellId;
            mRec.pendingMountSpellId = 0;
            confirmedMounted = bot->IsMounted();

            if (confirmedMounted)
            {
                mRec.state = bot->CanFly() ? MountState::MountedFlying : MountState::MountedGround;
                ++_stats.mountSuccesses;
            }
            else
            {
                mRec.state = MountState::Grounded;
                mRec.remountCooldownUntilMs = now + 3000;
                if (!bot->IsInCombat() && !bot->isMoving() &&
                    justTried != GetRacialGroundMountSpell(bot->getRace()) &&
                    justTried != GetDefaultFlyingMountSpell(bot))
                {
                    mRec.knownBadMountSpells.insert(justTried);
                }
            }
        }

        if (confirmedMounted)
            OnSpellCastSuccess(bot, justTried);
        else
            OnSpellCastInterrupt(bot, justTried);
    }

    void OnSpellCastStart(Player* bot, uint32 spellId)
    {
        if (!bot)
            return;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        BotMountRecord& mRec = _botMounts[bot->GetGUID()];
        mRec.pendingMountSpellId = spellId;
        mRec.mountCastStartedAt = NowMs();
        mRec.state = MountState::MountCasting;
    }

    void OnSpellCastSuccess(Player* bot, uint32 spellId)
    {
        if (!bot)
            return;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        BotMountRecord& mRec = _botMounts[bot->GetGUID()];
        if (mRec.pendingMountSpellId == spellId || spellId == 0)
        {
            mRec.pendingMountSpellId = 0;
            mRec.state = bot->CanFly() ? MountState::MountedFlying : MountState::MountedGround;
            ++_stats.mountSuccesses;
        }
    }

    void OnSpellCastInterrupt(Player* bot, uint32 spellId)
    {
        if (!bot)
            return;

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        BotMountRecord& mRec = _botMounts[bot->GetGUID()];
        if (mRec.pendingMountSpellId == spellId || spellId == 0)
        {
            mRec.pendingMountSpellId = 0;
            mRec.state = MountState::Grounded;
            mRec.remountCooldownUntilMs = NowMs() + 3000;
        }
    }

    // =========================================================================
    // Watchdog, Backtracking, & State Queries
    // =========================================================================

    bool BacktrackToSafePosition(Player* bot, MoveOwner owner)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        if (IsExternallyControlled(bot))
            return false;

        uint32 now = NowMs();
        SafePosition cand;
        bool found = false;

        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            SafePositionHistory& hist = _safeHistories[bot->GetGUID()];

            if (hist.count == 0)
                return false;

            if (now - hist.lastBacktrackMs < 5000 || hist.backtrackAttempts >= 3)
                return false;

            float bx = bot->GetPositionX();
            float by = bot->GetPositionY();

            // Find an entry in the ring buffer that is 8-40 yards away from current stuck spot and <60s old
            for (size_t i = 0; i < hist.count; ++i)
            {
                size_t idx = (hist.head + SAFE_HISTORY_CAPACITY - 1 - i) % SAFE_HISTORY_CAPACITY;
                SafePosition const& entry = hist.entries[idx];
                if (entry.timeMs == 0 || now - entry.timeMs > 60000)
                    continue;

                float d = Dist2d(bx, by, entry.x, entry.y);
                if (d >= 8.0f && d <= 40.0f)
                {
                    cand = entry;
                    found = true;
                    hist.lastBacktrackMs = now;
                    hist.backtrackAttempts++;
                    break;
                }
            }
        }

        if (!found)
            return false;

        LOG_INFO("module.coa-playerbots.navigation",
            "Bot '{}' executing backtrack to confirmed safe position ({:.1f}, {:.1f}, {:.1f}).",
            bot->GetName(), cand.x, cand.y, cand.z);

        ++_stats.backtracks;
        LocomotionToken token = MoveTo(bot, owner, cand.x, cand.y, cand.z, /*forceDestination=*/false);
        return token.IsValid();
    }

    void Update(Player* bot, uint32 diff)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return;

        uint32 now = NowMs();

        // 1. Resolve pending mount cast
        ResolveMountCast(bot);

        // 2. Debounce and handle Leader Mount Preference
        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            BotMountRecord& mRec = _botMounts[bot->GetGUID()];
            BotLocomotionRecord const& lRec = _botLocomotion[bot->GetGUID()];

            if (mRec.leaderDesiredState != DesiredMountState::None && (now - mRec.leaderStateObservedAt) >= LEADER_DEBOUNCE_MS)
            {
                // Only sync dismount if bot is not in the middle of long-distance autonomous navigation
                if (mRec.leaderDesiredState == DesiredMountState::PreferUnmounted)
                {
                    if (bot->IsMounted() && lRec.mode != MoveMode::Navigate)
                    {
                        // Safely request dismount with LeaderState reason
                        RequestDismount(bot, MoveOwner::Travel, DismountReason::Manual);
                    }
                }
                else if (mRec.leaderDesiredState == DesiredMountState::PreferGround || mRec.leaderDesiredState == DesiredMountState::PreferFlying)
                {
                    if (!bot->IsMounted() && CanMount(bot, 0.0f))
                    {
                        bool wantFlying = (mRec.leaderDesiredState == DesiredMountState::PreferFlying);
                        RequestMount(bot, MoveOwner::Travel, 0.0f, wantFlying);
                    }
                }
            }
        }

        // 3. Confirm and record Safe Positions (every 1000ms)
        uint32& lastCheck = _lastSafePosCheck[bot->GetGUID()];
        if (now - lastCheck >= 1000)
        {
            lastCheck = now;
            if (!IsExternallyControlled(bot) && !bot->GetTransport() &&
                !bot->HasUnitMovementFlag(MOVEMENTFLAG_SWIMMING | MOVEMENTFLAG_FALLING))
            {
                float groundZ = bot->GetPositionZ();
                if (bot->GetMap()->GetHeight(bot->GetPositionX(), bot->GetPositionY(), groundZ, true) &&
                    std::fabs(bot->GetPositionZ() - groundZ) <= 1.5f)
                {
                    std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
                    SafePositionHistory& hist = _safeHistories[bot->GetGUID()];

                    bool movedEnough = true;
                    if (hist.count > 0)
                    {
                        size_t lastIdx = (hist.head + SAFE_HISTORY_CAPACITY - 1) % SAFE_HISTORY_CAPACITY;
                        if (Dist2d(bot->GetPositionX(), bot->GetPositionY(), hist.entries[lastIdx].x, hist.entries[lastIdx].y) < 3.0f)
                            movedEnough = false;
                    }

                    if (movedEnough)
                    {
                        hist.entries[hist.head] = SafePosition{ bot->GetPositionX(), bot->GetPositionY(), groundZ, now };
                        hist.head = (hist.head + 1) % SAFE_HISTORY_CAPACITY;
                        if (hist.count < SAFE_HISTORY_CAPACITY)
                            hist.count++;
                        hist.lastRecordMs = now;
                        hist.backtrackAttempts = 0; // reset attempts when moving safely
                    }
                }
            }
        }

        // 4. Airborne Watchdog
        // Checks whether a ground bot is stuck floating in air >3.5 yards above navmesh for >1500ms
        if (!IsExternallyControlled(bot) && !bot->CanFly() && !bot->IsFlying() &&
            !bot->GetTransport() && !bot->HasUnitMovementFlag(MOVEMENTFLAG_SWIMMING))
        {
            float groundZ = bot->GetPositionZ();
            bool hasGround = bot->GetMap()->GetHeight(bot->GetPositionX(), bot->GetPositionY(), groundZ, true);
            float heightAboveGround = bot->GetPositionZ() - groundZ;

            if (hasGround && heightAboveGround > 3.5f)
            {
                uint32& airMs = _airborneMs[bot->GetGUID()];
                airMs += diff;
                if (airMs >= 1500)
                {
                    LOG_WARN("module.coa-playerbots.watchdog",
                        "AirborneWatchdog: bot '{}' suspended in air ({:.1f} yd above ground for {}ms) -- forcing landing.",
                        bot->GetName(), heightAboveGround, airMs);
                    airMs = 0;

                    ClearAuthorizedMovement(bot);
                    bot->GetMotionMaster()->MoveFall();
                }
            }
            else
            {
                _airborneMs[bot->GetGUID()] = 0;
            }
        }
        else
        {
            _airborneMs[bot->GetGUID()] = 0;
        }

        // 5. Invariant Warnings & Self-Healing
        {
            std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
            BotLocomotionRecord& rec = _botLocomotion[bot->GetGUID()];

            MovementGeneratorType genType = bot->GetMotionMaster()->GetCurrentMovementGeneratorType();

            // Invariant: internal mode Idle but MotionMaster has active bot locomotion generator
            if (rec.mode == MoveMode::Idle)
            {
                if (genType == POINT_MOTION_TYPE || genType == FOLLOW_MOTION_TYPE || genType == CHASE_MOTION_TYPE)
                {
                    LOG_WARN("module.coa-playerbots.invariant",
                        "LocomotionInvariant: bot '{}' internal state is Idle but MotionMaster has active generator {}. Reconciling.",
                        bot->GetName(), uint32(genType));
                    ClearAuthorizedMovement(bot);
                }
            }
        }
    }

    void ResetRequest(ObjectGuid botGuid)
    {
        std::lock_guard<std::mutex> lock(GetShardMutex(botGuid));
        _requests.erase(botGuid);
    }

    MovementRequest const* GetRequest(ObjectGuid botGuid)
    {
        std::lock_guard<std::mutex> lock(GetShardMutex(botGuid));
        auto itr = _requests.find(botGuid);
        return itr == _requests.end() ? nullptr : &itr->second;
    }

    void Forget(ObjectGuid botGuid)
    {
        std::lock_guard<std::mutex> lock(GetShardMutex(botGuid));
        _botLocomotion.erase(botGuid);
        _botMounts.erase(botGuid);
        _safeHistories.erase(botGuid);
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
            default:                      return "Idle";
        }
    }

    char const* StateName(LocomotionState state)
    {
        switch (state)
        {
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
            default:                           return "Idle";
        }
    }

    char const* MountStateName(MountState state)
    {
        switch (state)
        {
            case MountState::MountRequested:   return "MountRequested";
            case MountState::MountCasting:     return "MountCasting";
            case MountState::MountedGround:    return "MountedGround";
            case MountState::MountedFlying:    return "MountedFlying";
            case MountState::DismountRequested: return "DismountRequested";
            case MountState::Cooldown:         return "Cooldown";
            default:                           return "Grounded";
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

    char const* LeaderPrefName(DesiredMountState pref)
    {
        switch (pref)
        {
            case DesiredMountState::PreferGround:     return "PreferGround";
            case DesiredMountState::PreferFlying:     return "PreferFlying";
            case DesiredMountState::PreferUnmounted:  return "PreferUnmounted";
            default:                                  return "None";
        }
    }

    std::string Describe(Player* bot)
    {
        if (!bot)
            return "Null bot";

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto lItr = _botLocomotion.find(bot->GetGUID());
        auto mItr = _botMounts.find(bot->GetGUID());
        auto rItr = _requests.find(bot->GetGUID());
        auto hItr = _safeHistories.find(bot->GetGUID());

        uint32 now = NowMs();
        uint64 cmdId = lItr != _botLocomotion.end() ? lItr->second.commandId : 0;
        MoveOwner owner = lItr != _botLocomotion.end() ? lItr->second.owner : MoveOwner::None;
        MoveMode mode = lItr != _botLocomotion.end() ? lItr->second.mode : MoveMode::Idle;
        LocomotionState state = lItr != _botLocomotion.end() ? lItr->second.state : LocomotionState::Idle;
        MountState mState = mItr != _botMounts.end() ? mItr->second.state : (bot->IsMounted() ? MountState::MountedGround : MountState::Grounded);
        uint32 pendingSpell = mItr != _botMounts.end() ? mItr->second.pendingMountSpellId : 0;
        uint32 cooldownLeft = (mItr != _botMounts.end() && mItr->second.remountCooldownUntilMs > now) ? (mItr->second.remountCooldownUntilMs - now) : 0;
        DismountReason dReason = mItr != _botMounts.end() ? mItr->second.lastDismountReason : DismountReason::Manual;
        uint64 navGoal = rItr != _requests.end() ? rItr->second.goalId : 0;
        uint8 navStage = rItr != _requests.end() ? rItr->second.progress.stage : 0;
        uint8 btAttempts = hItr != _safeHistories.end() ? hItr->second.backtrackAttempts : 0;

        return Acore::StringFormat(
            "cmdId={} owner={} mode={} state={} gen={} extCtrl={} target={} ranges=[{:.1f}, {:.1f}, ang={:.2f}] "
            "mountState={} pendingSpell={} cdLeft={}ms dismountReason={} navGoal={} navStage={} btAttempts={}",
            cmdId, OwnerName(owner), ModeName(mode), StateName(state),
            uint32(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()),
            IsExternallyControlled(bot),
            lItr != _botLocomotion.end() ? lItr->second.targetGuid.ToString() : "none",
            lItr != _botLocomotion.end() ? lItr->second.chaseMinRange : 0.0f,
            lItr != _botLocomotion.end() ? lItr->second.chaseMaxRange : 0.0f,
            lItr != _botLocomotion.end() ? lItr->second.chaseAngle : 0.0f,
            MountStateName(mState), pendingSpell, cooldownLeft, DismountReasonName(dReason),
            navGoal, navStage, btAttempts);
    }

    std::string DescribeHistory(Player* bot)
    {
        if (!bot)
            return "Null bot";

        std::lock_guard<std::mutex> lock(GetShardMutex(bot->GetGUID()));
        auto itr = _histories.find(bot->GetGUID());
        if (itr == _histories.end() || itr->second.count == 0)
            return "No locomotion history";

        std::string out;
        size_t count = std::min<size_t>(itr->second.count, 8);
        for (size_t i = 0; i < count; ++i)
        {
            size_t idx = (itr->second.head + RING_BUFFER_SIZE - 1 - i) % RING_BUFFER_SIZE;
            TransitionRecord const& rec = itr->second.records[idx];
            out += Acore::StringFormat("[{}ms] {}->{} owner={} mode={} reason='{}' at ({:.1f}, {:.1f}, {:.1f})\n",
                rec.timeMs, StateName(rec.oldState), StateName(rec.newState), OwnerName(rec.owner),
                ModeName(rec.mode), rec.reason, rec.x, rec.y, rec.z);
        }
        return out;
    }

    MovementStats const& Stats()
    {
        return _stats;
    }
}
