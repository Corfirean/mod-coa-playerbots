/*
 * mod-coa-playerbots
 *
 * Class-specific combat rotations for custom Ascension classes.
 * Dispatched from BotAI::UpdateOffensive before falling back to generic heuristic spell selection.
 */

#ifndef COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_H
#define COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_H

#include "Define.h"
#include "ObjectGuid.h"

class Player;
class Unit;

namespace BotAI
{
    // Returns 0 if no rotation exists for this class/spec, or if no class ability is currently eligible.
    // Otherwise returns the specific spell ID to cast according to priority rules.
    uint32 SelectClassRotationSpell(Player* bot, Unit* target, uint8 classId, uint32 activeSpec);

    // Records a cast failure so this spell is temporarily blacklisted for this bot to avoid infinite retry loops.
    void RecordSpellCastFailure(ObjectGuid botGuid, uint32 spellId);

    // Checks if a spell is currently on failure backoff cooldown for this bot.
    bool IsSpellInFailureCooldown(ObjectGuid botGuid, uint32 spellId);

    // Cleans up any rotation state when a bot despawns.
    void ForgetRotationState(ObjectGuid botGuid);
}

#endif // COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_H
