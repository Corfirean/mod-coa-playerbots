/*
 * mod-coa-playerbots -- CoA World Map Intelligence
 * Event snapshot definitions and provider interface for dynamic world events.
 */

#ifndef COA_PLAYERBOTS_WORLD_MAP_EVENT_SNAPSHOT_H
#define COA_PLAYERBOTS_WORLD_MAP_EVENT_SNAPSHOT_H

#include "Define.h"
#include <string>
#include <vector>
#include <memory>

enum class WorldMapEventVisibility : uint8
{
    Public = 0,
    Hidden = 1,
    Discoverable = 2
};

enum class WorldMapEventType : uint8
{
    None = 0,
    FactionSkirmish = 1,
    PatrolAmbush = 2,
    BountyHunter = 3,
    AssassinContract = 4,
    TheWatcher = 5,
    Caravan = 6,
    RivalParty = 7,
    RareHunter = 8,
    Other = 9
};

struct WorldMapEventSnapshot
{
    uint64 eventId = 0;
    WorldMapEventType eventType = WorldMapEventType::None;
    uint32 mapId = 0;
    uint32 zoneId = 0;
    float centerX = 0.0f;
    float centerY = 0.0f;
    uint8 phase = 0; // 0=Candidate, 1=Prepare, 2=Active, 3=Resolve, 4=Cooldown
    WorldMapEventVisibility visibility = WorldMapEventVisibility::Hidden;
    uint32 allianceCount = 0;
    uint32 hordeCount = 0;
    std::string name;

    bool IsActive() const { return phase == 2; } // Active phase
};

class IWorldEventProvider
{
public:
    virtual ~IWorldEventProvider() = default;
    virtual std::vector<WorldMapEventSnapshot> GetActiveEvents(uint32 mapId) = 0;
    virtual bool IsBotInActiveEncounter(uint64 botGuidLow) = 0;
};

#endif // COA_PLAYERBOTS_WORLD_MAP_EVENT_SNAPSHOT_H
