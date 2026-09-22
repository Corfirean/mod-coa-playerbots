/*
 * mod-coa-playerbots
 *
 * Mapping between Ascension custom ClassId (12-32) + SpecId and BotRole (Dps/Tank/Healer/Support).
 * Sourced from exiles-db community wiki scrape and exact spellId cross-referencing
 * against AscensionCoATalentData.h, live-validated against Ascension CoA core.
 */

#include "ClassSpecRoles.h"

namespace BotAI
{
struct SpecRoleEntry
{
    uint8 classId;
    uint32 specId;
    BotRole role;
    char const* name;
};

static constexpr SpecRoleEntry SPEC_ROLE_TABLE[] =
{
    // Class 12: Barbarian
    { 12, 1,   BotRole::Dps,    "Headhunting" },
    { 12, 2,   BotRole::Dps,    "Brutality" },
    { 12, 3,   BotRole::Support,"Ancestry" },

    // Class 13: Witch Doctor
    { 13, 4,   BotRole::Dps,    "Shadowhunting" },
    { 13, 5,   BotRole::Dps,    "Voodoo" },
    { 13, 6,   BotRole::Healer, "Brewing" },

    // Class 14: Felsworn
    { 14, 7,   BotRole::Dps,    "Infernal" },
    { 14, 8,   BotRole::Dps,    "Slayer" },
    { 14, 9,   BotRole::Tank,   "Tyrant" },

    // Class 15: Witch Hunter
    { 15, 10,  BotRole::Dps,    "Boltslinger" },
    { 15, 11,  BotRole::Dps,    "Houndmaster" },
    { 15, 12,  BotRole::Dps,    "Inquisition" },
    { 15, 97,  BotRole::Tank,   "Black Knight" },

    // Class 16: Stormbringer
    { 16, 13,  BotRole::Support,"Wind" },
    { 16, 14,  BotRole::Dps,    "Maelstrom" },
    { 16, 15,  BotRole::Dps,    "Lightning" },

    // Class 17: Knight of Xoroth
    { 17, 16,  BotRole::Dps,    "Hellfire" },
    { 17, 17,  BotRole::Tank,   "Defiance" },
    { 17, 18,  BotRole::Dps,    "War" },

    // Class 18: Guardian
    { 18, 19,  BotRole::Dps,    "Gladiator" },
    { 18, 20,  BotRole::Support,"Inspiration" },
    { 18, 21,  BotRole::Tank,   "Vanguard" },

    // Class 19: Templar
    { 19, 22,  BotRole::Tank,   "Oathkeeper" },
    { 19, 23,  BotRole::Dps,    "Zealot" },
    { 19, 24,  BotRole::Dps,    "Crusader" },

    // Class 20: Bloodmage
    { 20, 25,  BotRole::Support,"Fleshweaver" },
    { 20, 26,  BotRole::Dps,    "Sanguine" },
    { 20, 27,  BotRole::Dps,    "Accursed" },
    { 20, 99,  BotRole::Tank,   "Eternal" },

    // Class 21: Ranger
    { 21, 28,  BotRole::Dps,    "Archery" },
    { 21, 29,  BotRole::Support,"Farstrider" },
    { 21, 30,  BotRole::Dps,    "Brigand" },

    // Class 22: Chronomancer
    { 22, 31,  BotRole::Healer, "Time" },
    { 22, 32,  BotRole::Dps,    "Infinite" },
    { 22, 33,  BotRole::Dps,    "Artificer" },

    // Class 23: Necromancer
    { 23, 34,  BotRole::Dps,    "Death" },
    { 23, 35,  BotRole::Dps,    "Animation" },
    { 23, 36,  BotRole::Dps,    "Rime" },

    // Class 24: Pyromancer
    { 24, 37,  BotRole::Healer, "Flameweaving" },
    { 24, 38,  BotRole::Dps,    "Incineration" },
    { 24, 39,  BotRole::Dps,    "Draconic" },

    // Class 25: Cultist
    { 25, 40,  BotRole::Healer, "Heretic" },
    { 25, 41,  BotRole::Dps,    "Corruption" },
    { 25, 42,  BotRole::Dps,    "Godblade" },
    { 25, 96,  BotRole::Tank,   "Dreadnought" },

    // Class 26: Starcaller
    { 26, 43,  BotRole::Healer, "Moon Priest" },
    { 26, 44,  BotRole::Dps,    "Sentinel" },
    { 26, 45,  BotRole::Dps,    "Warden" },
    { 26, 100, BotRole::Tank,   "Moon Guard" },

    // Class 27: Sun Cleric
    { 27, 46,  BotRole::Dps,    "Piety" },
    { 27, 47,  BotRole::Dps,    "Valkyrie" },
    { 27, 48,  BotRole::Tank,   "Seraphim" },
    { 27, 98,  BotRole::Healer, "Blessings" },

    // Class 28: Tinker
    { 28, 49,  BotRole::Dps,    "Demolition" },
    { 28, 50,  BotRole::Dps,    "Mechanics" },
    { 28, 51,  BotRole::Healer, "Invention" },

    // Class 29: Venomancer
    { 29, 52,  BotRole::Tank,   "Fortitude" },
    { 29, 53,  BotRole::Dps,    "Stalking" },
    { 29, 54,  BotRole::Dps,    "Venom" },
    { 29, 101, BotRole::Healer, "Vizier" },

    // Class 30: Reaper
    { 30, 55,  BotRole::Dps,    "Soul" },
    { 30, 56,  BotRole::Dps,    "Harvest" },
    { 30, 57,  BotRole::Tank,   "Domination" },

    // Class 31: Primalist
    { 31, 58,  BotRole::Healer, "Life" },
    { 31, 59,  BotRole::Dps,    "Primal" },
    { 31, 60,  BotRole::Tank,   "Mountain King" },
    { 31, 95,  BotRole::Dps,    "Geomancy" },

    // Class 32: Runemaster
    { 32, 61,  BotRole::Dps,    "Runic" },
    { 32, 62,  BotRole::Dps,    "Arcane" },
    { 32, 63,  BotRole::Dps,    "Riftblade" },
};

BotRole GetRoleForClassSpec(uint8 classId, uint32 specId)
{
    if (specId == 0)
        return BotRole::Dps;

    for (auto const& entry : SPEC_ROLE_TABLE)
    {
        if (entry.classId == classId && entry.specId == specId)
            return entry.role;
    }
    return BotRole::Dps;
}

char const* GetSpecName(uint8 classId, uint32 specId)
{
    for (auto const& entry : SPEC_ROLE_TABLE)
    {
        if (entry.classId == classId && entry.specId == specId)
            return entry.name;
    }
    return nullptr;
}

uint32 GetAvailableRolesMask(uint8 classId)
{
    uint32 mask = 1u << uint32(BotRole::Dps); // specId 0 always falls back to Dps
    for (auto const& entry : SPEC_ROLE_TABLE)
    {
        if (entry.classId == classId)
            mask |= 1u << uint32(entry.role);
    }
    return mask;
}

std::vector<uint32> GetSpecsForRole(uint8 classId, BotRole role)
{
    std::vector<uint32> specs;
    for (auto const& entry : SPEC_ROLE_TABLE)
        if (entry.classId == classId && entry.role == role)
            specs.push_back(entry.specId);
    return specs;
}

uint32 FindSpecForRole(uint8 classId, BotRole role, uint32 preferredSpecId)
{
    if (preferredSpecId && GetRoleForClassSpec(classId, preferredSpecId) == role)
        return preferredSpecId;

    for (auto const& entry : SPEC_ROLE_TABLE)
    {
        if (entry.classId == classId && entry.role == role)
            return entry.specId;
    }
    return 0;
}
}
