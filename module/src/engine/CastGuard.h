/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CastGuard
 * Manages current spell cast monitoring, self-interruption, and priority overrides.
 */

#ifndef COA_PLAYERBOTS_CAST_GUARD_H
#define COA_PLAYERBOTS_CAST_GUARD_H

#include "Define.h"

class Player;

namespace BotAI
{
    struct CombatContext;
    struct BotAction;

    class CastGuard
    {
    public:
        // Returns true if the bot is currently in the middle of a non-melee spell cast or channel.
        static bool IsCurrentlyCasting(Player* bot);

        // Determines whether an ongoing cast should be cancelled to execute a higher-priority action.
        static bool ShouldInterruptCurrentCast(CombatContext const& ctx, BotAction const& pendingAction);

        // Immediately cancels current non-melee spell cast.
        static void InterruptCurrentCast(Player* bot);
    };
}

#endif // COA_PLAYERBOTS_CAST_GUARD_H
