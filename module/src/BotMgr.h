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
class Roll;
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

    // Cleanly logs a bot out: LogoutPlayer(true) (saves + removes the Player
    // from world, same path a real disconnect takes) then frees the
    // WorldSession. Without this there was previously no way to remove a bot
    // short of restarting the whole server.
    void DespawnBot(ObjectGuid::LowType charLowGuid, ChatHandler* handler);

    // Called every world tick via a WorldScript hook. Auto-accepts any
    // pending group invite and auto-rolls Greed on any pending loot roll for
    // every active bot (both checked every tick, not throttled — a human
    // player expects a near-instant response to either), and logs a
    // periodic (throttled) heartbeat so a human watching the log can confirm
    // bots are still alive without polling in-game.
    void Update(uint32 diff);

private:
    BotMgr() = default;

    WorldSession* FindBotSession(ObjectGuid::LowType charLowGuid) const;

    // Calls the real WorldSession::HandleGroupAcceptOpcode handler directly
    // (a minimal padding WorldPacket stands in for the network payload it
    // reads and discards) — reuses all of the real validation logic instead
    // of duplicating it. Caller must have already confirmed there's a
    // pending invite (session->GetPlayer()->GetGroupInvite()). Also
    // teleports the bot to the group leader's exact spot on success — the
    // teleport-ack (see FinishPendingTeleport below) and MoveFollow happen
    // later, once the teleport has actually landed.
    void DoAcceptInvite(WorldSession* session);

    // A Player-type TeleportTo() (near or far) only *requests* the move --
    // the real position/grid update happens inside the ack handler
    // (HandleMoveTeleportAck / HandleMoveWorldportAck), which a real client
    // sends after its own network round trip. Firing that ack synchronously,
    // in the same tick as TeleportTo() itself, crashes with an IsInGrid()
    // assert in Map::PlayerRelocation -- confirmed live via a crash dump.
    // So: queue it here, and fire it from the *next* Update() tick instead,
    // giving the map's own per-tick processing a chance to settle first,
    // same as the real network delay would.
    void FinishPendingTeleport(WorldSession* session);

    // Calls the real WorldSession::HandleLootRoll handler directly with a
    // Greed vote for the given roll (itemGUID/itemSlot read straight off the
    // Roll object — no packet-guessing needed). Caller must have already
    // confirmed this bot has a pending, not-yet-answered vote on this roll.
    void DoRollGreed(WorldSession* session, Roll* roll);

    std::vector<WorldSession*> _botSessions;
    std::vector<WorldSession*> _pendingTeleportAck;
    uint32 _heartbeatTimer = 0;
};

#define sBotMgr BotMgr::instance()

#endif // COA_PLAYERBOTS_BOT_MGR_H
