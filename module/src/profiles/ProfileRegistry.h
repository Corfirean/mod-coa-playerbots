/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: ProfileRegistry
 * Central repository of class/spec/role data-driven combat profiles.
 */

#ifndef COA_PLAYERBOTS_PROFILE_REGISTRY_H
#define COA_PLAYERBOTS_PROFILE_REGISTRY_H

#include "profiles/CombatProfile.h"

namespace BotAI
{
    class ProfileRegistry
    {
    public:
        static void RegisterProfile(CombatProfile profile);
        static CombatProfile const* FindProfile(uint8 classId, uint32 specId, BotRole role);
        static bool HasProfile(uint8 classId, uint32 specId, BotRole role);
        static void Initialize();
    };
}

#endif // COA_PLAYERBOTS_PROFILE_REGISTRY_H
