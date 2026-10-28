/*
 * mod-coa-playerbots -- dungeon automation
 */

#include "dungeon/DungeonRoster.h"
#include "dungeon/BossRosterRegistry.h"
#include "dungeon/BossSpawnIndex.h"
#include "dungeon/DcBossOrdering.h"
#include "dungeon/DcDifficulty.h"
#include "dungeon/DungeonWingRegistry.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "StringFormat.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <set>

namespace
{
    std::mutex g_mutex;

    // (instance id, objective entry) of objectives finished in that instance.
    std::set<std::pair<uint32, uint32>> g_doneObjectives;

    // Per group: the anchor it is heading for, and the order key of the last one it finished or committed to (for "advance forward").
    struct Commitment
    {
        uint32 entry = 0;
        uint32 orderKey = 0;
        bool haveKey = false;
    };
    std::map<ObjectGuid, Commitment> g_commitment;

    // (group, entry) -> game time (ms) until which the anchor is left out.
    std::map<std::pair<ObjectGuid, uint32>, uint32> g_unreachableUntil;

    float Dist3(float ax, float ay, float az, float bx, float by, float bz)
    {
        return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by) + (az - bz) * (az - bz));
    }

    // Narrows a list to the wing the reference unit stands in, on a map whose wings are physically separate.
    void FilterToWing(uint32 mapId, Player* ref, std::vector<DungeonBossInfo>& list)
    {
        DungeonWingLayout const* layout = DungeonWingRegistry::Get(mapId);
        if (!layout || !layout->isolated || list.empty())
            return;

        DungeonWing const* wing = nullptr;
        if (layout->select == WingSelect::Explicit)
        {
            wing = DungeonWingRegistry::FindWing(*layout, layout->defaultWing);
        }
        else if (ref)
        {
            float best = 1.0e12f;
            for (DungeonBossInfo const& b : list)
            {
                if (b.kind != DungeonAnchorKind::Boss)
                    continue;
                DungeonWing const* owner = DungeonWingRegistry::WingOf(mapId, b.entry);
                if (!owner)
                    continue;
                float const d = Dist3(ref->GetPositionX(), ref->GetPositionY(), ref->GetPositionZ(), b.x, b.y, b.z);
                if (d < best)
                {
                    best = d;
                    wing = owner;
                }
            }
        }
        if (!wing)
            return;

        // An anchor without a wing of its own (an objective) belongs to the wing of the boss nearest to it.
        auto wingOfAnchor = [&](DungeonBossInfo const& a) -> DungeonWing const*
        {
            if (DungeonWing const* own = DungeonWingRegistry::WingOf(mapId, a.entry))
                return own;
            DungeonWing const* nearest = nullptr;
            float best = 1.0e12f;
            for (DungeonBossInfo const& b : list)
            {
                DungeonWing const* owner = b.kind == DungeonAnchorKind::Boss ? DungeonWingRegistry::WingOf(mapId, b.entry) : nullptr;
                if (!owner)
                    continue;
                float const d = Dist3(a.x, a.y, a.z, b.x, b.y, b.z);
                if (d < best)
                {
                    best = d;
                    nearest = owner;
                }
            }
            return nearest;
        };

        list.erase(std::remove_if(list.begin(), list.end(), [&](DungeonBossInfo const& a) { return wingOfAnchor(a) != wing; }), list.end());
    }
}

namespace DungeonRoster
{
    std::vector<DungeonBossInfo> Full(Map* map, Player* ref)
    {
        if (!map)
            return {};
        std::vector<DungeonBossInfo> list = BossSpawnIndex::Get(map->GetId(), map->GetDifficulty());
        list = BossRosterRegistry::Apply(map->GetId(), DcDifficulty::Of(map), std::move(list));
        FilterToWing(map->GetId(), ref, list);
        return list;
    }

    bool IsDone(Map* map, DungeonBossInfo const& a)
    {
        InstanceMap* instanceMap = map ? map->ToInstanceMap() : nullptr;
        InstanceScript* instance = instanceMap ? instanceMap->GetInstanceScript() : nullptr;

        if (instance)
        {
            if (a.doneBossStateIndex >= 0 && instance->GetBossState(uint32(a.doneBossStateIndex)) == DONE)
                return true;
            if (a.doneInstanceDataId >= 0 && instance->GetData(uint32(a.doneInstanceDataId)) == a.doneInstanceDataValue)
                return true;
        }

        if (a.kind == DungeonAnchorKind::Objective)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            return g_doneObjectives.count({ map ? map->GetInstanceId() : 0, a.entry }) != 0;
        }

        if (instance && a.doneBossStateIndex < 0 && a.encounterIndex < 32)
            return (instance->GetCompletedEncounterMask() & (1u << a.encounterIndex)) != 0;
        return false;
    }

    void MarkObjectiveDone(Map* map, uint32 entry)
    {
        if (!map)
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_doneObjectives.insert({ map->GetInstanceId(), entry });
    }

    std::vector<DungeonBossInfo> Remaining(Map* map, Player* ref, ObjectGuid groupKey)
    {
        std::vector<DungeonBossInfo> todo;
        for (DungeonBossInfo const& a : Full(map, ref))
            if (!a.skipByDesign && !IsDone(map, a))
                todo.push_back(a);

        // A satisfied gate creature (the real boss exists as a result of the event) finishes an objective.
        todo.erase(std::remove_if(todo.begin(), todo.end(), [&](DungeonBossInfo const& a)
        {
            return a.kind == DungeonAnchorKind::Objective && a.gateEntry && ref && ref->FindNearestCreature(a.gateEntry, 400.0f, true);
        }), todo.end());

        uint32 const now = getMSTime();
        std::vector<DungeonBossInfo> reachable;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            for (DungeonBossInfo const& a : todo)
            {
                auto it = g_unreachableUntil.find({ groupKey, a.entry });
                if (it != g_unreachableUntil.end() && int32(it->second - now) > 0)
                    continue;
                reachable.push_back(a);
            }
        }
        return reachable.empty() ? todo : reachable;
    }

    bool PickTarget(Map* map, Player* ref, ObjectGuid groupKey, DungeonBossInfo& out)
    {
        std::vector<DungeonBossInfo> todo = Remaining(map, ref, groupKey);
        if (todo.empty())
            return false;

        Commitment c;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            c = g_commitment[groupKey];
        }
        auto picked = DcBossOrdering::PickTarget(todo, c.entry, c.orderKey, c.haveKey);
        if (!picked)
            return false;
        out = *picked;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            Commitment& stored = g_commitment[groupKey];
            stored.entry = out.entry;
            stored.orderKey = BossOrderKey(out);
            stored.haveKey = true;
        }
        return true;
    }

    void ResolvePosition(Map* /*map*/, DungeonBossInfo const& a, float& x, float& y, float& z)
    {
        x = a.x;
        y = a.y;
        z = a.z;
    }

    void MarkUnreachable(ObjectGuid groupKey, uint32 entry, uint32 forMs)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_unreachableUntil[{ groupKey, entry }] = getMSTime() + forMs;
        auto it = g_commitment.find(groupKey);
        if (it != g_commitment.end() && it->second.entry == entry)
            it->second.entry = 0;  // release the commitment so the next pick can move on
    }

    void Forget(ObjectGuid groupKey)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_commitment.erase(groupKey);
        for (auto it = g_unreachableUntil.begin(); it != g_unreachableUntil.end();)
            it = it->first.first == groupKey ? g_unreachableUntil.erase(it) : std::next(it);
    }

    std::string Describe(Map* map, Player* ref)
    {
        std::string out;
        for (DungeonBossInfo const& a : Full(map, ref))
            out += Acore::StringFormat("{}{} ({}){}; ", a.kind == DungeonAnchorKind::Objective ? "[obj] " : "", a.name, a.entry, IsDone(map, a) ? " done" : "");
        return out;
    }
}
