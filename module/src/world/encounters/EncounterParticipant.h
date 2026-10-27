/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- participant state, priority arbitration against
 * higher-priority bot systems, and non-overlapping ownership registry.
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_PARTICIPANT_H
#define COA_PLAYERBOTS_ENCOUNTER_PARTICIPANT_H

#include "BotMovementPrimitives.h"
#include "EncounterTypes.h"
#include <unordered_map>
#include <vector>

// Snapshot of a bot's current activity state used to verify that an encounter
// never steals a bot from a higher-priority system (§11 Grouping & Priority Rules).
struct BotActivitySnapshot
{
    ObjectGuid botGuid;
    bool isOnlineAndInWorld = true;
    bool isAlive = true;
    bool isInFlight = false;
    bool inDungeonOrBG = false;
    bool inBattlegroundQueueCritical = false;
    bool hasManualCommand = false;       // Manual Stay/Follow/etc.
    bool inPlayerControlledGroup = false;// Real player or LFG/dungeon group
    bool inWorldParty = false;           // Existing WorldParties open-world party
    bool isHelpingSocial = false;        // WorldSocial rescue/help in progress
    bool isLootingOrCorpseRun = false;   // Active Loot or Corpse movement owner
    bool isCastingGatherOrFish = false;  // Active gathering/fishing cast
    bool alreadyInEncounter = false;     // Owned by another EncounterInstance
};

class EncounterPriorityArbiter
{
public:
    // Returns true ONLY if the bot is genuinely free in the open world and not
    // controlled by any higher-priority system.
    static bool CanClaimBot(BotActivitySnapshot const& snap, bool allowWorldPartyMember = false)
    {
        if (snap.botGuid.IsEmpty() || !snap.isOnlineAndInWorld || !snap.isAlive)
            return false;
        if (snap.isInFlight || snap.inDungeonOrBG || snap.inBattlegroundQueueCritical)
            return false;
        if (snap.hasManualCommand || snap.inPlayerControlledGroup)
            return false;
        if (!allowWorldPartyMember && snap.inWorldParty)
            return false;
        if (snap.isHelpingSocial || snap.isLootingOrCorpseRun || snap.isCastingGatherOrFish)
            return false;
        if (snap.alreadyInEncounter)
            return false;
        return true;
    }

    // Returns the stop reason if a currently participating bot becomes ineligible
    // mid-encounter due to a higher-priority preemption or world state change.
    static EncounterStopReason CheckMidEncounterPreemption(
        BotActivitySnapshot const& snap,
        uint32 expectedMapId,
        uint32 currentMapId)
    {
        if (!snap.isOnlineAndInWorld)
            return EncounterStopReason::BotLogoutOrDespawn;
        if (!snap.isAlive)
            return EncounterStopReason::ParticipantDeath;
        if (currentMapId != expectedMapId)
            return EncounterStopReason::MapChanged;
        if (snap.inDungeonOrBG)
            return EncounterStopReason::EnteredInstanceOrBG;
        if (snap.hasManualCommand)
            return EncounterStopReason::ManualBotCommand;
        if (snap.inPlayerControlledGroup)
            return EncounterStopReason::JoinedPlayerGroup;
        if (snap.isInFlight)
            return EncounterStopReason::PlayerInFlight;
        return EncounterStopReason::None;
    }
};

struct EncounterParticipant
{
    ObjectGuid botGuid;
    EncounterRole role = EncounterRole::None;
    uint32 teamId = 0; // TEAM_ALLIANCE (0) or TEAM_HORDE (1)
    uint32 mapId = 0;
    float originX = 0.0f;
    float originY = 0.0f;
    float originZ = 0.0f;

    LocomotionToken moveToken;
    ObjectGuid assignedTargetGuid;
    uint64 navGoalId = 0;

    uint32 joinedAtMs = 0;
    uint32 lastActionMs = 0;
    uint32 originalDisplayId = 0;
    uint32 originalFaction = 0;
    uint32 temporaryGobGuidLow = 0;
    uint32 temporaryGobMapId = 0;
    ObjectGuid temporaryGobGuid;

    float originalHealthPct = 100.0f;
    float originalRunSpeedRate = 1.0f;
    float lastNavTargetX = 0.0f;
    float lastNavTargetY = 0.0f;
    float lastNavTargetZ = 0.0f;
    uint32 subState = 0;
    uint32 subStateEnteredAtMs = 0;
    uint32 stealthSpellId = 0;
    uint32 openerSpellId = 0;

    float cautiousness = 0.5f; // Used for cowardly/cautious early PvP retreat check
    bool stealthCapable = false;
    bool stealthApplied = false;
    bool sitApplied = false;
    bool morphApplied = false;
    bool speedBoostApplied = false;
    bool engagedInCombat = false;
    bool arrivedAtStaging = false;
    bool emoteDone = false;
    bool narratorSpoken = false;
    bool retreated = false;
};

// Pure ownership registry ensuring each bot belongs to at most one encounter at a time.
class EncounterOwnershipTable
{
public:
    bool IsOwned(ObjectGuid botGuid) const
    {
        return _botToEncounter.find(botGuid) != _botToEncounter.end();
    }

    uint64 GetEncounterId(ObjectGuid botGuid) const
    {
        auto it = _botToEncounter.find(botGuid);
        return it != _botToEncounter.end() ? it->second : 0;
    }

    bool TryClaim(ObjectGuid botGuid, uint64 encounterId)
    {
        if (botGuid.IsEmpty() || encounterId == 0)
            return false;
        auto [it, inserted] = _botToEncounter.emplace(botGuid, encounterId);
        return inserted;
    }

    bool Release(ObjectGuid botGuid, uint64 expectedEncounterId = 0)
    {
        auto it = _botToEncounter.find(botGuid);
        if (it == _botToEncounter.end())
            return false;
        if (expectedEncounterId != 0 && it->second != expectedEncounterId)
            return false;
        _botToEncounter.erase(it);
        return true;
    }

    void ReleaseAllForEncounter(uint64 encounterId)
    {
        for (auto it = _botToEncounter.begin(); it != _botToEncounter.end();)
        {
            if (it->second == encounterId)
                it = _botToEncounter.erase(it);
            else
                ++it;
        }
    }

    size_t Size() const { return _botToEncounter.size(); }
    void Clear() { _botToEncounter.clear(); }

private:
    std::unordered_map<ObjectGuid, uint64> _botToEncounter;
};

#endif // COA_PLAYERBOTS_ENCOUNTER_PARTICIPANT_H
