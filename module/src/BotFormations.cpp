#include "BotFormations.h"
#include <algorithm>

char const* FormationToString(BotGroupFormation formation)
{
    switch (formation)
    {
        case BotGroupFormation::RoleBased:  return "RoleBased";
        case BotGroupFormation::Shieldwall: return "Shieldwall";
        case BotGroupFormation::Arrow:      return "Arrow";
        case BotGroupFormation::Circle:     return "Circle";
        case BotGroupFormation::Line:       return "Line";
        case BotGroupFormation::Chaos:      return "Chaos";
        default:                            return "RoleBased";
    }
}

BotGroupFormation ParseFormation(std::string const& str)
{
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "shieldwall" || lower == "wall")
        return BotGroupFormation::Shieldwall;
    if (lower == "arrow" || lower == "spear")
        return BotGroupFormation::Arrow;
    if (lower == "circle" || lower == "ring")
        return BotGroupFormation::Circle;
    if (lower == "line" || lower == "phalanx")
        return BotGroupFormation::Line;
    if (lower == "chaos" || lower == "spread")
        return BotGroupFormation::Chaos;

    return BotGroupFormation::RoleBased;
}
