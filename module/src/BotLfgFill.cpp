/*
 * mod-coa-playerbots
 *
 * BotLfgFill: Intelligent auto-fill for Dungeon Finder queues.
 * Hardened for Round 2.1:
 * - Strict intent gating (BOT_FILL only; MATCHMAKING & CURRENT_PARTY bypass bots)
 * - Multi-role player assignment solver
 * - Partial group role-aware fill (1 Tank, 1 Healer, 3 DPS)
 * - Operation ownership and cleanup tracking (LfgFillOperationId)
 * - Bot creation attempt limits
 * - GlobalScript registration with HasLfgAutoFillProvider()
 */

#include "engine/BotDebugLog.h"
#include "BotLfgFill.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "BotSpawnRandom.h"
#include "ClassSpecRoles.h"
#include "Config.h"
#include "DBCStores.h"
#include "Group.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldScript.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <random>
#include <unordered_map>
#include <unordered_set>
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
    uint64 opId = 0;
};
std::vector<PendingLfgBotJoin> pendingLogins;

// A bot that has joined the LFG queue -- watched every tick until it either enters
// LFG_STATE_PROPOSAL (accepted immediately) or leaves LFG_STATE_NONE some other way (dropped).
std::vector<ObjectGuid::LowType> queuedBots;

struct LfgFillOperation
{
    uint64 id{0};
    ObjectGuid initiator;
    ObjectGuid groupGuid;
    std::unordered_set<ObjectGuid::LowType> spawnedBotGuids;
    std::unordered_set<ObjectGuid::LowType> queuedBotGuids;
    uint32 creationAttempts{0};
    uint32 ageMs{0};
};

std::atomic<uint64> s_nextOpId{1};
std::unordered_map<uint64, LfgFillOperation> activeOperations;
std::unordered_map<ObjectGuid, uint64> initiatorToOpId;

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

bool JoinBotToLfg(Player* bot, uint8 roleBit, lfg::LfgDungeonSet dungeons, uint64 opId)
{
    filling = true;
    sLFGMgr->JoinLfg(bot, roleBit, dungeons, "");
    filling = false;

    if (sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_QUEUED)
    {
        LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
            "BotLfgFill: bot '{}' (role {}) was rejected from the LFG queue (not eligible for this dungeon?).",
            bot->GetName(), roleBit);
        return false;
    }

    LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "BotLfgFill: bot '{}' (role {}) joined the LFG queue (op {}).",
        bot->GetName(), roleBit, opId);
    queuedBots.push_back(bot->GetGUID().GetCounter());

    if (opId > 0)
    {
        auto it = activeOperations.find(opId);
        if (it != activeOperations.end())
            it->second.queuedBotGuids.insert(bot->GetGUID().GetCounter());
    }

    return true;
}

void FillRole(uint8 roleBit, TeamId team, lfg::LfgDungeonSet const& dungeons, ObjectGuid excludeGuid,
    std::vector<ObjectGuid>& attemptedThisPass, uint64 opId)
{
    std::vector<Player*> eligible;
    for (Player* bot : sBotMgr->GetOnlineBots())
    {
        if (bot->GetGUID() == excludeGuid)
            continue;
        if (bot->GetTeamId() != team || !bot->IsAlive())
            continue;
        if (bot->GetGroup())
            continue;
        if (sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE)
            continue;
        eligible.push_back(bot);
    }
    std::shuffle(eligible.begin(), eligible.end(), Rng());

    auto tryJoin = [&](Player* bot) -> bool
    {
        if (std::find(attemptedThisPass.begin(), attemptedThisPass.end(), bot->GetGUID()) != attemptedThisPass.end())
            return false;
        attemptedThisPass.push_back(bot->GetGUID());
        return JoinBotToLfg(bot, roleBit, dungeons, opId);
    };

    // Pass 1: an idle bot already spec'd for this role.
    for (Player* bot : eligible)
    {
        if (BotMatchesRole(bot, roleBit) && tryJoin(bot))
            return;
    }

    // Pass 2, Tank/Healer only: respec a random idle bot of some OTHER role into this one.
    if (roleBit == lfg::PLAYER_ROLE_TANK || roleBit == lfg::PLAYER_ROLE_HEALER)
    {
        BotRole neededRole = (roleBit == lfg::PLAYER_ROLE_TANK) ? BotRole::Tank : BotRole::Healer;
        for (Player* bot : eligible)
        {
            if (std::find(attemptedThisPass.begin(), attemptedThisPass.end(), bot->GetGUID()) != attemptedThisPass.end())
                continue;
            uint32 spec = BotAI::FindSpecForRole(bot->getClass(), neededRole);
            if (!spec)
                continue;

            LOG_DEBUG("module.coa-playerbots",
                "BotLfgFill: respeccing bot '{}' to spec {} for {} role.",
                bot->GetName(), spec, roleBit == lfg::PLAYER_ROLE_TANK ? "tank" : "healer");
            sBotMgr->LearnSpecialization(bot->GetGUID().GetCounter(), spec, nullptr);

            if (tryJoin(bot))
                return;
        }

        LOG_DEBUG("module.coa-playerbots",
            "BotLfgFill: no online {} bot available for team {} (none to respec either).",
            roleBit == lfg::PLAYER_ROLE_TANK ? "tank" : "healer", uint32(team));
        return;
    }

    // Pass 3: Damage role. Try every remaining eligible candidate.
    for (Player* bot : eligible)
    {
        if (tryJoin(bot))
            return;
    }

    // Pass 4: Spawn a new random bot if attempts allow.
    auto itOp = activeOperations.find(opId);
    if (itOp != activeOperations.end() && itOp->second.creationAttempts >= 5)
    {
        LOG_WARN("module.coa-playerbots", "BotLfgFill: op {} reached creation limit (5), aborting new bot spawn.", opId);
        return;
    }

    uint8 race = PickRaceForTeam(team);
    ObjectGuid::LowType newGuid = BotSpawn::CreateOneRandomBot(race, nullptr);
    if (!newGuid)
    {
        LOG_ERROR("module.coa-playerbots", "BotLfgFill: could not create reserve dps bot for team {}.", uint32(team));
        return;
    }

    if (itOp != activeOperations.end())
    {
        ++itOp->second.creationAttempts;
        itOp->second.spawnedBotGuids.insert(newGuid);
    }

    sBotMgr->SpawnBot(newGuid, nullptr);
    pendingLogins.push_back({ newGuid, roleBit, dungeons, 0, opId });
}

void ProcessPendingLogins(uint32 diff)
{
    constexpr uint32 PENDING_LOGIN_TIMEOUT_MS = 30000;
    for (auto it = pendingLogins.begin(); it != pendingLogins.end();)
    {
        it->ageMs += diff;
        if (Player* bot = sBotMgr->FindBotPlayer(it->guid))
        {
            JoinBotToLfg(bot, it->roleBit, it->dungeons, it->opId);
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
        ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(*it);
        Player* bot = ObjectAccessor::FindConnectedPlayer(guid);
        if (!bot)
        {
            it = queuedBots.erase(it);
            continue;
        }

        if (bot->IsBeingTeleportedNear() || bot->IsBeingTeleportedFar())
            sBotMgr->QueueTeleportAck(bot->GetSession());

        lfg::LfgState state = sLFGMgr->GetState(guid);
        if (state == lfg::LFG_STATE_PROPOSAL)
        {
            uint32 proposalId = sLFGMgr->GetProposalIdForPlayer(guid);
            if (proposalId)
            {
                sLFGMgr->UpdateProposal(proposalId, guid, true);
                LOG_DEBUG(BotAI::BotDebugLog::LoggerName(guid), "BotLfgFill: bot '{}' accepted LFG proposal {}.",
                    bot->GetName(), proposalId);
            }
            it = queuedBots.erase(it);
            continue;
        }

        if (state != lfg::LFG_STATE_QUEUED)
        {
            it = queuedBots.erase(it);
            continue;
        }

        ++it;
    }
}

std::unordered_set<ObjectGuid::LowType> roleCheckAnswered;
std::unordered_set<ObjectGuid::LowType> proposalAccepted;

uint8 RoleBitFor(Player* bot)
{
    switch (BotAI::GetRole(bot->GetGUID()))
    {
        case BotRole::Tank:   return lfg::PLAYER_ROLE_TANK;
        case BotRole::Healer: return lfg::PLAYER_ROLE_HEALER;
        default:              return lfg::PLAYER_ROLE_DAMAGE;
    }
}

void ProcessGroupRoleChecks()
{
    for (Player* bot : sBotMgr->GetOnlineBots())
    {
        if (!bot->GetGroup())
            continue;

        if (bot->IsBeingTeleportedNear() || bot->IsBeingTeleportedFar())
            sBotMgr->QueueTeleportAck(bot->GetSession());

        ObjectGuid::LowType lowGuid = bot->GetGUID().GetCounter();
        lfg::LfgState state = sLFGMgr->GetState(bot->GetGUID());

        if (state == lfg::LFG_STATE_PROPOSAL)
        {
            if (proposalAccepted.count(lowGuid))
                continue;
            uint32 proposalId = sLFGMgr->GetProposalIdForPlayer(bot->GetGUID());
            if (proposalId)
            {
                sLFGMgr->UpdateProposal(proposalId, bot->GetGUID(), true);
                proposalAccepted.insert(lowGuid);
                LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "BotLfgFill: bot '{}' (grouped) accepted LFG proposal {}.",
                    bot->GetName(), proposalId);
            }
            continue;
        }
        proposalAccepted.erase(lowGuid);

        if (state != lfg::LFG_STATE_ROLECHECK)
        {
            roleCheckAnswered.erase(lowGuid);
            continue;
        }
        if (roleCheckAnswered.count(lowGuid))
            continue;

        uint8 roleBit = RoleBitFor(bot);
        WorldPacket packet;
        packet << roleBit;
        bot->GetSession()->HandleLfgSetRolesOpcode(packet);
        roleCheckAnswered.insert(lowGuid);

        LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "BotLfgFill: bot '{}' answered group LFG role check as role {}.",
            bot->GetName(), roleBit);
    }
}

void CleanupOperation(uint64 opId)
{
    auto it = activeOperations.find(opId);
    if (it == activeOperations.end())
        return;

    for (ObjectGuid::LowType lowGuid : it->second.queuedBotGuids)
    {
        ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(lowGuid);
        if (sLFGMgr->GetState(guid) == lfg::LFG_STATE_QUEUED)
        {
            sLFGMgr->LeaveLfg(guid);
        }
    }

    initiatorToOpId.erase(it->second.initiator);
    activeOperations.erase(it);
}

void ProcessOperationsTimeout(uint32 diff)
{
    constexpr uint32 OPERATION_TIMEOUT_MS = 60000;
    std::vector<uint64> toCleanup;
    for (auto& [id, op] : activeOperations)
    {
        op.ageMs += diff;
        // Check if initiator is still in queue
        if (sLFGMgr->GetState(op.initiator) == lfg::LFG_STATE_NONE || op.ageMs > OPERATION_TIMEOUT_MS)
        {
            toCleanup.push_back(id);
        }
    }
    for (uint64 id : toCleanup)
        CleanupOperation(id);
}

class coa_lfg_fill_playerscript : public PlayerScript
{
public:
    coa_lfg_fill_playerscript() : PlayerScript("coa_lfg_fill_playerscript", { PLAYERHOOK_CAN_JOIN_LFG, PLAYERHOOK_ON_LOGOUT }) { }

    void OnPlayerLogout(Player* player) override
    {
        if (!player)
            return;
        auto it = initiatorToOpId.find(player->GetGUID());
        if (it != initiatorToOpId.end())
        {
            CleanupOperation(it->second);
        }
    }

    bool OnPlayerCanJoinLfg(Player* player, uint8 roles, std::set<uint32>& dungeons, std::string const& /*comment*/) override
    {
        if (filling || dungeons.empty() || !sConfigMgr->GetOption<bool>("CoaBots.LfgFill.Enable", true))
            return true;

        LFGDungeonEntry const* firstDungeon = sLFGDungeonStore.LookupEntry(*dungeons.begin());
        if (!firstDungeon || firstDungeon->TypeID == lfg::LFG_TYPE_RAID)
            return true; // Raids out of scope

        lfg::LfgQueuePolicy policy;
        sScriptMgr->OnResolveLfgQueuePolicy(player->GetGUID(), policy);

        // Strict intent gating: only BOT_FILL triggers bot spawning!
        if (policy.compositionMode != lfg::LfgCompositionMode::BOT_FILL)
            return true;

        uint64 const opId = s_nextOpId++;
        LfgFillOperation op;
        op.id = opId;
        op.initiator = player->GetGUID();
        if (player->GetGroup())
            op.groupGuid = player->GetGroup()->GetGUID();
        activeOperations[opId] = op;
        initiatorToOpId[op.initiator] = opId;

        lfg::LfgDungeonSet dungeonsCopy(dungeons.begin(), dungeons.end());
        TeamId team = player->GetTeamId();
        ObjectGuid initiator = player->GetGUID();
        std::vector<ObjectGuid> attemptedThisPass;

        if (Group* grp = player->GetGroup())
        {
            if (grp->GetMembersCount() >= 5)
                return true;

            // Multi-role deterministic assignment:
            // Check roles covered by real group members
            bool hasTank = false;
            bool hasHealer = false;
            uint32 dpsAssigned = 0;

            struct MemberRoleInfo
            {
                Player* player;
                uint8 roles;
                uint8 numRoles;
            };
            std::vector<MemberRoleInfo> members;
            for (GroupReference* itr = grp->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                if (Player* member = itr->GetSource())
                {
                    uint8 r = sLFGMgr->GetRoles(member->GetGUID());
                    if (r == 0)
                        r = lfg::PLAYER_ROLE_DAMAGE;
                    uint8 count = 0;
                    if (r & lfg::PLAYER_ROLE_TANK) ++count;
                    if (r & lfg::PLAYER_ROLE_HEALER) ++count;
                    if (r & lfg::PLAYER_ROLE_DAMAGE) ++count;
                    members.push_back({ member, r, count });
                }
            }

            // Sort members by specificity (fewer choices first)
            std::sort(members.begin(), members.end(), [](MemberRoleInfo const& a, MemberRoleInfo const& b)
            {
                return a.numRoles < b.numRoles;
            });

            // Pass 1: Assign Tank
            for (auto const& m : members)
            {
                if (!hasTank && (m.roles & lfg::PLAYER_ROLE_TANK))
                {
                    hasTank = true;
                    continue;
                }
            }

            // Pass 2: Assign Healer
            for (auto const& m : members)
            {
                if (!hasHealer && (m.roles & lfg::PLAYER_ROLE_HEALER))
                {
                    hasHealer = true;
                    continue;
                }
            }

            // Remaining slots filled by real DPS
            uint32 realMembers = static_cast<uint32>(members.size());
            uint32 realDps = 0;
            if (realMembers > (hasTank ? 1 : 0) + (hasHealer ? 1 : 0))
                realDps = realMembers - ((hasTank ? 1 : 0) + (hasHealer ? 1 : 0));
            dpsAssigned = realDps;

            if (!hasTank)
                FillRole(lfg::PLAYER_ROLE_TANK, team, dungeonsCopy, initiator, attemptedThisPass, opId);
            if (!hasHealer)
                FillRole(lfg::PLAYER_ROLE_HEALER, team, dungeonsCopy, initiator, attemptedThisPass, opId);

            uint32 const dpsNeeded = (dpsAssigned >= 3) ? 0 : (3 - dpsAssigned);
            for (uint32 i = 0; i < dpsNeeded; ++i)
                FillRole(lfg::PLAYER_ROLE_DAMAGE, team, dungeonsCopy, initiator, attemptedThisPass, opId);

            return true;
        }

        // Solo player bot fill
        bool const isTank = (roles & lfg::PLAYER_ROLE_TANK) != 0;
        bool const isHealer = (roles & lfg::PLAYER_ROLE_HEALER) != 0;
        bool const isDps = (roles & lfg::PLAYER_ROLE_DAMAGE) != 0;

        if (!isTank)
            FillRole(lfg::PLAYER_ROLE_TANK, team, dungeonsCopy, initiator, attemptedThisPass, opId);
        if (!isHealer)
            FillRole(lfg::PLAYER_ROLE_HEALER, team, dungeonsCopy, initiator, attemptedThisPass, opId);

        uint32 dpsNeeded = isDps ? 2 : 3;
        for (uint32 i = 0; i < dpsNeeded; ++i)
            FillRole(lfg::PLAYER_ROLE_DAMAGE, team, dungeonsCopy, initiator, attemptedThisPass, opId);

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
        ProcessGroupRoleChecks();
        ProcessOperationsTimeout(diff);
    }
};

class coa_lfg_fill_globalscript : public GlobalScript
{
public:
    coa_lfg_fill_globalscript() : GlobalScript("coa_lfg_fill_globalscript") { }

    bool HasLfgAutoFillProvider() const override
    {
        return sConfigMgr->GetOption<bool>("CoaBots.LfgFill.Enable", true);
    }
};
}

void AddSC_coa_bot_lfg_fill_script()
{
    new coa_lfg_fill_playerscript();
    new coa_lfg_fill_worldscript();
    new coa_lfg_fill_globalscript();
}

namespace BotLfgFill
{
void WatchForProposal(Player* bot)
{
    queuedBots.push_back(bot->GetGUID().GetCounter());
}
}
