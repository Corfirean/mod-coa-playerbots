#include "BotMgr.h"
#include "Chat.h"
#include "CommandScript.h"
#include "ObjectGuid.h"
#include "RBAC.h"
#include "ScriptMgr.h"

using namespace Acore::ChatCommands;

class botcmd_commandscript : public CommandScript
{
public:
    botcmd_commandscript() : CommandScript("botcmd_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable botcmdCommandTable =
        {
            { "spawnbot",     HandleBotSpawnCommand,        rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "acceptinvite", HandleBotAcceptInviteCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "botcmd", botcmdCommandTable }
        };

        return commandTable;
    }

    static bool HandleBotSpawnCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        sBotMgr->SpawnBot(charLowGuid, handler);
        return true;
    }

    static bool HandleBotAcceptInviteCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        sBotMgr->AcceptInvite(charLowGuid, handler);
        return true;
    }
};

void AddSC_botcmd_commandscript()
{
    new botcmd_commandscript();
}
