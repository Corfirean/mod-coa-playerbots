/*
 * mod-coa-playerbots
 *
 * In-memory talent build storage and allocation for custom Ascension classes.
 * Sourced from reference/ascensionsidekick-level-builds.json.
 */

#ifndef COA_PLAYERBOTS_BOT_TALENT_BUILDS_H
#define COA_PLAYERBOTS_BOT_TALENT_BUILDS_H

#include "Define.h"
#include <string>
#include <vector>

class Player;

namespace BotTalentBuilds
{
    struct TalentPick
    {
        uint8 level = 0;
        uint8 tree = 0;
        uint32 entryId = 0;
        uint8 rank = 0;
        uint32 spellId = 0;
    };

    struct TalentBuild
    {
        uint8 classId = 0;
        uint32 specId = 0;
        std::string clsName;
        std::string specName;
        std::string role;
        std::vector<TalentPick> picks;
    };

    // Initializes and parses the JSON build file once into static in-memory structures.
    // Thread-safe / idempotent.
    void Initialize();

    // Retrieves the build for (classId, specId), or nullptr if not found.
    TalentBuild const* GetBuild(uint8 classId, uint32 specId);

    // Applies talent picks from the bot's assigned spec up to toLevel (capped at 60).
    // Safely removes obsolete lower ranks, learns new ranks, and updates free talent points.
    void ApplyBuildForLevel(Player* bot, uint8 toLevel);

    // Chooses and assigns a specialization around level 10 if none is set.
    // Weighted toward roles the current bot population is short on (Tanks < 20%, Healers < 20%).
    uint32 ChooseSpecForBot(Player* bot);
}

#endif // COA_PLAYERBOTS_BOT_TALENT_BUILDS_H
