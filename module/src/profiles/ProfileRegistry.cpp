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

// Classic / Warcraft Reborn & Wildcard profiles
#include "profiles/ProfileWarrior.h"
#include "profiles/ProfilePaladin.h"
#include "profiles/ProfileHunter.h"
#include "profiles/ProfileRogue.h"
#include "profiles/ProfilePriest.h"
#include "profiles/ProfileDeathKnight.h"
#include "profiles/ProfileShaman.h"
#include "profiles/ProfileMage.h"
#include "profiles/ProfileWarlock.h"
#include "profiles/ProfileDruid.h"
#include "profiles/ProfileHero.h"

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

        // 1. Register Classic / Reborn profiles (Classes 1-9, 11) & Hero (10)
        RegisterWarriorProfiles();
        RegisterPaladinProfiles();
        RegisterHunterProfiles();
        RegisterRogueProfiles();
        RegisterPriestProfiles();
        RegisterDeathKnightProfiles();
        RegisterShamanProfiles();
        RegisterMageProfiles();
        RegisterWarlockProfiles();
        RegisterDruidProfiles();
        RegisterHeroProfiles();

        // 2. Register Conquest of Azeroth class profiles (Classes 12-32)
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

        // 1. Exact match (class, spec, role) - retail-style fixed talent specializations
        for (CombatProfile const& p : s_profiles)
        {
            if (p.classId == classId && p.specId == specId && p.role == role)
                return &p;
        }

        // 2. Class + Role fallback (when specId is 0 or an unmapped custom spec)
        for (CombatProfile const& p : s_profiles)
        {
            if (p.classId == classId && p.role == role)
                return &p;
        }

        // 3. Fallback for Hero (class 10) by role
        if (classId == 10)
        {
            for (CombatProfile const& p : s_profiles)
            {
                if (p.classId == 10 && p.role == role)
                    return &p;
            }
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
