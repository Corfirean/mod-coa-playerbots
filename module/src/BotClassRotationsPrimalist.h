/*
 * mod-coa-playerbots
 *
 * Real Primalist (class 31, internally CLASS_WILDWALKER -- see the header) priority rotation --
 * see BotClassRotationsPrimalist.cpp for the mod-ascension-compat source this was verified
 * against. Kept in its own file/dispatch function, separate from BotClassRotations.cpp and the
 * other per-class files, purely to avoid multiple people editing the same rotation-dispatch
 * file at once -- nothing architectural about the split. BotAI.cpp tries this after the shared
 * dispatchers and before the generic fallback.
 */

#ifndef COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_PRIMALIST_H
#define COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_PRIMALIST_H

class Player;
class Unit;

namespace BotAI
{
// Returns 0 for anything not class 31 -- internally CLASS_WILDWALKER in SharedDefines.h (a
// legacy WotLK codename reused for Primalist's "TITLE Primalist" display name). Falls through
// to whatever the caller tries next -- same contract as the other per-class dispatchers.
unsigned int SelectPrimalistRotationSpell(Player* bot, Unit* target);
}

#endif // COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_PRIMALIST_H
