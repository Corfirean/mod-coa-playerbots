/*
 * mod-coa-playerbots
 *
 * Real Reaper (class 30) priority rotation -- see BotClassRotationsReaper.cpp for the
 * mod-ascension-compat source this was verified against. Kept in its own file/dispatch
 * function, separate from BotClassRotations.cpp/BotAI::SelectClassRotationSpell, purely to
 * avoid two people editing the same rotation-dispatch file at once -- nothing architectural
 * about the split. BotAI.cpp tries this after the shared per-class dispatch and before the
 * generic fallback.
 */

#ifndef COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_REAPER_H
#define COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_REAPER_H

class Player;
class Unit;

namespace BotAI
{
// Returns 0 for anything not class 30 (CLASS_REAPER), so the caller falls through to
// whatever it tries next -- same contract as SelectClassRotationSpell.
unsigned int SelectReaperRotationSpell(Player* bot, Unit* target);
}

#endif // COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_REAPER_H
