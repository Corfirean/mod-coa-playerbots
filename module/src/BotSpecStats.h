/*
 * mod-coa-playerbots
 *
 * Per-specialization item stat model for gear scoring. Generated from the Ascension Sidekick STAT PRIORITY / PRIMARY STAT
 * sections (the same data the training gear plan uses); stat codes are ItemModType values (3 Agility, 4 Strength,
 * 5 Intellect, 6 Spirit). ScoreItemForBot used to guess from the class alone, with the ids of Reaper / Primalist / Runemaster
 * mixed up, so Sanguine / Artificer / Infinite / Time (Spirit), Accursed / Zealot / Engravement (Agility), Stormbringer and
 * Venomancer (Intellect) and every Sun Cleric damage spec were scored against the wrong stat.
 */
#ifndef COA_PLAYERBOTS_BOT_SPEC_STATS_H
#define COA_PLAYERBOTS_BOT_SPEC_STATS_H

#include <array>
#include <cstdint>

namespace BotAI
{
    struct SpecStatModel
    {
        uint8_t classId;
        uint32_t specId;
        uint8_t primary;     // main stat (full weight), 0 = none
        uint8_t secondary;   // second stat (about half weight), 0 = none
        bool usesAttackPower;
        bool usesSpellPower;
    };

    inline constexpr std::array<SpecStatModel, 70> SpecStatModels =
    {{
    { 12, 1, 3, 0, true, false }, // Headhunting
    { 12, 2, 3, 0, true, false }, // Brutality
    { 12, 3, 3, 0, true, false }, // Ancestry
    { 13, 4, 3, 0, true, false }, // Shadowhunting
    { 13, 5, 5, 6, false, true }, // Voodoo
    { 13, 6, 6, 0, false, true }, // Brewing
    { 14, 7, 6, 5, false, true }, // Infernal
    { 14, 8, 3, 0, true, false }, // Slayer
    { 14, 9, 3, 0, true, false }, // Tyrant
    { 15, 10, 3, 5, true, false }, // Boltslinger
    { 15, 11, 3, 5, true, false }, // Houndmaster
    { 15, 12, 3, 5, true, false }, // Inquisition
    { 15, 97, 3, 0, true, false }, // Black Knight
    { 16, 13, 5, 0, false, true }, // Wind
    { 16, 14, 5, 0, false, true }, // Maelstrom
    { 16, 15, 5, 0, false, true }, // Lightning
    { 17, 16, 4, 5, true, false }, // Hellfire
    { 17, 17, 4, 0, true, false }, // Defiance
    { 17, 18, 4, 0, true, false }, // War
    { 18, 19, 4, 0, true, false }, // Gladiator
    { 18, 20, 4, 0, true, false }, // Inspiration
    { 18, 21, 4, 0, true, false }, // Vanguard
    { 19, 22, 3, 0, true, false }, // Oathkeeper
    { 19, 23, 3, 0, true, false }, // Zealot
    { 19, 24, 3, 0, true, false }, // Crusader
    { 20, 25, 6, 0, false, true }, // Fleshweaver
    { 20, 26, 6, 0, false, true }, // Sanguine
    { 20, 27, 3, 0, true, false }, // Accursed
    { 20, 99, 3, 0, true, false }, // Eternal
    { 21, 28, 3, 0, true, false }, // Archery
    { 21, 29, 3, 0, true, false }, // Farstrider
    { 21, 30, 3, 0, true, false }, // Brigand
    { 22, 31, 6, 0, false, true }, // Time
    { 22, 32, 6, 0, false, true }, // Infinite
    { 22, 33, 6, 0, false, true }, // Artificer
    { 23, 34, 5, 0, false, true }, // Death
    { 23, 35, 5, 0, false, true }, // Animation
    { 23, 36, 5, 0, false, true }, // Rime
    { 24, 37, 6, 0, false, true }, // Flameweaving
    { 24, 38, 5, 0, false, true }, // Incineration
    { 24, 39, 5, 0, false, true }, // Draconic
    { 25, 40, 5, 4, false, true }, // Heretic
    { 25, 41, 5, 0, false, true }, // Corruption
    { 25, 42, 4, 0, true, false }, // Godblade
    { 25, 96, 4, 0, true, false }, // Dreadnought
    { 26, 43, 5, 0, false, true }, // Moon Priest
    { 26, 44, 5, 0, false, true }, // Sentinel
    { 26, 45, 5, 0, false, true }, // Warden
    { 26, 100, 5, 0, false, true }, // Moon Guard
    { 27, 46, 5, 0, false, true }, // Piety
    { 27, 47, 4, 0, true, false }, // Valkyrie
    { 27, 48, 4, 0, true, false }, // Seraphim
    { 27, 98, 5, 0, false, true }, // Blessings
    { 28, 49, 3, 5, true, false }, // Demolition
    { 28, 50, 3, 5, true, false }, // Mechanics
    { 28, 51, 5, 0, false, true }, // Invention
    { 29, 52, 3, 0, true, false }, // Fortitude
    { 29, 53, 5, 0, false, true }, // Stalking
    { 29, 54, 5, 0, false, true }, // Rot
    { 29, 101, 5, 0, false, true }, // Vizier
    { 30, 55, 4, 0, true, false }, // Soul
    { 30, 56, 4, 0, true, false }, // Harvest
    { 30, 57, 4, 0, true, false }, // Domination
    { 31, 58, 5, 4, true, false }, // Grovekeeper
    { 31, 59, 4, 0, true, false }, // Wildwalker
    { 31, 60, 4, 0, true, false }, // Mountain King
    { 31, 95, 5, 0, false, true }, // Geomancy
    { 32, 61, 3, 0, true, false }, // Engravement
    { 32, 62, 5, 6, false, true }, // Glyphic
    { 32, 63, 3, 0, true, false }, // Riftblade
    }};

    inline SpecStatModel const* FindSpecStatModel(uint8_t classId, uint32_t specId)
    {
        for (SpecStatModel const& model : SpecStatModels)
            if (model.classId == classId && model.specId == specId)
                return &model;
        return nullptr;
    }
}

#endif // COA_PLAYERBOTS_BOT_SPEC_STATS_H
