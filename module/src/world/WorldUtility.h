/*
 * mod-coa-playerbots
 *
 * The scoring the open-world planner and executors decide with, kept in one place with every
 * weight named. The planner never follows a scripted order ("finish A, turn in A, take B"); it
 * scores what is on offer -- objectives, turn-ins, new quests, objective areas, live mobs -- and
 * takes the best, with a small per-bot jitter so two bots with the same quest log do not make the
 * same choice in the same second.
 *
 * Every weight lives in these structs and is overridable from the config file (see
 * WorldBrainConfig). Pure functions over plain inputs, unit-tested in module/tests.
 */

#ifndef COA_PLAYERBOTS_WORLD_UTILITY_H
#define COA_PLAYERBOTS_WORLD_UTILITY_H

#include "Define.h"
#include <algorithm>
#include <cmath>

struct UtilityWeights
{
    float questProgress = 100.0f;     // finishing an objective at all
    float nearlyDone = 40.0f;         // scaled by how little is left
    float chainProgress = 25.0f;      // part of a chain the bot is already on
    float xpValue = 30.0f;            // quest XP as a share of the bot's current level
    float overlapBonus = 35.0f;       // per other objective served in the same place
    float routeSynergy = 15.0f;       // per other objective near the same place
    float personality = 0.4f;         // scale of the questing trait (0-100)
    float travelPer100Yards = 7.0f;   // walking distance cost
    float farTravelMultiplier = 1.8f; // cost multiplier beyond farTravelYards
    float farTravelYards = 450.0f;
    float crowdPerBot = 6.0f;         // per bot already at / heading to the place
    float dangerCost = 45.0f;         // elite/over-level targets
    float failureCost = 60.0f;        // per live failure strike on the place
    float turnInBase = 120.0f;        // a completed quest waiting to be handed in
    float turnInBatch = 30.0f;        // per extra completed quest handed in at the same NPC
    float acceptBase = 55.0f;         // a quest giver with work for the bot
    float acceptPerQuest = 20.0f;     // per acceptable quest on offer
};

struct TargetWeights
{
    float distance = 40.0f;           // closer is better, falls off across the search radius
    float questValue = 30.0f;         // per active objective the kill advances
    float reserved = 200.0f;          // another bot called it
    float tagged = 500.0f;            // someone else tapped it: no credit, no loot
    float inCombatWithOther = 80.0f;  // already fighting someone else
    float crowd = 6.0f;               // per bot near it
    float verticalPer10Yards = 12.0f; // likely on another level / behind geometry
    float recentFailure = 300.0f;     // failed on this exact target recently
    float levelAbove = 15.0f;         // per level above the bot
    float jitter = 6.0f;              // per-bot tie breaking
};

struct ClusterWeights
{
    float spawnCount = 4.0f;          // per spawn, with diminishing returns
    float dropChance = 30.0f;         // for collect objectives: better droppers first
    float travelPer100Yards = 7.0f;
    float occupancy = 12.0f;          // per bot already assigned to the place
    float crowd = 4.0f;               // per bot in the surrounding cells
    float failure = 80.0f;            // per live failure strike on the place
    float here = 25.0f;               // already standing in it: no trip needed
    float jitter = 10.0f;
};

struct ObjectiveUtilityInput
{
    float remainingFraction = 1.0f;   // 0 = done, 1 = nothing done yet
    float questXpShare = 0.0f;        // quest XP / XP needed for the bot's next level
    bool chainQuest = false;
    uint32 overlapCount = 0;          // other objectives served by the same targets
    uint32 routeNeighbours = 0;       // other objectives with an area nearby
    float distance = 0.0f;
    uint32 crowd = 0;
    uint32 failureStrikes = 0;
    bool dangerous = false;
    float questingTrait = 50.0f;      // personality, 0-100
    float jitter = 0.0f;              // 0-1
};

struct ClusterScoreInput
{
    uint32 spawnCount = 0;
    float dropChance = 1.0f;          // 0-1, 1 for kill objectives
    float distance = 0.0f;
    uint32 occupancy = 0;
    uint32 crowd = 0;
    uint32 failureStrikes = 0;
    bool here = false;
    float jitter = 0.0f;              // 0-1
};

struct TargetScoreInput
{
    float distance = 0.0f;
    float searchRadius = 50.0f;
    uint32 objectivesServed = 1;
    bool reservedByOther = false;
    bool tappedByOther = false;
    bool inCombatWithOther = false;
    uint32 nearbyBots = 0;
    float verticalGap = 0.0f;
    bool recentlyFailed = false;
    int32 levelAbove = 0;             // target level - bot level
    float jitter = 0.0f;              // 0-1
};

namespace WorldUtility
{
    inline float TravelCost(float distance, float per100, float farYards, float farMultiplier)
    {
        float d = std::max(0.0f, distance);
        if (d <= farYards)
            return d / 100.0f * per100;
        return farYards / 100.0f * per100 + (d - farYards) / 100.0f * per100 * farMultiplier;
    }

    inline float ScoreObjective(ObjectiveUtilityInput const& in, UtilityWeights const& w)
    {
        float remaining = std::clamp(in.remainingFraction, 0.0f, 1.0f);
        float score = w.questProgress;
        score += w.nearlyDone * (1.0f - remaining);
        score += w.xpValue * std::clamp(in.questXpShare, 0.0f, 1.0f);
        if (in.chainQuest)
            score += w.chainProgress;
        score += w.overlapBonus * float(std::min<uint32>(in.overlapCount, 4));
        score += w.routeSynergy * float(std::min<uint32>(in.routeNeighbours, 4));
        score += w.personality * (in.questingTrait - 50.0f);
        score -= TravelCost(in.distance, w.travelPer100Yards, w.farTravelYards, w.farTravelMultiplier);
        score -= w.crowdPerBot * float(std::min<uint32>(in.crowd, 20));
        score -= w.failureCost * float(std::min<uint32>(in.failureStrikes, 4));
        if (in.dangerous)
            score -= w.dangerCost;
        score += in.jitter * 8.0f;
        return score;
    }

    inline float ScoreCluster(ClusterScoreInput const& in, ClusterWeights const& w)
    {
        // Diminishing returns: the twentieth spawn in a camp matters less than the third.
        float score = w.spawnCount * std::sqrt(float(in.spawnCount)) * 3.0f;
        score += w.dropChance * std::clamp(in.dropChance, 0.0f, 1.0f);
        score -= TravelCost(in.distance, w.travelPer100Yards, 450.0f, 1.8f);
        score -= w.occupancy * float(std::min<uint32>(in.occupancy, 20));
        score -= w.crowd * float(std::min<uint32>(in.crowd, 30));
        score -= w.failure * float(std::min<uint32>(in.failureStrikes, 4));
        if (in.here)
            score += w.here;
        score += w.jitter * in.jitter;
        return score;
    }

    inline float ScoreTarget(TargetScoreInput const& in, TargetWeights const& w)
    {
        float radius = std::max(1.0f, in.searchRadius);
        float score = w.distance * (1.0f - std::clamp(in.distance / radius, 0.0f, 1.5f));
        score += w.questValue * float(std::max<uint32>(1, in.objectivesServed));
        if (in.reservedByOther)
            score -= w.reserved;
        if (in.tappedByOther)
            score -= w.tagged;
        if (in.inCombatWithOther)
            score -= w.inCombatWithOther;
        score -= w.crowd * float(std::min<uint32>(in.nearbyBots, 10));
        score -= w.verticalPer10Yards * std::max(0.0f, in.verticalGap - 3.0f) / 10.0f;
        if (in.recentlyFailed)
            score -= w.recentFailure;
        if (in.levelAbove > 0)
            score -= w.levelAbove * float(in.levelAbove);
        score += w.jitter * in.jitter;
        return score;
    }

    // Deterministic 0-1 noise from two ids: stable for one bot looking at one thing, different
    // between bots, so ties break differently per bot without any shared RNG state.
    inline float Jitter(uint64 a, uint64 b)
    {
        uint64 x = a * 0x9E3779B97F4A7C15ULL ^ (b + 0x632BE59BD9B4E019ULL + (a << 6) + (a >> 2));
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return float(x % 100000ULL) / 100000.0f;
    }
}

#endif // COA_PLAYERBOTS_WORLD_UTILITY_H
