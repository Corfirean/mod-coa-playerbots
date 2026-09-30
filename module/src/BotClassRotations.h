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

enum SpellCastResult : uint8;

class Player;
class Unit;

namespace BotAI
{
    // Returns 0 if no rotation exists for this class/spec, or if no class ability is currently eligible.
    // Otherwise returns the specific spell ID to cast according to priority rules.
    uint32 SelectClassRotationSpell(Player* bot, Unit* target, uint8 classId, uint32 activeSpec);

    // Returns 0 if no healer rotation exists for this class/spec, or if no class heal ability is currently eligible.
    // Otherwise returns the specific heal spell ID to cast on healTarget according to priority rules.
    uint32 SelectClassHealRotationSpell(Player* bot, Player* healTarget, uint8 classId, uint32 activeSpec);

    // Records a cast failure so this spell is temporarily blacklisted for this bot to avoid infinite retry loops.
    void RecordSpellCastFailure(ObjectGuid botGuid, uint32 spellId);

    // Same as RecordSpellCastFailure, but for a failed cast whose actual SpellCastResult is known.
    // A range/facing failure (SPELL_FAILED_TOO_CLOSE / _OUT_OF_RANGE / _UNIT_NOT_INFRONT) gets a
    // longer backoff than a generic failure and an active reaction (back away / re-face) instead of
    // just being blacklisted and retried from the same spot -- confirmed live that the 2s generic
    // cooldown alone lets a bot get stuck retrying an unreachable min-range ability forever. Prefer
    // this over the plain overload at any call site that already has the target and result handy.
    void HandleSpellCastFailure(Player* bot, Unit* target, uint32 spellId, SpellCastResult result);

    // Checks if a spell is currently on failure backoff cooldown for this bot.
    bool IsSpellInFailureCooldown(ObjectGuid botGuid, uint32 spellId);

    // Cleans up any rotation state when a bot despawns.
    void ForgetRotationState(ObjectGuid botGuid);
}

#endif // COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_H
