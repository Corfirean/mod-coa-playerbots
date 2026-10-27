/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- EncounterInstance data structure.
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_INSTANCE_H
#define COA_PLAYERBOTS_ENCOUNTER_INSTANCE_H

#include "EncounterParticipant.h"
#include "EncounterState.h"
#include "EncounterTypes.h"
#include <vector>

struct EncounterWaypoint
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct EncounterInstance
{
    uint64 id = 0;
    EncounterType type = EncounterType::None;
    EncounterStateMachine state;
    bool isForcedTest = false;

    uint32 mapId = 0;
    uint32 zoneId = 0;
    uint32 areaId = 0;

    // Center / staging / objective coordinates
    float centerX = 0.0f;
    float centerY = 0.0f;
    float centerZ = 0.0f;

    // Optional destination / route waypoints (Caravan, Patrol, HunterAndPrey, RivalParty)
    float destX = 0.0f;
    float destY = 0.0f;
    float destZ = 0.0f;
    std::vector<EncounterWaypoint> waypoints;
    size_t currentWaypointIdx = 0;

    // Target entities
    ObjectGuid targetPlayerGuid;    // Real player for personal encounters
    ObjectGuid targetCreatureGuid;  // Live rare creature for Rare Hunter
    uint32 targetCreatureEntry = 0;

    // Participants
    std::vector<EncounterParticipant> participants;

    // Handler-specific runtime state
    uint32 stalkAccumulatedMs = 0;
    uint32 subStage = 0;
    uint32 lastSubStageChangeMs = 0;
    bool introNarrated = false;
    bool climaxNarrated = false;
    bool duelCompleted = false;
    ObjectGuid duelWinnerGuid;
    ObjectGuid duelLoserGuid;

    EncounterParticipant* FindParticipant(ObjectGuid botGuid)
    {
        for (auto& p : participants)
        {
            if (p.botGuid == botGuid)
                return &p;
        }
        return nullptr;
    }

    EncounterParticipant const* FindParticipant(ObjectGuid botGuid) const
    {
        for (auto const& p : participants)
        {
            if (p.botGuid == botGuid)
                return &p;
        }
        return nullptr;
    }

    EncounterParticipant* FindByRole(EncounterRole role)
    {
        for (auto& p : participants)
        {
            if (p.role == role && !p.retreated)
                return &p;
        }
        return nullptr;
    }

    size_t ActiveParticipantCount() const
    {
        size_t n = 0;
        for (auto const& p : participants)
        {
            if (!p.retreated)
                ++n;
        }
        return n;
    }
};

#endif // COA_PLAYERBOTS_ENCOUNTER_INSTANCE_H
