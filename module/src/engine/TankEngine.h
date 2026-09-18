/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: TankEngine
 */

#ifndef COA_PLAYERBOTS_TANK_ENGINE_H
#define COA_PLAYERBOTS_TANK_ENGINE_H

#include "Define.h"

class Player;
class Unit;

namespace BotAI
{
    class TankEngine
    {
    public:
        // Returns true if handled by a data-driven profile, false to fall back to legacy AI.
        static bool Execute(Player* bot, Unit* target, uint32 diff, uint32& nextCastAllowedMs);
    };
}

#endif // COA_PLAYERBOTS_TANK_ENGINE_H
