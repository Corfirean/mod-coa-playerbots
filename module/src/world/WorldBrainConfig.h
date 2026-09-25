/*
 * mod-coa-playerbots
 *
 * Every tunable of the open-world layer in one struct, read once at startup
 * (CoaBots.WorldBrain.* in mod_coa_playerbots.conf.dist). Named here instead of scattered as
 * magic numbers through the executors.
 */

#ifndef COA_PLAYERBOTS_WORLD_BRAIN_CONFIG_H
#define COA_PLAYERBOTS_WORLD_BRAIN_CONFIG_H

#include "Define.h"
#include "WorldUtility.h"

struct WorldBrainConfig
{
    bool enabled = true;

    // Stagger windows. Every bot draws its own interval inside the window, so a population never
    // thinks in lockstep.
    uint32 plannerMinMs = 2000;
    uint32 plannerMaxMs = 8000;
    uint32 scanMinMs = 500;
    uint32 scanMaxMs = 1500;
    uint32 presenceMinMs = 4000;
    uint32 presenceMaxMs = 7000;

    // Live target search around the bot inside an objective area.
    float searchRadius = 50.0f;
    // Distance at which the bot engages instead of walking closer; the combat engine then takes
    // over positioning (a caster stops at its own range, a melee closes in).
    float engageRange = 28.0f;

    // Phase budgets. Nothing in the executor may run forever.
    uint32 searchTimeoutMs = 35000;
    uint32 approachTimeoutMs = 25000;
    uint32 travelTimeoutMs = 360000;
    uint32 interactTimeoutMs = 30000;
    uint32 lootTimeoutMs = 8000;
    uint32 taskTimeoutMs = 1800000;

    // Quest log policy.
    uint32 maxActiveQuests = 15;
    float maxObjectiveDistance = 3000.0f;
    float giverSearchRadius = 500.0f;
    int32 questLevelBelow = 6;   // quests this many levels below the bot are skipped (grey)
    int32 questLevelAbove = 3;   // and this many above
    uint32 maxLevelAbove = 3;    // mobs this far above the bot are not engaged
    bool acceptElite = false;    // solo bots leave elite quests alone
    uint32 maxBadClusters = 3;   // areas given up per objective before the quest is suspended
    uint32 questSuspendMs = 600000;          // first time a quest is set aside; doubles per repeat
    uint32 questSuspendMaxMs = 2 * 60 * 60 * 1000;

    // Failure-memory ttls.
    uint32 targetFailMs = 60000;
    uint32 objectFailMs = 120000;
    uint32 clusterFailMs = 300000;
    uint32 npcFailMs = 300000;

    // Reservation ttls.
    uint32 targetReserveMs = 90000;
    uint32 objectReserveMs = 30000;
    uint32 clusterJoinMs = 120000;

    // Travel.
    float mountDistanceMin = 60.0f;  // per-bot threshold drawn from [min, max]
    float mountDistanceMax = 95.0f;
    float taxiMinDistance = 900.0f;

    // Humanisation.
    uint32 reactionMinMs = 300;
    uint32 reactionMaxMs = 1400;
    bool sessionBreaks = true;
    uint32 sessionMinMs = 20 * 60 * 1000;
    uint32 sessionMaxMs = 45 * 60 * 1000;
    uint32 breakMinMs = 3 * 60 * 1000;
    uint32 breakMaxMs = 8 * 60 * 1000;
    bool gatherDetours = true;
    float detourRadius = 15.0f;

    // Social: helping others, and temporary bot-only parties.
    bool helpOthers = true;           // step into a fight a nearby player/bot is losing (their tagged mob only)
    float helpRadius = 30.0f;
    float helpHealthPct = 40.0f;
    bool resurrectOthers = true;      // resurrect a friendly corpse nearby, if the bot knows how
    bool temporaryParties = false;    // off until live-tested: bots with a shared quest team up for a while
    uint32 partyMaxSize = 3;          // including the leader, 2-5
    float partyRadius = 60.0f;
    uint32 partyMinMs = 5 * 60 * 1000;
    uint32 partyMaxMs = 20 * 60 * 1000;

    UtilityWeights utility;
    TargetWeights target;
    ClusterWeights cluster;
};

namespace WorldBrainSettings
{
    void Load();
    WorldBrainConfig const& Get();
}

#endif // COA_PLAYERBOTS_WORLD_BRAIN_CONFIG_H
