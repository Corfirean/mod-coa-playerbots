/*
 * mod-coa-playerbots
 *
 * Real Chronomancer (class 22) priority rotation -- see BotClassRotationsChronomancer.cpp for
 * the mod-ascension-compat source this was verified against. Kept in its own file/dispatch
 * function, separate from BotClassRotations.cpp and the other per-class files, purely to avoid
 * multiple people editing the same rotation-dispatch file at once -- nothing architectural
 * about the split. BotAI.cpp tries this after the shared dispatchers and before the generic
 * fallback.
 */

#ifndef COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_CHRONOMANCER_H
#define COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_CHRONOMANCER_H

class Player;
class Unit;

namespace BotAI
{
// Returns 0 for anything not class 22, so the caller falls through to whatever it tries next --
// same contract as the other per-class dispatchers.
unsigned int SelectChronomancerRotationSpell(Player* bot, Unit* target);
}

#endif // COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_CHRONOMANCER_H
