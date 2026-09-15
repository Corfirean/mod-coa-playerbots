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

namespace BotSpawn
{
// requestedCount == 0 means "use CoaBots.RandomSpawn.DefaultCount"; the result is always
// clamped to CoaBots.RandomSpawn.MaxCount regardless. Synchronous (runs on the calling/world
// thread) and intended for an admin command on a low-population dev/test realm -- see the
// .cpp for why a large batch is not something to run casually with real players online.
void SpawnRandomBots(uint32 requestedCount, ChatHandler* handler);

// Creates one new bot character of a specific race (rather than SpawnRandomBots' fully random
// race) -- for a caller that needs faction control, e.g. BotBattlegroundFill topping off a
// specific side. Random class/name/gender, same cloning mechanism as SpawnRandomBots. Does NOT
// log the bot in -- returns 0 on failure (no template roster, no free/creatable account),
// otherwise the new character's guid for the caller to pass to BotMgr::SpawnBot when ready.
ObjectGuid::LowType CreateOneRandomBot(uint8 race, ChatHandler* handler = nullptr);
}

#endif // COA_PLAYERBOTS_BOT_SPAWN_RANDOM_H
