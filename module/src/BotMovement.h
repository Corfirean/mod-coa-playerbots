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
 */

#ifndef COA_PLAYERBOTS_BOT_MOVEMENT_H
#define COA_PLAYERBOTS_BOT_MOVEMENT_H

#include "Define.h"
#include "ObjectGuid.h"

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
    QuestTracker,
    Loot,
    Corpse,
    AutoDungeon,
    Battleground,
    Avoidance,
};

namespace BotMovement
{
    // Claims the movement slot for `owner` and walks the bot to the point, returning false
    // without moving anything when a higher-priority owner currently holds it (or when the bot
    // is mid-cast, which the previous MoveBotToPoint also refused). Z is resolved to allowed
    // ground height for non-flying bots exactly as before. The owner is passed through as the
    // MovePoint id so a claim is also visible on the generator itself when debugging.
    bool MoveTo(Player* bot, MoveOwner owner, float x, float y, float z);

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
}

#endif // COA_PLAYERBOTS_BOT_MOVEMENT_H
