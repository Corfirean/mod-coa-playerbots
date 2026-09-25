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
    inline constexpr float PROFILE_MELEE_ENGAGE_DISTANCE = 4.0f;
    inline constexpr float PROFILE_RANGED_ENGAGE_DISTANCE = 30.0f;

    struct CombatProfile
    {
        uint8 classId = 0;
        uint32 specId = 0; // Ascension CoA specialization ID (retail-style fixed spec)
        BotRole role = BotRole::Dps;
        std::string profileName;

        bool useRangedAutoRepeat = false;
        float preferredEngageDistance = 0.0f; // 0.0f = auto-detect from abilities / spec

        std::vector<AbilityDescriptor> abilities;
    };
}

#endif // COA_PLAYERBOTS_COMBAT_PROFILE_H
