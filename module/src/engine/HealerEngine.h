/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: HealerEngine
 */

#ifndef COA_PLAYERBOTS_HEALER_ENGINE_H
#define COA_PLAYERBOTS_HEALER_ENGINE_H

#include "Define.h"

class Player;

namespace BotAI
{
    class HealerEngine
    {
    public:
        // Returns true if handled by a data-driven profile, false to fall back to legacy AI.
        static bool Execute(Player* bot, uint32 diff, uint32& nextCastAllowedMs);
    };
}

#endif // COA_PLAYERBOTS_HEALER_ENGINE_H
