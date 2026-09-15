/*
 * mod-coa-playerbots
 *
 * Real Bloodmage (class 20, internally CLASS_SON_OF_ARUGAL -- see the header) priority
 * rotation -- see BotClassRotationsBloodmage.cpp for the mod-ascension-compat source this was
 * verified against. Kept in its own file/dispatch function, separate from BotClassRotations.cpp
 * and the other per-class files, purely to avoid multiple people editing the same
 * rotation-dispatch file at once -- nothing architectural about the split. BotAI.cpp tries this
 * after the shared dispatchers and before the generic fallback.
 */

#ifndef COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_BLOODMAGE_H
#define COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_BLOODMAGE_H

class Player;
class Unit;

namespace BotAI
{
// Returns 0 for anything not class 20 -- internally CLASS_SON_OF_ARUGAL in SharedDefines.h (a
// legacy WotLK codename reused for Bloodmage's "TITLE Bloodmage" display name). Falls through
// to whatever the caller tries next -- same contract as the other per-class dispatchers.
unsigned int SelectBloodmageRotationSpell(Player* bot, Unit* target);
}

#endif // COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_BLOODMAGE_H
