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
#include "BotFormations.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ChatHandler;
class Guild;
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
    // not returned synchronously. onReady, if given, fires once the bot's real Player object
    // exists (right after a successful login) -- for a caller that needs to do one-time setup
    // on a freshly created character (see BotSpawnRandom.cpp's leveled-bot spawner) without
    // adding yet another pending-queue mechanism just for that.
    void SpawnBot(ObjectGuid::LowType charLowGuid, ChatHandler* handler, std::function<void(Player*)> onReady = nullptr);

    // Manual/debug entry point: accepts a pending group invite on a bot's
    // behalf right now, reporting success/failure to handler. Bots normally
    // don't need this called explicitly any more (see Update() below) — kept
    // as an explicit override for testing/debugging.
    void AcceptInvite(ObjectGuid::LowType charLowGuid, ChatHandler* handler);

    // Manual/debug entry point: accepts a pending guild invite on a bot's behalf
    // right now, reporting success/failure to handler.
    void AcceptGuildInvite(ObjectGuid::LowType charLowGuid, ChatHandler* handler);

    // Manual/debug entry point: has a bot in a guild invite another online player
    // (bot or real client, matched by name) to its guild.
    void GuildInvite(ObjectGuid::LowType charLowGuid, std::string const& targetName, ChatHandler* handler);

    // Creates a new guild with this bot as Guild Master.
    void GuildCreate(ObjectGuid::LowType charLowGuid, std::string const& guildName, ChatHandler* handler);

    // Deposits items of itemEntry from bot's inventory into guild bank.
    // count == 0 means deposit all matching items. Returns deposited count.
    uint32 GuildDepositItem(ObjectGuid::LowType charLowGuid, uint32 itemEntry, uint32 count, ChatHandler* handler);

    // Withdraws items of itemEntry from guild bank into bot's inventory. Returns withdrawn count.
    uint32 GuildWithdrawItem(ObjectGuid::LowType charLowGuid, uint32 itemEntry, uint32 count, ChatHandler* handler);

    // Deposits money (in copper) from bot into guild bank.
    void GuildDepositMoney(ObjectGuid::LowType charLowGuid, uint32 copper, ChatHandler* handler);

    // Withdraws money (in copper) from guild bank to bot.
    void GuildWithdrawMoney(ObjectGuid::LowType charLowGuid, uint32 copper, ChatHandler* handler);

    // Orders bot to gather itemEntry up to targetCount and deposit into guild bank.
    void GuildGather(ObjectGuid::LowType charLowGuid, uint32 itemEntry, uint32 targetCount, ChatHandler* handler);

    // Finds an online guild-mate bot of `requesterCharLowGuid` that knows a recipe spell
    // producing itemEntry (any learned spell with a SPELL_EFFECT_CREATE_ITEM effect targeting
    // it -- see CraftingRecipeIndex in BotMgr.cpp), and orders it to craft `count` of them. If
    // the crafter already holds enough reagents, crafts immediately; otherwise queues a
    // background order (_craftOrders) that retries every tick once reagents show up (e.g. via
    // the crafter's own autonomous gathering AI) -- same "wait for it" shape as
    // _guildGatherOrders. Finished items are mailed to the requester via the real MailDraft
    // path (works whether they're online or not), never deposited to the guild bank -- see
    // docs/addon-protocol.md's CRAFTORDER verb for why that's a deliberate v1 simplification.
    void CraftOrder(ObjectGuid::LowType requesterCharLowGuid, uint32 itemEntry, uint32 count, ChatHandler* handler);

    // Addon-facing counterpart to GuildGather: picks a suitable online guild-mate bot itself
    // (same "auto-pick, don't make the player know a bot's raw guid" idea as CraftOrder) rather
    // than requiring a specific charLowGuid, then places the same background order via the
    // existing GuildGather. Prefers a bot not already busy with a gather/craft order of its own;
    // falls back to the first online guild-mate bot if all are busy (same overwrite tolerance
    // CraftOrder already has for _craftOrders). See docs/addon-protocol.md's GATHERORDER verb.
    void GatherOrder(ObjectGuid::LowType requesterCharLowGuid, uint32 itemEntry, uint32 count, ChatHandler* handler);

    struct GuildGatherOrder
    {
        uint32 itemEntry = 0;
        uint32 targetCount = 0;
        uint32 gatheredCount = 0;
        uint32 remainingCount = 0;
        uint32 targetMapId = 0;
        float targetX = 0.0f;
        float targetY = 0.0f;
        float targetZ = 0.0f;
        bool hasTargetLocation = false;
    };
    GuildGatherOrder const* GetGuildGatherOrder(ObjectGuid const& guid) const;
    void LoadGuildGatherOrders();
    void SaveGuildGatherOrder(ObjectGuid const& guid, GuildGatherOrder const& order);
    void DeleteGuildGatherOrder(ObjectGuid const& guid);

    // One pre-formatted "ROSTER:botGuidLow:name:class:level:task:prof1=skill1,prof2=skill2"
    // body per online bot in `commander`'s guild -- the addon task-board query (see
    // docs/addon-protocol.md's GUILDROSTER verb). One string per bot rather than one combined
    // message, same reasoning as everything else on this wire channel: WoW chat messages have
    // a real length cap, and a big guild's full roster could exceed it in a single body.
    std::vector<std::string> GetGuildRosterInfo(Player* commander) const;

    // Addon catalog queries (see docs/addon-protocol.md's GETGATHERCATALOG/GETRECIPECATALOG
    // verbs) -- both return pre-chunked "GCAT:category:entry,name|entry,name|..." /
    // "RCAT:entry,name|..." reply bodies (several per call, chat-length-safe, same chunking
    // reasoning as GetGuildRosterInfo) so the addon can build icon-menu pickers instead of
    // making the player type a raw item id. GetGatherCatalog is guild-agnostic (any online bot
    // can gather any of these once granted every profession, see GrantAllProfessions) and its
    // underlying data is cached process-wide after the first call. GetRecipeCatalog is scoped
    // to `commander`'s guild -- only items an online guild-mate bot can *actually* craft right
    // now (Player::HasSpell on a real SPELL_EFFECT_CREATE_ITEM spell, same definition of
    // "knows a recipe" CraftOrder itself uses) are offered, per the user's explicit ask to only
    // show recipes bots really have.
    std::vector<std::string> GetGatherCatalog() const;
    std::vector<std::string> GetRecipeCatalog(Player* commander) const;

    // Diagnostic-only, not a wire-protocol verb: reports, per profession, how many distinct
    // craftable items any CURRENTLY ONLINE bot in the whole population actually knows a recipe
    // for. Added because this realm's `spell_dbc`/`skilllineability_dbc` SQL export tables turned
    // out to be a small, stale subset (4486 rows, IDs up to ~80000) that doesn't cover this
    // fork's real custom spell content (confirmed live: a working craft order used spell 807053,
    // absent from that table entirely) -- so a SQL-only check of "does any bot know a real
    // Blacksmithing/Leatherworking/Tailoring recipe" is unreliable. This asks the same live,
    // authoritative in-memory `sSpellMgr`/`Player::HasSpell` data `CraftOrder` itself already
    // trusts, instead.
    void DumpRecipeCoverage(ChatHandler* handler) const;

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

    // Despawns every online bot and permanently deletes every bot character row (any account
    // matching CoaBots.RandomSpawn.AccountPrefix, online or not) via the real
    // Player::DeleteFromDB, so nothing gets left behind for a name/guid collision on the next
    // spawn batch. For clearing out an old naked/broken population before respawning fresh
    // ones with .botcmd spawnrandom or spawnleveled -- does not create any replacement bots
    // itself.
    void PurgeAllBots(ChatHandler* handler);

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

    // One-button dungeon group fill: brings `commander`'s group up to 5 (tank + healer + 3
    // dps), inviting online bots to cover whichever of those roles it's currently missing.
    // Guildmates of `commander` are preferred over other bots; among equally-eligible
    // candidates, closest character level then closest average item level wins. Invites are
    // issued through `commander`'s own real WorldSession::HandleGroupInviteOpcode (same packet
    // shape as BotMgr::Invite, just fired from the real player's session instead of a bot's) --
    // no new accept-side code needed, since a bot's pending GetGroupInvite() is already
    // auto-accepted every tick by the existing BotMgr::Update() loop (DoAcceptInvite), which
    // also handles the teleport-to-leader. `commander` must be a real (non-bot) player; see
    // docs/addon-protocol.md's QUICKFILL verb.
    void QuickFillGroup(Player* commander, ChatHandler* handler);

    // Toggles autonomous dungeon/raid play for `leader`'s whole group: while enabled, a Tank
    // bot idle inside an instance (no target, leader not already fighting) scans for the
    // nearest hostile pack and pulls it on its own -- see BotAI.cpp's TryAutoPullInInstance
    // for why this deliberately does not attempt to encode per-dungeon boss order or mechanics
    // (relies on the instance's own real gating -- locked doors/gameobjects that only open
    // after a prerequisite boss dies -- the same way a human group is naturally kept in the
    // intended order without needing to be told it explicitly). Keyed by leader guid so it
    // covers the group as a whole regardless of which bot is currently idle; persists across
    // a wipe/regroup until explicitly turned off.
    void SetAutoDungeonMode(ObjectGuid leaderGuid, bool enabled);
    bool IsAutoDungeonModeEnabled(ObjectGuid leaderGuid) const;
    void MarkBossCleared(ObjectGuid leaderGuid, uint32 bossEntry);
    bool IsBossCleared(ObjectGuid leaderGuid, uint32 bossEntry) const;
    void ClearBosses(ObjectGuid leaderGuid);

    void SetGroupFormation(ObjectGuid leaderGuid, BotGroupFormation formation);
    BotGroupFormation GetGroupFormation(ObjectGuid leaderGuid) const;

    // Out-of-combat "bring bots to me": teleports every bot in `commander`'s group to
    // `commander`'s exact location, same two-phase TeleportTo()-then-queue-ack sequencing
    // DoAcceptInvite/TryFollowLeaderAcrossMaps already use (see FinishPendingTeleport's
    // comment for why the ack can't fire same-tick) -- FinishPendingTeleport's existing
    // post-landing re-follow picks formation/follow back up with no extra code here. Refuses
    // (no-op) if `commander` is in combat; skips (rather than aborts) any individual bot that
    // is itself in combat, so pulling the group together never yanks one bot out of a fight it's
    // already in. See docs/addon-protocol.md's TELEPORT verb.
    void TeleportBotsToPlayer(Player* commander, ChatHandler* handler);

    // Per-bot gear preference (which basic armor/weapon subclass to favor when rolling on and
    // auto-equipping loot), persisted via PlayerSetting the same way "core.ascension_active_spec"
    // is (survives a relog, in-memory only until then). Armor preference is stored as a raw
    // ItemSubclassArmor value with 0 (ITEM_SUBCLASS_ARMOR_MISC, never itself a preference choice)
    // doubling as the "auto/any" sentinel. Weapon preference is stored as (subclass + 1) since
    // ITEM_SUBCLASS_WEAPON_AXE is itself 0 and would otherwise collide with an "unset" sentinel;
    // 0 means "auto/any". See docs/addon-protocol.md's GETGEAR/SETGEARPREF verbs.
    uint32 GetGearPreference(Player* bot, bool weapon) const;
    void SetGearPreference(Player* bot, bool weapon, uint32 subclass);

    // Whether `itemEntry` (an armor or weapon item) is one this bot should Greed-roll on / treat
    // as a valid upgrade candidate, given its real equip proficiency (CanEquipNewItem) and its
    // gear preference above. Non-armor/non-weapon items, and armor/weapon subclasses outside the
    // 4 basic armor types or 6 basic one-handed/staff weapon types this feature covers, always
    // return true (unaffected -- see docs/addon-protocol.md's TELEPORT verb's neighbor entries
    // for why this deliberately doesn't try to cover shields/rings/trinkets/ranged weapons/etc).
    bool MatchesGearPreference(Player* bot, uint32 itemEntry) const;

    // Which of the 4 basic armor subclasses (cloth/leather/mail/plate) and which of the 6 basic
    // weapon subclasses (axe/mace/sword/staff/fist/dagger) this bot's class can actually equip at
    // all, probed live via the real CanEquipNewItem proficiency check against one representative
    // real item per subclass (same item ids/technique GearUpBot already uses) -- no
    // classId-to-proficiency table exists for Ascension's custom classes, so this has to ask the
    // engine rather than look anything up. Returns raw ItemSubclassArmor/ItemSubclassWeapon
    // values. See docs/addon-protocol.md's GETGEAR verb.
    std::vector<uint32> GetLegalArmorSubclasses(Player* bot) const;
    std::vector<uint32> GetLegalWeaponSubclasses(Player* bot) const;

    // One "GEAR:botGuidLow:slot:itemEntry:itemName" line per currently-equipped item (skips empty
    // slots and the cosmetic-only shirt/tabard slots, which have no "type" concept relevant to
    // gear preference) -- the addon-facing gear inspector. See docs/addon-protocol.md's GETGEAR
    // verb.
    std::vector<std::string> GetEquippedGearInfo(Player* bot) const;

    // Called from a new PLAYERHOOK_ON_LOGIN hook whenever a REAL (non-bot) player logs in.
    // Group membership itself already survives a restart natively -- Player::_LoadGroup()
    // reattaches `player` to its pre-existing Group (loaded at world boot by
    // GroupMgr::LoadGroups()) purely from the persisted group_member table, no action needed
    // here for that part. What doesn't happen automatically is the bots' own login: an offline
    // groupmate just stays offline forever unless something calls SpawnBot for it. This walks
    // `player`'s (already-reattached) group's full member list -- including offline members,
    // via Group::GetMemberSlots(), not just the online GroupReference list -- and SpawnBots
    // any member that (a) isn't already online and (b) actually belongs to a bot-hosting
    // account (checked via IsBotAccountId, matched against the same
    // CoaBots.RandomSpawn.AccountPrefix config key BotSpawnRandom.cpp uses to create these
    // accounts) -- that account check is the safety rail that stops this from ever
    // auto-logging-in some other real player's alt just because it was left grouped.
    void RestoreGroupBotsOnLogin(Player* player);

    // One-off bootstrap/maintenance operation for `.botcmd geartrainer` -- tops up any EMPTY
    // (not already-occupied, never replaces existing gear) equipment slot with a fixed, modest
    // "heroic entry" item (real WotLK ilvl-200 blue tier-9-equivalent pieces, picked per the
    // bot's armor proficiency from its base WoW class) so a freshly-created random bot (which
    // only starts with mod-ascension-compat's minimal starter kit) can actually meet a
    // dungeon's average-item-level gate (e.g. Halls of Stone heroic's 180) instead of dragging
    // a Quick-Filled group's average down to zero. Deliberately blue/ilvl-200, not raid epics --
    // enough to clear common heroic gates without bots one-shotting content. Safe to call
    // repeatedly (StoreNewItemInBestSlots leaves already-filled slots untouched).
    void GearUpBot(Player* bot, ChatHandler* handler);

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

    // Public hook for any OTHER real-engine call that can itself trigger a TeleportTo() this
    // module didn't initiate directly -- Player::RepopAtGraveyard() (called from the real
    // WorldSession::HandleRepopRequestOpcode, itself called from BotAI::UpdateDeathHandling's
    // "call the real thing" release-spirit flow) is the confirmed case: releasing at a
    // graveyard teleports the ghost there internally, same as the module's own explicit
    // teleports elsewhere (DoAcceptInvite, TryFollowLeaderAcrossMaps,
    // TryReturnGhostToCorpseMap), but nothing was queuing *that* one for its ack. Left
    // unacked, a bot gets permanently stuck with IsBeingTeleportedNear()/Far() == true forever
    // (nothing else ever clears it for a null-socket session) -- confirmed live: bots that
    // died stopped cross-map-following their leader afterward, while one that never died kept
    // working. Callers outside BotMgr.cpp (BotAI.cpp's death handling) use this instead of
    // reaching into _pendingTeleportAck directly.
    void QueueTeleportAck(WorldSession* session);

    // Checks whether the given account ID belongs to a bot-hosting account.
    static bool IsBotAccountId(uint32 accountId);

    // Called from a new GroupScript::OnRemoveMember hook whenever a real (non-bot) player
    // leaves/is removed from a group that leaves no real player behind -- see that hook's own
    // comment for why bots shouldn't just sit there leaderless. Queued for the next Update()
    // tick rather than acted on immediately: OnRemoveMember fires from inside
    // Group::RemoveMember itself, and having a bot call RemoveMember again on the same Group
    // while it's still unwinding the first removal is exactly the kind of reentrancy the
    // existing _pendingTeleportAck/QueueTeleportAck pattern was already built to avoid elsewhere
    // in this file.
    void QueueBotGroupLeave(ObjectGuid botGuid);

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

    // Calls WorldSession::HandleGuildAcceptOpcode with a synthetic CMSG_GUILD_ACCEPT
    // packet, mirroring DoAcceptInvite for groups. Caller should ensure bot has a pending
    // guild invite (bot->GetGuildIdInvited() != 0).
    void DoAcceptGuildInvite(WorldSession* session);

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

    // Same shape as DoRollGreed but casts ROLL_PASS -- used when MatchesGearPreference says this
    // roll's item is an armor/weapon type this bot's class can't wear or doesn't prefer. See
    // Update()'s roll loop and docs/addon-protocol.md's SETGEARPREF verb.
    void DoRollPass(WorldSession* session, Roll* roll);

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

    void EnsureBotBankRights(Player* bot, Guild* guild);

    std::unordered_map<ObjectGuid, GuildGatherOrder> _guildGatherOrders;

    // See CraftOrder's header comment. A crafter (bot guid) may have at most one active order
    // at a time -- a second CraftOrder call for a crafter already crafting replaces it rather
    // than queuing, matching this feature's "simplest that works" scope.
    struct CraftOrderState
    {
        ObjectGuid requesterGuid;
        uint32 spellId = 0;
        uint32 itemEntry = 0;
        uint32 remainingCount = 0;
        // Set once a cast has been fired, so the next tick(s) wait for it to actually finish
        // (Unit::IsNonMeleeSpellCast) instead of re-casting over it -- some tradeskill recipes
        // have a real cast time, not just instant ones, and firing a new cast mid-cast would
        // interrupt and restart it every tick, never letting it finish.
        bool awaitingCastResult = false;
        uint32 itemCountBeforeCast = 0;
    };
    std::unordered_map<ObjectGuid, CraftOrderState> _craftOrders;
    void ProcessCraftOrders();

    std::vector<WorldSession*> _botSessions;
    std::vector<WorldSession*> _pendingTeleportAck;
    std::vector<ObjectGuid> _pendingGroupLeaves;
    std::vector<ObjectGuid::LowType> _pendingAutoLoginQueue;
    uint32 _autoLoginThrottleMs = 0;
    uint32 _heartbeatTimer = 0;
    std::unordered_set<ObjectGuid> _autoDungeonLeaders;
    std::unordered_map<ObjectGuid, std::unordered_set<uint32>> _clearedBosses;
    std::unordered_map<ObjectGuid, BotGroupFormation> _groupFormations;

public:
    // Called once from a new WorldScript::OnStartup hook, gated behind CoaBots.AutoLoginOnStartup
    // -- queries every character on a bot-hosting account (same COABOTHOST-prefix check
    // IsBotAccountId uses elsewhere) not already online, and queues them for the same gradual,
    // throttled login BotMgr::Update already drains SpawnRandomBots/SpawnLeveledBots's queues
    // with. Also callable directly (e.g. from a GM command) to (re)populate the queue against an
    // already-running server without needing a restart.
    void QueueAllBotsForAutoLogin();
};

#define sBotMgr BotMgr::instance()

#endif // COA_PLAYERBOTS_BOT_MGR_H
