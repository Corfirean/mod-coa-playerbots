/*
 * mod-coa-playerbots
 *
 * Arbitration for the single POINT_MOTION_TYPE slot every bot subsystem competes for.
 *
 * Before this existed, every subsystem issued MovePoint(0, ...) and several cleaned up with a
 * bare "if the generator is POINT_MOTION_TYPE, Clear() it" check. Because the id was always 0,
 * no subsystem could tell its own movement from anyone else's, so those cleanups cancelled
 * whichever walk happened to be in flight. That cost a real, live-confirmed bug: the grind
 * anchor's cleanup fired on the *gather* walk on the ticks between gather scans, so the bot was
 * yanked back toward its anchor mid-walk and never reached the node (see gatherWalkTargetGuid's
 * comment in BotAI.cpp). The workaround was a dedicated walk-tracking guid per subsystem, which
 * keeps the two apart in BotAI.cpp but does nothing for any new subsystem added later.
 *
 * This replaces that with an explicit claim. A subsystem asks to move, gets refused when a
 * higher-priority owner is already moving the bot, and releases only movement it actually owns.
 * The claim is stored here rather than in BotAIState because BotAIState is private to BotAI.cpp
 * and the ambient world-behavior layer lives in its own translation unit.
 *
 * Movement requests are persistent and idempotent (2026-09-24 open-world rework). Asking to walk
 * to the place the bot is already walking to is a no-op: the old MoveTo did Clear() + MovePoint()
 * on every call, and callers that asked every tick (the long-distance quest walk, the grind
 * walk-back) restarted the spline and re-ran path generation every single tick. Navigate() adds
 * the other half: it remembers the goal, splits long trips into legs, measures progress toward the
 * goal and runs a recovery ladder when progress stops, instead of walking into a wall forever.
 */

#ifndef COA_PLAYERBOTS_BOT_MOVEMENT_H
#define COA_PLAYERBOTS_BOT_MOVEMENT_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>

class Player;

// Who a bot's current point movement belongs to. Ordered low to high priority: a claim can
// always be taken over by an equal or higher owner, never by a lower one. Combat avoidance wins
// outright because it exists to move the bot out of something that is actively killing it, and
// Ambient sits at the bottom because cosmetic wandering must always yield to real work.
enum class MoveOwner : uint8
{
    None,
    Ambient,
    Grind,
    Gather,
    Fish,
    Quest,
    Loot,
    Corpse,
    AutoDungeon,
    Battleground,
    Avoidance,
};

enum class NavStatus : uint8
{
    Moving,   // on the way (or waiting to be allowed to start the next leg)
    Arrived,  // within the acceptance radius; point movement released
    Blocked,  // a higher-priority owner is moving the bot right now
    Stuck,    // the recovery ladder is exhausted; the caller must pick something else
};

// The persistent half of a Navigate() call: what the bot is walking to and how it is going.
struct MovementRequest
{
    MoveOwner owner = MoveOwner::None;
    uint64 goalId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float acceptRadius = 0.0f;
    uint32 startedAt = 0;
    uint32 lastProgressAt = 0;
    uint32 lastCallAt = 0;
    float bestDistance = 0.0f;
    uint8 retries = 0;
    uint32 legs = 0;
    uint32 lastIssueAt = 0;
    float legX = 0.0f;
    float legY = 0.0f;
    bool detour = false;
    float detourX = 0.0f;
    float detourY = 0.0f;
    float detourZ = 0.0f;
};

struct MovementStats
{
    uint64 issued = 0;          // MovePoint calls actually made
    uint64 redundantSkipped = 0; // MoveTo calls that asked for the walk already running
    uint64 stuckEvents = 0;     // progress stalls that started a recovery
    uint64 repaths = 0;
    uint64 detours = 0;
    uint64 gaveUp = 0;          // Navigate returned Stuck
};

namespace BotMovement
{
    // Claims the movement slot for `owner` and walks the bot to the point, returning false
    // without moving anything when a higher-priority owner currently holds it (or when the bot
    // is mid-cast, which the previous MoveBotToPoint also refused). Z is resolved to allowed
    // ground height for non-flying bots exactly as before. The owner is passed through as the
    // MovePoint id so a claim is also visible on the generator itself when debugging.
    //
    // Idempotent: when `owner` is already walking the bot to (within a yard of) this point, this
    // returns true and leaves the running spline alone.
    bool MoveTo(Player* bot, MoveOwner owner, float x, float y, float z);

    // Goal-directed travel with progress tracking. Call every tick while the bot should be going
    // somewhere; it only touches the MotionMaster when a new leg is actually needed (first call,
    // leg finished, goal moved, recovery). `goalId` identifies the destination: the same id with a
    // moved point (a mob that walked off) updates the destination without resetting the stuck
    // budget; a new id starts a fresh request.
    //
    // Recovery ladder when the bot stops closing on the goal: repath, detour to one side, detour
    // to the other side, then NavStatus::Stuck -- at which point the caller escalates (another
    // spawn, another area, blacklist, replan). Time spent not calling Navigate (combat, resting,
    // looting) never counts as being stuck.
    NavStatus Navigate(Player* bot, MoveOwner owner, uint64 goalId, float x, float y, float z, float acceptRadius);

    // Drops the Navigate() request (not the claim) so the next call starts fresh.
    void ResetRequest(ObjectGuid botGuid);

    MovementRequest const* GetRequest(ObjectGuid botGuid);

    // Stops point movement only when `owner` is the one that claimed it. This is the direct
    // replacement for the old bare "clear a stray POINT_MOTION_TYPE" cleanups: a subsystem
    // saying "I'm done walking" must not cancel a walk that belongs to someone else.
    void Release(Player* bot, MoveOwner owner);

    // The live owner of the bot's point movement, or None. A claim only means anything while the
    // movement it was made for is still running, so anything that ends that movement -- arrival,
    // a teleport, a combat chase taking the slot -- makes the claim stale. Stale claims are
    // dropped here rather than lingering and locking every lower-priority owner out forever.
    MoveOwner CurrentOwner(Player* bot);

    // True when `owner` may claim the slot right now. Lets a caller decide between activities
    // before doing the work to pick a destination.
    bool CanClaim(Player* bot, MoveOwner owner);

    // Drops any claim held for this guid. Called from BotAI::Forget so the claim map doesn't
    // grow across repeated spawn/despawn cycles.
    void Forget(ObjectGuid botGuid);

    char const* OwnerName(MoveOwner owner);

    // One line for `.botcmd brain`.
    std::string Describe(Player* bot);

    MovementStats const& Stats();
}

#endif // COA_PLAYERBOTS_BOT_MOVEMENT_H
