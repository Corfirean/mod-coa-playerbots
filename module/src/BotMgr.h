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

    // Manual/debug entry point: accepts a pending group invite on a bot's
    // behalf right now, reporting success/failure to handler. Bots normally
    // don't need this called explicitly any more (see Update() below) — kept
    // as an explicit override for testing/debugging.
    void AcceptInvite(ObjectGuid::LowType charLowGuid, ChatHandler* handler);

    // Called every world tick via a WorldScript hook. Auto-accepts any
    // pending group invite on every active bot (checked every tick, not
    // throttled — a human player expects a near-instant response to an
    // invite) and logs a periodic (throttled) heartbeat so a human watching
    // the log can confirm bots are still alive without polling in-game.
    void Update(uint32 diff);

private:
    BotMgr() = default;

    WorldSession* FindBotSession(ObjectGuid::LowType charLowGuid) const;

    // Calls the real WorldSession::HandleGroupAcceptOpcode handler directly
    // (a minimal padding WorldPacket stands in for the network payload it
    // reads and discards) — reuses all of the real validation logic instead
    // of duplicating it. Caller must have already confirmed there's a
    // pending invite (session->GetPlayer()->GetGroupInvite()).
    void DoAcceptInvite(WorldSession* session);

    std::vector<WorldSession*> _botSessions;
    uint32 _heartbeatTimer = 0;
};

#define sBotMgr BotMgr::instance()

#endif // COA_PLAYERBOTS_BOT_MGR_H
