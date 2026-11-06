/*
 * mod-coa-playerbots
 *
 * Mapping between Ascension custom ClassId (12-32), Classic / Reborn ClassId (1-9, 11),
 * Wildcard ClassId (10) + SpecId and BotRole (Dps/Tank/Healer/Support).
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
    // =========================================================================
    // Classic / Warcraft Reborn Classes (1-9, 11) + Wildcard Hero (10)
    // =========================================================================
    // Class 1: Warrior
    { 1, 1,    BotRole::Dps,    "Arms" },
    { 1, 2,    BotRole::Dps,    "Fury" },
    { 1, 3,    BotRole::Tank,   "Protection" },

    // Class 2: Paladin
    { 2, 1,    BotRole::Healer, "Holy" },
    { 2, 2,    BotRole::Tank,   "Protection" },
    { 2, 3,    BotRole::Dps,    "Retribution" },

    // Class 3: Hunter
    { 3, 1,    BotRole::Dps,    "Beast Mastery" },
    { 3, 2,    BotRole::Dps,    "Marksmanship" },
    { 3, 3,    BotRole::Dps,    "Survival" },

    // Class 4: Rogue
    { 4, 1,    BotRole::Dps,    "Assassination" },
    { 4, 2,    BotRole::Dps,    "Combat" },
    { 4, 3,    BotRole::Dps,    "Subtlety" },

    // Class 5: Priest
    { 5, 1,    BotRole::Healer, "Discipline" },
    { 5, 2,    BotRole::Healer, "Holy" },
    { 5, 3,    BotRole::Dps,    "Shadow" },

    // Class 6: Death Knight
    { 6, 1,    BotRole::Tank,   "Blood" },
    { 6, 2,    BotRole::Dps,    "Frost" },
    { 6, 3,    BotRole::Dps,    "Unholy" },

    // Class 7: Shaman
    { 7, 1,    BotRole::Dps,    "Elemental" },
    { 7, 2,    BotRole::Dps,    "Enhancement" },
    { 7, 3,    BotRole::Healer, "Restoration" },
    { 7, 4,    BotRole::Tank,   "Spirit Guard" }, // Reborn Shaman Tank

    // Class 8: Mage
    { 8, 1,    BotRole::Dps,    "Arcane" },
    { 8, 2,    BotRole::Dps,    "Fire" },
    { 8, 3,    BotRole::Dps,    "Frost" },

    // Class 9: Warlock
    { 9, 1,    BotRole::Dps,    "Affliction" },
    { 9, 2,    BotRole::Dps,    "Demonology" },
    { 9, 3,    BotRole::Dps,    "Destruction" },
    { 9, 4,    BotRole::Tank,   "Dark Apotheosis" }, // Reborn Warlock Tank

    // Class 10: Hero (Wildcard / Classless)
    { 10, 1,   BotRole::Tank,   "Hero Juggernaut" },
    { 10, 2,   BotRole::Healer, "Hero Priest" },
    { 10, 3,   BotRole::Dps,    "Hero Berserker" },
    { 10, 4,   BotRole::Dps,    "Hero Ranger" },
    { 10, 5,   BotRole::Dps,    "Hero Elementalist" },

    // Class 11: Druid
    { 11, 1,   BotRole::Dps,    "Balance" },
    { 11, 2,   BotRole::Dps,    "Feral Combat" },
    { 11, 3,   BotRole::Healer, "Restoration" },
    { 11, 4,   BotRole::Tank,   "Guardian Bear" },

    // =========================================================================
    // Conquest of Azeroth Custom Classes (12-32)
    // =========================================================================
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
    { 29, 54,  BotRole::Dps,    "Rot" },
    { 29, 101, BotRole::Healer, "Vizier" },

    // Class 30: Reaper
    { 30, 55,  BotRole::Dps,    "Soul" },
    { 30, 56,  BotRole::Dps,    "Harvest" },
    { 30, 57,  BotRole::Tank,   "Domination" },

    // Class 31: Primalist
    { 31, 58,  BotRole::Support,"Grovekeeper" },
    { 31, 59,  BotRole::Dps,    "Wildwalker" },
    { 31, 60,  BotRole::Tank,   "Mountain King" },
    { 31, 95,  BotRole::Dps,    "Geomancy" },

    // Class 32: Runemaster
    { 32, 61,  BotRole::Dps,    "Engravement" },
    { 32, 62,  BotRole::Dps,    "Glyphic" },
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

std::vector<SpecInfo> GetAllSpecs(uint8 classId)
{
    std::vector<SpecInfo> specs;
    for (auto const& entry : SPEC_ROLE_TABLE)
        if (entry.classId == classId)
            specs.push_back({ entry.specId, entry.role, entry.name });
    return specs;
}
}
