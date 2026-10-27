#include "DangerMap.h"
#include "Common.h"
#include "CreatureData.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "UnitDefines.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <queue>
#include <tuple>
#include <unordered_map>

namespace
{
    constexpr float CELL = 40.0f;           // static grid cell, yards
    constexpr float STEP = 50.0f;           // route search grid, yards
    constexpr float SAMPLE = 25.0f;         // sampling interval along a segment, yards
    constexpr float MIN_ROUTE_YARDS = 150.0f;
    constexpr float BOX_PADDING = 250.0f;
    constexpr int MAX_GRID = 80;            // 4000 yards a side
    constexpr float RISK_MARGIN = 3.0f;     // levels above the bot that are still fine
    constexpr float RISK_CAP = 12.0f;
    constexpr float RISK_WEIGHT = 0.5f;     // extra cost per risk point, as a fraction of the distance
    constexpr float DEATH_WEIGHT = 4.0f;
    constexpr uint32 DEATH_MEMORY_MS = 30 * MINUTE * IN_MILLISECONDS;
    constexpr size_t MAX_WAYPOINTS = 6;

    struct StaticCell
    {
        uint8 eff[2] = { 0, 0 }; // highest effective level that attacks Alliance / Horde players on sight
    };

    struct DeathCell
    {
        float weight = 0.0f;
        uint32 atMs = 0;
    };

    std::unordered_map<uint64, StaticCell> s_static;
    std::unordered_map<uint64, DeathCell> s_deaths;
    std::mutex s_deathMutex;
    std::string s_summary = "danger map: not built";

    int32 CellOf(float v) { return int32(std::floor(v / CELL)); }

    uint64 Key(uint32 mapId, int32 cx, int32 cy)
    {
        return (uint64(mapId) << 40) | (uint64(uint32(cx + 32768) & 0xFFFFF) << 20) | uint64(uint32(cy + 32768) & 0xFFFFF);
    }

    bool OpenWorld(uint32 mapId)
    {
        return mapId == 0 || mapId == 1 || mapId == 530 || mapId == 571;
    }

    uint32 NowMs()
    {
        return uint32(GameTime::GetGameTimeMS().count());
    }

    // Static risk of one 40-yard cell for a bot of this level and team, without neighbours.
    float StaticRisk(uint32 mapId, int32 cx, int32 cy, uint8 level, uint8 team)
    {
        auto itr = s_static.find(Key(mapId, cx, cy));
        if (itr == s_static.end())
            return 0.0f;
        return std::clamp(float(itr->second.eff[team & 1]) - float(level) - RISK_MARGIN, 0.0f, RISK_CAP);
    }

    float DeathRisk(uint32 mapId, int32 cx, int32 cy, uint32 now)
    {
        std::lock_guard<std::mutex> lock(s_deathMutex);
        auto itr = s_deaths.find(Key(mapId, cx, cy));
        if (itr == s_deaths.end())
            return 0.0f;
        uint32 age = now - itr->second.atMs;
        if (age >= DEATH_MEMORY_MS)
            return 0.0f;
        return itr->second.weight * (1.0f - float(age) / float(DEATH_MEMORY_MS));
    }

    // Cost of walking `length` yards through a place of this risk.
    float Cost(float length, float risk)
    {
        return length * (1.0f + RISK_WEIGHT * risk);
    }

    float SegmentCost(uint32 mapId, float ax, float ay, float bx, float by, uint8 level, uint8 team)
    {
        float length = std::hypot(bx - ax, by - ay);
        if (length < 1.0f)
            return 0.0f;
        int samples = std::max(1, int(std::ceil(length / SAMPLE)));
        float cost = 0.0f;
        for (int i = 0; i < samples; ++i)
        {
            float t = (float(i) + 0.5f) / float(samples);
            cost += Cost(length / float(samples), DangerMap::Risk(mapId, ax + (bx - ax) * t, ay + (by - ay) * t, level, team));
        }
        return cost;
    }
}

namespace DangerMap
{
    void Initialize()
    {
        s_static.clear();
        uint32 spawns = 0;
        uint32 dangerous = 0;

        // Service flags that make a creature a townsperson, not a threat.
        constexpr uint32 SERVICE_FLAGS = UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_FLIGHTMASTER | UNIT_NPC_FLAG_INNKEEPER |
            UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_AUCTIONEER | UNIT_NPC_FLAG_SPIRITHEALER | UNIT_NPC_FLAG_SPIRITGUIDE |
            UNIT_NPC_FLAG_PETITIONER | UNIT_NPC_FLAG_STABLEMASTER | UNIT_NPC_FLAG_REPAIR | UNIT_NPC_FLAG_BATTLEMASTER;

        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            if (!OpenWorld(data.mapid))
                continue;
            CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(data.id);
            if (!t)
                continue;
            ++spawns;
            if ((t->npcflag & SERVICE_FLAGS) || t->type == CREATURE_TYPE_CRITTER || t->type == CREATURE_TYPE_TOTEM ||
                t->type == CREATURE_TYPE_NON_COMBAT_PET || t->type == CREATURE_TYPE_GAS_CLOUD)
                continue;
            if (t->flags_extra & (CREATURE_FLAG_EXTRA_CIVILIAN | CREATURE_FLAG_EXTRA_TRIGGER))
                continue;
            if (t->unit_flags & (UNIT_FLAG_NON_ATTACKABLE | UNIT_FLAG_IMMUNE_TO_PC | UNIT_FLAG_NOT_ATTACKABLE_1))
                continue;
            FactionTemplateEntry const* faction = sFactionTemplateStore.LookupEntry(t->faction);
            if (!faction)
                continue;

            bool all = (faction->hostileMask & FACTION_MASK_PLAYER) != 0;
            bool hostile[2] = { all || (faction->hostileMask & FACTION_MASK_ALLIANCE) != 0, all || (faction->hostileMask & FACTION_MASK_HORDE) != 0 };
            if (!hostile[0] && !hostile[1])
                continue;

            uint32 eff = t->maxlevel;
            if (t->rank == CREATURE_ELITE_ELITE || t->rank == CREATURE_ELITE_RAREELITE)
                eff += 3;
            else if (t->rank == CREATURE_ELITE_WORLDBOSS)
                eff += 10;
            eff = std::min<uint32>(eff, 250);

            StaticCell& cell = s_static[Key(data.mapid, CellOf(data.posX), CellOf(data.posY))];
            for (int team = 0; team < 2; ++team)
                if (hostile[team] && eff > cell.eff[team])
                {
                    cell.eff[team] = uint8(eff);
                    ++dangerous;
                }
        }
        s_summary = Acore::StringFormat("danger map: {} spawns read, {} cells with creatures that attack on sight", spawns, s_static.size());
        LOG_INFO("module.coa-playerbots", "{}", s_summary);
        (void)dangerous;
    }

    void NoteDeath(uint32 mapId, float x, float y)
    {
        if (!OpenWorld(mapId))
            return;
        uint32 now = NowMs();
        std::lock_guard<std::mutex> lock(s_deathMutex);
        DeathCell& cell = s_deaths[Key(mapId, CellOf(x), CellOf(y))];
        uint32 age = now - cell.atMs;
        float current = age >= DEATH_MEMORY_MS ? 0.0f : cell.weight * (1.0f - float(age) / float(DEATH_MEMORY_MS));
        cell.weight = std::min(current + DEATH_WEIGHT, RISK_CAP);
        cell.atMs = now;
    }

    float Risk(uint32 mapId, float x, float y, uint8 level, uint8 team)
    {
        if (!OpenWorld(mapId))
            return 0.0f;
        uint32 now = NowMs();
        int32 cx = CellOf(x);
        int32 cy = CellOf(y);
        float staticRisk = 0.0f;
        float deathRisk = 0.0f;
        // A creature a cell away still sees a bot at the edge of this one; deaths around add up.
        for (int32 dx = -1; dx <= 1; ++dx)
            for (int32 dy = -1; dy <= 1; ++dy)
            {
                staticRisk = std::max(staticRisk, StaticRisk(mapId, cx + dx, cy + dy, level, team));
                deathRisk += DeathRisk(mapId, cx + dx, cy + dy, now);
            }
        return std::min(staticRisk + std::min(deathRisk, RISK_CAP), RISK_CAP + DEATH_WEIGHT * 2.0f);
    }

    std::vector<Waypoint> Plan(uint32 mapId, float fromX, float fromY, float toX, float toY, uint8 level, uint8 team)
    {
        if (!OpenWorld(mapId) || std::hypot(toX - fromX, toY - fromY) < MIN_ROUTE_YARDS)
            return {};

        float minX = std::min(fromX, toX) - BOX_PADDING;
        float minY = std::min(fromY, toY) - BOX_PADDING;
        float maxX = std::max(fromX, toX) + BOX_PADDING;
        float maxY = std::max(fromY, toY) + BOX_PADDING;
        int w = int(std::ceil((maxX - minX) / STEP)) + 1;
        int h = int(std::ceil((maxY - minY) / STEP)) + 1;
        if (w > MAX_GRID || h > MAX_GRID)
            return {};

        auto center = [&](int gx, int gy) { return std::make_pair(minX + (float(gx) + 0.5f) * STEP, minY + (float(gy) + 0.5f) * STEP); };
        auto cellOf = [&](float x, float y) { return std::make_pair(std::clamp(int((x - minX) / STEP), 0, w - 1), std::clamp(int((y - minY) / STEP), 0, h - 1)); };

        auto [sx, sy] = cellOf(fromX, fromY);
        auto [ex, ey] = cellOf(toX, toY);

        // Risk per search cell, once.
        std::vector<float> risk(size_t(w) * h, 0.0f);
        float worst = 0.0f;
        for (int gy = 0; gy < h; ++gy)
            for (int gx = 0; gx < w; ++gx)
            {
                auto [cx, cy] = center(gx, gy);
                float r = Risk(mapId, cx, cy, level, team);
                risk[size_t(gy) * w + gx] = r;
                worst = std::max(worst, r);
            }
        if (worst <= 0.0f)
            return {};

        // The ends are where the bot is and where it must go: their own risk does not decide the route.
        risk[size_t(sy) * w + sx] = 0.0f;
        risk[size_t(ey) * w + ex] = 0.0f;

        using Node = std::tuple<float, float, int>; // estimate, cost so far, cell
        std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
        std::vector<float> best(size_t(w) * h, 1e30f);
        std::vector<int> previous(size_t(w) * h, -1);
        auto index = [&](int gx, int gy) { return gy * w + gx; };
        best[index(sx, sy)] = 0.0f;
        open.emplace(0.0f, 0.0f, index(sx, sy));

        while (!open.empty())
        {
            auto [estimate, cost, cur] = open.top();
            open.pop();
            (void)estimate;
            if (cost > best[cur])
                continue;
            int cx = cur % w;
            int cy = cur / w;
            if (cx == ex && cy == ey)
                break;
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                {
                    if (!dx && !dy)
                        continue;
                    int nx = cx + dx;
                    int ny = cy + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    float length = (dx && dy) ? STEP * 1.4142f : STEP;
                    float nextCost = cost + Cost(length, risk[size_t(ny) * w + nx]);
                    int next = index(nx, ny);
                    if (nextCost >= best[next])
                        continue;
                    best[next] = nextCost;
                    previous[next] = cur;
                    open.emplace(nextCost + std::hypot(float(ex - nx), float(ey - ny)) * STEP, nextCost, next);
                }
        }

        if (previous[index(ex, ey)] < 0 && !(sx == ex && sy == ey))
            return {};

        std::vector<Waypoint> cells;
        for (int cur = index(ex, ey); cur >= 0; cur = previous[cur])
        {
            auto [x, y] = center(cur % w, cur / w);
            cells.push_back(Waypoint{ x, y });
        }
        std::reverse(cells.begin(), cells.end());
        if (cells.size() < 2)
            return {};
        cells.front() = Waypoint{ fromX, fromY };
        cells.back() = Waypoint{ toX, toY };

        float directCost = SegmentCost(mapId, fromX, fromY, toX, toY, level, team);
        float directLength = std::hypot(toX - fromX, toY - fromY);

        // Smooth: from each anchor jump to the farthest cell whose straight segment costs no more than 15% over walking the
        // cells in between, so the route is a few waypoints and not a staircase.
        std::vector<Waypoint> route;
        size_t anchor = 0;
        float routeCost = 0.0f;
        float routeLength = 0.0f;
        while (anchor + 1 < cells.size())
        {
            size_t reach = anchor + 1;
            float viaCells = SegmentCost(mapId, cells[anchor].x, cells[anchor].y, cells[reach].x, cells[reach].y, level, team);
            for (size_t j = anchor + 2; j < cells.size(); ++j)
            {
                viaCells += SegmentCost(mapId, cells[j - 1].x, cells[j - 1].y, cells[j].x, cells[j].y, level, team);
                float straight = SegmentCost(mapId, cells[anchor].x, cells[anchor].y, cells[j].x, cells[j].y, level, team);
                if (straight <= viaCells * 1.15f)
                    reach = j;
            }
            routeCost += SegmentCost(mapId, cells[anchor].x, cells[anchor].y, cells[reach].x, cells[reach].y, level, team);
            routeLength += std::hypot(cells[reach].x - cells[anchor].x, cells[reach].y - cells[anchor].y);
            anchor = reach;
            route.push_back(cells[anchor]);
        }
        if (!route.empty() && route.back().x == toX && route.back().y == toY)
            route.pop_back();

        // Only when the straight way really crosses danger (the part of its cost beyond its length), the route removes most
        // of that, and the detour is not across the continent.
        float directExcess = directCost - directLength;
        float routeExcess = routeCost - routeLength;
        if (route.empty() || route.size() > MAX_WAYPOINTS || directExcess < 100.0f || routeExcess > directExcess * 0.4f || routeLength > directLength * 1.7f)
            return {};
        return route;
    }

    char const* Summary()
    {
        return s_summary.c_str();
    }
}
