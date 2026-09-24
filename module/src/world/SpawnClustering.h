/*
 * mod-coa-playerbots
 *
 * Turns a creature's (or object's) static spawn points into "places": the kobold camp north of
 * the mine, the entrance, the lower level. The quest layer plans against these places and never
 * against a single spawn coordinate -- a spawn coordinate is where one mob *might* be, and walking
 * a hundred bots to it is how the old tracker produced crowds standing on an empty spot.
 *
 * Algorithm, chosen to be boring and cheap:
 *  1. single-link grouping: two spawns belong together when they are within `linkDistance` on
 *     the ground and within `maxVerticalGap` in height (so a mine's upper and lower levels, or a
 *     cliff-top camp above a valley camp, stay separate places);
 *  2. any group wider than `maxRadius` is cut into grid-sized pieces, because a creature spawned
 *     along an entire river is not one destination;
 *  3. spawns in different phases are never linked: a bot sees a place only if it sees every spawn
 *     in it, so its anchor, its spawn count and its wander points are all real for that bot. (An
 *     area whose phase was the union of its members' let a phase-1 bot walk to a phase-2 anchor.)
 * Runs once per entry, lazily, over at most a few thousand points -- no library needed.
 *
 * Pure: plain points in, index groups out. Unit-tested standalone in module/tests.
 */

#ifndef COA_PLAYERBOTS_SPAWN_CLUSTERING_H
#define COA_PLAYERBOTS_SPAWN_CLUSTERING_H

#include "Define.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <vector>

struct ClusterInputPoint
{
    float x;
    float y;
    float z;
    uint32 phaseMask = 0; // only points with the same mask are linked
};

struct ClusterShape
{
    float cx = 0.0f;
    float cy = 0.0f;
    float cz = 0.0f;
    float radius = 0.0f;
    // Member closest to the centroid: a real spawn position, so it is walkable ground, unlike the
    // centroid itself which can land inside a rock or over water.
    uint32 anchor = 0;
};

namespace SpawnClustering
{
    inline int64 GridKey(int32 cx, int32 cy)
    {
        return (int64(cx) << 32) ^ int64(uint32(cy));
    }

    inline ClusterShape Shape(std::vector<ClusterInputPoint> const& points, std::vector<uint32> const& members)
    {
        ClusterShape shape;
        if (members.empty())
            return shape;

        double sx = 0, sy = 0, sz = 0;
        for (uint32 i : members)
        {
            sx += points[i].x;
            sy += points[i].y;
            sz += points[i].z;
        }
        shape.cx = float(sx / members.size());
        shape.cy = float(sy / members.size());
        shape.cz = float(sz / members.size());

        float best = -1.0f;
        for (uint32 i : members)
        {
            float d = std::hypot(points[i].x - shape.cx, points[i].y - shape.cy);
            shape.radius = std::max(shape.radius, d);
            if (best < 0.0f || d < best)
            {
                best = d;
                shape.anchor = i;
            }
        }
        return shape;
    }

    // Returns groups of indices into `points`, largest group first. Every input point lands in
    // exactly one group.
    inline std::vector<std::vector<uint32>> Cluster(std::vector<ClusterInputPoint> const& points,
        float linkDistance, float maxVerticalGap, float maxRadius)
    {
        std::vector<std::vector<uint32>> result;
        if (points.empty())
            return result;

        uint32 n = uint32(points.size());
        std::vector<uint32> parent(n);
        std::iota(parent.begin(), parent.end(), 0u);

        auto find = [&parent](uint32 i)
        {
            while (parent[i] != i)
            {
                parent[i] = parent[parent[i]];
                i = parent[i];
            }
            return i;
        };

        auto cellOf = [linkDistance](float v) { return int32(std::floor(v / linkDistance)); };

        std::unordered_map<int64, std::vector<uint32>> grid;
        grid.reserve(n);
        for (uint32 i = 0; i < n; ++i)
            grid[GridKey(cellOf(points[i].x), cellOf(points[i].y))].push_back(i);

        float linkSq = linkDistance * linkDistance;
        for (uint32 i = 0; i < n; ++i)
        {
            int32 cx = cellOf(points[i].x);
            int32 cy = cellOf(points[i].y);
            for (int32 dx = -1; dx <= 1; ++dx)
            {
                for (int32 dy = -1; dy <= 1; ++dy)
                {
                    auto itr = grid.find(GridKey(cx + dx, cy + dy));
                    if (itr == grid.end())
                        continue;
                    for (uint32 j : itr->second)
                    {
                        if (j <= i)
                            continue;
                        float ddx = points[i].x - points[j].x;
                        float ddy = points[i].y - points[j].y;
                        if (ddx * ddx + ddy * ddy > linkSq)
                            continue;
                        if (std::fabs(points[i].z - points[j].z) > maxVerticalGap)
                            continue;
                        if (points[i].phaseMask != points[j].phaseMask)
                            continue;
                        uint32 a = find(i);
                        uint32 b = find(j);
                        if (a != b)
                            parent[std::max(a, b)] = std::min(a, b);
                    }
                }
            }
        }

        std::unordered_map<uint32, std::vector<uint32>> components;
        for (uint32 i = 0; i < n; ++i)
            components[find(i)].push_back(i);

        for (auto& [root, members] : components)
        {
            ClusterShape shape = Shape(points, members);
            if (shape.radius <= maxRadius || members.size() < 2)
            {
                result.push_back(std::move(members));
                continue;
            }

            // Too wide to be one destination: cut it into maxRadius-sized pieces.
            float piece = std::max(maxRadius, 1.0f);
            std::unordered_map<int64, std::vector<uint32>> pieces;
            for (uint32 i : members)
                pieces[GridKey(int32(std::floor(points[i].x / piece)), int32(std::floor(points[i].y / piece)))].push_back(i);
            for (auto& [key, part] : pieces)
                result.push_back(std::move(part));
        }

        // Deterministic order: biggest place first, ties by lowest member index.
        for (auto& group : result)
            std::sort(group.begin(), group.end());
        std::sort(result.begin(), result.end(), [](std::vector<uint32> const& a, std::vector<uint32> const& b)
        {
            if (a.size() != b.size())
                return a.size() > b.size();
            return a.front() < b.front();
        });
        return result;
    }
}

#endif // COA_PLAYERBOTS_SPAWN_CLUSTERING_H
