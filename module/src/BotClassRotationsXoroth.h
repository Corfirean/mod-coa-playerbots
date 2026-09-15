/*
 * mod-coa-playerbots
 *
 * Real Knight of Xoroth (class 17, internally CLASS_FLESHWARDEN -- see the header) priority
 * rotation -- see BotClassRotationsXoroth.cpp for the mod-ascension-compat source this was
 * verified against. Kept in its own file/dispatch function, separate from
 * BotClassRotations.cpp and the other per-class files, purely to avoid multiple people editing
 * the same rotation-dispatch file at once -- nothing architectural about the split. BotAI.cpp
 * tries this after the shared dispatchers and before the generic fallback.
 */

#ifndef COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_XOROTH_H
#define COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_XOROTH_H

class Player;
class Unit;

namespace BotAI
{
// Returns 0 for anything not class 17 -- internally CLASS_FLESHWARDEN in SharedDefines.h (a
// legacy WotLK codename reused for Knight of Xoroth's "TITLE Knight of Xoroth" display name).
// Falls through to whatever the caller tries next -- same contract as the other per-class
// dispatchers.
unsigned int SelectXorothRotationSpell(Player* bot, Unit* target);
}

#endif // COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_XOROTH_H
