#include "BotDungeonEncounters.h"
#include "InstanceScript.h"
#include "Map.h"
#include "ObjectMgr.h"
#include <unordered_map>

namespace
{
    struct MapDifficultyKey
    {
        uint32 mapId = 0;
        uint8 difficulty = 0;
        bool operator==(MapDifficultyKey const& other) const
        {
            return mapId == other.mapId && difficulty == other.difficulty;
        }
    };

    struct MapDifficultyKeyHash
    {
        size_t operator()(MapDifficultyKey const& key) const
        {
            return std::hash<uint64>()((uint64(key.mapId) << 8) | key.difficulty);
        }
    };

    std::unordered_map<MapDifficultyKey, std::unordered_map<uint32, uint32>, MapDifficultyKeyHash> _cache;

    // creditEntry -> encounterIndex for one map/difficulty, built once from
    // ObjectMgr::GetDungeonEncounterList and cached forever (this is static server content).
    std::unordered_map<uint32, uint32> const& CreditEntryToEncounterIndex(uint32 mapId, Difficulty difficulty)
    {
        MapDifficultyKey key{ mapId, uint8(difficulty) };
        auto it = _cache.find(key);
        if (it != _cache.end())
            return it->second;

        std::unordered_map<uint32, uint32> lookup;
        if (DungeonEncounterList const* list = sObjectMgr->GetDungeonEncounterList(mapId, difficulty))
        {
            for (DungeonEncounter const* encounter : *list)
            {
                if (encounter && encounter->dbcEntry && encounter->creditEntry)
                    lookup[encounter->creditEntry] = encounter->dbcEntry->encounterIndex;
            }
        }

        return _cache.emplace(key, std::move(lookup)).first->second;
    }
}

namespace BotDungeonEncounters
{
    bool HasEncounterData(Map* map, uint32 creatureEntry)
    {
        if (!map)
            return false;
        auto const& lookup = CreditEntryToEncounterIndex(map->GetId(), map->GetDifficulty());
        return lookup.find(creatureEntry) != lookup.end();
    }

    bool IsEncounterCompleted(Map* map, uint32 creatureEntry)
    {
        if (!map)
            return false;

        auto const& lookup = CreditEntryToEncounterIndex(map->GetId(), map->GetDifficulty());
        auto it = lookup.find(creatureEntry);
        if (it == lookup.end())
            return false;

        InstanceMap* instanceMap = map->ToInstanceMap();
        InstanceScript* instance = instanceMap ? instanceMap->GetInstanceScript() : nullptr;
        if (!instance)
            return false;

        return (instance->GetCompletedEncounterMask() & (1u << it->second)) != 0;
    }

    int32 GetEncounterOrderIndex(Map* map, uint32 creatureEntry)
    {
        if (!map)
            return -1;

        auto const& lookup = CreditEntryToEncounterIndex(map->GetId(), map->GetDifficulty());
        auto it = lookup.find(creatureEntry);
        return it != lookup.end() ? int32(it->second) : -1;
    }
}
