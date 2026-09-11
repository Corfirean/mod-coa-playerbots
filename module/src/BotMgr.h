/*
 * mod-coa-playerbots
 *
 * Bot companions for CoA's custom Ascension classes. A bot is a real Player
 * driven by a WorldSession with a nullptr socket instead of a real client
 * connection — see C:\games\source\server\mod-coa-playerbots\docs for why.
 *
 * This module grew out of pilot/ (proved the login mechanism works on CoA's
 * live core). BotMgr carries that proven logic forward; group/loot/guild
 * support and bot AI are added incrementally on top of it, milestone by
 * milestone — see AGENTS.md for what's done and what's next.
 */

#ifndef COA_PLAYERBOTS_BOT_MGR_H
#define COA_PLAYERBOTS_BOT_MGR_H

#include "ObjectGuid.h"
#include <vector>

class ChatHandler;
class WorldSession;

class BotMgr
{
public:
    static BotMgr* instance();

    // Logs an existing character in through a fake, null-socket WorldSession
    // instead of a real client connection. Async: the result (success or
    // failure) is only known once the login query completes and is logged,
    // not returned synchronously.
    void SpawnBot(ObjectGuid::LowType charLowGuid, ChatHandler* handler);

    // Accepts a pending group invite on a bot's behalf by calling the real
    // WorldSession::HandleGroupAcceptOpcode handler directly (a minimal
    // padding WorldPacket stands in for the network payload it reads and
    // discards) — reuses all of the real validation logic instead of
    // duplicating it. The GM still has to issue the invite themselves via a
    // real client; there is no GM-console equivalent of /invite for party
    // invites (unlike guild invites).
    void AcceptInvite(ObjectGuid::LowType charLowGuid, ChatHandler* handler);

    // Called every world tick via a WorldScript hook. Logs a periodic
    // heartbeat for every active bot session so a human watching the log
    // can confirm bots are still alive without polling in-game.
    void Update(uint32 diff);

private:
    BotMgr() = default;

    WorldSession* FindBotSession(ObjectGuid::LowType charLowGuid) const;

    std::vector<WorldSession*> _botSessions;
    uint32 _heartbeatTimer = 0;
};

#define sBotMgr BotMgr::instance()

#endif // COA_PLAYERBOTS_BOT_MGR_H
