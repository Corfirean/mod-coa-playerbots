#ifndef _BOT_AVOIDANCE_H
#define _BOT_AVOIDANCE_H

#include "Common.h"

class Player;
class Unit;

class BotAvoidance
{
public:
    enum class GroundHazardSeverity : uint8
    {
        None,
        Dangerous,
        Critical
    };

    // Classifies the hostile dynamic object under the bot without moving it.  Ordinary hostile
    // ground effects remain Dangerous; only clearly damaging effects combined with immediate
    // health risk (or percentage/instant-kill damage) are Critical enough to preempt a cast.
    static GroundHazardSeverity GetGroundHazardSeverity(Player* bot);

    // True only for the point-blank boss casts for which waiting for our cast to finish is unsafe.
    // Frontal/formation correction deliberately is not considered critical cast-breaking movement.
    static bool HasCriticalBossMechanic(Player* bot, Unit* target);

    // Returns true if bot was standing in a hostile ground AoE/void zone and began moving out
    static bool TryAvoidGroundHazards(Player* bot);

    // Returns true if bot reacted to boss cleave / point-blank AoE and repositioned
    static bool TryAvoidBossTelegraphedAttacks(Player* bot, Unit* target);
};

#endif
