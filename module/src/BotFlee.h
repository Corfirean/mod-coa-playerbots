/*
 * mod-coa-playerbots
 *
 * Open-world "this fight is unwinnable, get out" response. Two independent triggers:
 *  - Lethal: the bot's own incoming-damage trend says it dies in a few seconds and it's already
 *    hurt badly -- covers "one mob way above my level" and "too many mobs piling on" the same
 *    way, since both just show up as a high incoming-DPS number relative to remaining HP.
 *  - Stalemate: the bot isn't in danger, but the target it's fighting hasn't taken a new low in
 *    HP for a long time -- a fight the bot can never actually win (e.g. a single mob dozens of
 *    levels above it), which the Lethal trigger alone would never catch since nobody's dying.
 *
 * Deliberately NOT based on a mob-count threshold or a level-gap number: either would misfire
 * for an AoE-capable spec that can legitimately clear a pack a single-target spec would panic
 * over, or would need per-class tuning to avoid it. Both triggers key off measured combat
 * outcome (damage actually landing, damage actually incoming) instead of guessing from static
 * class/level data, so the same two checks work unmodified for every class/spec in the roster.
 */

#ifndef COA_PLAYERBOTS_BOT_FLEE_H
#define COA_PLAYERBOTS_BOT_FLEE_H

#include "Define.h"
#include "ObjectGuid.h"

class Player;
class Unit;

namespace BotAI { struct CombatContext; }

enum class FleeReason : uint8
{
    None,
    Lethal,     // about to die at the current damage rate, already hurt badly
    Stalemate,  // safe, but making no real progress against this target for a long time
};

// Embedded as a field in BotAIState (BotAI.cpp) -- rides along with the rest of the bot's
// per-tick state and is cleared for free by the existing states.erase(botGuid) in
// BotAI::Forget, so this needs no separate Forget() hook of its own.
struct BotFleeState
{
    bool fleeing = false;
    FleeReason reason = FleeReason::None;
    uint32 fleeCommitUntilMs = 0;
    uint32 nextDestinationPickMs = 0;
    float fleeX = 0.0f, fleeY = 0.0f, fleeZ = 0.0f;

    // Lethal-trigger debounce: how long the lethal condition has held continuously, so one
    // transient bad-data tick can't commit the bot to a multi-second retreat by itself.
    uint32 lethalFirstSeenMs = 0;

    // Stalemate tracking: lowest HP% seen on the current target and when that low was reached.
    // Resets whenever the target changes or a genuinely lower HP% is reached.
    ObjectGuid stalemateTargetGuid;
    float stalemateLowestHpPct = 100.0f;
    uint32 stalemateSinceMs = 0;
};

namespace BotFlee
{
    // Self-danger check: works for any role and needs no enemy target, since it only looks at
    // the bot's own HP and incoming-damage trend. Deliberately stricter than
    // CombatContext::worthDefensiveCooldown (25% HP / 6s) -- this is the last-resort fallback for
    // when a defensive cooldown doesn't exist for this class/spec or already wasn't enough, not
    // the first line of response.
    bool IsLethal(Player* bot, BotAI::CombatContext const& ctx, BotFleeState& state, uint32 nowMs);

    // Progress check against a specific enemy `target` -- only meaningful for a role actually
    // fighting one (Dps/Tank/Support via UpdateOffensive's single victim), not a pure Healer.
    bool IsStalemate(Unit* target, BotAI::CombatContext const& ctx, BotFleeState& state, uint32 nowMs);

    // Convenience wrapper for UpdateOffensive: runs both checks against a live target and
    // returns whichever fired first (Lethal takes priority as the more urgent condition).
    FleeReason Evaluate(Player* bot, Unit* target, BotAI::CombatContext const& ctx, BotFleeState& state, uint32 nowMs);

    // Starts (if not already fleeing) or continues driving the retreat: picks a point away from
    // the densest cluster of nearby hostiles and claims MoveOwner::Avoidance to walk there,
    // re-picking periodically in case the chosen direction turns out to lead toward more
    // trouble. Returns true while the bot should keep skipping normal combat processing; false
    // once the flee has ended (reached its minimum commit duration and is no longer in combat)
    // and normal behavior should resume.
    bool Drive(Player* bot, BotFleeState& state, FleeReason reason, uint32 nowMs);
}

#endif // COA_PLAYERBOTS_BOT_FLEE_H
