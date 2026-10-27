/*
 * mod-coa-playerbots
 *
 * WorldEncounterDirector -- orchestrates open-world Dynamic Bot Encounters.
 *
 * Key invariant:
 *   Dynamic Encounters are NOT a second WorldBrain. They temporarily intercept
 *   open-world intent for selected real existing playerbots in the world; while
 *   active, the bot's normal WorldBrain task is paused in place. When the encounter
 *   completes or terminates for any reason, all movement ownership, target claims,
 *   visual overrides, and encounter ownership are released and the bot seamlessly
 *   resumes its normal WorldBrain activity.
 */

#ifndef COA_PLAYERBOTS_WORLD_ENCOUNTER_DIRECTOR_H
#define COA_PLAYERBOTS_WORLD_ENCOUNTER_DIRECTOR_H

#include "EncounterInstance.h"
#include "EncounterTypes.h"
#include "WorldBrain.h"
#include <string>

struct BrainState;
class Player;
class Unit;

namespace WorldEncounterDirector
{
    // Load / reload configuration from mod_coa_playerbots.conf.
    void LoadConfig();

    // Staggered world-level tick invoked from BotMgr::Update(diff).
    void Update(uint32 diff);

    // Per-bot open-world intercept hook invoked from WorldBrain::Update.
    // Returns true while an active encounter owns the bot's open-world intent.
    bool UpdateBot(Player* bot, BrainState& brainState, WorldPersona const& persona, uint32 diff);

    // Query whether a bot is currently participating in an active encounter.
    bool IsBotInEncounter(ObjectGuid botGuid);

    // Lifecycle & preemption hooks
    void OnBotSuspended(Player* bot, char const* reason);
    void OnParticipantDeath(Unit* victim, Unit* killer = nullptr);
    void OnPvPKill(ObjectGuid killerGuid, ObjectGuid victimGuid);
    void OnPlayerLogout(ObjectGuid playerGuid);
    void ForgetBot(ObjectGuid botGuid);
    void ResetAll();

    // Debug & administrative commands (.botcmd encounter ...)
    std::string DescribeBot(ObjectGuid botGuid);
    std::string DescribeStats();
    std::string DescribeBountyAndNemesis(ObjectGuid playerGuid);
    bool ForceStartEncounter(EncounterType type, Player* targetOrNearPlayer, std::string& outMessage);
    bool StopEncounter(uint64 encounterId, std::string& outMessage);
    uint32 StopAllEncounters();

    // Snapshot of metrics
    EncounterMetrics GetMetrics();
    uint32 GetActiveEncounterCount();
}

#endif // COA_PLAYERBOTS_WORLD_ENCOUNTER_DIRECTOR_H
