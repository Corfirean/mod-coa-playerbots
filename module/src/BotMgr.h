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
#include <string>
#include <vector>

class ChatHandler;
class Player;
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

    // Manual/debug entry point: has a bot invite another online player (bot or real client,
    // matched by name) to its group, by calling the real WorldSession::HandleGroupInviteOpcode
    // handler directly with a minimal packet -- same "call the real thing" pattern as
    // DoAcceptInvite. There's no plain ".invite" GM command in this codebase (only
    // ".guild invite" exists) and a real client only ever sends this via its UI, so this is
    // the only way to form a test group between two bots without a real client's cooperation.
    void Invite(ObjectGuid::LowType charLowGuid, std::string const& targetName, ChatHandler* handler);

    // Cleanly logs a bot out: LogoutPlayer(true) (saves + removes the Player
    // from world, same path a real disconnect takes) then frees the
    // WorldSession. Without this there was previously no way to remove a bot
    // short of restarting the whole server.
    void DespawnBot(ObjectGuid::LowType charLowGuid, ChatHandler* handler);

    // Diagnostic-only, not bot-specific: dumps every current aura (spell id,
    // name, whether it carries SPELL_AURA_PREVENT_REGENERATE_POWER) on any
    // online player found by low guid -- bot or real client. Added to chase
    // the out-of-combat regen bug; not part of the bot-companion feature set.
    void ListAuras(ObjectGuid::LowType charLowGuid, ChatHandler* handler);

    // Diagnostic-only, not bot-specific: runs an arbitrary chat command
    // (e.g. ".localspec 52") as the given online player (bot or real
    // client), by feeding it through that player's own WorldSession's
    // ChatHandler -- same trick as the direct-handler calls elsewhere in
    // this file, but for chat commands specifically. Needed because some
    // gameplay systems (e.g. Ascension's custom talent trees) are
    // implemented as SEC_PLAYER chat commands a client addon sends on the
    // player's behalf, not as normal opcodes.
    void RunChatCommand(ObjectGuid::LowType charLowGuid, std::string const& command, ChatHandler* handler);

    // Diagnostic-only, not bot-specific: reports Player::HasSpell() for a
    // fixed set of spell ids on any online player by low guid. Added to
    // settle exactly which prerequisite spell a stuck Ascension talent
    // entry is missing, instead of inferring it from aura presence (auras
    // don't exist for every learned spell) or from chat messages (easy to
    // miss in a busy log).
    void HasSpells(ObjectGuid::LowType charLowGuid, std::vector<uint32> const& spellIds, ChatHandler* handler);

    // Debug/testing entry point: finds the nearest non-friendly unit within range and
    // has the bot attack it via the same Unit::Attack() path a real client's attack
    // action would use. Needed because there's no way to click "attack" for a bot from
    // the RA console -- BotAI.cpp then takes over once combat starts (chase/cast/etc.).
    void AttackNearestHostile(ObjectGuid::LowType charLowGuid, float range, ChatHandler* handler);

    // Debug/testing entry point: kills a bot outright via the real Unit::Kill() death-processing
    // path (same one any normal combat death goes through -- loot generation, death state
    // transition, everything), for exercising BotAI's death-handling flow (grace period,
    // release spirit, corpse-run, self-res) on demand. Added because there's no reliable way
    // to manufacture a controlled bot death from the RA console otherwise: .die needs a real
    // client selection, and reducing HP via .modify was inconsistent (sometimes the bot's own
    // damage output or regen wins the race before a mob's next hit lands).
    void Kill(ObjectGuid::LowType charLowGuid, ChatHandler* handler);

    // Assigns a bot's AI role ("dps", "tank", or "healer" -- case-insensitive) for
    // BotAI::Update() to dispatch on. See BotRole in BotAI.h for what changes per role, and
    // docs/roles.md for why this is manual rather than read from the bot's actual Ascension
    // spec right now.
    void SetRole(ObjectGuid::LowType charLowGuid, std::string const& roleName, ChatHandler* handler);

    // Diagnostic: reports what BotAI's own offensive/taunt/heal spellbook filters find for
    // this bot right now. See BotAI::ReportSpellbookRoleSignals for why this exists (mapping
    // a class's numeric Ascension SpecId to a real role empirically).
    void CheckRole(ObjectGuid::LowType charLowGuid, ChatHandler* handler);

    // Gives a bot a real Ascension specialization: persists the same
    // "core.ascension_active_spec" PlayerSetting mod-ascension-compat's own login hook reads,
    // and directly learns every non-automatic ("paid") talent entry in that spec plus the
    // shared class tree at its highest rank -- the ones AECost/TECost-gated behind
    // `.localtalent`, a SEC_PLAYER chat command that (like all of them) doesn't work on a
    // null-socket bot session (see RunChatCommand's doc comment). This is a companion, not a
    // stripped-down pet substitute (docs/architecture.md's scope decision) -- a real player's
    // group companion has their talents spent, so a bot should too. Automatic (free) entries
    // are left to mod-ascension-compat's own SynchronizeProgression, triggered by the
    // PlayerSetting write on next login; this only handles the paid ones it can't reach.
    // specId 0 grants only the shared tree (no spec chosen). See docs/roles.md for known
    // class -> SpecId -> role mappings.
    void LearnSpecialization(ObjectGuid::LowType charLowGuid, uint32 specId, ChatHandler* handler);

    // Public counterpart to the private FindBotSession, for callers (BotAddonChat.cpp) that
    // need to confirm a guid is really one of our tracked bots and get its Player* -- e.g. to
    // reject an addon-message command referencing a guid that isn't actually a bot session.
    // Returns nullptr for anything not currently in _botSessions with a live Player.
    Player* FindBotPlayer(ObjectGuid::LowType charLowGuid) const;

    // All currently-online bots with a live Player (skips a session mid-login with no Player
    // yet). For callers that need to pick one out of the whole roster rather than look up one
    // specific guid -- e.g. BotBattlegroundFill.cpp finding an idle bot of a given faction to
    // queue, without needing BotMgr to track a separate faction-indexed roster of its own.
    std::vector<Player*> GetOnlineBots() const;

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

    // Checked every tick for every grouped bot not already mid-teleport: if the bot's map
    // and instance id no longer match its group leader's (leader zoned into a dungeon, raid,
    // or battleground -- or back out), queues a teleport to the leader's exact location, using
    // the same TeleportTo()-then-queue-ack-for-next-tick sequencing DoAcceptInvite already
    // established (see FinishPendingTeleport's comment for why the ack can't fire same-tick).
    // A real client walks through a portal themselves; a bot has nothing to walk through, so
    // this is the bot-side equivalent of that action, done automatically instead of requiring
    // a manual command -- see docs/addon-protocol.md and AGENTS.md for the user request this
    // implements. Deliberately unconditional on map *type* (open world vs. instance vs. BG):
    // simplest correct rule, and a real player's own boat/zeppelin transport has no bot-usable
    // equivalent anyway. Battleground entry specifically is a known open item -- teleporting a
    // bot onto a BG map doesn't run it through the BG's own join/team-assignment bookkeeping,
    // see AGENTS.md for what's confirmed vs. still-to-verify there.
    void TryFollowLeaderAcrossMaps(WorldSession* session);

    // Checked every tick for every ghosted bot not already mid-teleport and not in a
    // battleground: if the bot's own corpse is on a different map than the bot currently is
    // (a dungeon's nearest graveyard can be outside the instance entirely -- confirmed live in
    // Deadmines), teleports the bot onto the corpse's map so BotAI's corpse-walk logic has a
    // coordinate space that actually matches. Same TeleportTo()-then-queue-ack-for-next-tick
    // sequencing as TryFollowLeaderAcrossMaps/DoAcceptInvite, for the same reason (see
    // FinishPendingTeleport's comment).
    void TryReturnGhostToCorpseMap(WorldSession* session);

    // Calls the real WorldSession::HandleLootRoll handler directly with a
    // Greed vote for the given roll (itemGUID/itemSlot read straight off the
    // Roll object — no packet-guessing needed). Caller must have already
    // confirmed this bot has a pending, not-yet-answered vote on this roll.
    void DoRollGreed(WorldSession* session, Roll* roll);

    // Checked alongside the heartbeat (every 10s): if every currently active bot session is
    // already at the level cap, logs a one-time (edge-triggered) notice that there's no lower-
    // level companion left in the active roster to send out leveling in the world.
    // Deliberately does NOT pick a character and auto-spawn it: unlike everything else in this
    // file, "which characters count as part of our companion roster" isn't something SpawnBot's
    // own account-agnostic design tracks anywhere (there's no roster/pool concept at all -- any
    // guid on any account works), so silently guessing at that pool risks pulling in a character
    // nobody meant to include, and creating a brand new one outright is against this project's
    // own standing rule of never creating characters without being asked. See AGENTS.md's open
    // question on this for what a real answer needs (an explicit config-listed twink pool, at
    // minimum) before this can safely go further than a notice.
    void CheckAllBotsMaxLevel();
    bool _allBotsMaxLevelNotified = false;

    std::vector<WorldSession*> _botSessions;
    std::vector<WorldSession*> _pendingTeleportAck;
    uint32 _heartbeatTimer = 0;
};

#define sBotMgr BotMgr::instance()

#endif // COA_PLAYERBOTS_BOT_MGR_H
