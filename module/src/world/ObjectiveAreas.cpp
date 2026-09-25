#include "ObjectiveAreas.h"
#include "SpawnClustering.h"
#include <algorithm>
#include <deque>
#include <unordered_map>

namespace
{
    // Spawns within this of each other on the ground are one camp...
    constexpr float LINK_YARDS = 45.0f;
    // ...unless this far apart vertically (a mine's upper and lower level, a cliff-top camp).
    constexpr float VERTICAL_GAP_YARDS = 14.0f;
    // A camp wider than this is cut up: one entry spawned along a whole river is not one place.
    constexpr float MAX_AREA_RADIUS = 120.0f;
    // Points kept per area for search wandering.
    constexpr size_t MAX_WANDER_POINTS = 48;

    std::deque<ObjectiveArea> _areas;
    std::unordered_map<uint64, std::vector<uint32>> _byEntry;
    std::vector<uint32> const _empty;

    uint64 Key(bool gameObject, uint32 entry)
    {
        return (uint64(gameObject) << 32) | entry;
    }

    std::vector<uint32> const& Build(bool gameObject, uint32 entry)
    {
        std::vector<uint32>& ids = _byEntry[Key(gameObject, entry)];
        std::vector<SpawnPoint> const* spawns = gameObject ? QuestKB::GameObjectSpawns(entry) : QuestKB::CreatureSpawns(entry);
        if (!spawns || spawns->empty())
            return ids;

        std::unordered_map<uint32, std::vector<SpawnPoint const*>> byMap;
        for (SpawnPoint const& sp : *spawns)
            byMap[sp.mapId].push_back(&sp);

        for (auto const& [mapId, mapSpawns] : byMap)
        {
            std::vector<ClusterInputPoint> points;
            points.reserve(mapSpawns.size());
            for (SpawnPoint const* sp : mapSpawns)
                points.push_back(ClusterInputPoint{ sp->x, sp->y, sp->z, sp->phaseMask });

            for (auto const& members : SpawnClustering::Cluster(points, LINK_YARDS, VERTICAL_GAP_YARDS, MAX_AREA_RADIUS))
            {
                ClusterShape shape = SpawnClustering::Shape(points, members);
                ObjectiveArea& area = _areas.emplace_back();
                area.id = uint32(_areas.size());
                area.mapId = mapId;
                area.gameObject = gameObject;
                area.entry = entry;
                area.x = points[shape.anchor].x;
                area.y = points[shape.anchor].y;
                area.z = points[shape.anchor].z;
                area.cx = shape.cx;
                area.cy = shape.cy;
                area.radius = shape.radius;
                area.spawnCount = uint32(members.size());
                // Every member shares it (SpawnClustering never links different phases).
                area.phaseMask = mapSpawns[members.front()]->phaseMask;

                // Spread the kept wander points across the area rather than taking the first N.
                size_t step = std::max<size_t>(1, members.size() / MAX_WANDER_POINTS);
                for (size_t i = 0; i < members.size(); ++i)
                {
                    SpawnPoint const* sp = mapSpawns[members[i]];
                    if (i % step == 0 && area.points.size() < MAX_WANDER_POINTS)
                        area.points.push_back(*sp);
                }
                ids.push_back(area.id);
            }
        }
        return ids;
    }
}

namespace ObjectiveAreas
{
    std::vector<uint32> const& ForEntry(bool gameObject, uint32 entry)
    {
        auto itr = _byEntry.find(Key(gameObject, entry));
        if (itr != _byEntry.end())
            return itr->second;
        if (!QuestKB::IsReady())
            return _empty;
        return Build(gameObject, entry);
    }

    ObjectiveArea const* Get(uint32 areaId)
    {
        if (!areaId || areaId > _areas.size())
            return nullptr;
        return &_areas[areaId - 1];
    }

    size_t CachedAreas()
    {
        return _areas.size();
    }
}
