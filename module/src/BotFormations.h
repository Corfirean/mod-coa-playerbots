#ifndef _BOT_FORMATIONS_H
#define _BOT_FORMATIONS_H

#include "Common.h"
#include <string>

enum class BotGroupFormation : uint8
{
    RoleBased = 0,
    Shieldwall = 1,
    Arrow = 2,
    Circle = 3,
    Line = 4,
    Chaos = 5
};

char const* FormationToString(BotGroupFormation formation);
BotGroupFormation ParseFormation(std::string const& str);

#endif
