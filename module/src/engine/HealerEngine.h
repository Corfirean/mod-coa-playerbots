/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: HealerEngine
 */

#ifndef COA_PLAYERBOTS_HEALER_ENGINE_H
#define COA_PLAYERBOTS_HEALER_ENGINE_H

#include "Define.h"
#include "ObjectGuid.h"
#include "engine/CombatResult.h"

class Player;

namespace BotAI
{
    struct CombatContext;

    class HealerEngine
    {
    public:
        // See CombatResult's own comment -- NoAction (no profile, or a profile that found
        // nothing castable) is the caller's signal to fall through to the legacy AI instead of
        // treating the tick as handled; unlike Busy, it must never leave nextCastAllowedMs set
        // (item 1, Phase 2 fixup) -- this engine paces its own re-attempts internally instead
        // (see ForgetBot), invisibly to the caller. `ctx` is built once by the caller and shared
        // with the Utility Layer (item 14).
        static CombatResult Execute(Player* bot, CombatContext const& ctx, uint32 diff, uint32& nextCastAllowedMs);

        // Drops this bot's internal no-action recompute gate -- called on despawn (see
        // BotAI::Forget) so the map doesn't grow across repeated spawn/despawn cycles.
        static void ForgetBot(ObjectGuid botGuid);
    };
}

#endif // COA_PLAYERBOTS_HEALER_ENGINE_H
