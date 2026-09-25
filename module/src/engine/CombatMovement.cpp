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

        bool requiresStationary = (spellInfo->CalcCastTime(bot) > 0 || spellInfo->IsChanneled());
        if (bot->CanCastSpellWhileMoving(spellInfo))
            requiresStationary = false;

        if (!requiresStationary)
            return true; // instant / mobile spell -- no movement interaction needed

        if (!bot->isMoving())
            return true; // already stationary

        // Begin stopping -- the movement flag doesn't clear until the stop is actually processed,
        // so the cast itself is deferred to whichever tick next observes !bot->isMoving().
        bot->StopMoving();
        return false;
    }
}
