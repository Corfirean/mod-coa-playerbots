/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatMovement implementation
 */

#include "engine/CombatMovement.h"
#include "BotMovement.h"
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

        // Stop movement immediately for cast-time / channeled spells.
        // StopMoving() synchronously clears UNIT_STATE_MOVING and halts spline movement.
        // Returning true allows the spell cast to begin immediately in this AI tick.
        // Once the cast begins, TargetedMovementGenerator::DoUpdate detects owner->IsNonMeleeSpellCast()
        // and keeps movement safely paused until the cast finishes, preventing chase oscillations.
        bot->StopMoving();
        return true;
    }
}
