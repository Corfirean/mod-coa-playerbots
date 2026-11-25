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

    // Warcraft Reborn bots: spends the bot's talent points on its spec's 51-point build, as far as its level pays for
    // (through the core's free-pick build, the same path a player's talent window uses). Returns true when the build is
    // up to date or there is nothing to do, false when the bot cannot respec right now (dead, in combat, refused) and
    // the caller should try again later.
    bool ApplyRebornBuild(Player* bot);

    // The specialization a Warcraft Reborn bot of this class should take, by the population's tank and healer shares
    // (CoaBots.SpecBalance.*) and spread over the class's damage specs; see BotRebornSpecChoice.h.
    uint32 ChooseRebornSpec(Player* bot);

    // Debug: why each pick of the bot's build up to its level was or was not accepted by the talent budget (one line per refused pick).
    std::string DescribeBuildPicks(Player* bot);

    // Chooses and assigns a specialization around level 10 if none is set.
    // Weighted toward roles the current bot population is short on (Tanks < 20%, Healers < 20%).
    uint32 ChooseSpecForBot(Player* bot);
}

#endif // COA_PLAYERBOTS_BOT_TALENT_BUILDS_H
