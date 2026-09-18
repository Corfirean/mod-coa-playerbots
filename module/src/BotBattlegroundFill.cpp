/*
 * mod-coa-playerbots
 *
 * Real players (and bots that join through the real opcode handlers) enter a Battleground
 * queue via WorldSession::HandleBattlemasterJoinOpcode, which -- for a solo, non-arena join --
 * boils down to (BattleGroundHandler.cpp, HandleBattlemasterJoinOpcode):
 *   GroupQueueInfo* ginfo = bgQueue.AddGroup(player, nullptr, bgTypeId, bracketEntry, 0, false, false, 0, 0);
 *   player->AddBattlegroundQueueId(bgQueueTypeId);
 *   sScriptMgr->OnPlayerJoinBG(player);
 * and, once invited (the engine's own already-running BattlegroundQueueUpdate fills and
 * invites automatically -- nothing here needs to force that), accepting the invite
 * (HandleBattleFieldPortOpcode, action==1) boils down to a real TeleportTo via
 * BattlegroundMgr::SendToBattleground plus a handful of queue/state bookkeeping calls. Both
 * sequences are called here directly against a bot's Player*, the same "call the real thing"
 * pattern as every other bot action in this module (see BotMgr.cpp) -- no synthetic packets
 * needed at all, since these are plain C++ calls once you have a real Player pointer.
 *
 * The actual feature: PLAYERHOOK_ON_PLAYER_JOIN_BG fires right after anyone (real or bot,
 * though bots queued by this file skip that hook -- they never go through the real opcode
 * handler) joins a normal (non-arena, non-rated) queue. On that signal, top off BOTH factions'
 * queues for that exact bracket up to the battleground's own real max-per-team, so a match is
 * always full and pops immediately instead of waiting for real population. Arenas and rated
 * matches are explicitly out of scope (team balance/MMR/rating there is a different, much
 * more delicate problem than "always fill a normal BG").
 *
 * Bot sourcing: prefers an already-online, idle (not already in a BG or queue, not dead, no
 * deserter debuff) bot of the needed faction over creating a new one. Only creates a new
 * character (BotSpawn::CreateOneRandomBot, race constrained to the needed faction) when no
 * suitable online bot exists -- login is async (BotMgr::SpawnBot's established pattern), so a
 * freshly created bot's queue-join is deferred until it actually appears online
 * (BotMgr::FindBotPlayer polled per tick), not attempted synchronously.
 */

#include "BotBattlegroundFill.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "BotMgr.h"
#include "BotSpawnRandom.h"
#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "Group.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "WorldScript.h"
#include <algorithm>
#include <array>
#include <random>
#include <tuple>
#include <vector>

namespace
{
constexpr std::array<uint8, 5> ALLIANCE_RACES = { 1, 3, 4, 7, 11 };
constexpr std::array<uint8, 5> HORDE_RACES = { 2, 5, 6, 8, 10 };

// A bot whose character was just created and whose async login (BotMgr::SpawnBot) hasn't
// completed yet -- checked every tick until it appears in BotMgr::GetOnlineBots(), then
// queued for real. Given up on after PENDING_LOGIN_TIMEOUT_MS in case something's actually
// wrong (bad template roster, DB hiccup) rather than retrying forever.
struct PendingBotJoin
{
    ObjectGuid::LowType guid;
    BattlegroundTypeId bgTypeId;
    BattlegroundQueueTypeId bgQueueTypeId;
    PvPDifficultyEntry const* bracketEntry; // points into a static DBC store, safe to hold
    uint32 ageMs = 0;
};
std::vector<PendingBotJoin> pendingLogins;

// A bot already queued, waiting for BattlegroundQueueUpdate's own per-tick invite pass (already
// running regardless of this file) to invite it -- checked every tick until invited, then
// ported in and dropped from this list.
struct QueuedBotWatch
{
    ObjectGuid::LowType guid;
    BattlegroundTypeId bgTypeId;
    BattlegroundQueueTypeId bgQueueTypeId;
    BattlegroundBracketId bracketId;
};
std::vector<QueuedBotWatch> queuedBots;

// BattlegroundQueue::CheckNormalMatch greedily stops selecting queued groups the instant each
// side reaches the bracket's real *minimum*, not maximum (confirmed by reading
// BattlegroundQueue.cpp directly) -- so a burst of bots queuing in close succession can easily
// pop an undersized first match and strand the rest, with nothing left to prompt the engine's
// own BattlegroundQueueUpdate to try forming a second match from what's left, since normal
// (non-arena) BG queues have no periodic re-check of their own, only the explicit
// ScheduleQueueUpdate calls each real join already makes. Re-nudging every
// REQUEUE_NUDGE_INTERVAL_MS for as long as anything is still waiting in queuedBots keeps
// giving the engine's own matching another chance to pick up the stragglers into a follow-up
// match, without this file needing to reimplement any of that matching logic itself.
constexpr uint32 REQUEUE_NUDGE_INTERVAL_MS = 3000;
uint32 requeueNudgeTimerMs = 0;

std::mt19937& Rng()
{
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

uint8 PickRaceForTeam(TeamId team)
{
    auto const& races = (team == TEAM_ALLIANCE) ? ALLIANCE_RACES : HORDE_RACES;
    std::uniform_int_distribution<size_t> dist(0, races.size() - 1);
    return races[dist(Rng())];
}

// Replicates HandleBattleFieldPortOpcode's action==1 (accept) branch for a bot that's just
// been invited -- see this file's header comment for why this is a direct call, not a packet.
void PortBotIntoBattleground(Player* bot, BattlegroundQueueTypeId bgQueueTypeId)
{
    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
    GroupQueueInfo ginfo;
    if (!bgQueue.GetPlayerGroupInfoData(bot->GetGUID(), &ginfo) || !ginfo.IsInvitedToBGInstanceGUID)
        return;

    BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(bgQueueTypeId);
    Battleground* bg = sBattlegroundMgr->GetBattleground(ginfo.IsInvitedToBGInstanceGUID, bgTypeId);
    if (!bg)
        return;

    uint32 queueSlot = bot->GetBattlegroundQueueIndex(bgQueueTypeId);

    if (!bot->InBattleground())
        bot->SetEntryPoint();
    if (!bot->IsAlive())
    {
        bot->ResurrectPlayer(1.0f);
        bot->SpawnCorpseBones();
    }

    TeamId teamId = ginfo.teamId;
    bgQueue.RemovePlayer(bot->GetGUID(), false);

    if (Battleground* currentBg = bot->GetBattleground())
        currentBg->RemovePlayerAtLeave(bot);

    for (uint8 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
    {
        BattlegroundQueueTypeId otherQueueTypeId = bot->GetBattlegroundQueueTypeId(i);
        if (otherQueueTypeId != BATTLEGROUND_QUEUE_NONE && otherQueueTypeId != bgQueueTypeId)
        {
            bot->RemoveBattlegroundQueueId(otherQueueTypeId);
            sBattlegroundMgr->GetBattlegroundQueue(otherQueueTypeId).RemovePlayer(bot->GetGUID(), true);
        }
    }

    sLFGMgr->LeaveAllLfgQueues(bot->GetGUID(), false);

    bot->SetBattlegroundId(bg->GetInstanceID(), bg->GetBgTypeID(), queueSlot, true, bgTypeId == BATTLEGROUND_RB, teamId);

    if (!sBattlegroundMgr->SendToBattleground(bot, ginfo.IsInvitedToBGInstanceGUID, bgTypeId))
    {
        // Mirrors the real handler's own rollback for a teleport that never started --
        // otherwise the invited reservation leaks forever (see BattleGroundHandler.cpp).
        bg->DecreaseInvitedCount(teamId);
        if (bg->HasFreeSlots())
            bg->AddToBGFreeSlotQueue();
        bot->RemoveBattlegroundQueueId(bgQueueTypeId);
        bot->SetBattlegroundId(0, BATTLEGROUND_TYPE_NONE, PLAYER_MAX_BATTLEGROUND_QUEUES, false, false, TEAM_NEUTRAL);
        LOG_ERROR("module.coa-playerbots", "BotBGFill: bot '{}' failed to teleport into bg instance {}.",
            bot->GetName(), bg->GetInstanceID());
        return;
    }

    // Confirmed live: bots reported "ported into battleground" here (SendToBattleground
    // returned true -- the TeleportTo call itself didn't fail) but never actually became visible
    // or interactive in the instance. Root cause -- SendToBattleground's TeleportTo crosses maps
    // (a real "worldport"), which for a bot (no real client to ever send back
    // MSG_MOVE_WORLDPORT_ACK) never actually finishes without something calling that ack on its
    // behalf -- see BotMgr::QueueTeleportAck/FinishPendingTeleport, the same pattern
    // BotZoneProgression::RelocateBot and DoAcceptInvite's group-teleport already use. This file
    // never called it at all, so a bot ported into a battleground was left permanently stuck
    // mid-teleport (IsBeingTeleportedFar() forever true) -- present in the queue/instance
    // bookkeeping, but never actually landed.
    sBotMgr->QueueTeleportAck(bot->GetSession());

    LOG_INFO("module.coa-playerbots", "BotBGFill: bot '{}' ported into battleground (type {}, instance {}, team {}).",
        bot->GetName(), uint32(bgTypeId), bg->GetInstanceID(), uint32(teamId));
}

// Replicates HandleBattlemasterJoinOpcode's solo-join branch for a bot already online.
void JoinBotToQueue(Player* bot, BattlegroundTypeId bgTypeId, BattlegroundQueueTypeId bgQueueTypeId,
    PvPDifficultyEntry const* bracketEntry)
{
    Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bgTemplate || bot->InBattleground() || bot->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeId))
        return;
    if (!bot->CanJoinToBattleground(bgTemplate) || !bot->HasFreeBattlegroundQueueId())
        return;

    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);

    // Confirmed live -- still crashing here even after gating OnPlayerJoinBG to a single call per
    // join event (see that function's own comment): a bot that's a member of the *joining
    // player's own group* gets registered into this exact queue's m_QueuedPlayers by the real
    // engine's own AddGroup(player, group, ...) call for the whole group, but nothing on the
    // bot's own Player object (InBattlegroundQueueForBattlegroundQueueType, checked above) ever
    // reflects that -- AddBattlegroundQueueId is only ever called on the player that actually
    // triggered the opcode, not on every member of their group. So this bot looked completely
    // free to this file's own eligibility checks while the queue itself already considered it
    // taken, and calling AddGroup on it a second time hit BattlegroundQueue::AddGroup's own
    // `m_QueuedPlayers.count(leader->GetGUID()) == 0` assertion. GetPlayerGroupInfoData is the
    // exact same lookup that assertion is protecting -- checking it first here catches this
    // (and any other way a bot could already be group-queued) without needing to reason about
    // every possible caller.
    GroupQueueInfo existingInfo;
    if (bgQueue.GetPlayerGroupInfoData(bot->GetGUID(), &existingInfo))
        return;

    GroupQueueInfo* ginfo = bgQueue.AddGroup(bot, nullptr, bgTypeId, bracketEntry, 0, false, false, 0, 0);
    if (!ginfo)
        return;
    bot->AddBattlegroundQueueId(bgQueueTypeId);
    sBattlegroundMgr->ScheduleQueueUpdate(0, 0, bgQueueTypeId, bgTypeId, bracketEntry->GetBracketId());

    LOG_INFO("module.coa-playerbots", "BotBGFill: bot '{}' (team {}) queued for bg type {}.",
        bot->GetName(), uint32(bot->GetTeamId()), uint32(bgTypeId));

    queuedBots.push_back({ bot->GetGUID().GetCounter(), bgTypeId, bgQueueTypeId, bracketEntry->GetBracketId() });
}

// Finds an already-online, idle bot of the given faction and queues it; if none exists,
// creates a new one of an appropriate race and defers the actual queue-join until it logs in.
void QueueBotForSide(TeamId team, BattlegroundTypeId bgTypeId, BattlegroundQueueTypeId bgQueueTypeId,
    PvPDifficultyEntry const* bracketEntry)
{
    for (Player* bot : sBotMgr->GetOnlineBots())
    {
        if (bot->GetTeamId() != team || !bot->IsAlive())
            continue;
        if (bot->InBattleground() || bot->InBattlegroundQueue())
            continue;
        JoinBotToQueue(bot, bgTypeId, bgQueueTypeId, bracketEntry);
        return;
    }

    uint8 race = PickRaceForTeam(team);
    ObjectGuid::LowType newGuid = BotSpawn::CreateOneRandomBot(race, nullptr);
    if (!newGuid)
    {
        LOG_ERROR("module.coa-playerbots", "BotBGFill: could not create a reserve bot for team {}.", uint32(team));
        return;
    }
    sBotMgr->SpawnBot(newGuid, nullptr);
    pendingLogins.push_back({ newGuid, bgTypeId, bgQueueTypeId, bracketEntry });
}

void TopOffQueue(BattlegroundTypeId bgTypeId, BattlegroundQueueTypeId bgQueueTypeId, Battleground* bgTemplate,
    PvPDifficultyEntry const* bracketEntry)
{
    uint32 target = sConfigMgr->GetOption<uint32>("CoaBots.BGFill.TargetPlayersPerTeam", 0);
    if (!target)
        target = bgTemplate->GetMaxPlayersPerTeam();
    if (!target)
        return;

    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
    BattlegroundBracketId bracketId = bracketEntry->GetBracketId();
    uint32 allianceCount = bgQueue.GetPlayersCountInGroupsQueue(bracketId, BG_QUEUE_NORMAL_ALLIANCE);
    uint32 hordeCount = bgQueue.GetPlayersCountInGroupsQueue(bracketId, BG_QUEUE_NORMAL_HORDE);

    uint32 allianceNeeded = allianceCount < target ? target - allianceCount : 0;
    uint32 hordeNeeded = hordeCount < target ? target - hordeCount : 0;

    for (uint32 i = 0; i < allianceNeeded; ++i)
        QueueBotForSide(TEAM_ALLIANCE, bgTypeId, bgQueueTypeId, bracketEntry);
    for (uint32 i = 0; i < hordeNeeded; ++i)
        QueueBotForSide(TEAM_HORDE, bgTypeId, bgQueueTypeId, bracketEntry);
}

void ProcessPendingLogins(uint32 diff)
{
    constexpr uint32 PENDING_LOGIN_TIMEOUT_MS = 30000;
    for (auto it = pendingLogins.begin(); it != pendingLogins.end();)
    {
        it->ageMs += diff;
        if (Player* bot = sBotMgr->FindBotPlayer(it->guid))
        {
            JoinBotToQueue(bot, it->bgTypeId, it->bgQueueTypeId, it->bracketEntry);
            it = pendingLogins.erase(it);
        }
        else if (it->ageMs > PENDING_LOGIN_TIMEOUT_MS)
        {
            LOG_ERROR("module.coa-playerbots", "BotBGFill: reserve bot (guid {}) never logged in, giving up.", it->guid);
            it = pendingLogins.erase(it);
        }
        else
            ++it;
    }
}

void ProcessQueuedBots()
{
    for (auto it = queuedBots.begin(); it != queuedBots.end();)
    {
        Player* bot = sBotMgr->FindBotPlayer(it->guid);
        if (!bot)
        {
            it = queuedBots.erase(it);
            continue;
        }
        if (!bot->IsInvitedForBattlegroundQueueType(it->bgQueueTypeId))
        {
            ++it;
            continue;
        }
        PortBotIntoBattleground(bot, it->bgQueueTypeId);
        it = queuedBots.erase(it);
    }
}

// See REQUEUE_NUDGE_INTERVAL_MS's comment for why this is needed at all: a still-waiting entry
// in queuedBots means its group was queued but never invited, and normal BG queues only ever
// get re-evaluated when something explicitly calls ScheduleQueueUpdate -- so without this,
// stragglers left behind by an undersized first match would wait forever.
void NudgeStalledQueues(uint32 diff)
{
    if (queuedBots.empty())
    {
        requeueNudgeTimerMs = 0;
        return;
    }

    if (requeueNudgeTimerMs > diff)
    {
        requeueNudgeTimerMs -= diff;
        return;
    }
    requeueNudgeTimerMs = REQUEUE_NUDGE_INTERVAL_MS;

    std::vector<std::tuple<BattlegroundQueueTypeId, BattlegroundTypeId, BattlegroundBracketId>> seen;
    for (QueuedBotWatch const& watch : queuedBots)
    {
        auto key = std::make_tuple(watch.bgQueueTypeId, watch.bgTypeId, watch.bracketId);
        if (std::find(seen.begin(), seen.end(), key) != seen.end())
            continue;
        seen.push_back(key);
        sBattlegroundMgr->ScheduleQueueUpdate(0, 0, watch.bgQueueTypeId, watch.bgTypeId, watch.bracketId);
    }
}

class coa_bg_fill_playerscript : public PlayerScript
{
public:
    coa_bg_fill_playerscript() : PlayerScript("coa_bg_fill_playerscript", { PLAYERHOOK_ON_PLAYER_JOIN_BG }) { }

    void OnPlayerJoinBG(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("CoaBots.BGFill.Enable", true))
            return;

        // Confirmed live crash: a *grouped* real-player join (WorldSession::HandleBattlemasterJoinOpcode's
        // group branch) fires this hook once per group member via Group::DoForAllMembers -- for a
        // player queuing with 3 bots, that's 4 separate calls for what is logically one join event.
        // Each call independently re-ran the full top-off pass below, and a bot from the player's OWN
        // just-queued group could get selected again as a "free" fill candidate by QueueBotForSide's
        // online-bot scan before its own AddBattlegroundQueueId flag caught up with the group's already-
        // registered queue state, so JoinBotToQueue tried to solo-AddGroup a bot the queue already had
        // registered -- BattlegroundQueue::AddGroup's own `m_QueuedPlayers.count(leader->GetGUID()) == 0`
        // assertion caught the duplicate and crashed the whole process. Running the top-off pass only
        // once per join event (triggered by the group's leader specifically, or by the player themself
        // when solo) removes the repeat calls entirely instead of trying to make each one individually
        // safe against a race in the engine's own per-member bookkeeping.
        Group* group = player->GetGroup();
        if (group && group->GetLeaderGUID() != player->GetGUID())
            return;

        for (uint32 i = 0; i < PLAYER_MAX_BATTLEGROUND_QUEUES; ++i)
        {
            BattlegroundQueueTypeId bgQueueTypeId = player->GetBattlegroundQueueTypeId(i);
            if (bgQueueTypeId == BATTLEGROUND_QUEUE_NONE)
                continue;
            if (BattlegroundMgr::BGArenaType(bgQueueTypeId) != 0)
                continue; // arenas/rated matches out of scope -- see header comment

            BattlegroundTypeId bgTypeId = BattlegroundMgr::BGTemplateId(bgQueueTypeId);
            Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
            if (!bgTemplate)
                continue;

            PvPDifficultyEntry const* bracketEntry =
                GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), player->GetLevel());
            if (!bracketEntry)
                continue;

            // Confirmed live: a real player queuing WITH a group of bots ends up porting in
            // alone. Root cause -- WorldSession::HandleBattlemasterJoinOpcode's group branch
            // (BattleGroundHandler.cpp) DOES already call member->AddBattlegroundQueueId(...) and
            // sScriptMgr->OnPlayerJoinBG(member) for every group member via Group::DoForAllMembers,
            // bots included -- so a bot's own _BgBattlegroundQueueID bookkeeping and later invite
            // flag (BattlegroundQueue::InviteGroupToBG, called once the match pops) both end up set
            // correctly. What's actually missing: NOTHING ever watches for that invite and ports the
            // bot in -- a real client's own game engine auto-sends the accept opcode
            // (HandleBattleFieldPortOpcode) the instant its invite packet arrives, but a bot has no
            // client to do that. queuedBots is this module's own watch-list for exactly that step
            // (ProcessQueuedBots polls it every tick and calls PortBotIntoBattleground once
            // IsInvitedForBattlegroundQueueType flips true) -- it's just never populated for a real
            // player's own bot group-mates, only for bots this file queued itself (JoinBotToQueue).
            // Fix: register every bot group-mate here too. DoForAllMembers's iteration order isn't
            // guaranteed relative to when this leader-triggered firing runs, so AddBattlegroundQueueId
            // is called again defensively (idempotent -- see its own body) in case this bot's own
            // turn in the engine's loop hasn't happened yet.
            if (group)
            {
                for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                {
                    Player* member = itr->GetSource();
                    if (!member || member == player)
                        continue;
                    if (!member->GetSession() || !sBotMgr->IsBotAccountId(member->GetSession()->GetAccountId()))
                        continue;
                    member->AddBattlegroundQueueId(bgQueueTypeId);
                    queuedBots.push_back({ member->GetGUID().GetCounter(), bgTypeId, bgQueueTypeId, bracketEntry->GetBracketId() });
                    LOG_INFO("module.coa-playerbots", "BotBGFill: watching group-mate bot '{}' for its own queue invite (bg type {}).",
                        member->GetName(), uint32(bgTypeId));
                }
            }

            TopOffQueue(bgTypeId, bgQueueTypeId, bgTemplate, bracketEntry);
        }
    }
};

class coa_bg_fill_worldscript : public WorldScript
{
public:
    coa_bg_fill_worldscript() : WorldScript("coa_bg_fill_worldscript", { WORLDHOOK_ON_UPDATE }) { }

    void OnUpdate(uint32 diff) override
    {
        ProcessPendingLogins(diff);
        ProcessQueuedBots();
        NudgeStalledQueues(diff);
    }
};
}

void AddSC_coa_bot_bg_fill_script()
{
    new coa_bg_fill_playerscript();
    new coa_bg_fill_worldscript();
}

namespace BotBGFill
{
bool JoinBotToBattlegroundQueue(Player* bot, uint32 bgTypeId_, ChatHandler* handler)
{
    if (!sBattlemasterListStore.LookupEntry(bgTypeId_))
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: invalid battleground type id {}.", bgTypeId_);
        return false;
    }

    BattlegroundTypeId bgTypeId = BattlegroundTypeId(bgTypeId_);
    BattlegroundQueueTypeId bgQueueTypeId = BattlegroundMgr::BGQueueTypeId(bgTypeId, 0);
    if (bgQueueTypeId == BATTLEGROUND_QUEUE_NONE)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: type id {} is an arena, not a battleground.", bgTypeId_);
        return false;
    }

    if (bot->InBattleground())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' is already in a battleground.", bot->GetName());
        return false;
    }

    Battleground* bgTemplate = sBattlegroundMgr->GetBattlegroundTemplate(bgTypeId);
    if (!bgTemplate)
        return false;

    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), bot->GetLevel());
    if (!bracketEntry)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no battleground bracket covers bot '{}' at level {}.",
                bot->GetName(), bot->GetLevel());
        return false;
    }

    if (!bot->HasFreeBattlegroundQueueId())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' has no free battleground queue slot.", bot->GetName());
        return false;
    }
    if (!bot->CanJoinToBattleground(bgTemplate))
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' can't join (deserter debuff?).", bot->GetName());
        return false;
    }
    if (!bot->GetBGAccessByLevel(bgTypeId))
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' isn't eligible for this battleground at its level.", bot->GetName());
        return false;
    }

    BattlegroundQueue& bgQueue = sBattlegroundMgr->GetBattlegroundQueue(bgQueueTypeId);
    GroupQueueInfo* ginfo = bgQueue.AddGroup(bot, nullptr, bgTypeId, bracketEntry, 0, false, false, 0, 0);
    if (!ginfo)
        return false;
    bot->AddBattlegroundQueueId(bgQueueTypeId);

    // Also watch the joining bot itself for its own invite so a bot-initiated test run (unlike
    // a real player, who accepts the invite popup themselves) actually completes the trip into
    // the battleground -- harmless no-op for the real usage pattern (a real human player
    // triggering the auto-fill hook), since their guid was never a tracked bot session to begin
    // with and FindBotPlayer on it will simply never resolve.
    queuedBots.push_back({ bot->GetGUID().GetCounter(), bgTypeId, bgQueueTypeId, bracketEntry->GetBracketId() });

    // The real signal this file's auto-fill hook reacts to -- calling it here is what makes
    // this debug entry point exercise the exact same path a real player's join does.
    sScriptMgr->OnPlayerJoinBG(bot);

    if (handler)
        handler->PSendSysMessage("BotMgr: bot '{}' (team {}) joined the queue for battleground type {}.",
            bot->GetName(), uint32(bot->GetTeamId()), bgTypeId_);
    return true;
}
}
