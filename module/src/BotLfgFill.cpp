/*
 * mod-coa-playerbots
 *
 * A real client's Dungeon Finder join (WorldSession::HandleLfgJoinOpcode, LFGHandler.cpp) is
 * just `sLFGMgr->JoinLfg(player, roles, dungeons, comment)` -- a single call, no packet
 * assembly needed to replicate it for a bot. For a *solo* join (not already in a group -- see
 * LFGMgr::JoinLfg's own `if (grp) {...} else { add player to queue directly }` branch) it also
 * skips the group-only role-check phase entirely and goes straight to LFG_STATE_QUEUED, so a
 * solo bot's join is just that one call plus, later, accepting whatever proposal forms
 * (LFGMgr::UpdateProposal, again just a direct call -- HandleLfgProposalResultOpcode does
 * nothing else). Once every member of a proposal accepts, LFGMgr::MakeNewGroup creates the
 * real group *and teleports everyone itself* -- unlike Battlegrounds, there's no port step for
 * this file to replicate at all.
 *
 * The one piece with no existing public accessor: a client learns its own proposal id from the
 * SMSG_LFG_PROPOSAL_UPDATE packet it received, which a bot's null-socket session never gets.
 * Added LFGMgr::GetProposalIdForPlayer(guid) (small patch to the real engine's LFGMgr.h/.cpp,
 * a straightforward scan of the already-existing private ProposalsStore -- this project's
 * mod-ascension-compat sibling already patches core where a module genuinely can't reach
 * something otherwise) so this file can find it the same way UpdateProposal itself expects.
 *
 * The actual feature, and its v1 scope: the moment a *solo* player (real or bot) queues for a
 * *dungeon* (not raid -- composition there is a different, bigger problem) via
 * PLAYERHOOK_CAN_JOIN_LFG (a permission-gate hook this file always allows, using it purely for
 * the "someone just queued" signal), fill whichever of Tank/Healer/3x Damage their own role
 * selection doesn't already cover with bots, so a full 5-man is ready immediately. Deliberately
 * NOT handling group joins (a partial group already has some real members' roles to account
 * for, which aren't knowable until the group-only role-check phase -- meaningfully more state
 * to track for a "nice to have" pass) or raids (5+ role slots, different composition rules
 * entirely).
 *
 * Bot sourcing mirrors BotBattlegroundFill.cpp: prefers an already-online, idle bot of the
 * right faction with the right role (BotAI::GetRole(), this module's own existing
 * spec-derived role detection) over creating a new one. Tank and Healer specifically are NOT
 * auto-created on demand -- unlike a Battleground team slot, a freshly cloned random-class bot
 * has no guarantee of actually being tank/healer-capable, so those slots simply stay open
 * (logged) if no suitable bot is already online; only Damage slots fall back to creating a new
 * bot, since any class defaults to a Dps-shaped BotRole.
 */

#include "BotLfgFill.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "BotSpawnRandom.h"
#include "Config.h"
#include "DBCStores.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"
#include "WorldScript.h"
#include <algorithm>
#include <array>
#include <random>
#include <vector>

namespace
{
constexpr std::array<uint8, 5> ALLIANCE_RACES = { 1, 3, 4, 7, 11 };
constexpr std::array<uint8, 5> HORDE_RACES = { 2, 5, 6, 8, 10 };

// Guards against this file's own bots' JoinLfg calls re-triggering PLAYERHOOK_CAN_JOIN_LFG and
// cascading into filling for a fill-bot's own join.
bool filling = false;

struct PendingLfgBotJoin
{
    ObjectGuid::LowType guid;
    uint8 roleBit;
    lfg::LfgDungeonSet dungeons;
    uint32 ageMs = 0;
};
std::vector<PendingLfgBotJoin> pendingLogins;

// A bot that has joined the LFG queue -- watched every tick until it either enters
// LFG_STATE_PROPOSAL (accepted immediately) or leaves LFG_STATE_NONE some other way (dropped).
std::vector<ObjectGuid::LowType> queuedBots;

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

bool BotMatchesRole(Player* bot, uint8 roleBit)
{
    BotRole role = BotAI::GetRole(bot->GetGUID());
    switch (roleBit)
    {
        case lfg::PLAYER_ROLE_TANK:   return role == BotRole::Tank;
        case lfg::PLAYER_ROLE_HEALER: return role == BotRole::Healer;
        case lfg::PLAYER_ROLE_DAMAGE: return role == BotRole::Dps || role == BotRole::Support;
        default: return false;
    }
}

// Returns true only if the bot's LFG state actually became QUEUED -- JoinLfg silently returns
// without changing any state at all when its own eligibility checks reject the join (e.g.
// LFG_JOIN_NOT_MEET_REQS for a dungeon this specific character is locked out of, or
// LFG_JOIN_DISCONNECTED for a bot in some other transient bad state) instead of throwing or
// reporting a result this file can inspect directly, so the *state* is the only reliable signal
// that anything actually happened. Confirmed live: without this check, a rejected join looked
// identical to a successful one from this file's side, and a candidate that keeps failing (still
// LFG_STATE_NONE) could get redundantly re-selected on a later same-pass role slot instead of
// this file trying a different bot.
bool JoinBotToLfg(Player* bot, uint8 roleBit, lfg::LfgDungeonSet dungeons)
{
    filling = true;
    sLFGMgr->JoinLfg(bot, roleBit, dungeons, "");
    filling = false;

    if (sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_QUEUED)
    {
        LOG_INFO("module.coa-playerbots",
            "BotLfgFill: bot '{}' (role {}) was rejected from the LFG queue (not eligible for this dungeon?).",
            bot->GetName(), roleBit);
        return false;
    }

    LOG_INFO("module.coa-playerbots", "BotLfgFill: bot '{}' (role {}) joined the LFG queue.",
        bot->GetName(), roleBit);
    queuedBots.push_back(bot->GetGUID().GetCounter());
    return true;
}

void FillRole(uint8 roleBit, TeamId team, lfg::LfgDungeonSet const& dungeons, ObjectGuid excludeGuid,
    std::vector<ObjectGuid>& attemptedThisPass)
{
    for (Player* bot : sBotMgr->GetOnlineBots())
    {
        // Never re-select the player who triggered this fill pass -- their own LFG state
        // hasn't been set to anything but LFG_STATE_NONE yet at this point in JoinLfg (the
        // hook fires before that happens), so without this exclusion a triggering player who
        // also happens to match one of the *other* needed roles (e.g. a bot-initiated test
        // join whose class defaults to a tank-shaped BotRole) gets redundantly re-queued for
        // that role too, silently overwriting -- not adding to -- their own original join
        // (JoinLfg's own re-join handling removes and replaces a still-queued entry).
        if (bot->GetGUID() == excludeGuid)
            continue;
        // Skip anything already tried this pass, successful or not -- see JoinBotToLfg's
        // comment for why a rejected candidate must not be retried indefinitely within the
        // same fill pass.
        if (std::find(attemptedThisPass.begin(), attemptedThisPass.end(), bot->GetGUID()) != attemptedThisPass.end())
            continue;
        if (bot->GetTeamId() != team || !bot->IsAlive())
            continue;
        // A bot already in a group (e.g. left over from an unrelated earlier test) takes
        // JoinLfg's group branch instead of the solo one, which has its own membership/online
        // checks this file isn't trying to satisfy -- cheap to skip up front rather than
        // find out via a rejected join.
        if (bot->GetGroup())
            continue;
        if (sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE)
            continue;
        if (!BotMatchesRole(bot, roleBit))
            continue;

        attemptedThisPass.push_back(bot->GetGUID());
        if (JoinBotToLfg(bot, roleBit, dungeons))
            return;
        // Rejected -- keep scanning for a different candidate instead of giving up the slot.
    }

    if (roleBit != lfg::PLAYER_ROLE_DAMAGE)
    {
        LOG_INFO("module.coa-playerbots",
            "BotLfgFill: no online {} bot available for team {}, that slot stays open.",
            roleBit == lfg::PLAYER_ROLE_TANK ? "tank" : "healer", uint32(team));
        return;
    }

    uint8 race = PickRaceForTeam(team);
    ObjectGuid::LowType newGuid = BotSpawn::CreateOneRandomBot(race, nullptr);
    if (!newGuid)
    {
        LOG_ERROR("module.coa-playerbots", "BotLfgFill: could not create a reserve dps bot for team {}.", uint32(team));
        return;
    }
    sBotMgr->SpawnBot(newGuid, nullptr);
    pendingLogins.push_back({ newGuid, roleBit, dungeons });
}

void ProcessPendingLogins(uint32 diff)
{
    constexpr uint32 PENDING_LOGIN_TIMEOUT_MS = 30000;
    for (auto it = pendingLogins.begin(); it != pendingLogins.end();)
    {
        it->ageMs += diff;
        if (Player* bot = sBotMgr->FindBotPlayer(it->guid))
        {
            JoinBotToLfg(bot, it->roleBit, it->dungeons);
            it = pendingLogins.erase(it);
        }
        else if (it->ageMs > PENDING_LOGIN_TIMEOUT_MS)
        {
            LOG_ERROR("module.coa-playerbots", "BotLfgFill: reserve bot (guid {}) never logged in, giving up.", it->guid);
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
        Player* bot = sBotMgr->FindBotPlayer(*it);
        if (!bot)
        {
            it = queuedBots.erase(it);
            continue;
        }
        lfg::LfgState state = sLFGMgr->GetState(bot->GetGUID());
        if (state == lfg::LFG_STATE_PROPOSAL)
        {
            uint32 proposalId = sLFGMgr->GetProposalIdForPlayer(bot->GetGUID());
            if (proposalId)
            {
                sLFGMgr->UpdateProposal(proposalId, bot->GetGUID(), true);
                LOG_INFO("module.coa-playerbots", "BotLfgFill: bot '{}' accepted LFG proposal {}.",
                    bot->GetName(), proposalId);
            }
            it = queuedBots.erase(it);
            continue;
        }
        if (state == lfg::LFG_STATE_NONE)
        {
            // Left the queue some other way (kicked, timed out, etc.) -- stop watching it.
            it = queuedBots.erase(it);
            continue;
        }
        ++it;
    }
}

class coa_lfg_fill_playerscript : public PlayerScript
{
public:
    coa_lfg_fill_playerscript() : PlayerScript("coa_lfg_fill_playerscript", { PLAYERHOOK_CAN_JOIN_LFG }) { }

    bool OnPlayerCanJoinLfg(Player* player, uint8 roles, std::set<uint32>& dungeons, std::string const& /*comment*/) override
    {
        if (filling || dungeons.empty() || !sConfigMgr->GetOption<bool>("CoaBots.LfgFill.Enable", true))
            return true;
        if (player->GetGroup())
            return true; // v1 scope: solo joins only -- see header comment

        LFGDungeonEntry const* firstDungeon = sLFGDungeonStore.LookupEntry(*dungeons.begin());
        if (!firstDungeon || firstDungeon->TypeID == lfg::LFG_TYPE_RAID)
            return true; // raids out of scope -- composition there is a different problem

        lfg::LfgDungeonSet dungeonsCopy(dungeons.begin(), dungeons.end());
        TeamId team = player->GetTeamId();

        ObjectGuid initiator = player->GetGUID();
        std::vector<ObjectGuid> attemptedThisPass;
        if (!(roles & lfg::PLAYER_ROLE_TANK))
            FillRole(lfg::PLAYER_ROLE_TANK, team, dungeonsCopy, initiator, attemptedThisPass);
        if (!(roles & lfg::PLAYER_ROLE_HEALER))
            FillRole(lfg::PLAYER_ROLE_HEALER, team, dungeonsCopy, initiator, attemptedThisPass);

        uint32 dpsNeeded = (roles & lfg::PLAYER_ROLE_DAMAGE) ? 2 : 3;
        for (uint32 i = 0; i < dpsNeeded; ++i)
            FillRole(lfg::PLAYER_ROLE_DAMAGE, team, dungeonsCopy, initiator, attemptedThisPass);

        return true;
    }
};

class coa_lfg_fill_worldscript : public WorldScript
{
public:
    coa_lfg_fill_worldscript() : WorldScript("coa_lfg_fill_worldscript", { WORLDHOOK_ON_UPDATE }) { }

    void OnUpdate(uint32 diff) override
    {
        ProcessPendingLogins(diff);
        ProcessQueuedBots();
    }
};
}

void AddSC_coa_bot_lfg_fill_script()
{
    new coa_lfg_fill_playerscript();
    new coa_lfg_fill_worldscript();
}

namespace BotLfgFill
{
void WatchForProposal(Player* bot)
{
    queuedBots.push_back(bot->GetGUID().GetCounter());
}
}
