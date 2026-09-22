/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: TargetEvaluator (item 1/#8, Phase 2)
 *
 * A group is too dependent on current target / leader target / whichever enemy happened to be
 * found first -- this scores nearby hostiles by how much they actually matter right now (leader/
 * marked target, tank's target, attacking the healer, mid-cast, low HP) and refines the bot's
 * already-acquired baseline target (see BotAI.cpp's UpdateOffensive) toward the best one, with a
 * stickiness margin so it doesn't ping-pong between similarly-scored enemies every tick.
 */

#ifndef COA_PLAYERBOTS_TARGET_EVALUATOR_H
#define COA_PLAYERBOTS_TARGET_EVALUATOR_H

#include "Define.h"
#include "ObjectGuid.h"

class Player;
class Unit;

namespace BotAI
{
    class TargetEvaluator
    {
    public:
        // Higher is better. A large negative score means "don't pick this" (unreachable, out of
        // LOS, or would break a groupmate's crowd control).
        static float ScoreTarget(Player* bot, Unit* candidate);

        // Re-evaluates currentTarget against nearby hostiles within scanRange and returns the
        // better choice, applying a stickiness margin (a candidate must clearly outscore the
        // current target to replace it, not just barely) AND a minimum target-lock duration
        // (see MIN_TARGET_LOCK_MS in the .cpp) -- confirmed live during Phase 2 testing that the
        // score margin alone wasn't enough: two similarly-dangerous enemies trading which one is
        // mid-cast this exact tick could still flip the winner every tick, oscillating a bot
        // between them indefinitely (score margin resets each recompute, it has no memory).
        // Returns currentTarget unchanged when it's still the best (or only) valid/locked option,
        // and nullptr only when neither currentTarget nor any scanned candidate qualifies at all
        // -- callers should treat that as "nothing to suggest," not "clear the existing target."
        static Unit* RefineTarget(Player* bot, Unit* currentTarget, float scanRange = 30.0f);

        // Drops this bot's target-lock timer -- called on despawn (see BotAI::Forget) so the map
        // doesn't grow across repeated spawn/despawn cycles.
        static void ForgetBot(ObjectGuid botGuid);
    };
}

#endif // COA_PLAYERBOTS_TARGET_EVALUATOR_H
