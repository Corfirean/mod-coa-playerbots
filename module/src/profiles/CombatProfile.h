/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatProfile
 */

#ifndef COA_PLAYERBOTS_COMBAT_PROFILE_H
#define COA_PLAYERBOTS_COMBAT_PROFILE_H

#include "Define.h"
#include "BotAI.h"
#include "engine/AbilityDescriptor.h"
#include <string>
#include <vector>

namespace BotAI
{
    struct CombatProfile
    {
        uint8 classId = 0;
        uint32 specId = 0; // 0 = matches any spec of this class
        BotRole role = BotRole::Dps;
        std::string profileName;

        std::vector<AbilityDescriptor> abilities;
    };
}

#endif // COA_PLAYERBOTS_COMBAT_PROFILE_H
