/*
 * mod-coa-playerbots -- bot strength scaling (see BotScaling.cpp).
 */

#ifndef COA_PLAYERBOTS_BOT_SCALING_H
#define COA_PLAYERBOTS_BOT_SCALING_H

#include "ObjectGuid.h"
#include <string>

void AddSC_coa_bot_scaling_script();

namespace BotScaling
{
    // The last few creature hits a bot took ("spell 52960 for 3100 by 28923 4 s ago, ..."), for the death log: what actually kills a bot in a boss fight.
    std::string RecentHits(ObjectGuid guid);
}

#endif
