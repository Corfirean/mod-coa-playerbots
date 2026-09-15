/*
 * mod-coa-playerbots
 *
 * Real Necromancer (class 23) priority rotation -- see BotClassRotationsNecromancer.cpp for
 * the mod-ascension-compat source this was verified against. Kept in its own file/dispatch
 * function, separate from BotClassRotations.cpp and the other per-class files, purely to avoid
 * multiple people editing the same rotation-dispatch file at once -- nothing architectural
 * about the split. BotAI.cpp tries this after the shared dispatchers and before the generic
 * fallback.
 */

#ifndef COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_NECROMANCER_H
#define COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_NECROMANCER_H

class Player;
class Unit;

namespace BotAI
{
// Returns 0 for anything not class 23, so the caller falls through to whatever it tries next --
// same contract as the other per-class dispatchers.
unsigned int SelectNecromancerRotationSpell(Player* bot, Unit* target);
}

#endif // COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_NECROMANCER_H
