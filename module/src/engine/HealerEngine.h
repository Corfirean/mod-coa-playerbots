/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: HealerEngine
 */

#ifndef COA_PLAYERBOTS_HEALER_ENGINE_H
#define COA_PLAYERBOTS_HEALER_ENGINE_H

#include "Define.h"
#include "engine/CombatResult.h"

class Player;

namespace BotAI
{
    class HealerEngine
    {
    public:
        // See CombatResult's own comment -- NoAction (no profile, or a profile that found
        // nothing castable) is the caller's signal to fall through to the legacy AI instead of
        // treating the tick as handled.
        static CombatResult Execute(Player* bot, uint32 diff, uint32& nextCastAllowedMs);
    };
}

#endif // COA_PLAYERBOTS_HEALER_ENGINE_H
