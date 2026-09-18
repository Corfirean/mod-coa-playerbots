/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: ProfileRegistry implementation
 */

#include "profiles/ProfileRegistry.h"
#include "profiles/ProfileWitchDoctor.h"
#include <vector>

namespace BotAI
{
    namespace
    {
        std::vector<CombatProfile> s_profiles;
        bool s_initialized = false;
    }

    void ProfileRegistry::Initialize()
    {
        if (s_initialized)
            return;

        s_initialized = true;
        s_profiles.clear();

        // Register class profiles
        RegisterWitchDoctorProfiles();
    }

    void ProfileRegistry::RegisterProfile(CombatProfile profile)
    {
        s_profiles.push_back(std::move(profile));
    }

    CombatProfile const* ProfileRegistry::FindProfile(uint8 classId, uint32 specId, BotRole role)
    {
        Initialize();

        // Pass 1: Exact match (class, spec, role)
        for (CombatProfile const& p : s_profiles)
        {
            if (p.classId == classId && p.specId != 0 && p.specId == specId && p.role == role)
                return &p;
        }

        // Pass 2: Wildcard spec match (class, spec == 0, role)
        for (CombatProfile const& p : s_profiles)
        {
            if (p.classId == classId && p.specId == 0 && p.role == role)
                return &p;
        }

        return nullptr;
    }

    bool ProfileRegistry::HasProfile(uint8 classId, uint32 specId, BotRole role)
    {
        return FindProfile(classId, specId, role) != nullptr;
    }
}
