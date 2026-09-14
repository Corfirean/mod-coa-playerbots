#include "BotAI.h"
#include "BotMgr.h"
#include "Chat.h"
#include "CommandScript.h"
#include "ObjectGuid.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include <sstream>
#include <string>
#include <vector>

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
            { "acceptinvite", HandleBotAcceptInviteCommand, rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "invite",       HandleBotInviteCommand,       rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "despawn",      HandleBotDespawnCommand,      rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "listauras",    HandleBotListAurasCommand,    rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "runchat",      HandleBotRunChatCommand,      rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "hasspells",    HandleBotHasSpellsCommand,    rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "attack",       HandleBotAttackCommand,       rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "setrole",      HandleBotSetRoleCommand,      rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "checkrole",    HandleBotCheckRoleCommand,    rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "learnspec",    HandleBotLearnSpecCommand,    rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "follow",       HandleBotFollowCommand,       rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "stay",         HandleBotStayCommand,         rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "stopattack",   HandleBotStopAttackCommand,   rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "kill",         HandleBotKillCommand,         rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "suspend",      HandleBotSuspendCommand,      rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes },
            { "resume",       HandleBotResumeCommand,       rbac::RBAC_PERM_COMMAND_DEBUG, Console::Yes }
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

    static bool HandleBotInviteCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid, std::string targetName)
    {
        sBotMgr->Invite(charLowGuid, targetName, handler);
        return true;
    }

    static bool HandleBotDespawnCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        sBotMgr->DespawnBot(charLowGuid, handler);
        return true;
    }

    static bool HandleBotListAurasCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        sBotMgr->ListAuras(charLowGuid, handler);
        return true;
    }

    static bool HandleBotRunChatCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid, Tail command)
    {
        sBotMgr->RunChatCommand(charLowGuid, std::string(command), handler);
        return true;
    }

    static bool HandleBotHasSpellsCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid, Tail spellIdList)
    {
        std::vector<uint32> spellIds;
        std::string text(spellIdList);
        std::istringstream stream(text);
        uint32 spellId;
        while (stream >> spellId)
            spellIds.push_back(spellId);

        sBotMgr->HasSpells(charLowGuid, spellIds, handler);
        return true;
    }

    static bool HandleBotAttackCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid, Optional<float> range)
    {
        sBotMgr->AttackNearestHostile(charLowGuid, range.value_or(0.0f), handler);
        return true;
    }

    static bool HandleBotSetRoleCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid, std::string roleName)
    {
        sBotMgr->SetRole(charLowGuid, roleName, handler);
        return true;
    }

    static bool HandleBotCheckRoleCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        sBotMgr->CheckRole(charLowGuid, handler);
        return true;
    }

    static bool HandleBotLearnSpecCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid, uint32 specId)
    {
        sBotMgr->LearnSpecialization(charLowGuid, specId, handler);
        return true;
    }

    // Debug/testing entry points for the manual movement commands the bot-control addon
    // drives via docs/addon-protocol.md's FOLLOW/STAY/STOPATTACK verbs -- no console/RA
    // equivalent existed to exercise BotAI::SetManualCommand/StopAttack without a real client
    // addon, same reasoning as .botcmd setrole/checkrole for the role system.
    static bool HandleBotFollowCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        BotAI::SetManualCommand(ObjectGuid::Create<HighGuid::Player>(charLowGuid), BotManualCommand::Follow);
        if (handler)
            handler->PSendSysMessage("BotMgr: guid {} set to manual command Follow.", charLowGuid);
        return true;
    }

    static bool HandleBotStayCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        BotAI::SetManualCommand(ObjectGuid::Create<HighGuid::Player>(charLowGuid), BotManualCommand::Stay);
        if (handler)
            handler->PSendSysMessage("BotMgr: guid {} set to manual command Stay.", charLowGuid);
        return true;
    }

    static bool HandleBotStopAttackCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        BotAI::StopAttack(ObjectGuid::Create<HighGuid::Player>(charLowGuid));
        if (handler)
            handler->PSendSysMessage("BotMgr: guid {} attack stopped, manual command reset.", charLowGuid);
        return true;
    }

    static bool HandleBotKillCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        sBotMgr->Kill(charLowGuid, handler);
        return true;
    }

    // Lets any external tool that spawns a character through .botcmd spawnbot (reusing this
    // module's chassis for convenience, not because it wants an autonomous bot) immediately
    // opt that guid out of BotAI's own combat AI, closing the race window between "character
    // finishes logging in" and "the caller gets around to taking manual control of it" --
    // see BotAI::SetSuspended's doc comment for why this matters (AscensionClassTester hit a
    // real crash from exactly this gap).
    static bool HandleBotSuspendCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        BotAI::SetSuspended(ObjectGuid::Create<HighGuid::Player>(charLowGuid), true);
        if (handler)
            handler->PSendSysMessage("BotMgr: guid {} BotAI suspended.", charLowGuid);
        return true;
    }

    static bool HandleBotResumeCommand(ChatHandler* handler, ObjectGuid::LowType charLowGuid)
    {
        BotAI::SetSuspended(ObjectGuid::Create<HighGuid::Player>(charLowGuid), false);
        if (handler)
            handler->PSendSysMessage("BotMgr: guid {} BotAI resumed.", charLowGuid);
        return true;
    }
};

void AddSC_botcmd_commandscript()
{
    new botcmd_commandscript();
}
