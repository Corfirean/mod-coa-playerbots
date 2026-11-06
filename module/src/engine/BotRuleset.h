/*
 * mod-coa-playerbots
 *
 * Universal Multi-Realm Ruleset & Identity Detection:
 * - Conquest of Azeroth (CoA): custom classes 12-32, 1-tree retail specs
 * - Wildcard: Classless Hero (class 10), pre-made archetypes
 * - Warcraft Reborn: classic classes 1-9, 11, twin spells, mystic enchants
 */

#ifndef COA_PLAYERBOTS_BOT_RULESET_H
#define COA_PLAYERBOTS_BOT_RULESET_H

#include "Define.h"

class Player;

namespace BotAI
{
    enum class BotRuleset : uint8
    {
        CoA = 0,
        Wildcard = 1,
        Reborn = 2
    };

    BotRuleset GetServerRuleset();
    BotRuleset GetBotRuleset(Player const* bot);

    bool IsCoA(Player const* bot);
    bool IsWildcard(Player const* bot);
    bool IsReborn(Player const* bot);

    bool IsCoAClass(uint8 classId);
    bool IsHeroClass(uint8 classId);
    bool IsStockClass(uint8 classId);
}

#endif // COA_PLAYERBOTS_BOT_RULESET_H
