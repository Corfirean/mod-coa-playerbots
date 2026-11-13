#include "BotPerformance.h"
#include "BotWorkBudget.h"
#include "Config.h"
#include "BotMovement.h"
#include "dungeon/DcNavPenaltyRegistry.h"
#include "AscensionCollectibleSpellData.h"
#include "Optional.h"
#include "GridTerrainData.h"
#include "GameTime.h"
#include "Group.h"
#include "Log.h"
#include "LocalLevelScaling.h"
#include "Map.h"
#include "MapCollisionData.h"
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
#include <string>
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
        std::atomic<uint64> pathQueries{0};
        std::atomic<uint64> sharedNoPathHits{0};
        std::atomic<uint64> legSwitchbacksHandled{0};
        std::atomic<uint64> verticalRejections{0};

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
            s.pathQueries = pathQueries.load(std::memory_order_relaxed);
            s.sharedNoPathHits = sharedNoPathHits.load(std::memory_order_relaxed);
            s.legSwitchbacksHandled = legSwitchbacksHandled.load(std::memory_order_relaxed);
            s.verticalRejections = verticalRejections.load(std::memory_order_relaxed);
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
            LocomotionStateStore::AdvanceIntentEpochLocked(shard, bot->GetGUID(), owner);
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

    // PathGenerator refuses a bot's path with NOPATH once it needs more than MAX_POINT_PATH_LENGTH smooth points (74 of 4 yards, about 300 yards of
    // walking). A winding cave is longer than that long before its end: from the Ragefire Chasm entrance the way to the first boss is some 470 yards
    // and to the last one over 1200, so a bot told to walk there never moved. Ask the navigation mesh for the whole corridor ourselves and return a
    // point on it that PathGenerator can still reach; the next leg continues from there.
    constexpr float ROUTE_STEP_YARDS = 150.0f;

    // A route waypoint already carries the navigation mesh's own height. Looking the ground up again would snap it to whatever floor lies under it in a
    // dungeon with stacked levels (a corridor at -37 became -52 below it) and the point would be off the mesh.
    thread_local bool g_trustGivenZ = false;

    uint64 ResolveStartPolyRef(Player const* bot, float x, float y, float z)
    {
        if (!bot)
            return 0;
        Map* map = bot->GetMap();
        if (!map)
            return 0;
        dtNavMeshQuery const* query = map->GetMapCollisionData().GetMMapData().GetNavMeshQuery();
        if (!query)
            return 0;

        dtQueryFilter filter;
        filter.setIncludeFlags(NAV_GROUND | NAV_WATER);
        filter.setExcludeFlags(0);

        float const start[3] = { y, z, x };
        float const extents[3] = { 3.0f, 6.0f, 3.0f };
        dtPolyRef startRef = 0;
        float startPt[3];
        if (dtStatusFailed(query->findNearestPoly(start, extents, &filter, &startRef, startPt)))
            return 0;
        return static_cast<uint64>(startRef);
    }

    SharedNoPathContext MakeSharedNoPathContext(Player const* bot, float sx, float sy, float sz)
    {
        SharedNoPathContext ctx;
        if (!bot)
        {
            ctx.ambiguous = true;
            return ctx;
        }
        ctx.mapId = bot->GetMapId();
        ctx.instanceId = bot->GetInstanceId();
        ctx.phaseMask = bot->GetPhaseMask();
        ctx.capabilityFlags = SharedNoPathCache::PackCapabilities(
            bot->CanFly(),
            bot->IsInWater(),
            bot->HasPlayerFlag(PLAYER_FLAGS_GHOST),
            bot->IsMounted());
        ctx.startPolyRef = ResolveStartPolyRef(bot, sx, sy, sz);
        ctx.botGuid = bot->GetGUID().GetRawValue();
        ctx.ambiguous = (ctx.startPolyRef == 0);
        return ctx;
    }

    // Detour query filter that makes the no-go volumes of dungeon/DcNavPenaltyRegistry expensive (a ledge face the mesh generator stitched into a
    // "ramp" that a player cannot climb). Never a hard rejection: if the boxed edge is the only way, the route still takes it. A route that
    // begins inside a volume ignores the volumes, so a party standing in one is never caged.
    class PenaltyFilter : public dtQueryFilter
    {
    public:
        PenaltyFilter(uint32 mapId, float x, float y, float z)
            : _mapId(mapId), _active(DcNavPenaltyRegistry::HasVolumes(mapId) && !DcNavPenaltyRegistry::IsInsideRegion(mapId, x, y, z))
        {
        }

        float getCost(float const* pa, float const* pb, dtPolyRef prevRef, dtMeshTile const* prevTile, dtPoly const* prevPoly, dtPolyRef curRef,
            dtMeshTile const* curTile, dtPoly const* curPoly, dtPolyRef nextRef, dtMeshTile const* nextTile, dtPoly const* nextPoly) const override
        {
            float const base = dtQueryFilter::getCost(pa, pb, prevRef, prevTile, prevPoly, curRef, curTile, curPoly, nextRef, nextTile, nextPoly);
            if (!_active)
                return base;
            // Edge midpoint back in world space (Detour's vertex order is y, z, x).
            return base * DcNavPenaltyRegistry::PenaltyAt(_mapId, (pa[2] + pb[2]) * 0.5f, (pa[0] + pb[0]) * 0.5f, (pa[1] + pb[1]) * 0.5f);
        }

        bool Active() const { return _active; }

    private:
        uint32 _mapId;
        bool _active;
    };

    bool RouteWaypoint(Player* bot, float gx, float gy, float gz, float& ox, float& oy, float& oz, float stepYards = ROUTE_STEP_YARDS)
    {
        Map* map = bot->GetMap();
        if (!map || bot->CanFly())
            return false;
        dtNavMeshQuery const* query = map->GetMapCollisionData().GetMMapData().GetNavMeshQuery();
        if (!query)
            return false;

        _stats.pathQueries.fetch_add(1, std::memory_order_relaxed);

        PenaltyFilter filter(map->GetId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
        filter.setIncludeFlags(NAV_GROUND | NAV_WATER);
        filter.setExcludeFlags(0);

        // Detour works in (y, z, x) of the world coordinates.
        float const start[3] = { bot->GetPositionY(), bot->GetPositionZ(), bot->GetPositionX() };
        float const end[3] = { gy, gz, gx };
        float const extents[3] = { 5.0f, 20.0f, 5.0f };
        dtPolyRef startRef = 0, endRef = 0;
        float startPt[3], endPt[3];
        if (dtStatusFailed(query->findNearestPoly(start, extents, &filter, &startRef, startPt)) || !startRef)
            return false;
        if (dtStatusFailed(query->findNearestPoly(end, extents, &filter, &endRef, endPt)) || !endRef)
            return false;

        constexpr int MAX_POLYS = 1024;
        constexpr int MAX_POINTS = 512;
        dtPolyRef polys[MAX_POLYS];
        int polyCount = 0;
        if (dtStatusFailed(query->findPath(startRef, endRef, startPt, endPt, &filter, polys, &polyCount, MAX_POLYS)) || polyCount <= 0)
            return false;

        float closestEnd[3];
        bool overPoly = false;
        if (dtStatusFailed(query->closestPointOnPoly(polys[polyCount - 1], endPt, closestEnd, &overPoly)))
            return false;

        float points[MAX_POINTS * 3];
        int pointCount = 0;
        if (dtStatusFailed(query->findStraightPath(startPt, closestEnd, polys, polyCount, points, nullptr, nullptr, &pointCount, MAX_POINTS)) || pointCount < 2)
            return false;

        float walked = 0.0f;
        float lastX = points[0], lastY = points[1], lastZ = points[2];
        for (int i = 1; i < pointCount; ++i)
        {
            float const dx = points[i * 3] - lastX, dy = points[i * 3 + 1] - lastY, dz = points[i * 3 + 2] - lastZ;
            float const seg = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (walked + seg >= stepYards)
            {
                float const t = seg > 0.0f ? (stepYards - walked) / seg : 0.0f;
                // back to world coordinates: x = detour z, y = detour x, z = detour y
                ox = lastZ + dz * t;
                oy = lastX + dx * t;
                oz = lastY + dy * t;
                return true;
            }
            walked += seg;
            lastX = points[i * 3];
            lastY = points[i * 3 + 1];
            lastZ = points[i * 3 + 2];
        }
        // the whole corridor is short enough: go to its end
        ox = lastZ;
        oy = lastX;
        oz = lastY;
        return true;
    }

    LocomotionToken IssueLeg(Player* bot, MovementRequest& req)
    {
        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();
        float bz = bot->GetPositionZ();

        float tx = req.x;
        float ty = req.y;
        float tz = req.z;
        bool usedRouteWaypoint = false;

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
                // Prefer navmesh corridor waypoint first for long legs (>120 yd) instead of
                // guessing a straight-line 2D point that may land inside a wall, cliff, or wrong floor.
                float wx = 0.0f, wy = 0.0f, wz = 0.0f;
                if (RouteWaypoint(bot, req.x, req.y, req.z, wx, wy, wz) && Dist2d(bx, by, wx, wy) > SAME_POINT_YARDS)
                {
                    tx = wx;
                    ty = wy;
                    tz = wz;
                    usedRouteWaypoint = true;
                }
                else
                {
                    float t = LEG_YARDS / dist;
                    tx = bx + (req.x - bx) * t;
                    ty = by + (req.y - by) * t;
                    tz = bz;
                }
            }
        }

        // Token-safe: Issue leg under explicit Navigate MoveMode and carry request generation guard.
        MovementDispatchGuard guard{req.requestGeneration, req.goalId};
        NavFailureKind failureKind = NavFailureKind::None;
        // On a map with no-go volumes (dungeon/DcNavPenaltyRegistry) the short legs follow the penalty-aware corridor: the engine's own path
        // generator does not know the volumes and would take the ledge shortcut again.
        LocomotionToken token;
        if (req.recoveryMode == RecoveryMode::None && bot->GetMap() && DcNavPenaltyRegistry::HasVolumes(bot->GetMapId()))
        {
            float wx = 0.0f, wy = 0.0f, wz = 0.0f;
            if (RouteWaypoint(bot, req.x, req.y, req.z, wx, wy, wz, 45.0f) && Dist2d(bx, by, wx, wy) > SAME_POINT_YARDS)
            {
                g_trustGivenZ = true;
                token = BotMovement::MoveToInternal(bot, req.owner, wx, wy, wz, /*forceDestination=*/false, MoveMode::Navigate, guard);
                g_trustGivenZ = false;
                if (token.IsValid())
                {
                    tx = wx;
                    ty = wy;
                    tz = wz;
                }
            }
        }
        if (!token.IsValid())
        {
            if (usedRouteWaypoint)
                g_trustGivenZ = true;
            token = BotMovement::MoveToInternal(bot, req.owner, tx, ty, tz, /*forceDestination=*/false, MoveMode::Navigate, guard, &failureKind);
            if (usedRouteWaypoint)
                g_trustGivenZ = false;
        }
        if (!token.IsValid() && req.recoveryMode == RecoveryMode::None && (tx != req.x || ty != req.y))
        {
            // The leg point is a straight-line guess towards the goal. In a cave or a corridor it lies inside rock, has no path, and the first leg
            // used to fail for good (an auto-dungeon tank stood at the entrance because of it). Ask for the path to the goal itself instead: the
            // navigation mesh finds the way, and the next leg continues from wherever that path ends.
            tx = req.x;
            ty = req.y;
            tz = req.z;
            token = BotMovement::MoveToInternal(bot, req.owner, tx, ty, tz, /*forceDestination=*/false, MoveMode::Navigate, guard, &failureKind);
        }
        if (!token.IsValid() && failureKind == NavFailureKind::NoPath && req.recoveryMode == RecoveryMode::None && !usedRouteWaypoint)
        {
            // Short/medium leg failed direct PathGenerator (e.g. winding switchback > 74 polys even though 2D dist <= 120 yd)
            // or flying/fallback leg failed: ask RouteWaypoint for a reachable corridor point.
            float wx = 0.0f, wy = 0.0f, wz = 0.0f;
            if (RouteWaypoint(bot, req.x, req.y, req.z, wx, wy, wz) && Dist2d(bx, by, wx, wy) > SAME_POINT_YARDS)
            {
                tx = wx;
                ty = wy;
                tz = wz;
                g_trustGivenZ = true;
                token = BotMovement::MoveToInternal(bot, req.owner, tx, ty, tz, /*forceDestination=*/false, MoveMode::Navigate, guard, &failureKind);
                g_trustGivenZ = false;
            }
            else if (tx != req.x || ty != req.y)
            {
                tx = req.x;
                ty = req.y;
                tz = req.z;
                token = BotMovement::MoveToInternal(bot, req.owner, tx, ty, tz, /*forceDestination=*/false, MoveMode::Navigate, guard, &failureKind);
            }
        }
        req.lastFailureKind = failureKind;
        if (!token.IsValid())
            return {};

        uint32 now = NowMs();
        NavigateTransaction::CommitLeg(req, token, tx, ty, now, tz);
        req.progress.StartLeg(Dist2d(bx, by, tx, ty), std::abs(bz - tz), now);
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

    bool QueryDirectPathLength(Player const* bot, float gx, float gy, float gz, float& outLength)
    {
        outLength = -1.0f;
        if (!bot)
            return false;
        if (bot->CanFly())
        {
            outLength = std::hypot(std::hypot(bot->GetPositionX() - gx, bot->GetPositionY() - gy), bot->GetPositionZ() - gz);
            return true;
        }
        Map* map = bot->GetMap();
        if (!map)
            return false;
        dtNavMeshQuery const* query = map->GetMapCollisionData().GetMMapData().GetNavMeshQuery();
        if (!query)
            return false;

        _stats.pathQueries.fetch_add(1, std::memory_order_relaxed);

        dtQueryFilter filter;
        filter.setIncludeFlags(NAV_GROUND | NAV_WATER);
        filter.setExcludeFlags(0);

        float const start[3] = { bot->GetPositionY(), bot->GetPositionZ(), bot->GetPositionX() };
        float const end[3] = { gy, gz, gx };
        float const extents[3] = { 5.0f, 10.0f, 5.0f };
        dtPolyRef startRef = 0, endRef = 0;
        float startPt[3], endPt[3];
        if (dtStatusFailed(query->findNearestPoly(start, extents, &filter, &startRef, startPt)) || !startRef)
            return true;
        if (dtStatusFailed(query->findNearestPoly(end, extents, &filter, &endRef, endPt)) || !endRef)
            return true;
        if (std::fabs(endPt[1] - gz) > 6.0f)
            return true;

        constexpr int MAX_POLYS = 256;
        constexpr int MAX_POINTS = 256;
        dtPolyRef polys[MAX_POLYS];
        int polyCount = 0;
        dtStatus const pathStatus = query->findPath(startRef, endRef, startPt, endPt, &filter, polys, &polyCount, MAX_POLYS);
        if (dtStatusFailed(pathStatus) || (pathStatus & DT_PARTIAL_RESULT) || polyCount <= 0 || polys[polyCount - 1] != endRef)
            return true;

        float closestEnd[3];
        bool overPoly = false;
        if (dtStatusFailed(query->closestPointOnPoly(polys[polyCount - 1], endPt, closestEnd, &overPoly)))
            return true;

        float points[MAX_POINTS * 3];
        int pointCount = 0;
        if (dtStatusFailed(query->findStraightPath(startPt, closestEnd, polys, polyCount, points, nullptr, nullptr, &pointCount, MAX_POINTS)) || pointCount < 1)
            return true;

        float total = 0.0f;
        float lastX = startPt[0], lastY = startPt[1], lastZ = startPt[2];
        for (int i = 0; i < pointCount; ++i)
        {
            float const dx = points[i * 3] - lastX;
            float const dy = points[i * 3 + 1] - lastY;
            float const dz = points[i * 3 + 2] - lastZ;
            total += std::sqrt(dx * dx + dy * dy + dz * dz);
            lastX = points[i * 3];
            lastY = points[i * 3 + 1];
            lastZ = points[i * 3 + 2];
        }
        outLength = total;
        return true;
    }

    // =========================================================================
    // Arbitrated Locomotion Primitives (Linearized Physical Dispatch & Token-Safe)
    // =========================================================================

    // Why a movement request of the auto-dungeon owner was refused: one line per bot and reason every 5 seconds. A bot that is told to walk and does not
    // gives no other trace, as every refusal below is a silent "return {}".
    static void NoteMoveRefusal(Player* bot, MoveOwner owner, char const* reason, float x, float y)
    {
        if (owner != MoveOwner::AutoDungeon && owner != MoveOwner::Corpse)
            return;
        static std::unordered_map<ObjectGuid, std::pair<std::string, uint32>> last;
        auto& entry = last[bot->GetGUID()];
        uint32 const now = getMSTime();
        std::string const key = Acore::StringFormat("{} {:.0f} {:.0f}", reason, x, y);
        // A corpse run tries several legs and fallbacks per second, most of which fail on the way to one that works: one line per minute and bot is
        // enough, the outcome is logged by the ghost handler itself.
        if (owner == MoveOwner::Corpse ? now - entry.second < 60000 : (entry.first == key && now - entry.second < 5000))
            return;
        entry = { key, now };
        LOG_INFO("module.coa-playerbots", "{}: movement of '{}' refused: {} (goal {:.0f} {:.0f}, now {:.0f} {:.0f} {:.0f}).",
            owner == MoveOwner::Corpse ? "CorpseRun" : "AutoDungeon", bot->GetName(), reason, x, y, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    }

    LocomotionToken MoveToInternal(Player* bot, MoveOwner owner, float x, float y, float z, bool forceDestination, MoveMode executionMode, std::optional<MovementDispatchGuard> guard, NavFailureKind* outFailureKind)
    {
        auto setFailure = [&](NavFailureKind kind)
        {
            if (outFailureKind)
                *outFailureKind = kind;
        };
        setFailure(NavFailureKind::None);

        if (!bot || !bot->IsInWorld())
        {
            setFailure(NavFailureKind::ExternallyControlled);
            return {};
        }

        // A released ghost is not alive, but walking back to its corpse is exactly what it has to do: refusing
        // every move of a dead bot meant no ghost ever reached its body and each came back only on the failsafe.
        if (!bot->IsAlive() && !(owner == MoveOwner::Corpse && bot->HasPlayerFlag(PLAYER_FLAGS_GHOST)))
        {
            setFailure(NavFailureKind::ExternallyControlled);
            return {};
        }

        // An active Auto Shot / Shoot (the auto-repeat slot) is not a cast in progress: it stays set for the whole fight and, counted here, kept every
        // ranged bot from walking anywhere (retreating from a target in its dead zone, following, auto-dungeon legs) while it was on.
        if (bot->IsNonMeleeSpellCast(false, false, /*skipAutorepeat=*/true))
        {
            setFailure(NavFailureKind::ExternallyControlled);
            NoteMoveRefusal(bot, owner, "the bot is casting a spell", x, y);
            return {};
        }

        if (IsExternallyControlled(bot))
        {
            setFailure(NavFailureKind::ExternallyControlled);
            NoteMoveRefusal(bot, owner, "the bot is externally controlled (flight, stun, charge, ...)", x, y);
            return {};
        }

        uint32 now = NowMs();
        float groundZ = g_trustGivenZ ? z : ResolveGroundZ(bot, x, y, z);
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());

        // Step 1: Pre-validation and plan ticket allocation under shard state lock.
        // TRANSACTIONAL: Do NOT commit owner or increment commandId before PathGenerator succeeds!
        uint64 myPlanTicket = 0;
        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
            {
                setFailure(NavFailureKind::BlockedByOwner);
                NoteMoveRefusal(bot, owner, "another movement owner holds the bot", x, y);
                return {};
            }

            myPlanTicket = LocomotionStateStore::AllocatePlanTicketLocked(shard, bot->GetGUID(), owner);

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
                setFailure(NavFailureKind::NoPath);
                CoaBotPerf::Increment("path_cache_hits");
                NoteMoveRefusal(bot, owner, "no path to this point (cached)", x, y);
                return {};
            }
        }

        // Cross-bot shared no-path corridor check: if multiple nearby bots in the same topology/phase/capability context
        // already confirmed this corridor has no path, skip repeating the expensive PathGenerator query until TTL expires.
        float const startX = bot->GetPositionX();
        float const startY = bot->GetPositionY();
        float const startZ = bot->GetPositionZ();
        SharedNoPathContext const noPathCtx = MakeSharedNoPathContext(bot, startX, startY, startZ);
        if (!bot->CanFly() && !forceDestination &&
            LocomotionStateStore::IsSharedCorridorBlocked(noPathCtx, startX, startY, startZ, x, y, groundZ, now))
        {
            setFailure(NavFailureKind::NoPath);
            _stats.sharedNoPathHits.fetch_add(1, std::memory_order_relaxed);
            NoteMoveRefusal(bot, owner, "no path to this corridor (shared cache)", x, y);
            return {};
        }

        // Step 2: Outside shard state lock AND outside dispatch gate: compute PathGenerator!
        // This ensures unrelated bots sharing the striped dispatch gate are not blocked during pathfinding.
        static bool const reusePath = sConfigMgr->GetOption<bool>("CoaBots.Navigation.ReuseValidatedPath", false);
        Movement::PointsArray validatedPath;
        uint32 const sourceMap = bot->GetMapId(), sourceInstance = bot->GetInstanceId(), sourcePhase = bot->GetPhaseMask();
        bool pathValid = true;
        uint32 failedPathType = 0;
        if (!bot->CanFly())
        {
            _stats.pathQueries.fetch_add(1, std::memory_order_relaxed);
            PathGenerator path(bot);
            {
                CoaBotPerf::Scope scope(CoaBotPerf::Metric::Navigation);
                path.CalculatePath(x, y, groundZ, forceDestination);
            }
            CoaBotPerf::Increment("path_requests");
            if (reusePath && !forceDestination && (path.GetPathType() & PATHFIND_NORMAL) &&
                !(path.GetPathType() & PATHFIND_INCOMPLETE))
                validatedPath = path.GetPath();
            if (path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_NOT_USING_PATH))
            {
                pathValid = false;
                failedPathType = uint32(path.GetPathType());
            }
        }

        // Step 3: Handle path result under shard state lock
        if (!pathValid)
        {
            CoaBotPerf::Increment("path_failures");
            setFailure(NavFailureKind::NoPath);
            LocomotionStateStore::RecordSharedCorridorFailure(noPathCtx, startX, startY, startZ, x, y, groundZ, now);
            std::lock_guard<std::mutex> lock(shard.mutex);
            // Record failure in nopath cache WITHOUT touching rec.owner or rec.commandId!
            NopathCacheEntry& entry = shard.nopathCache[bot->GetGUID()];
            entry.owner = owner;
            entry.x = x;
            entry.y = y;
            entry.z = groundZ;
            entry.expiresAtMs = now + NOPATH_RECHECK_COOLDOWN_MS;
            NoteMoveRefusal(bot, owner, Acore::StringFormat("the navigation mesh has no path to this point (path type {}, z {:.1f})", failedPathType, groundZ).c_str(), x, y);
            return {};
        }

        if (!bot->CanFly())
        {
            LocomotionStateStore::RecordSharedCorridorSuccess(noPathCtx, startX, startY, startZ, x, y, groundZ);
        }

        // Step 4: Serialize physical MotionMaster dispatch under dispatch gate
        std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));
        LocomotionToken token;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (reusePath && (bot->GetMapId() != sourceMap || bot->GetInstanceId() != sourceInstance ||
                bot->GetPhaseMask() != sourcePhase))
            {
                CoaBotPerf::Increment("path_context_cancelled");
                setFailure(NavFailureKind::ExternallyControlled);
                return {};
            }

            // Revalidate CanClaim in case state changed during PathGenerator calculation
            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
            {
                setFailure(NavFailureKind::BlockedByOwner);
                return {};
            }

            // Planning ticket epoch check: older plan cannot overwrite newer intent
            if (!LocomotionStateStore::IsPlanCurrentLocked(shard, bot->GetGUID(), owner, myPlanTicket))
            {
                setFailure(NavFailureKind::BlockedByOwner);
                return {};
            }

            // Stale Navigate guard check: validate BEFORE physical commit and command bump!
            if (executionMode == MoveMode::Navigate && guard.has_value())
            {
                auto rItr = shard.requests.find(bot->GetGUID());
                if (rItr == shard.requests.end() ||
                    rItr->second.owner != owner ||
                    rItr->second.requestGeneration != guard->requestGeneration ||
                    rItr->second.goalId != guard->goalId)
                {
                    // Stale request! Zero physical mutation, zero commandId bump.
                    setFailure(NavFailureKind::BlockedByOwner);
                    return {};
                }
            }

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
        bool reused = false;
#ifdef COA_VALIDATED_POINT_PATH
        if (reusePath && validatedPath.size() >= 2)
            reused = bot->GetMotionMaster()->MovePointWithValidatedPath(uint32(owner), x, y, groundZ, validatedPath);
#endif
        if (reused) CoaBotPerf::Increment("validated_paths_reused");
        else
            bot->GetMotionMaster()->MovePoint(uint32(owner), x, y, groundZ, FORCED_MOVEMENT_NONE, 0.0f, 0.0f,
                /*generatePath=*/true, forceDestination);
        _stats.issued.fetch_add(1, std::memory_order_relaxed);

        return token;
    }

    LocomotionToken MoveTo(Player* bot, MoveOwner owner, float x, float y, float z, bool forceDestination)
    {
        return MoveToInternal(bot, owner, x, y, z, forceDestination, MoveMode::Point);
    }

    NavStatus Navigate(Player* bot, MoveOwner owner, uint64 goalId, float x, float y, float z, float acceptRadius, LocomotionToken* outToken, bool allowMount)
    {
        if (!bot || !bot->IsInWorld())
            return NavStatus::Blocked;

        // A released ghost walking back to its corpse (see MoveToInternal).
        if (!bot->IsAlive() && !(owner == MoveOwner::Corpse && bot->HasPlayerFlag(PLAYER_FLAGS_GHOST)))
            return NavStatus::Blocked;

        uint32 now = NowMs();
        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());

        if (IsExternallyControlled(bot))
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            auto rItr = shard.requests.find(bot->GetGUID());
            if (rItr != shard.requests.end() && rItr->second.owner == owner && rItr->second.goalId == goalId)
            {
                rItr->second.progress.Hold(now);
                rItr->second.holdReason = NavHoldReason::ExternalControl;
                rItr->second.lastFailureKind = NavFailureKind::ExternallyControlled;
            }
            return NavStatus::Blocked;
        }

        // Context-aware pause: rooted or actively casting a non-melee spell (including mount cast).
        // Do not advance the stall timer or trigger false recovery while legitimately stationary.
        bool const isRooted = bot->HasUnitState(UNIT_STATE_ROOT);
        bool const isCasting = bot->IsNonMeleeSpellCast(false, false, /*skipAutorepeat=*/true);
        if (isRooted || isCasting)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
            {
                auto rItr = shard.requests.find(bot->GetGUID());
                if (rItr != shard.requests.end() && rItr->second.owner == owner && rItr->second.goalId == goalId)
                {
                    rItr->second.progress.Hold(now);
                    rItr->second.holdReason = NavHoldReason::HigherPriorityOwner;
                    rItr->second.lastFailureKind = NavFailureKind::BlockedByOwner;
                }
                return NavStatus::Blocked;
            }

            auto rItr = shard.requests.find(bot->GetGUID());
            if (rItr != shard.requests.end() && rItr->second.owner == owner && rItr->second.goalId == goalId)
            {
                rItr->second.lastCallAt = now;
                rItr->second.progress.Hold(now);
                rItr->second.holdReason = isRooted ? NavHoldReason::Rooted : NavHoldReason::Casting;
                if (outToken && rItr->second.movementCommandId != 0)
                    *outToken = LocomotionToken{bot->GetGUID(), owner, rItr->second.movementCommandId};
            }
            return NavStatus::Moving;
        }

        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();
        float bz = bot->GetPositionZ();
        float distToGoal = Dist2d(bx, by, x, y);
        float distZ = std::abs(bz - z);

        // 3D Arrival Check: require both planar proximity and vertical floor compatibility
        if (distToGoal <= acceptRadius)
        {
            if (NavigateTransaction::IsWithin3dArrival(distToGoal, acceptRadius, distZ))
            {
                LocomotionToken tokenToRelease;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    auto rItr = shard.requests.find(bot->GetGUID());
                    if (rItr != shard.requests.end())
                    {
                        if (NavigateTransaction::ValidateArrival(rItr->second, owner, goalId, distToGoal, acceptRadius, tokenToRelease, distZ))
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
            else
            {
                // Horizontally within acceptRadius, but on a different vertical floor/bridge/cave level!
                _stats.verticalRejections.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Step 1: Snapshot and prepare request under shard state lock
        MovementRequest reqCopy;
        uint64 expectedRequestGen = 0;
        bool isNewRequest = false;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);

            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
            {
                auto rItr = shard.requests.find(bot->GetGUID());
                if (rItr != shard.requests.end() && rItr->second.owner == owner && rItr->second.goalId == goalId)
                {
                    rItr->second.progress.Hold(now);
                    rItr->second.holdReason = NavHoldReason::HigherPriorityOwner;
                    rItr->second.lastFailureKind = NavFailureKind::BlockedByOwner;
                }
                return NavStatus::Blocked;
            }

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
                req.allowMount = allowMount;
                req.startedAt = now;
                req.lastCallAt = now;
                req.progress.Start(distToGoal, distZ, now);
                req.legs = 0;
                req.lastIssueAt = 0;
                req.legX = 0.0f;
                req.legY = 0.0f;
                req.legZ = 0.0f;
                req.detour = false;
                req.recoveryMode = RecoveryMode::None;
                req.backtrackAttempts = 0;
                req.consecutiveNoPath = 0;
                req.lastFailureKind = NavFailureKind::None;
                req.holdReason = NavHoldReason::None;

                reqCopy = req;
            }
            else
            {
                rItr->second.lastCallAt = now;
                rItr->second.acceptRadius = acceptRadius;
                rItr->second.allowMount = allowMount;
                rItr->second.holdReason = NavHoldReason::None;
                expectedRequestGen = rItr->second.requestGeneration;
                // If previous first-leg attempt had no path (movementCommandId == 0), retry first leg when cooldown expires
                if (rItr->second.movementCommandId == 0)
                    isNewRequest = true;
                reqCopy = rItr->second;
            }
        }

        std::optional<CoaBotScheduling::WorkScope> navigationWork;
        NavStatus deferredStatus = NavStatus::Moving;
        auto admitNavigation = [&]()
        {
            if (navigationWork) return bool(*navigationWork);
            navigationWork.emplace(CoaBotScheduling::Work::Navigation, bot, expectedRequestGen);
            if (*navigationWork) return true;
            std::lock_guard<std::mutex> lock(shard.mutex);
            auto pending = shard.requests.find(bot->GetGUID());
            if (pending != shard.requests.end() && pending->second.requestGeneration == expectedRequestGen &&
                pending->second.goalId == goalId && pending->second.owner == owner &&
                pending->second.movementCommandId == reqCopy.movementCommandId)
            {
                pending->second.progress.Hold(now);
                pending->second.holdReason = NavHoldReason::WorkBudget;
            }
            else deferredStatus = NavStatus::Superseded;
            return false;
        };
        reqCopy.holdReason = NavHoldReason::None;

        // Step 2: Handle First Leg (or retry after unissued first-leg failure)
        if (isNewRequest)
        {
            SharedNoPathContext const noPathCtx = MakeSharedNoPathContext(bot, bx, by, bz);
            if (reqCopy.consecutiveNoPath > 0 && (now - reqCopy.lastIssueAt < NOPATH_RECHECK_COOLDOWN_MS) &&
                !LocomotionStateStore::IsSharedCorridorBlocked(noPathCtx, bx, by, bz, x, y, z, now))
            {
                // Waiting for nopath recheck cooldown before strike 2
                return NavStatus::Moving;
            }

            if (!admitNavigation()) return deferredStatus;
            uint64 previousMovementCommandId = reqCopy.movementCommandId;
            LocomotionToken token = IssueLeg(bot, reqCopy);
            if (token.IsValid())
            {
                bool committed = false;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    committed = LocomotionStateStore::CommitNavigateLegLocked(
                        shard, bot->GetGUID(), expectedRequestGen, goalId, previousMovementCommandId, reqCopy, token);
                    if (committed && outToken)
                        *outToken = token;
                }

                if (!committed)
                {
                    Release(bot, token);
                    return NavStatus::Superseded;
                }
                return NavStatus::Moving;
            }
            else
            {
                // If first leg was refused due to temporary ownership preemption or external control,
                // hold progress clocks and return Blocked without counting a geometric NoPath strike.
                if (reqCopy.lastFailureKind == NavFailureKind::BlockedByOwner ||
                    reqCopy.lastFailureKind == NavFailureKind::ExternallyControlled)
                {
                    reqCopy.progress.Hold(now);
                    reqCopy.holdReason = (reqCopy.lastFailureKind == NavFailureKind::BlockedByOwner)
                        ? NavHoldReason::HigherPriorityOwner
                        : NavHoldReason::ExternalControl;
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    auto rItr = shard.requests.find(bot->GetGUID());
                    if (rItr != shard.requests.end() &&
                        NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId) &&
                        rItr->second.movementCommandId == 0)
                    {
                        rItr->second = reqCopy;
                    }
                    return NavStatus::Blocked;
                }

                // First-leg geometric path failure: classify and escalate after 2 strikes (or immediately if shared corridor is blocked)
                reqCopy.consecutiveNoPath++;
                reqCopy.lastIssueAt = now;
                reqCopy.lastFailureKind = (distToGoal <= acceptRadius * 2.0f && distZ > NavigateTransaction::MaxArrivalZ(acceptRadius))
                    ? NavFailureKind::VerticallyDisconnected
                    : NavFailureKind::NoPath;

                bool const corridorConfirmedBlocked =
                    reqCopy.consecutiveNoPath >= 2 ||
                    LocomotionStateStore::IsSharedCorridorBlocked(noPathCtx, bx, by, bz, x, y, z, now);

                if (corridorConfirmedBlocked)
                {
                    _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    LocomotionStateStore::EraseRequestIfCurrentLocked(shard, bot->GetGUID(), expectedRequestGen, 0);
                    return NavStatus::Stuck;
                }

                // Strike 1: retain request state so next attempt after cooldown can escalate to Stuck if still unreachable
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    auto rItr = shard.requests.find(bot->GetGUID());
                    if (rItr != shard.requests.end() &&
                        NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId) &&
                        rItr->second.movementCommandId == 0)
                    {
                        rItr->second = reqCopy;
                    }
                }
                return NavStatus::Moving;
            }
        }

        // Step 3: Backtrack state machine
        if (reqCopy.recoveryMode == RecoveryMode::Backtrack)
        {
            float distToBacktrack = Dist2d(bx, by, reqCopy.backtrackX, reqCopy.backtrackY);
            if (distToBacktrack <= 3.5f)
            {
                if (!admitNavigation()) return deferredStatus;
                // Arrived at backtrack safe position! Rebase progress and resume journey toward original goal
                reqCopy.recoveryMode = RecoveryMode::None;
                reqCopy.backtrackAttempts = 0;
                reqCopy.progress.Rebase(distToGoal, distZ, now);
                uint64 previousMovementCommandId = reqCopy.movementCommandId;
                LocomotionToken token = IssueLeg(bot, reqCopy);
                if (token.IsValid())
                {
                    bool committed = false;
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        committed = LocomotionStateStore::CommitNavigateLegLocked(
                            shard, bot->GetGUID(), expectedRequestGen, goalId, previousMovementCommandId, reqCopy, token);
                        if (committed && outToken)
                            *outToken = token;
                    }
                    if (!committed)
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
                    if (!admitNavigation()) return deferredStatus;
                    _stats.backtracks.fetch_add(1, std::memory_order_relaxed);
                    reqCopy.backtrackX = safeTarget->x;
                    reqCopy.backtrackY = safeTarget->y;
                    reqCopy.backtrackZ = safeTarget->z;
                    uint64 previousMovementCommandId = reqCopy.movementCommandId;
                    LocomotionToken token = IssueLeg(bot, reqCopy);
                    if (token.IsValid())
                    {
                        bool committed = false;
                        {
                            std::lock_guard<std::mutex> lock(shard.mutex);
                            committed = LocomotionStateStore::CommitNavigateLegLocked(
                                shard, bot->GetGUID(), expectedRequestGen, goalId, previousMovementCommandId, reqCopy, token);
                            if (committed && outToken)
                                *outToken = token;
                        }
                        if (!committed)
                        {
                            Release(bot, token);
                            return NavStatus::Superseded;
                        }
                    }
                }
                else
                {
                    _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                    bool erased = false;
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        erased = LocomotionStateStore::EraseRequestIfCurrentLocked(
                            shard, bot->GetGUID(), expectedRequestGen, reqCopy.movementCommandId);
                    }
                    if (erased)
                    {
                        if (reqCopy.movementCommandId != 0)
                            Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.movementCommandId});
                        return NavStatus::Stuck;
                    }
                    return NavStatus::Superseded;
                }
            }
        }

        // Step 4: Route-aware progress evaluation (tracks both final goal and active corridor/detour leg)
        bool const hasLegTarget = (reqCopy.legX != 0.0f || reqCopy.legY != 0.0f);
        float const activeLegX = hasLegTarget ? reqCopy.legX : x;
        float const activeLegY = hasLegTarget ? reqCopy.legY : y;
        float const activeLegZ = hasLegTarget ? reqCopy.legZ : z;
        float const legDist2d = Dist2d(bx, by, activeLegX, activeLegY);
        float const legDistZ = std::abs(bz - activeLegZ);
        uint32 const prevSwitchbacks = reqCopy.progress.legSwitchbacks;

        NavRecovery recovery = reqCopy.progress.UpdateRoute(distToGoal, distZ, legDist2d, legDistZ, bx, by, now, PROGRESS_RULES);
        if (reqCopy.progress.legSwitchbacks > prevSwitchbacks)
            _stats.legSwitchbacksHandled.fetch_add(reqCopy.progress.legSwitchbacks - prevSwitchbacks, std::memory_order_relaxed);

        // If progress said GiveUp, check if Backtrack candidates exist before giving up!
        if (recovery == NavRecovery::GiveUp)
        {
            reqCopy.lastFailureKind = NavFailureKind::StallExhausted;
            if (reqCopy.recoveryMode != RecoveryMode::Backtrack && reqCopy.backtrackAttempts < 3)
            {
                std::optional<SafePosition> safeTarget;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    safeTarget = shard.safeHistories[bot->GetGUID()].FindBacktrackTarget(bx, by, bz, bot->GetMapId(), reqCopy.backtrackAttempts);
                }

                if (safeTarget)
                {
                    if (!admitNavigation()) return deferredStatus;
                    _stats.backtracks.fetch_add(1, std::memory_order_relaxed);
                    reqCopy.recoveryMode = RecoveryMode::Backtrack;
                    reqCopy.backtrackAttempts++;
                    reqCopy.backtrackX = safeTarget->x;
                    reqCopy.backtrackY = safeTarget->y;
                    reqCopy.backtrackZ = safeTarget->z;
                    uint64 previousMovementCommandId = reqCopy.movementCommandId;
                    LocomotionToken token = IssueLeg(bot, reqCopy);
                    if (token.IsValid())
                    {
                        bool committed = false;
                        {
                            std::lock_guard<std::mutex> lock(shard.mutex);
                            committed = LocomotionStateStore::CommitNavigateLegLocked(
                                shard, bot->GetGUID(), expectedRequestGen, goalId, previousMovementCommandId, reqCopy, token);
                            if (committed && outToken)
                                *outToken = token;
                        }
                        if (!committed)
                        {
                            Release(bot, token);
                            return NavStatus::Superseded;
                        }
                    }
                }
                else
                {
                    _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                    bool erased = false;
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        erased = LocomotionStateStore::EraseRequestIfCurrentLocked(
                            shard, bot->GetGUID(), expectedRequestGen, reqCopy.movementCommandId);
                    }
                    if (erased)
                    {
                        if (reqCopy.movementCommandId != 0)
                            Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.movementCommandId});
                        return NavStatus::Stuck;
                    }
                    return NavStatus::Superseded;
                }
            }
            else
            {
                _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                bool erased = false;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    erased = LocomotionStateStore::EraseRequestIfCurrentLocked(
                        shard, bot->GetGUID(), expectedRequestGen, reqCopy.movementCommandId);
                }
                if (erased)
                {
                    if (reqCopy.movementCommandId != 0)
                        Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.movementCommandId});
                    return NavStatus::Stuck;
                }
                return NavStatus::Superseded;
            }
        }
        else if (recovery == NavRecovery::Repath)
        {
            if (!admitNavigation()) return deferredStatus;
            _stats.stuckEvents.fetch_add(1, std::memory_order_relaxed);
            _stats.repaths.fetch_add(1, std::memory_order_relaxed);
            reqCopy.recoveryMode = RecoveryMode::Repath;
            reqCopy.detour = false;
            uint64 previousMovementCommandId = reqCopy.movementCommandId;
            LocomotionToken token = IssueLeg(bot, reqCopy);
            if (token.IsValid())
            {
                bool committed = false;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    committed = LocomotionStateStore::CommitNavigateLegLocked(
                        shard, bot->GetGUID(), expectedRequestGen, goalId, previousMovementCommandId, reqCopy, token);
                    if (committed && outToken)
                        *outToken = token;
                }
                if (!committed)
                {
                    Release(bot, token);
                    return NavStatus::Superseded;
                }
            }
        }
        else if (recovery == NavRecovery::DetourLeft || recovery == NavRecovery::DetourRight)
        {
            if (!admitNavigation()) return deferredStatus;
            _stats.stuckEvents.fetch_add(1, std::memory_order_relaxed);
            _stats.detours.fetch_add(1, std::memory_order_relaxed);
            reqCopy.recoveryMode = (recovery == NavRecovery::DetourLeft) ? RecoveryMode::DetourLeft : RecoveryMode::DetourRight;
            reqCopy.detour = true;
            float const goalBearing = std::atan2(y - by, x - bx);
            float angle = goalBearing + (recovery == NavRecovery::DetourLeft ? 1.047f : -1.047f);
            reqCopy.detourX = bx + std::cos(angle) * 15.0f;
            reqCopy.detourY = by + std::sin(angle) * 15.0f;
            reqCopy.detourZ = ResolveGroundZ(bot, reqCopy.detourX, reqCopy.detourY, bz);

            uint64 previousMovementCommandId = reqCopy.movementCommandId;
            LocomotionToken token = IssueLeg(bot, reqCopy);
            if (!token.IsValid() && recovery == NavRecovery::DetourLeft)
            {
                // Left detour blocked on navmesh -> immediately try Right detour in the same tick
                reqCopy.recoveryMode = RecoveryMode::DetourRight;
                reqCopy.progress.stage = 3;
                float const rightAngle = goalBearing - 1.047f;
                reqCopy.detourX = bx + std::cos(rightAngle) * 15.0f;
                reqCopy.detourY = by + std::sin(rightAngle) * 15.0f;
                reqCopy.detourZ = ResolveGroundZ(bot, reqCopy.detourX, reqCopy.detourY, bz);
                token = IssueLeg(bot, reqCopy);
            }
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
                    if (!admitNavigation()) return deferredStatus;
                    _stats.backtracks.fetch_add(1, std::memory_order_relaxed);
                    reqCopy.recoveryMode = RecoveryMode::Backtrack;
                    reqCopy.backtrackAttempts++;
                    reqCopy.backtrackX = safeTarget->x;
                    reqCopy.backtrackY = safeTarget->y;
                    reqCopy.backtrackZ = safeTarget->z;
                    uint64 bPrevCmdId = reqCopy.movementCommandId;
                    LocomotionToken bToken = IssueLeg(bot, reqCopy);
                    if (bToken.IsValid())
                    {
                        bool committed = false;
                        {
                            std::lock_guard<std::mutex> lock(shard.mutex);
                            committed = LocomotionStateStore::CommitNavigateLegLocked(
                                shard, bot->GetGUID(), expectedRequestGen, goalId, bPrevCmdId, reqCopy, bToken);
                            if (committed && outToken)
                                *outToken = bToken;
                        }
                        if (!committed)
                        {
                            Release(bot, bToken);
                            return NavStatus::Superseded;
                        }
                    }
                }
                else
                {
                    _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                    bool erased = false;
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        erased = LocomotionStateStore::EraseRequestIfCurrentLocked(
                            shard, bot->GetGUID(), expectedRequestGen, reqCopy.movementCommandId);
                    }
                    if (erased)
                    {
                        if (reqCopy.movementCommandId != 0)
                            Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.movementCommandId});
                        return NavStatus::Stuck;
                    }
                    return NavStatus::Superseded;
                }
            }
            else
            {
                bool committed = false;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    committed = LocomotionStateStore::CommitNavigateLegLocked(
                        shard, bot->GetGUID(), expectedRequestGen, goalId, previousMovementCommandId, reqCopy, token);
                    if (committed && outToken)
                        *outToken = token;
                }
                if (!committed)
                {
                    Release(bot, token);
                    return NavStatus::Superseded;
                }
            }
        }

        // Check if Detour leg completed (arrived near detour waypoint) or stalled early
        if (reqCopy.recoveryMode == RecoveryMode::DetourLeft || reqCopy.recoveryMode == RecoveryMode::DetourRight)
        {
            float const distToDetour = Dist2d(bx, by, reqCopy.detourX, reqCopy.detourY);
            if (distToDetour <= 4.0f)
            {
                if (!admitNavigation()) return deferredStatus;
                // Completed detour leg! Resume normal pathing toward original goal from this new vantage point.
                reqCopy.recoveryMode = RecoveryMode::None;
                reqCopy.detour = false;
                uint64 previousMovementCommandId = reqCopy.movementCommandId;
                LocomotionToken token = IssueLeg(bot, reqCopy);
                if (token.IsValid())
                {
                    bool committed = false;
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        committed = LocomotionStateStore::CommitNavigateLegLocked(
                            shard, bot->GetGUID(), expectedRequestGen, goalId, previousMovementCommandId, reqCopy, token);
                        if (committed && outToken)
                            *outToken = token;
                    }
                    if (!committed)
                    {
                        Release(bot, token);
                        return NavStatus::Superseded;
                    }
                    return NavStatus::Moving;
                }
            }
            else if (!bot->isMoving() && (now - reqCopy.lastIssueAt > 1500))
            {
                std::optional<SafePosition> safeTarget;
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    safeTarget = shard.safeHistories[bot->GetGUID()].FindBacktrackTarget(bx, by, bz, bot->GetMapId(), reqCopy.backtrackAttempts);
                }

                if (safeTarget && reqCopy.backtrackAttempts < 3)
                {
                    if (!admitNavigation()) return deferredStatus;
                    _stats.backtracks.fetch_add(1, std::memory_order_relaxed);
                    reqCopy.backtrackAttempts++;
                    reqCopy.recoveryMode = RecoveryMode::Backtrack;
                    reqCopy.backtrackX = safeTarget->x;
                    reqCopy.backtrackY = safeTarget->y;
                    reqCopy.backtrackZ = safeTarget->z;
                    uint64 previousMovementCommandId = reqCopy.movementCommandId;
                    LocomotionToken token = IssueLeg(bot, reqCopy);
                    if (token.IsValid())
                    {
                        bool committed = false;
                        {
                            std::lock_guard<std::mutex> lock(shard.mutex);
                            committed = LocomotionStateStore::CommitNavigateLegLocked(
                                shard, bot->GetGUID(), expectedRequestGen, goalId, previousMovementCommandId, reqCopy, token);
                            if (committed && outToken)
                                *outToken = token;
                        }
                        if (!committed)
                        {
                            Release(bot, token);
                            return NavStatus::Superseded;
                        }
                    }
                }
                else
                {
                    _stats.gaveUp.fetch_add(1, std::memory_order_relaxed);
                    bool erased = false;
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        erased = LocomotionStateStore::EraseRequestIfCurrentLocked(
                            shard, bot->GetGUID(), expectedRequestGen, reqCopy.movementCommandId);
                    }
                    if (erased)
                    {
                        if (reqCopy.movementCommandId != 0)
                            Release(bot, LocomotionToken{bot->GetGUID(), owner, reqCopy.movementCommandId});
                        return NavStatus::Stuck;
                    }
                    return NavStatus::Superseded;
                }
            }
        }

        // Check if current normal leg completed (or was interrupted mid-leg by temporary combat/control preemption) and next leg is needed
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE && !bot->isMoving())
        {
            float legDist = Dist2d(bx, by, reqCopy.legX, reqCopy.legY);
            bool const legReachedOrInterrupted = (legDist <= 4.0f) ||
                (reqCopy.recoveryMode == RecoveryMode::None && now - reqCopy.lastIssueAt >= 1500);
            if (legReachedOrInterrupted && !NavigateTransaction::IsWithin3dArrival(distToGoal, acceptRadius, distZ))
            {
                if (!admitNavigation()) return deferredStatus;
                uint64 previousMovementCommandId = reqCopy.movementCommandId;
                LocomotionToken token = IssueLeg(bot, reqCopy);
                if (token.IsValid())
                {
                    bool committed = false;
                    {
                        std::lock_guard<std::mutex> lock(shard.mutex);
                        committed = LocomotionStateStore::CommitNavigateLegLocked(
                            shard, bot->GetGUID(), expectedRequestGen, goalId, previousMovementCommandId, reqCopy, token);
                        if (committed && outToken)
                            *outToken = token;
                    }
                    if (!committed)
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
                NavigateTransaction::IsRequestCurrent(rItr->second, expectedRequestGen, goalId) &&
                rItr->second.movementCommandId == reqCopy.movementCommandId)
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

            LocomotionStateStore::AdvanceIntentEpochLocked(shard, bot->GetGUID(), owner);

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

            LocomotionStateStore::AdvanceIntentEpochLocked(shard, bot->GetGUID(), owner);

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

            LocomotionStateStore::AdvanceIntentEpochLocked(shard, bot->GetGUID(), owner);

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

    bool CanAttemptFlyingMount(Player* bot)
    {
        if (!bot || !CanUseFlyingMount(bot->GetMapId(), bot->GetLevel(),
            LocalLevelScaling::FlightUnlockLevel.load(std::memory_order_relaxed), bot->GetBaseSkillValue(SKILL_RIDING)))
            return false;
        return SelectMountSpell(bot, true) != 0;
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

            auto lItr = shard.locomotion.find(bot->GetGUID());
            if (lItr != shard.locomotion.end())
            {
                // Never mount during Hold!
                if (lItr->second.mode == MoveMode::Hold)
                    return false;
            }

            if (!LocomotionStateStore::CanMountLocked(shard, bot->GetGUID(), travelDistance, now,
                    bot->IsOutdoors(), bot->IsInCombat(), bot->IsNonMeleeSpellCast(false),
                    bot->GetLevel(), false, isDungeon, isRaid, isInWater, isSwimming))
                return false;

            if (!LocomotionStateStore::CanClaimLocked(shard, bot->GetGUID(), owner, false))
                return false;

            BotMountRecord const& mRec = shard.mounts[bot->GetGUID()];
            if (now < mRec.noMountAvailableUntilMs)
                return false;
        }

        // Step 2: Select mount spell BEFORE stopping locomotion!
        // If the bot has no valid mount spell, locomotion must NOT be disrupted.
        if (wantFlying)
        {
            if (!CanAttemptFlyingMount(bot))
                wantFlying = false;
        }

        uint32 spellId = SelectMountSpell(bot, wantFlying);
        if (!spellId)
        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            shard.mounts[bot->GetGUID()].noMountAvailableUntilMs = now + 5000;
            return false;
        }

        // Step 3: Valid mount spell is resolved -- stop active movement under this owner so cast succeeds.
        ForceStopOwnerInternal(bot, owner);
        if (bot->isMoving())
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
            mRec.castingGeneration = mRec.mountGeneration;
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
        // Mechanics' native combat form uses a mounted aura. It is not a travel
        // mount: stripping it tears down Mechsuit and forces endless rebuilds.
        if (bot->IsInTinkerMechsuit())
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

        if (leader->IsFlying() || leader->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED))
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

        // Serialize mount resolution under per-bot dispatch gate
        std::lock_guard<std::mutex> dispatchLock(LocomotionStateStore::GetDispatchGate(bot->GetGUID()));

        LocomotionShard& shard = LocomotionStateStore::GetShard(bot->GetGUID());
        uint32 now = NowMs();
        bool removeStaleAura = false;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotMountRecord& mRec = shard.mounts[bot->GetGUID()];

            if (mRec.state == MountState::MountCasting)
            {
                // Mount casts take ~1.5s. If bot is still non-melee casting, wait for it!
                if (bot->IsNonMeleeSpellCast(false))
                    return;

                uint64 castGen = mRec.castingGeneration;
                uint32 justTried = mRec.pendingMountSpellId;
                mRec.pendingMountSpellId = 0;

                // Generation check: if castingGeneration != mountGeneration, cast was cancelled/invalidated!
                if (castGen != mRec.mountGeneration)
                {
                    if (bot->IsMounted())
                    {
                        removeStaleAura = true;
                    }
                    // Do NOT count success, do NOT transition to Mounted!
                }
                else if (bot->IsMounted())
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
            else
            {
                // Bot is in Cooldown or Unmounted
                if (bot->IsMounted())
                {
                    // If in Cooldown or from an invalidated generation, strip stale mount aura!
                    if (mRec.state == MountState::Cooldown || (mRec.castingGeneration != 0 && mRec.castingGeneration != mRec.mountGeneration))
                    {
                        removeStaleAura = true;
                    }
                    else if (mRec.state == MountState::Unmounted && now >= mRec.remountCooldownUntilMs)
                    {
                        // Clean external mount sync
                        mRec.state = bot->CanFly() ? MountState::MountedFlying : MountState::MountedGround;
                    }
                }
            }
        }

        if (removeStaleAura && bot->IsMounted() && !bot->IsInTinkerMechsuit())
        {
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
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

        bool const canAttemptFlight = CanAttemptFlyingMount(bot);
        MountPolicyDecision decision;
        MoveOwner currentOwner = MoveOwner::None;
        float travelDist = 0.0f;

        {
            std::lock_guard<std::mutex> lock(shard.mutex);
            BotLocomotionRecord const& rec = shard.locomotion[bot->GetGUID()];
            BotMountRecord const& mRec = shard.mounts[bot->GetGUID()];
            currentOwner = rec.owner;

            // Travel distance is strictly tied to an active, matching Navigate request in Navigate mode!
            bool allowMount = true;
            if (rec.mode == MoveMode::Navigate)
            {
                auto reqIt = shard.requests.find(bot->GetGUID());
                if (reqIt != shard.requests.end() && reqIt->second.movementCommandId == rec.commandId)
                {
                    travelDist = Dist2d(bot->GetPositionX(), bot->GetPositionY(), reqIt->second.x, reqIt->second.y);
                    allowMount = reqIt->second.allowMount;
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
                canAttemptFlight,
                bot->IsMounted(),
                isDungeon,
                isRaid,
                isInWater,
                isSwimming,
                allowMount
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

            MoveOwner oldOwner = rec.owner;
            MoveMode oldMode = rec.mode;
            LocomotionState oldState = rec.state;

            if (GeneratorReconciler::ReconcilePoint(rec, genType, isMoving, now))
            {
                RecordTransitionLocked(shard, bot->GetGUID(), oldState, LocomotionState::Idle,
                    oldOwner, oldMode, "PointArrivedReconciled", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), now);
            }
            else if (GeneratorReconciler::ReconcileFollow(rec, genType, isMoving, now))
            {
                RecordTransitionLocked(shard, bot->GetGUID(), oldState, LocomotionState::Idle,
                    oldOwner, oldMode, "FollowEndedReconciled", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), now);
            }
            else if (GeneratorReconciler::ReconcileChase(rec, genType, isMoving, now))
            {
                RecordTransitionLocked(shard, bot->GetGUID(), oldState, LocomotionState::Idle,
                    oldOwner, oldMode, "ChaseEndedReconciled", bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), now);
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
        float legDist = (rItr != shard.requests.end() && (rItr->second.legX != 0.0f || rItr->second.legY != 0.0f))
            ? Dist2d(bot->GetPositionX(), bot->GetPositionY(), rItr->second.legX, rItr->second.legY)
            : 0.0f;
        float distZ = rItr != shard.requests.end() ? std::abs(bot->GetPositionZ() - rItr->second.z) : 0.0f;
        NavHoldReason holdReason = rItr != shard.requests.end() ? rItr->second.holdReason : NavHoldReason::None;
        NavFailureKind failKind = rItr != shard.requests.end() ? rItr->second.lastFailureKind : NavFailureKind::None;
        uint32 switchbacks = rItr != shard.requests.end() ? rItr->second.progress.legSwitchbacks : 0;
        uint8 noPathStrikes = rItr != shard.requests.end() ? rItr->second.consecutiveNoPath : 0;

        return Acore::StringFormat(
            "cmdId={} owner={} mode={} state={} gen={} extCtrl={} target={} ranges=[{:.1f}, {:.1f}, ang={:.2f}] "
            "mountState={} pendingSpell={} cdLeft={}ms dismountReason={} navGoal={} navStage={} btAttempts={} "
            "legDist={:.1f}yd distZ={:.1f}yd hold={} fail={} switchbacks={} noPathStrikes={}",
            cmdId, MoveOwnerName(owner), MoveModeName(mode), LocomotionStateName(state),
            uint32(bot->GetMotionMaster()->GetCurrentMovementGeneratorType()),
            IsExternallyControlled(bot),
            (lItr != shard.locomotion.end() && !lItr->second.targetGuid.IsEmpty()) ? lItr->second.targetGuid.ToString() : "none",
            lItr != shard.locomotion.end() ? lItr->second.chaseMinRange : 0.0f,
            lItr != shard.locomotion.end() ? lItr->second.chaseMaxRange : 0.0f,
            lItr != shard.locomotion.end() ? lItr->second.chaseAngle : 0.0f,
            MountStateName(mState), pendingSpell, cooldownLeft, DismountReasonName(dReason),
            navGoal, navStage, btAttempts,
            legDist, distZ, NavHoldReasonName(holdReason), NavFailureKindName(failKind), switchbacks, noPathStrikes);
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
        shard.activePlanning.erase(botGuid);
        shard.nextPlanTicket.erase(botGuid);
    }

}
