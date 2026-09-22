/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatMovement implementation
 */

#include "engine/CombatMovement.h"
#include "Player.h"
#include "SpellInfo.h"

namespace BotAI
{
    bool CombatMovement::ReadyToCast(Player* bot, SpellInfo const* spellInfo)
    {
        if (!bot || !spellInfo)
            return false;

        if (spellInfo->CalcCastTime(bot) <= 0)
            return true; // instant -- no movement interaction needed

        if (!bot->isMoving())
            return true; // already stationary

        // Begin stopping -- the movement flag doesn't clear until the stop is actually processed,
        // so the cast itself is deferred to whichever tick next observes !bot->isMoving().
        bot->StopMoving();
        return false;
    }
}
