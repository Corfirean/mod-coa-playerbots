/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- Eligibility & PvP safety evaluator (§4 Eligibility, §9 PvP Safety).
 *
 * Pure, header-only rules so unit tests can verify every safety constraint:
 *   - Real players only for player-targeted events (never attach to bots)
 *   - No PvP in sanctuaries, capitals, or safe zones
 *   - No bypassing faction restrictions or core PvP eligibility
 *   - No attacking players in flight, dungeon/BG, dead, or AFK in safe areas
 *   - Strict level difference enforcement (MaxLevelDiff)
 *   - Distance and party-size checks
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_ELIGIBILITY_H
#define COA_PLAYERBOTS_ENCOUNTER_ELIGIBILITY_H

#include "EncounterParticipant.h"
#include "EncounterTypes.h"
#include <cmath>
#include <cstdlib>

struct TargetPlayerSnapshot
{
    ObjectGuid guid;
    bool isRealPlayer = true;           // False if player is actually a playerbot
    bool isOnlineAndInWorld = true;
    bool isAlive = true;
    bool isInFlight = false;
    bool isInDungeonOrBG = false;
    bool isInSanctuary = false;
    bool isInCapitalOrSafeZone = false;
    bool isContestedOrPvPArea = true;
    bool isPvPFlaggedOrAttackable = true;
    bool isAFK = false;
    bool isInCombat = false;
    bool isGameMaster = false;

    uint32 mapId = 0;
    uint32 zoneId = 0;
    uint32 areaId = 0;
    uint32 teamId = 0; // 0 = Alliance, 1 = Horde
    uint8 level = 60;
    uint32 partySize = 1;

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float distanceToCapitalYards = 500.0f;
};

struct CandidateBotSnapshot
{
    BotActivitySnapshot activity;
    uint32 mapId = 0;
    uint32 zoneId = 0;
    uint32 areaId = 0;
    uint32 teamId = 0; // 0 = Alliance, 1 = Horde
    uint8 level = 60;
    uint8 classId = 1;

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float healthPct = 100.0f;
    float distanceToCapitalYards = 500.0f;

    bool isInSanctuary = false;
    bool isInCapitalOrSafeZone = false;
    bool isContestedOrPvPArea = true;
    bool canLegallyAttackTarget = true; // Result of core IsValidAttackTarget / PvP rule check
};

class EncounterEligibility
{
public:
    static float Distance2D(float ax, float ay, float bx, float by)
    {
        return std::hypot(ax - bx, ay - by);
    }

    static uint32 LevelDifference(uint8 levelA, uint8 levelB)
    {
        return static_cast<uint32>(std::abs(static_cast<int>(levelA) - static_cast<int>(levelB)));
    }

    // Evaluates whether a real player can be targeted by a personal encounter.
    static bool IsPlayerEligibleForEncounter(
        EncounterType type,
        TargetPlayerSnapshot const& player,
        EncounterConfig const& cfg)
    {
        if (player.guid.IsEmpty() || !player.isRealPlayer)
            return false;
        if (!player.isOnlineAndInWorld || !player.isAlive || player.isGameMaster)
            return false;
        if (player.isInFlight || player.isInDungeonOrBG)
            return false;
        if (player.isInSanctuary)
            return false;

        if (IsHostilePlayerEncounter(type))
        {
            // Hostile personal encounters (Assassin, Bounty, Nemesis):
            // Never in capitals or safe zones, never when AFK in safe zone,
            // never when PvP is impossible or player is already in heavy group.
            if (player.isInCapitalOrSafeZone || !player.isContestedOrPvPArea || !player.isPvPFlaggedOrAttackable)
                return false;
            if (player.isAFK)
                return false;
            if (player.distanceToCapitalYards < cfg.minCityDistanceYards)
                return false;
            if (type == EncounterType::AssassinContract && player.partySize > cfg.maxPlayerPartySizeForSoloAssassin)
                return false;
        }
        else if (type == EncounterType::TheWatcher || type == EncounterType::MysteryEncounter)
        {
            // Non-combat atmospheric encounters: still avoid capitals/sanctuaries and AFK players.
            if (player.isInCapitalOrSafeZone || player.isAFK)
                return false;
        }

        return true;
    }

    // Evaluates whether a candidate bot can participate in an encounter targeting `player`.
    static bool IsBotEligibleForPlayerEncounter(
        EncounterType type,
        CandidateBotSnapshot const& bot,
        TargetPlayerSnapshot const& player,
        EncounterConfig const& cfg)
    {
        if (!EncounterPriorityArbiter::CanClaimBot(bot.activity))
            return false;
        if (bot.mapId != player.mapId)
            return false;
        if (bot.isInSanctuary || bot.isInCapitalOrSafeZone)
            return false;
        if (bot.healthPct < 65.0f)
            return false;

        float dist = Distance2D(bot.x, bot.y, player.x, player.y);
        if (dist > cfg.searchRadiusYards || dist < 12.0f)
            return false;

        if (LevelDifference(bot.level, player.level) > cfg.maxLevelDiff)
            return false;

        if (IsHostilePlayerEncounter(type))
        {
            // Must be opposite faction and legally able to PvP attack the player
            if (bot.teamId == player.teamId)
                return false;
            if (!bot.isContestedOrPvPArea || !bot.canLegallyAttackTarget)
                return false;
        }

        return true;
    }

    // Evaluates whether a candidate bot is eligible for a world/ambient encounter at (anchorX, anchorY).
    static bool IsBotEligibleForWorldEncounter(
        EncounterType type,
        CandidateBotSnapshot const& bot,
        uint32 expectedMapId,
        float anchorX,
        float anchorY,
        uint8 referenceLevel,
        EncounterConfig const& cfg)
    {
        if (!EncounterPriorityArbiter::CanClaimBot(bot.activity))
            return false;
        if (bot.mapId != expectedMapId)
            return false;
        if (bot.isInSanctuary)
            return false;
        if (bot.healthPct < 60.0f && type != EncounterType::Deserter)
            return false;

        float dist = Distance2D(bot.x, bot.y, anchorX, anchorY);
        if (dist > cfg.searchRadiusYards)
            return false;

        if (referenceLevel > 0 && LevelDifference(bot.level, referenceLevel) > cfg.maxLevelDiff)
            return false;

        if (IsPvPEncounter(type))
        {
            if (bot.isInCapitalOrSafeZone || !bot.isContestedOrPvPArea)
                return false;
            if (bot.distanceToCapitalYards < cfg.minCityDistanceYards)
                return false;
        }

        return true;
    }

    // Evaluates whether two bots can form a pair for Road Duel (same faction) or Hunter & Prey (opposite faction).
    static bool AreBotsCompatibleForPair(
        EncounterType type,
        CandidateBotSnapshot const& botA,
        CandidateBotSnapshot const& botB,
        EncounterConfig const& cfg)
    {
        if (botA.activity.botGuid == botB.activity.botGuid)
            return false;
        if (!EncounterPriorityArbiter::CanClaimBot(botA.activity) ||
            !EncounterPriorityArbiter::CanClaimBot(botB.activity))
            return false;
        if (botA.mapId != botB.mapId)
            return false;
        if (botA.isInSanctuary || botB.isInSanctuary ||
            botA.isInCapitalOrSafeZone || botB.isInCapitalOrSafeZone)
            return false;
        if (LevelDifference(botA.level, botB.level) > cfg.maxLevelDiff)
            return false;

        float dist = Distance2D(botA.x, botA.y, botB.x, botB.y);
        if (dist > cfg.searchRadiusYards)
            return false;

        if (type == EncounterType::RoadDuel)
        {
            return botA.teamId == botB.teamId && botA.healthPct >= 80.0f && botB.healthPct >= 80.0f;
        }
        if (type == EncounterType::HunterAndPrey || type == EncounterType::FactionSkirmish || type == EncounterType::PatrolAmbush)
        {
            return botA.teamId != botB.teamId &&
                   botA.isContestedOrPvPArea && botB.isContestedOrPvPArea &&
                   botA.canLegallyAttackTarget &&
                   botA.distanceToCapitalYards >= cfg.minCityDistanceYards &&
                   botB.distanceToCapitalYards >= cfg.minCityDistanceYards;
        }
        return true;
    }

    // Mid-encounter check for a player-targeted encounter; returns stop reason if the player
    // logs out, dies, flies away, teleports/changes map, enters safe zone/sanctuary, or exceeds max distance.
    static EncounterStopReason CheckOngoingPlayerEligibility(
        EncounterType type,
        TargetPlayerSnapshot const& player,
        uint32 expectedMapId,
        float distToBotYards,
        EncounterConfig const& cfg)
    {
        if (!player.isOnlineAndInWorld)
            return EncounterStopReason::PlayerLogout;
        if (!player.isAlive)
            return EncounterStopReason::ParticipantDeath;
        if (player.mapId != expectedMapId)
            return EncounterStopReason::MapChanged;
        if (player.isInDungeonOrBG)
            return EncounterStopReason::EnteredInstanceOrBG;
        if (player.isInFlight)
            return EncounterStopReason::PlayerInFlight;
        if (player.isInSanctuary || player.isInCapitalOrSafeZone)
            return EncounterStopReason::EnteredSafeZone;
        if (distToBotYards > cfg.maxChaseDistanceYards)
            return EncounterStopReason::TooFar;

        if (IsHostilePlayerEncounter(type))
        {
            if (!player.isContestedOrPvPArea || !player.isPvPFlaggedOrAttackable)
                return EncounterStopReason::PvPEligibilityLost;
            if (type == EncounterType::AssassinContract && player.partySize > cfg.maxPlayerPartySizeForSoloAssassin)
                return EncounterStopReason::PlayerGroupTooLarge;
        }

        return EncounterStopReason::None;
    }
};

#endif // COA_PLAYERBOTS_ENCOUNTER_ELIGIBILITY_H
