/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: ProfileRegistry implementation
 */

#include "profiles/ProfileRegistry.h"
#include "profiles/ProfileWitchDoctor.h"
#include "profiles/ProfileSunCleric.h"
#include "profiles/ProfileRunemaster.h"
#include "profiles/ProfilePrimalist.h"
#include "profiles/ProfileRanger.h"
#include "profiles/ProfileBarbarian.h"
#include "profiles/ProfileFelsworn.h"
#include "profiles/ProfileWitchHunter.h"
#include "profiles/ProfileStormbringer.h"
#include "profiles/ProfileKnightOfXoroth.h"
#include "profiles/ProfileGuardian.h"
#include "profiles/ProfileTemplar.h"
#include "profiles/ProfileBloodmage.h"
#include "profiles/ProfileChronomancer.h"
#include "profiles/ProfileNecromancer.h"
#include "profiles/ProfilePyromancer.h"
#include "profiles/ProfileCultist.h"
#include "profiles/ProfileStarcaller.h"
#include "profiles/ProfileTinker.h"
#include "profiles/ProfileVenomancer.h"
#include "profiles/ProfileReaper.h"
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
        RegisterSunClericProfiles();
        RegisterRunemasterProfiles();
        RegisterPrimalistProfiles();
        RegisterRangerProfiles();
        RegisterBarbarianProfiles();
        RegisterFelswornProfiles();
        RegisterWitchHunterProfiles();
        RegisterStormbringerProfiles();
        RegisterKnightOfXorothProfiles();
        RegisterGuardianProfiles();
        RegisterTemplarProfiles();
        RegisterBloodmageProfiles();
        RegisterChronomancerProfiles();
        RegisterNecromancerProfiles();
        RegisterPyromancerProfiles();
        RegisterCultistProfiles();
        RegisterStarcallerProfiles();
        RegisterTinkerProfiles();
        RegisterVenomancerProfiles();
        RegisterReaperProfiles();
    }

    void ProfileRegistry::RegisterProfile(CombatProfile profile)
    {
        s_profiles.push_back(std::move(profile));
    }

    CombatProfile const* ProfileRegistry::FindProfile(uint8 classId, uint32 specId, BotRole role)
    {
        Initialize();

        // Exact match (class, spec, role) - retail-style fixed talent specializations
        for (CombatProfile const& p : s_profiles)
        {
            if (p.classId == classId && p.specId == specId && p.role == role)
                return &p;
        }

        return nullptr;
    }

    bool ProfileRegistry::HasProfile(uint8 classId, uint32 specId, BotRole role)
    {
        return FindProfile(classId, specId, role) != nullptr;
    }

    std::vector<CombatProfile> const& ProfileRegistry::GetAllProfiles()
    {
        Initialize();
        return s_profiles;
    }
}
