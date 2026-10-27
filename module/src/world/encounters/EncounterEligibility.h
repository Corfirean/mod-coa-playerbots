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

enum class EncounterCombatRole : uint8
{
    Dps     = 0,
    Tank    = 1,
    Healer  = 2,
    Support = 3
};

inline char const* EncounterCombatRoleName(EncounterCombatRole role)
{
    switch (role)
    {
        case EncounterCombatRole::Dps:     return "Dps";
        case EncounterCombatRole::Tank:    return "Tank";
        case EncounterCombatRole::Healer:  return "Healer";
        case EncounterCombatRole::Support: return "Support";
        default:                           return "Unknown";
    }
}

// Capability profile derived from a bot's actual active spec and spellbook (SpellInfo),
// completely independent of vanilla class identity (supports CoA custom classes 12-32).
struct AssassinCapability
{
    uint8 classId = 0;
    uint32 specId = 0;
    uint32 spellCount = 0;
    EncounterCombatRole combatRole = EncounterCombatRole::Dps;
    uint32 stealthSpellId = 0;       // Usable spell granting stealth / invisibility / stealth form
    uint32 openerSpellId = 0;        // Usable stealth opener / ambush / gap-closer strike
    bool hasStealthOrInvis = false;  // True when stealthSpellId != 0
    bool hasStealthOpener = false;   // True when openerSpellId != 0
    bool isMeleeStalker = false;     // True when spec/spellbook is melee-oriented DPS
};

// Caches resolved AssassinCapability by (classId, specId).
class AssassinCapabilityCache
{
public:
    static constexpr uint64 MakeKey(uint8 classId, uint32 specId)
    {
        return (static_cast<uint64>(classId) << 32) | static_cast<uint64>(specId);
    }

    bool TryGet(uint8 classId, uint32 specId, AssassinCapability& outCap) const
    {
        auto it = _entries.find(MakeKey(classId, specId));
        if (it == _entries.end())
            return false;
        outCap = it->second;
        return true;
    }

    void Store(AssassinCapability const& cap)
    {
        _entries[MakeKey(cap.classId, cap.specId)] = cap;
    }

    void Clear()
    {
        _entries.clear();
    }

    size_t Size() const
    {
        return _entries.size();
    }

private:
    std::unordered_map<uint64, AssassinCapability> _entries;
};

struct CandidateBotSnapshot
{
    BotActivitySnapshot activity;
    uint32 mapId = 0;
    uint32 zoneId = 0;
    uint32 areaId = 0;
    uint32 teamId = 0; // 0 = Alliance, 1 = Horde
    uint8 level = 60;
    uint8 classId = 12;
    uint32 specId = 0;

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float healthPct = 100.0f;
    float distanceToCapitalYards = 500.0f;

    bool isInSanctuary = false;
    bool isInCapitalOrSafeZone = false;
    bool isContestedOrPvPArea = true;
    bool canLegallyAttackTarget = true; // Result of core IsValidAttackTarget / PvP rule check
    bool hasStealthSpell = false;       // True if bot has a usable stealth/invisibility spell in its spellbook/spec
    AssassinCapability assassinCap;
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

    // Shared helper (§6): checks whether a bot's level is within [referenceLevel + minDelta, referenceLevel + maxDelta].
    static bool IsLevelCompatible(uint8 candidateLevel, uint8 referenceLevel, int32 minDelta, int32 maxDelta)
    {
        int32 delta = static_cast<int32>(candidateLevel) - static_cast<int32>(referenceLevel);
        return delta >= minDelta && delta <= maxDelta;
    }

    // Assassin Contract (§1): strict [playerLevel, playerLevel + 3], never lower, never > +3.
    static bool IsAssassinLevelCompatible(uint8 botLevel, uint8 playerLevel)
    {
        return IsLevelCompatible(botLevel, playerLevel, 0, 3);
    }

    // Hunter and Prey (§4): strict abs(hunterLevel - preyLevel) <= 2.
    static bool IsHunterPreyLevelCompatible(uint8 hunterLevel, uint8 preyLevel)
    {
        return LevelDifference(hunterLevel, preyLevel) <= 2;
    }

    // Caravan & Rival Party (§3, §5): guards/members within +-3 levels of leader.
    static bool IsPartyMemberLevelCompatible(uint8 memberLevel, uint8 leaderLevel, uint32 maxDiff = 3)
    {
        return LevelDifference(memberLevel, leaderLevel) <= maxDiff;
    }

    // Shared helper (§6): strict opposing faction check (Alliance vs Horde) + legal attack check.
    static bool AreOpposingFactions(uint32 teamA, uint32 teamB, bool canLegallyAttack = true)
    {
        return (teamA == 0 || teamA == 1) && (teamB == 0 || teamB == 1) &&
               (teamA != teamB) && canLegallyAttack;
    }

    // Shared helper (§6): true only when bot is free from player groups, dungeons/BG, manual commands,
    // existing WorldParties, social rescue, looting/corpse run, and other encounters.
    static bool IsAvailableForEncounter(BotActivitySnapshot const& snap, bool allowWorldPartyMember = false)
    {
        return EncounterPriorityArbiter::CanClaimBot(snap, allowWorldPartyMember);
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

        if (type == EncounterType::AssassinContract)
        {
            if (!IsAssassinLevelCompatible(bot.level, player.level))
                return false;
        }
        else if (LevelDifference(bot.level, player.level) > cfg.maxLevelDiff)
        {
            return false;
        }

        if (IsHostilePlayerEncounter(type))
        {
            // Must be opposite faction and legally able to PvP attack the player
            if (!AreOpposingFactions(bot.teamId, player.teamId, bot.canLegallyAttackTarget))
                return false;
            if (!bot.isContestedOrPvPArea)
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
        EncounterConfig const& cfg,
        bool bypassRestrictions = false)
    {
        if (botA.activity.botGuid == botB.activity.botGuid)
            return false;
        if (botA.mapId != botB.mapId)
            return false;

        if (!bypassRestrictions)
        {
            if (!EncounterPriorityArbiter::CanClaimBot(botA.activity) ||
                !EncounterPriorityArbiter::CanClaimBot(botB.activity))
                return false;
            if (botA.isInSanctuary || botB.isInSanctuary ||
                botA.isInCapitalOrSafeZone || botB.isInCapitalOrSafeZone)
                return false;
            if (LevelDifference(botA.level, botB.level) > cfg.maxLevelDiff)
                return false;

            float dist = Distance2D(botA.x, botA.y, botB.x, botB.y);
            if (dist > cfg.searchRadiusYards)
                return false;
        }

        if (type == EncounterType::RoadDuel)
        {
            if (!IsAvailableForEncounter(botA.activity) || !IsAvailableForEncounter(botB.activity))
                return false;
            if (botA.teamId != botB.teamId)
                return false;
            if (!IsPartyMemberLevelCompatible(botA.level, botB.level, 3))
                return false;
            if (bypassRestrictions)
                return true;
            return botA.healthPct >= 80.0f && botB.healthPct >= 80.0f;
        }
        if (type == EncounterType::HunterAndPrey)
        {
            if (!IsAvailableForEncounter(botA.activity) || !IsAvailableForEncounter(botB.activity))
                return false;
            bool legalPvp = bypassRestrictions || (botA.isContestedOrPvPArea && botB.isContestedOrPvPArea && botA.canLegallyAttackTarget);
            if (!AreOpposingFactions(botA.teamId, botB.teamId, legalPvp))
                return false;
            if (!IsHunterPreyLevelCompatible(botA.level, botB.level))
                return false;
            if (bypassRestrictions)
                return true;
            return botA.distanceToCapitalYards >= cfg.minCityDistanceYards &&
                   botB.distanceToCapitalYards >= cfg.minCityDistanceYards;
        }
        if (type == EncounterType::FactionSkirmish || type == EncounterType::PatrolAmbush)
        {
            bool legalPvp = bypassRestrictions || (botA.isContestedOrPvPArea && botB.isContestedOrPvPArea && botA.canLegallyAttackTarget);
            if (!AreOpposingFactions(botA.teamId, botB.teamId, legalPvp))
                return false;
            if (bypassRestrictions)
                return true;
            return botA.distanceToCapitalYards >= cfg.minCityDistanceYards &&
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
        if (distToBotYards > cfg.maxChaseDistanceYards)
            return EncounterStopReason::TooFar;

        if (IsHostilePlayerEncounter(type))
        {
            if (player.isInSanctuary || player.isInCapitalOrSafeZone)
                return EncounterStopReason::EnteredSafeZone;
            if (!player.isContestedOrPvPArea || !player.isPvPFlaggedOrAttackable)
                return EncounterStopReason::PvPEligibilityLost;
            if (type == EncounterType::AssassinContract && player.partySize > cfg.maxPlayerPartySizeForSoloAssassin)
                return EncounterStopReason::PlayerGroupTooLarge;
        }

        return EncounterStopReason::None;
    }
};

#endif // COA_PLAYERBOTS_ENCOUNTER_ELIGIBILITY_H
