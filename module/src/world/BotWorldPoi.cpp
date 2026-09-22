#include "BotWorldPoi.h"
#include "BotAI.h"
#include "CreatureData.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "SharedDefines.h"
#include "UnitDefines.h"
#include <array>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace
{
    // Large enough that a typical 150-350 yard query touches a few dozen cells rather than
    // hundreds, small enough that a query centred in a capital doesn't drag in half the continent.
    constexpr float CELL_SIZE = 200.0f;

    using CellBuckets = std::array<std::vector<Poi>, size_t(PoiKind::Count)>;

    struct MapIndex
    {
        std::unordered_map<uint64, CellBuckets> cells;
    };

    std::unordered_map<uint32, MapIndex> _maps;

    int32 PoiCellOf(float v)
    {
        return int32(std::floor(v / CELL_SIZE));
    }

    uint64 CellKey(int32 cx, int32 cy)
    {
        return (uint64(uint32(cx)) << 32) | uint32(cy);
    }

    void Add(MapIndex& index, Poi const& poi)
    {
        index.cells[CellKey(PoiCellOf(poi.x), PoiCellOf(poi.y))][size_t(poi.kind)].push_back(poi);
    }

    Poi MakePoi(PoiKind kind, uint32 entry, SpawnData const& data)
    {
        return Poi{ kind, entry, uint32(data.spawnId), data.posX, data.posY, data.posZ, 0, 0, 0, 0 };
    }

    // A creature is only worth indexing as something to fight when a player could actually fight
    // it: no service role, not a critter, not an invisible trigger, not flagged unattackable, and
    // a normal-rank mob (elites and bosses are not solo idle-time targets).
    bool IsSoloGrindTarget(CreatureTemplate const* tmpl)
    {
        if (tmpl->npcflag || tmpl->type == CREATURE_TYPE_CRITTER || tmpl->rank != CREATURE_ELITE_NORMAL)
            return false;
        if (tmpl->HasFlagsExtra(CREATURE_FLAG_EXTRA_TRIGGER) || !tmpl->minlevel)
            return false;
        return !(tmpl->unit_flags & (UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE));
    }

    void IndexCreature(MapIndex& index, CreatureData const& data)
    {
        CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(data.id);
        if (!tmpl)
            return;

        static constexpr std::pair<uint32, PoiKind> services[] =
        {
            { UNIT_NPC_FLAG_VENDOR_MASK, PoiKind::Vendor },
            { UNIT_NPC_FLAG_REPAIR, PoiKind::Repair },
            { UNIT_NPC_FLAG_BANKER, PoiKind::Banker },
            { UNIT_NPC_FLAG_AUCTIONEER, PoiKind::Auctioneer },
            { UNIT_NPC_FLAG_INNKEEPER, PoiKind::Innkeeper },
            { UNIT_NPC_FLAG_TRAINER_PROFESSION, PoiKind::ProfessionTrainer },
            { UNIT_NPC_FLAG_FLIGHTMASTER, PoiKind::FlightMaster },
        };

        for (auto const& [flag, kind] : services)
        {
            if (!(tmpl->npcflag & flag))
                continue;
            Poi poi = MakePoi(kind, data.id, data);
            poi.faction = tmpl->faction;
            Add(index, poi);
        }

        if (!IsSoloGrindTarget(tmpl))
            return;

        Poi poi = MakePoi(PoiKind::Hostile, data.id, data);
        poi.faction = tmpl->faction;
        poi.minLevel = tmpl->minlevel;
        poi.maxLevel = tmpl->maxlevel;
        Add(index, poi);
    }

    void IndexGameObject(MapIndex& index, GameObjectData const& data)
    {
        GameObjectTemplate const* tmpl = sObjectMgr->GetGameObjectTemplate(data.id);
        if (!tmpl)
            return;

        if (tmpl->type == GAMEOBJECT_TYPE_MAILBOX)
        {
            Add(index, MakePoi(PoiKind::Mailbox, data.id, data));
            return;
        }

        if (tmpl->type != GAMEOBJECT_TYPE_CHEST || BotAI::IsQuestOnlyGameObjectLoot(tmpl->GetLootId()))
            return;

        LockEntry const* lock = sLockStore.LookupEntry(tmpl->GetLockId());
        if (!lock)
            return;

        for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
        {
            if (lock->Type[i] != LOCK_KEY_SKILL)
                continue;

            PoiKind kind;
            if (lock->Index[i] == LOCKTYPE_HERBALISM)
                kind = PoiKind::Herb;
            else if (lock->Index[i] == LOCKTYPE_MINING)
                kind = PoiKind::Ore;
            else
                continue;

            Poi poi = MakePoi(kind, data.id, data);
            poi.requiredSkill = uint16(lock->Skill[i]);
            Add(index, poi);
            return;
        }
    }

    MapIndex const& GetOrBuild(uint32 mapId)
    {
        auto itr = _maps.find(mapId);
        if (itr != _maps.end())
            return itr->second;

        MapIndex& index = _maps[mapId];
        size_t count = 0;

        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            if (data.mapid != mapId)
                continue;
            IndexCreature(index, data);
            ++count;
        }

        for (auto const& [spawnId, data] : sObjectMgr->GetAllGOData())
        {
            if (data.mapid != mapId)
                continue;
            IndexGameObject(index, data);
            ++count;
        }

        LOG_INFO("module.coa-playerbots.world", "BotWorldPoi: indexed map {} ({} spawns scanned, {} cells).",
            mapId, count, index.cells.size());
        return index;
    }
}

namespace BotWorldPoi
{
    void Query(uint32 mapId, float x, float y, float radius, PoiKind kind, std::vector<Poi const*>& out)
    {
        MapIndex const& index = GetOrBuild(mapId);
        float radiusSq = radius * radius;

        for (int32 cx = PoiCellOf(x - radius); cx <= PoiCellOf(x + radius); ++cx)
        {
            for (int32 cy = PoiCellOf(y - radius); cy <= PoiCellOf(y + radius); ++cy)
            {
                auto itr = index.cells.find(CellKey(cx, cy));
                if (itr == index.cells.end())
                    continue;

                for (Poi const& poi : itr->second[size_t(kind)])
                {
                    float dx = poi.x - x;
                    float dy = poi.y - y;
                    if (dx * dx + dy * dy <= radiusSq)
                        out.push_back(&poi);
                }
            }
        }
    }

    char const* KindName(PoiKind kind)
    {
        switch (kind)
        {
            case PoiKind::Vendor:            return "Vendor";
            case PoiKind::Repair:            return "Repair";
            case PoiKind::Banker:            return "Banker";
            case PoiKind::Auctioneer:        return "Auctioneer";
            case PoiKind::Innkeeper:         return "Innkeeper";
            case PoiKind::ProfessionTrainer: return "ProfessionTrainer";
            case PoiKind::FlightMaster:      return "FlightMaster";
            case PoiKind::Mailbox:           return "Mailbox";
            case PoiKind::Herb:              return "Herb";
            case PoiKind::Ore:               return "Ore";
            case PoiKind::Hostile:           return "Hostile";
            default:                         return "Unknown";
        }
    }
}
