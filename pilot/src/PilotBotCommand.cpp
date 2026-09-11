#include "Chat.h"
#include "CommandScript.h"
#include "ObjectGuid.h"
#include "PilotBotMgr.h"
#include "RBAC.h"
#include "ScriptMgr.h"

using namespace Acore::ChatCommands;

class pilot_commandscript : public CommandScript
{
public:
    pilot_commandscript() : CommandScript("pilot_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable pilotCommandTable =
        {
            { "spawnbot", HandlePilotSpawnBotCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "pilot", pilotCommandTable }
        };

        return commandTable;
    }

    static bool HandlePilotSpawnBotCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        sPilotBotMgr->SpawnPilotBot(charLowGuid, handler);
        return true;
    }
};

void AddSC_pilot_commandscript()
{
    new pilot_commandscript();
}
