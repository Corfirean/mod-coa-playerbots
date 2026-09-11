#include "BotMgr.h"
#include <algorithm>
#include "CharacterCache.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "PetDefines.h"
#include "Player.h"
#include "QueryHolder.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

BotMgr* BotMgr::instance()
{
    static BotMgr instance;
    return &instance;
}

WorldSession* BotMgr::FindBotSession(ObjectGuid::LowType charLowGuid) const
{
    for (WorldSession* session : _botSessions)
    {
        if (Player* bot = session->GetPlayer())
            if (bot->GetGUID().GetCounter() == charLowGuid)
                return session;
    }
    return nullptr;
}

void BotMgr::SpawnBot(ObjectGuid::LowType charLowGuid, ChatHandler* handler)
{
    ObjectGuid playerGuid = ObjectGuid::Create<HighGuid::Player>(charLowGuid);

    uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(playerGuid);
    if (!accountId)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no character with guid {} found in the character cache.", charLowGuid);
        return;
    }

    // Real WorldSession, real Player, but a null socket instead of a real
    // client connection — this is the core mechanism proven in pilot/. No
    // bot-aware constructor flag: CoA's WorldSession constructor is used
    // exactly as-is (see mod-coa-playerbots' core-diff-analysis.md for why
    // playerbots-fork's own constructor adds one and why this project
    // deliberately doesn't, yet).
    WorldSession* botSession = new WorldSession(accountId, "", 0x0, nullptr, SEC_PLAYER,
        EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0), sWorld->GetDefaultDbcLocale(), 0, false, false, 0);

    std::shared_ptr<LoginQueryHolder> holder = std::make_shared<LoginQueryHolder>(accountId, playerGuid);
    if (!holder->Initialize())
    {
        LOG_ERROR("module.coa-playerbots", "BotMgr: LoginQueryHolder::Initialize() failed for guid {}", charLowGuid);
        delete botSession;
        return;
    }

    _botSessions.push_back(botSession);

    // Deliberately sWorld->AddQueryHolderCallback, not botSession->AddQueryHolderCallback:
    // a detached, null-socket session is never registered with WorldSessionMgr, so its own
    // (private, World-friend-only) ProcessQueryCallbacks() never gets driven by anything.
    // World::ProcessQueryCallbacks() runs unconditionally every tick regardless of session
    // registration, so callbacks queued on the World-level processor still complete. See
    // pilot/README.md for the full story of how this was found.
    sWorld->AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(holder)).AfterComplete(
        [botSession](SQLQueryHolderBase const& completedHolder)
        {
            botSession->HandlePlayerLoginFromDB(static_cast<LoginQueryHolder const&>(completedHolder));

            if (Player* bot = botSession->GetPlayer())
            {
                LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' ({}) logged in successfully.",
                    bot->GetName(), bot->GetGUID().ToString());
            }
            else
            {
                LOG_ERROR("module.coa-playerbots", "BotMgr: bot login failed, Player is null after HandlePlayerLoginFromDB.");
            }
        });

    if (handler)
        handler->PSendSysMessage("BotMgr: login query issued for guid {}, watch the server log for the result.", charLowGuid);
}

void BotMgr::DoAcceptInvite(WorldSession* session)
{
    // HandleGroupAcceptOpcode only does recvData.read_skip<uint32>() before the real logic
    // (RemoveInvite, validation, Create-if-new, AddMember, BroadcastGroupUpdate) — calling it
    // directly with a minimal padding packet reuses that real logic verbatim instead of
    // duplicating it by hand. Same trick pilot/ already used for login.
    WorldPacket fakePacket;
    fakePacket << uint32(0);
    session->HandleGroupAcceptOpcode(fakePacket);

    // Snap to the leader first, then start following — a real MotionMaster follow
    // generator, the same mechanism pets/NPC escorts use, not anything bot-specific.
    // No combat AI exists yet, so nothing will interrupt this once set; that's a
    // real future problem (re-follow after combat, obstacles, etc.), not this pass.
    Player* bot = session->GetPlayer();
    if (!bot)
    {
        LOG_ERROR("module.coa-playerbots", "BotMgr: DoAcceptInvite: session has no Player after HandleGroupAcceptOpcode.");
        return;
    }

    Group* group = bot->GetGroup();
    if (!group)
    {
        LOG_ERROR("module.coa-playerbots", "BotMgr: DoAcceptInvite: bot '{}' has no group after HandleGroupAcceptOpcode -- AddMember must have failed or returned early.", bot->GetName());
        return;
    }

    LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' is now in a group, leader guid {}.", bot->GetName(), group->GetLeaderGUID().ToString());

    if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
    {
        LOG_INFO("module.coa-playerbots", "BotMgr: resolved leader '{}' (in world: {}).", leader->GetName(), leader->IsInWorld());
        if (leader != bot)
        {
            LOG_INFO("module.coa-playerbots", "BotMgr: teleporting bot '{}' to group leader '{}' and starting follow.",
                bot->GetName(), leader->GetName());
            bot->TeleportTo(leader->GetWorldLocation());

            // TeleportTo() only *requests* the move: for a same-map (near) teleport the
            // actual position isn't applied until the client sends MSG_MOVE_TELEPORT_ACK
            // (WorldSession::HandleMoveTeleportAck -> Player::UpdatePosition); for a
            // cross-map (far) one it's HandleMoveWorldportAck. A bot has no client to
            // ever send that ack, so without this the bot stays semaphore-locked at its
            // old position forever and MoveFollow has nothing real to work from. Found
            // empirically: the teleport call produced no error, but the bot never
            // actually moved. HandleMoveWorldportAck() already has a no-packet
            // "for server-side calls" overload; the near case needs a minimal packed-guid
            // packet built the same way as every other "call the real handler directly"
            // trick this module already uses.
            if (bot->IsBeingTeleportedNear())
            {
                WorldPacket ackPacket;
                ackPacket << bot->GetGUID().WriteAsPacked();
                ackPacket << uint32(0); // flags, unused by the handler
                ackPacket << uint32(0); // time, unused by the handler
                session->HandleMoveTeleportAck(ackPacket);
            }
            else if (bot->IsBeingTeleportedFar())
            {
                session->HandleMoveWorldportAck();
            }

            bot->GetMotionMaster()->MoveFollow(leader, PET_FOLLOW_DIST, bot->GetFollowAngle());
        }
    }
    else
    {
        LOG_ERROR("module.coa-playerbots", "BotMgr: DoAcceptInvite: ObjectAccessor::FindPlayer could not resolve leader guid {}.",
            group->GetLeaderGUID().ToString());
    }
}

void BotMgr::AcceptInvite(ObjectGuid::LowType charLowGuid, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    if (!session)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {} (spawn it first).", charLowGuid);
        return;
    }

    Player* bot = session->GetPlayer();
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot session for guid {} has no Player yet (login still pending?).", charLowGuid);
        return;
    }

    if (!bot->GetGroupInvite())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' has no pending group invite — invite it first from a real client.", bot->GetName());
        return;
    }

    DoAcceptInvite(session);

    if (handler)
        handler->PSendSysMessage("BotMgr: accept-invite issued for bot '{}', check .group list to confirm.", bot->GetName());
}

void BotMgr::DespawnBot(ObjectGuid::LowType charLowGuid, ChatHandler* handler)
{
    auto itr = std::find_if(_botSessions.begin(), _botSessions.end(), [charLowGuid](WorldSession* session)
    {
        Player* bot = session->GetPlayer();
        return bot && bot->GetGUID().GetCounter() == charLowGuid;
    });

    if (itr == _botSessions.end())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {}.", charLowGuid);
        return;
    }

    WorldSession* session = *itr;
    std::string name = session->GetPlayer() ? session->GetPlayer()->GetName() : "?";

    // Same shape as the failure-path cleanup already used in SpawnBot: LogoutPlayer
    // saves+removes the Player from world, but does not delete the WorldSession
    // itself -- that's still on us, same as any other owner of a WorldSession.
    session->LogoutPlayer(true);
    delete session;
    _botSessions.erase(itr);

    LOG_INFO("module.coa-playerbots", "BotMgr: despawned bot '{}' (guid {}).", name, charLowGuid);
    if (handler)
        handler->PSendSysMessage("BotMgr: bot '{}' despawned.", name);
}

void BotMgr::DoRollGreed(WorldSession* session, Roll* roll)
{
    // HandleLootRoll reads itemGUID/itemSlot/rollType then calls Group::CountRollVote,
    // which does the real vote bookkeeping and broadcasts the update via SendLootRoll
    // (already null-socket-tolerant). Same "build the real packet, call the real
    // handler" pattern as DoAcceptInvite/login.
    WorldPacket packet;
    packet << roll->itemGUID;
    packet << uint32(roll->itemSlot);
    packet << uint8(ROLL_GREED);
    session->HandleLootRoll(packet);
}

void BotMgr::Update(uint32 diff)
{
    if (_botSessions.empty())
        return;

    // Auto-accept: checked every tick, not throttled. A bot with a real Player
    // and a pending invite accepts it immediately, same as a human would.
    for (WorldSession* session : _botSessions)
    {
        Player* bot = session->GetPlayer();
        if (bot && bot->GetGroupInvite())
        {
            LOG_INFO("module.coa-playerbots", "BotMgr: auto-accepting pending group invite for bot '{}'.", bot->GetName());
            DoAcceptInvite(session);
        }
    }

    // Loot rolls: also checked every tick. Policy for now is always Greed — real
    // need-eligibility (armor type/class fit) is future AI work, not this milestone.
    for (WorldSession* session : _botSessions)
    {
        Player* bot = session->GetPlayer();
        if (!bot)
            continue;

        Group* group = bot->GetGroup();
        if (!group)
            continue;

        for (Roll* roll : group->GetRolls())
        {
            auto voteItr = roll->playerVote.find(bot->GetGUID());
            if (voteItr != roll->playerVote.end() && voteItr->second == NOT_EMITED_YET)
            {
                LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' rolling Greed on item {} (slot {}).",
                    bot->GetName(), roll->itemid, roll->itemSlot);
                DoRollGreed(session, roll);
            }
        }
    }

    _heartbeatTimer += diff;
    if (_heartbeatTimer < 10000)
        return;
    _heartbeatTimer = 0;

    for (WorldSession* session : _botSessions)
    {
        Player* bot = session->GetPlayer();
        LOG_INFO("module.coa-playerbots", "BotMgr heartbeat: account {} -> {}",
            session->GetAccountId(),
            bot ? (bot->IsInWorld() ? "in world" : "player exists, not in world") : "no player yet (login pending)");
    }
}
