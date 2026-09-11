#include "PilotBotMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "Player.h"
#include "QueryHolder.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"

PilotBotMgr* PilotBotMgr::instance()
{
    static PilotBotMgr instance;
    return &instance;
}

void PilotBotMgr::SpawnPilotBot(ObjectGuid::LowType charLowGuid, ChatHandler* handler)
{
    ObjectGuid playerGuid = ObjectGuid::Create<HighGuid::Player>(charLowGuid);

    uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(playerGuid);
    if (!accountId)
    {
        if (handler)
            handler->PSendSysMessage("PilotBotMgr: no character with guid {} found in the character cache.", charLowGuid);
        return;
    }

    // Real WorldSession, real Player, but a null socket instead of a real
    // client connection — this is the core mechanism being piloted. No
    // bot-aware constructor flag: CoA's WorldSession constructor is used
    // exactly as-is (see mod-coa-playerbots' core-diff-analysis.md for why
    // playerbots-fork's own constructor adds one and why this pilot
    // deliberately doesn't yet).
    WorldSession* botSession = new WorldSession(accountId, "", 0x0, nullptr, SEC_PLAYER,
        EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0), sWorld->GetDefaultDbcLocale(), 0, false, false, 0);

    std::shared_ptr<LoginQueryHolder> holder = std::make_shared<LoginQueryHolder>(accountId, playerGuid);
    if (!holder->Initialize())
    {
        LOG_ERROR("module.pilot", "PilotBotMgr: LoginQueryHolder::Initialize() failed for guid {}", charLowGuid);
        delete botSession;
        return;
    }

    _pilotSessions.push_back(botSession);

    // Deliberately sWorld->AddQueryHolderCallback, not botSession->AddQueryHolderCallback:
    // a detached, null-socket session is never registered with WorldSessionMgr, so its own
    // (private, World-friend-only) ProcessQueryCallbacks() never gets driven by anything.
    // World::ProcessQueryCallbacks() runs unconditionally every tick regardless of session
    // registration, so callbacks queued on the World-level processor still complete. This
    // needed a small, now-verified-necessary addition to World/IWorld (see AGENTS.md).
    sWorld->AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(holder)).AfterComplete(
        [botSession](SQLQueryHolderBase const& completedHolder)
        {
            botSession->HandlePlayerLoginFromDB(static_cast<LoginQueryHolder const&>(completedHolder));

            if (Player* bot = botSession->GetPlayer())
            {
                LOG_INFO("module.pilot", "PilotBotMgr: pilot bot '{}' ({}) logged in successfully.",
                    bot->GetName(), bot->GetGUID().ToString());
            }
            else
            {
                LOG_ERROR("module.pilot", "PilotBotMgr: pilot bot login failed, Player is null after HandlePlayerLoginFromDB.");
            }
        });

    if (handler)
        handler->PSendSysMessage("PilotBotMgr: login query issued for guid {}, watch the server log for the result.", charLowGuid);
}

void PilotBotMgr::Update(uint32 diff)
{
    if (_pilotSessions.empty())
        return;

    _heartbeatTimer += diff;
    if (_heartbeatTimer < 10000)
        return;
    _heartbeatTimer = 0;

    for (WorldSession* session : _pilotSessions)
    {
        Player* bot = session->GetPlayer();
        LOG_INFO("module.pilot", "PilotBotMgr heartbeat: account {} -> {}",
            session->GetAccountId(),
            bot ? (bot->IsInWorld() ? "in world" : "player exists, not in world") : "no player yet (login pending)");
    }
}
