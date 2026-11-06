/*
 * mod-coa-playerbots
 *
 * Universal Multi-Realm Ruleset & Identity Detection implementation
 */

#include "engine/BotRuleset.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "SharedDefines.h"
#include "World.h"
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
        // 1. Check RealmID configuration
        uint32 realmId = sConfigMgr->GetOption<uint32>("RealmID", 1);
        if (realmId == 2)
            return BotRuleset::Wildcard;
        if (realmId == 3)
            return BotRuleset::Reborn;

        // 2. Check CoA.ClassModel configuration
        std::string classModel = sConfigMgr->GetOption<std::string>("CoA.ClassModel", "coa");
        if (classModel.find("hero") != std::string::npos)
            return BotRuleset::Wildcard;
        if (classModel.find("wcr") != std::string::npos)
            return BotRuleset::Reborn;

        // 3. Check realm challenges / game modes
        std::string modes = sConfigMgr->GetOption<std::string>("CoAChallenges.GameModes.Realm", "");
        if (modes.find("WildCard") != std::string::npos || modes.find("wildcard") != std::string::npos)
            return BotRuleset::Wildcard;

        return BotRuleset::CoA;
    }

    BotRuleset GetBotRuleset(Player const* bot)
    {
        BotRuleset serverRuleset = GetServerRuleset();
        if (!bot)
            return serverRuleset;

        // Server ruleset is the primary authority
        if (serverRuleset == BotRuleset::Wildcard)
            return BotRuleset::Wildcard;

        if (serverRuleset == BotRuleset::Reborn)
            return BotRuleset::Reborn;

        // On a CoA server, characters operate under CoA ruleset by default.
        // If a character specifically possesses Hero class (10), treat as Wildcard.
        if (IsHeroClass(bot->getClass()))
            return BotRuleset::Wildcard;

        // Never let a stock class accidentally trigger Reborn mechanics on a CoA realm
        return BotRuleset::CoA;
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
