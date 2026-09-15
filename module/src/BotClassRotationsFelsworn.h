/*
 * mod-coa-playerbots
 *
 * Real Felsworn (class 14) priority rotation -- see BotClassRotationsFelsworn.cpp for the
 * mod-ascension-compat source this was verified against. Kept in its own file/dispatch
 * function, separate from BotClassRotations.cpp (Barbarian/Venomancer/Pyromancer) and
 * BotClassRotationsReaper.cpp, purely to avoid multiple people editing the same
 * rotation-dispatch file at once -- nothing architectural about the split. BotAI.cpp tries
 * this after the shared dispatchers and before the generic fallback.
 */

#ifndef COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_FELSWORN_H
#define COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_FELSWORN_H

class Player;
class Unit;

namespace BotAI
{
// Returns 0 for anything not class 14 -- internally CLASS_DEMON_HUNTER in SharedDefines.h (a
// legacy WotLK codename reused for Felsworn's "TITLE Felsworn" display name, same gotcha as
// CLASS_PROPHET=29 actually being Venomancer -- check the TITLE comment, not the enum name).
// Falls through to whatever the caller tries next -- same contract as
// SelectClassRotationSpell/SelectReaperRotationSpell.
unsigned int SelectFelswornRotationSpell(Player* bot, Unit* target);
}

#endif // COA_PLAYERBOTS_BOT_CLASS_ROTATIONS_FELSWORN_H
