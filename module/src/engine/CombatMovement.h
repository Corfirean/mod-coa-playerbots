/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatMovement
 *
 * The narrow gap this closes (item 5/#20 of the combat-engine rework): a ranged/caster bot can
 * be actively repositioned by a persistent ChaseMovementGenerator (see BotAI.cpp's UpdateOffensive/
 * UpdateHealer) at the exact moment the AI wants to start a cast-time spell, guaranteeing
 * SPELL_FAILED_MOVING. The approach/hold/kite banding itself (MoveChase with a ChaseRange) is
 * already solid and already has hysteresis built in (the engine's own ChaseMovementGenerator is a
 * no-op while inside its band, so this deliberately doesn't re-implement or replace it) -- what's
 * missing is just the "stop before a cast-time spell, resume after" gate. Reuses the exact same
 * proven shape this codebase already uses for the same problem in gathering (see BotAI.cpp's
 * CastGatherAt: stop this tick, cast next tick once the movement flag has actually cleared).
 */

#ifndef COA_PLAYERBOTS_COMBAT_MOVEMENT_H
#define COA_PLAYERBOTS_COMBAT_MOVEMENT_H

#include "Define.h"

class Player;
class SpellInfo;

namespace BotAI
{
    class CombatMovement
    {
    public:
        // True when it's safe to start casting spellInfo right now. An instant spell (cast time
        // 0) is always ready -- the engine already allows casting those while moving, and this
        // must not touch auto-attack/auto-repeat either way. A cast-time spell needs the bot
        // actually stationary first: if it's still moving, this calls StopMoving() and returns
        // false for this tick (the caller should hold the cast and retry shortly), and returns
        // true once the movement flag has actually cleared. Positioning resumes on its own the
        // next time the caller's own MoveChase gate re-checks GetCurrentMovementGeneratorType()
        // (StopMoving clears the chase generator, so that check naturally re-arms it).
        static bool ReadyToCast(Player* bot, SpellInfo const* spellInfo);
    };
}

#endif // COA_PLAYERBOTS_COMBAT_MOVEMENT_H
