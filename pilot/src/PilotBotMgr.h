/*
 * mod-coa-playerbots-pilot
 *
 * Pilot module for the mod-coa-playerbots project. Proves that a real Player
 * can exist via a fake, null-socket WorldSession on CoA's core. No AI, no
 * strategies, no combat logic — see C:\games\source\server\mod-coa-playerbots
 * for the project this pilot is de-risking.
 */

#ifndef PILOT_BOT_MGR_H
#define PILOT_BOT_MGR_H

#include "ObjectGuid.h"
#include <vector>

class ChatHandler;
class WorldSession;

class PilotBotMgr
{
public:
    static PilotBotMgr* instance();

    // Logs an existing character in through a fake, null-socket WorldSession
    // instead of a real client connection. Async: the result (success or
    // failure) is only known once the login query completes and is logged,
    // not returned synchronously.
    void SpawnPilotBot(ObjectGuid::LowType charLowGuid, ChatHandler* handler);

    // Called every world tick via a WorldScript hook. Logs a periodic
    // heartbeat for every active pilot session so a human watching the log
    // can confirm the bot is still alive without polling in-game.
    void Update(uint32 diff);

private:
    PilotBotMgr() = default;

    std::vector<WorldSession*> _pilotSessions;
    uint32 _heartbeatTimer = 0;
};

#define sPilotBotMgr PilotBotMgr::instance()

#endif // PILOT_BOT_MGR_H
