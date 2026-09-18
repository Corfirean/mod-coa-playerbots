/*
 * mod-coa-playerbots
 *
 * Bulk random-bot creation: `.botcmd spawnrandom [count]` (BotCommand.cpp) makes brand new
 * characters on the fly, in whatever quantity is asked for, instead of only being able to
 * spawn existing hand-made test characters (BotMgr::SpawnBot). See BotSpawnRandom.cpp's
 * header comment for why this clones an existing character row rather than simulating the
 * real CMSG_CHAR_CREATE packet flow.
 */

#ifndef COA_PLAYERBOTS_BOT_SPAWN_RANDOM_H
#define COA_PLAYERBOTS_BOT_SPAWN_RANDOM_H

#include "Define.h"
#include "ObjectGuid.h"

class ChatHandler;
class Player;

namespace BotSpawn
{
// requestedCount == 0 means "use CoaBots.RandomSpawn.DefaultCount"; the result is always
// clamped to CoaBots.RandomSpawn.MaxCount regardless. Queues the batch for gradual creation
// (a few bots every ProcessPendingRandomBotSpawns tick, not all of them in one go) and returns
// immediately -- confirmed live that spawning 1000 in one synchronous burst crashed the
// worldserver outright (no crash dump, consistent with the OS killing it under a memory/
// resource spike from creating that many characters and WorldSessions back to back with nothing
// to spread the load). See the .cpp for the throttle constants.
void SpawnRandomBots(uint32 requestedCount, ChatHandler* handler);

// Drains the queue SpawnRandomBots fills, a small batch at a time, throttled by real elapsed
// time (not just tick count, so it stays paced the same regardless of server tick rate). Called
// every tick from BotMgr::Update; a no-op whenever nothing is queued.
void ProcessPendingRandomBotSpawns(uint32 diff);

// Creates one new bot character of a specific race (rather than SpawnRandomBots' fully random
// race) -- for a caller that needs faction control, e.g. BotBattlegroundFill topping off a
// specific side. Random class/name/gender, same cloning mechanism as SpawnRandomBots. Does NOT
// log the bot in -- returns 0 on failure (no template roster, no free/creatable account),
// otherwise the new character's guid for the caller to pass to BotMgr::SpawnBot when ready.
ObjectGuid::LowType CreateOneRandomBot(uint8 race, ChatHandler* handler = nullptr);

// `.botcmd spawnleveled [count]` -- same gradual-queue mechanism as SpawnRandomBots, but each
// bot gets a random level (weighted mostly 1-10, tapering off toward 80 -- see
// RollWeightedLevel in the .cpp) instead of always 80, plus a level-bracketed gear pass,
// every profession this realm allows, starting bags, and basic food/water. Clamped to twice
// CoaBots.RandomSpawn.MaxCount (this path is meant for bigger population batches, 1000-1500).
void SpawnLeveledBots(uint32 requestedCount, ChatHandler* handler);

// Drains the queue SpawnLeveledBots fills. Called every tick from BotMgr::Update alongside
// ProcessPendingRandomBotSpawns; a no-op whenever nothing is queued.
void ProcessPendingLeveledBotSpawns(uint32 diff);

// `.botcmd professiontrainer [charLowGuid]` -- grants every profession this realm allows to one
// online bot (or every online bot, if omitted) at its current level's skill cap. Standalone from
// ApplyFreshBotSetup so it can backfill a bot that never went through that path (this project's
// original hand-made test characters predate it and have zero profession skills).
void GrantAllProfessions(Player* bot, uint8 level);
}

#endif // COA_PLAYERBOTS_BOT_SPAWN_RANDOM_H
