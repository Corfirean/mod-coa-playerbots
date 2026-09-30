#include "BotMovement.h"
// GridTerrainData.h owns INVALID_HEIGHT but is not self-contained: it uses Optional without
// including it, so it only compiles when something else has already pulled Optional.h in.
#include "Optional.h"
#include "GridTerrainData.h"
#include "GameTime.h"
#include "Log.h"
#include "MotionMaster.h"
#include "PathGenerator.h"
#include "Player.h"
#include "StringFormat.h"
#include <cmath>
#include <unordered_map>

namespace
{
    std::unordered_map<ObjectGuid, MoveOwner> _claims;

    // The point each owner last asked MotionMaster for, so an identical request can be skipped.
    struct IssuedPoint
    {
        MoveOwner owner = MoveOwner::None;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };
    std::unordered_map<ObjectGuid, IssuedPoint> _issued;
    std::unordered_map<ObjectGuid, MovementRequest> _requests;
    MovementStats _stats;

    // Confirmed live (2026-09-27): without this, a bot whose caller keeps re-requesting the same
    // genuinely unreachable point every tick (quest/grind travel has no repath backoff of its
    // own -- only Navigate()'s leg ladder does) turned the NOPATH pre-check below into a tight
    // busy loop: a full PathGenerator::CalculatePath (a real navmesh query, not cheap) plus a log
    // line, hundreds of times a second, indefinitely, for as long as the destination stayed
    // unreachable. Caching "this owner already confirmed NOPATH to about here, recently" and
    // skipping straight to the same false return is the fix -- same outcome, none of the repeated
    // work. A real repath attempt (new leg, new target, or just time passing) still gets a fresh
    // check once the cooldown lapses, so a spawn that becomes reachable later isn't stuck forever.
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

    // A request asked for the same point when it is within this of what is already running.
    constexpr float SAME_POINT_YARDS = 1.0f;

    // Long trips are walked as a chain of legs: path generation over a very long straight line
    // truncates, and a leg is the unit the stuck detector judges.
    constexpr float LEG_YARDS = 120.0f;

    // What counts as progress and when the recovery ladder climbs or resets (BotNavProgress.h).
    NavProgressRules const PROGRESS_RULES;

    // A goal that moved farther than this (a mob wandering) gets a fresh leg immediately.
    constexpr float GOAL_MOVED_YARDS = 4.0f;

    // Longer than this between two Navigate calls means the bot was doing something else
    // (fighting, resting, looting); the stuck clock restarts instead of counting that time.
    constexpr uint32 RESUME_GAP_MS = 2500;

    // A leg that ended well short of its end point (path generation gave up) is not re-issued more
    // often than this; the stuck detector decides what to do about it instead of the bot spamming
    // a fresh MovePoint every tick.
    constexpr uint32 EARLY_END_REISSUE_MS = 1500;

    // Detours step sideways this far, then forward toward the goal.
    constexpr float DETOUR_SIDE_YARDS = 16.0f;
    constexpr float DETOUR_FORWARD_YARDS = 12.0f;

    // Ordering is the enum's own declaration order, so adding an owner in the right place is all
    // that's needed to give it a priority -- there's no second table to keep in sync.
    uint8 Priority(MoveOwner owner)
    {
        return uint8(owner);
    }

    bool HasPointMovement(Player const* bot)
    {
        return bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE;
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

    // Issues the next leg of `req`. `force` skips the idempotency check (a repath).
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
                // The ground height at the leg end is resolved from the bot's own height, not the
                // goal's: over 120 yards of hills the goal's z is meaningless for the leg.
                tz = bot->GetPositionZ();
            }
        }

        if (force && HasPointMovement(bot) && BotMovement::CurrentOwner(bot) == req.owner)
        {
            bot->GetMotionMaster()->Clear();
            _issued.erase(bot->GetGUID());
        }

        // forceDestination=false: a leg is an intermediate hop, not the final goal -- if the real
        // navmesh path doesn't quite reach (tx,ty,tz), stop at wherever it actually gets to instead
        // of force-shortcutting a straight line through a wall/floor. The repath/detour ladder in
        // Navigate() above continues from the bot's real position on the next tick either way, so
        // this loses nothing when the leg genuinely completes and only helps when it doesn't.
        if (!BotMovement::MoveTo(bot, req.owner, tx, ty, tz, /*forceDestination=*/false))
            return false;
        ++req.legs;
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

        auto itr = _claims.find(bot->GetGUID());
        if (itr == _claims.end())
            return MoveOwner::None;

        if (!HasPointMovement(bot))
        {
            _claims.erase(itr);
            return MoveOwner::None;
        }

        return itr->second;
    }

    bool CanClaim(Player* bot, MoveOwner owner)
    {
        MoveOwner current = CurrentOwner(bot);
        return current == MoveOwner::None || current == owner || Priority(current) <= Priority(owner);
    }

    bool MoveTo(Player* bot, MoveOwner owner, float x, float y, float z, bool forceDestination)
    {
        if (!bot || bot->IsNonMeleeSpellCast(false))
            return false;

        if (!CanClaim(bot, owner))
            return false;

        float groundZ = ResolveGroundZ(bot, x, y, z);

        // Already walking there for the same owner: leave the spline alone. Re-issuing restarted
        // path generation and visibly stuttered the bot every tick.
        if (CurrentOwner(bot) == owner)
        {
            auto issued = _issued.find(bot->GetGUID());
            if (issued != _issued.end() && issued->second.owner == owner &&
                std::fabs(issued->second.x - x) <= SAME_POINT_YARDS &&
                std::fabs(issued->second.y - y) <= SAME_POINT_YARDS &&
                std::fabs(issued->second.z - groundZ) <= SAME_POINT_YARDS * 3.0f)
            {
                ++_stats.redundantSkipped;
                return true;
            }
        }

        // Confirmed live and fatal (2026-09-27): when the real navmesh has NO path at all between
        // the bot and (x,y,z) -- not merely an imperfect/partial one -- PointMovementGenerator's own
        // fallback builds a straight-line spline through walls/floors regardless of forceDestination
        // (documented as a known residual limitation when Phase 1 shipped; live testing then hit it
        // hard: a tank clipped through a wall to pull an entire unrelated room, wiping the group).
        // Pre-checking the path ourselves and refusing to move at all on a genuine PATHFIND_NOPATH
        // closes this for every caller of MoveTo, not just Navigate's leg-issuing one -- a bot must
        // never walk through geometry, full stop, regardless of what it was trying to reach.
        {
            uint32 now = NowMs();
            auto cached = _nopathCache.find(bot->GetGUID());
            if (cached != _nopathCache.end() && cached->second.owner == owner && now < cached->second.expiresAtMs &&
                std::fabs(cached->second.x - x) <= SAME_POINT_YARDS &&
                std::fabs(cached->second.y - y) <= SAME_POINT_YARDS &&
                std::fabs(cached->second.z - groundZ) <= SAME_POINT_YARDS * 3.0f)
            {
                return false;
            }

            PathGenerator path(bot);
            path.CalculatePath(x, y, groundZ, false);
            if (path.GetPathType() & PATHFIND_NOPATH)
            {
                LOG_DEBUG("module.coa-playerbots.navigation",
                    "MoveTo: bot '{}' has NO real path to ({:.1f}, {:.1f}, {:.1f}) -- refusing to move through geometry.",
                    bot->GetName(), x, y, groundZ);
                _nopathCache[bot->GetGUID()] = NopathCacheEntry{ owner, x, y, groundZ, now + NOPATH_RECHECK_COOLDOWN_MS };
                return false;
            }
        }

        // Redirecting an existing point movement needs the old generator gone first, same as
        // before. The CanClaim check above is what makes this safe now: at this point the slot is
        // either unowned, stale, ours, or held by someone we outrank.
        if (HasPointMovement(bot))
            bot->GetMotionMaster()->Clear();

        _claims[bot->GetGUID()] = owner;
        _issued[bot->GetGUID()] = IssuedPoint{ owner, x, y, groundZ };
        bot->GetMotionMaster()->MovePoint(uint32(owner), x, y, groundZ, FORCED_MOVEMENT_NONE, 0.f, 0.0f,
            /*generatePath=*/true, forceDestination);
        ++_stats.issued;
        return true;
    }

    NavStatus Navigate(Player* bot, MoveOwner owner, uint64 goalId, float x, float y, float z, float acceptRadius)
    {
        uint32 now = NowMs();
        MovementRequest& req = _requests[bot->GetGUID()];

        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();
        float dist = Dist2d(bx, by, x, y);
        float dz = std::fabs(bot->GetPositionZ() - z);

        bool fresh = req.owner != owner || req.goalId != goalId;
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
            // Same goal, new position (a mob walking): measure progress against where it is now.
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

        if (dist <= acceptRadius && dz <= std::max(6.0f, acceptRadius))
        {
            Release(bot, owner);
            _requests.erase(bot->GetGUID());
            return NavStatus::Arrived;
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
                // Recovery 1: repath from where the bot actually is.
                ++_stats.stuckEvents;
                ++_stats.repaths;
                req.detour = false;
                forceReissue = true;
                LOG_DEBUG("module.coa-playerbots.navigation", "Bot '{}' made no progress toward goal {} ({:.0f} yd, {:.0f} yd height "
                    "left) -- repathing.", bot->GetName(), goalId, dist, dz);
                break;
            case NavRecovery::DetourLeft:
            case NavRecovery::DetourRight:
            {
                // Recovery 2 and 3: step around whatever is in the way, one side then the other.
                ++_stats.stuckEvents;
                ++_stats.detours;
                float len = std::max(0.001f, dist);
                float dirX = (x - bx) / len;
                float dirY = (y - by) / len;
                float side = req.progress.stage == 2 ? 1.0f : -1.0f;
                req.detour = true;
                req.detourX = bx + dirX * DETOUR_FORWARD_YARDS - dirY * side * DETOUR_SIDE_YARDS;
                req.detourY = by + dirY * DETOUR_FORWARD_YARDS + dirX * side * DETOUR_SIDE_YARDS;
                req.detourZ = bot->GetPositionZ();
                forceReissue = true;
                LOG_DEBUG("module.coa-playerbots.navigation", "Bot '{}' still stuck toward goal {} -- detour {} (stage {}).",
                    bot->GetName(), goalId, req.progress.stage == 2 ? "left" : "right", uint32(req.progress.stage));
                break;
            }
            case NavRecovery::GiveUp:
            default:
                ++_stats.stuckEvents;
                ++_stats.gaveUp;
                LOG_DEBUG("module.coa-playerbots.navigation", "Bot '{}' gave up on goal {} after 3 recoveries ({:.0f} yd, {:.0f} yd "
                    "height left).", bot->GetName(), goalId, dist, dz);
                Release(bot, owner);
                return NavStatus::Stuck;
        }

        if (req.detour && Dist2d(bx, by, req.detourX, req.detourY) <= 3.0f)
        {
            req.detour = false;
            forceReissue = true;
        }

        bool running = CurrentOwner(bot) == owner && HasPointMovement(bot);
        if (running && !forceReissue)
            return NavStatus::Moving;

        // The previous leg ended far from where it was meant to (no path, blocked): give the stuck
        // detector a moment instead of re-issuing the same doomed leg every tick.
        bool endedEarly = req.legs && Dist2d(bx, by, req.legX, req.legY) > 3.0f;
        if (!forceReissue && endedEarly && now - req.lastIssueAt < EARLY_END_REISSUE_MS)
            return NavStatus::Moving;

        IssueLeg(bot, req, forceReissue);

        return NavStatus::Moving;
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

    void Release(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return;

        auto req = _requests.find(bot->GetGUID());
        if (req != _requests.end() && req->second.owner == owner)
            _requests.erase(req);

        auto itr = _claims.find(bot->GetGUID());
        if (itr == _claims.end())
            return;

        if (itr->second != owner)
            return;

        _claims.erase(itr);
        _issued.erase(bot->GetGUID());

        if (HasPointMovement(bot))
            bot->GetMotionMaster()->Clear();
    }

    void Forget(ObjectGuid botGuid)
    {
        _claims.erase(botGuid);
        _issued.erase(botGuid);
        _requests.erase(botGuid);
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
            case MoveOwner::Avoidance:    return "Avoidance";
            default:                      return "None";
        }
    }

    std::string Describe(Player* bot)
    {
        MoveOwner owner = CurrentOwner(bot);
        MovementRequest const* req = GetRequest(bot->GetGUID());
        if (!req)
            return Acore::StringFormat("movement: owner {} (no goal-directed request)", OwnerName(owner));

        uint32 now = NowMs();
        NavProgress const& p = req->progress;
        return Acore::StringFormat("movement: owner {}, request {} goal {} -> ({:.0f}, {:.0f}, {:.0f}) {:.0f} yd / {:.0f} yd height "
            "left, best {:.0f} / {:.0f}, last progress {:.1f}s ago, legs {}, recovery: {}{}",
            OwnerName(owner), OwnerName(req->owner), req->goalId, req->x, req->y, req->z,
            Dist2d(bot->GetPositionX(), bot->GetPositionY(), req->x, req->y), std::fabs(bot->GetPositionZ() - req->z),
            p.best2d, p.bestDz, float(now - p.lastProgressAt) / 1000.0f, req->legs, NavRecoveryStageName(p.stage),
            req->detour ? ", detouring" : "");
    }

    MovementStats const& Stats()
    {
        return _stats;
    }
}
