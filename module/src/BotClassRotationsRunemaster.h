/*
 * mod-coa-playerbots
 *
 * Real Runemaster (class 32, internally CLASS_SPIRIT_MAGE -- see the header) priority rotation
 * -- see BotClassRotationsRunemaster.cpp for the mod-ascension-compat source this was verified
 * against. Kept in its own file/dispatch function, separate from BotClassRotations.cpp and the
 * other per-class files, purely to avoid multiple people editing the same rotation-dispatch
 * file at once -- nothing architectural about the split. BotAI.cpp tries this after the shared
 * dispatchers and before the generic fallback.
 */

#ifndef COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_RUNEMASTER_H
#define COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_RUNEMASTER_H

class Player;
class Unit;

namespace BotAI
{
// Returns 0 for anything not class 32 -- internally CLASS_SPIRIT_MAGE in SharedDefines.h (a
// legacy WotLK codename reused for Runemaster's "TITLE Runemaster" display name). Falls through
// to whatever the caller tries next -- same contract as the other per-class dispatchers.
unsigned int SelectRunemasterRotationSpell(Player* bot, Unit* target);
}

#endif // COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_RUNEMASTER_H
