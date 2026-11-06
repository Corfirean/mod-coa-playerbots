/*
 * mod-coa-playerbots
 *
 * Universal Multi-Realm Ruleset & Identity Detection implementation
 */

#include "engine/BotRuleset.h"
#include "Config.h"
#include "Player.h"
#include <string>

namespace BotAI
{
    bool IsCoAClass(uint8 classId)
    {
        return classId >= 12 && classId <= 32;
    }

    bool IsHeroClass(uint8 classId)
    {
        return classId == 10;
    }

    bool IsStockClass(uint8 classId)
    {
        return (classId >= 1 && classId <= 9) || classId == 11;
    }

    BotRuleset GetServerRuleset()
    {
        std::string classModel = sConfigMgr->GetOption<std::string>("CoA.ClassModel", "coa");
        std::string modes = sConfigMgr->GetOption<std::string>("CoAChallenges.GameModes.Realm", "");

        if (classModel.find("hero") != std::string::npos || modes.find("WildCard") != std::string::npos)
            return BotRuleset::Wildcard;

        if (classModel.find("wcr") != std::string::npos)
            return BotRuleset::Reborn;

        return BotRuleset::CoA;
    }

    BotRuleset GetBotRuleset(Player const* bot)
    {
        if (!bot)
            return GetServerRuleset();

        uint8 cls = bot->getClass();
        if (IsHeroClass(cls))
            return BotRuleset::Wildcard;
        if (IsCoAClass(cls))
            return BotRuleset::CoA;
        if (IsStockClass(cls))
            return BotRuleset::Reborn;

        return GetServerRuleset();
    }

    bool IsCoA(Player const* bot)
    {
        return GetBotRuleset(bot) == BotRuleset::CoA;
    }

    bool IsWildcard(Player const* bot)
    {
        return GetBotRuleset(bot) == BotRuleset::Wildcard;
    }

    bool IsReborn(Player const* bot)
    {
        return GetBotRuleset(bot) == BotRuleset::Reborn;
    }
}
