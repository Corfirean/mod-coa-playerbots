#ifndef _BOT_AVOIDANCE_H
#define _BOT_AVOIDANCE_H

#include "Common.h"

class Player;
class Unit;

class BotAvoidance
{
public:
    // Returns true if bot was standing in a hostile ground AoE/void zone and began moving out
    static bool TryAvoidGroundHazards(Player* bot);

    // Returns true if bot reacted to boss cleave / point-blank AoE and repositioned
    static bool TryAvoidBossTelegraphedAttacks(Player* bot, Unit* target);
};

#endif
