/*
 * mod-coa-playerbots
 *
 * Coarse spatial census of the bot population: which 100-yard cells bots are standing in, and
 * which cells they are on their way to. The planner reads it as a crowding cost -- "twelve bots
 * already farm this camp, three more are walking there" -- so a population with the same quest
 * spreads across the camps that exist instead of converging on the nearest one.
 *
 * Deliberately incremental: a bot only touches the grid when it changes cell, changes activity or
 * picks a new destination, so thousands of bots cost a few hash-map updates per second in total.
 * Pure data structure (bots are raw ids, no core types) so it is unit-tested standalone.
 */

#ifndef COA_PLAYERBOTS_POPULATION_GRID_H
#define COA_PLAYERBOTS_POPULATION_GRID_H

#include "Define.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>

enum class PopulationActivity : uint8
{
    Idle,
    Questing,
    Gathering,
    Other,
};

struct PopulationCell
{
    uint16 activeBots = 0;
    uint16 incomingBots = 0;
    uint16 questingBots = 0;
    uint16 gatheringBots = 0;

    uint32 Crowd() const { return uint32(activeBots) + incomingBots; }

    PopulationCell& operator+=(PopulationCell const& other)
    {
        activeBots = uint16(activeBots + other.activeBots);
        incomingBots = uint16(incomingBots + other.incomingBots);
        questingBots = uint16(questingBots + other.questingBots);
        gatheringBots = uint16(gatheringBots + other.gatheringBots);
        return *this;
    }

    bool Empty() const { return !activeBots && !incomingBots && !questingBots && !gatheringBots; }
};

class PopulationGrid
{
public:
    explicit PopulationGrid(float cellSize = 100.0f) : _cellSize(cellSize) { }

    float CellSize() const { return _cellSize; }

    // Where the bot stands and what it is doing. A no-op unless the cell or activity changed.
    void UpdatePresence(uint64 bot, uint32 mapId, float x, float y, PopulationActivity activity)
    {
        uint64 key = KeyFor(mapId, x, y);
        BotEntry& entry = _bots[bot];
        if (entry.hasPresence && entry.presenceCell == key && entry.activity == activity)
            return;

        if (entry.hasPresence)
            Adjust(entry.presenceCell, entry.activity, -1, false);

        entry.presenceCell = key;
        entry.activity = activity;
        entry.hasPresence = true;
        Adjust(key, activity, +1, false);
    }

    // Where the bot is heading. Replaces any previous destination.
    void SetIncoming(uint64 bot, uint32 mapId, float x, float y)
    {
        uint64 key = KeyFor(mapId, x, y);
        BotEntry& entry = _bots[bot];
        if (entry.hasIncoming && entry.incomingCell == key)
            return;
        if (entry.hasIncoming)
            Adjust(entry.incomingCell, PopulationActivity::Idle, -1, true);
        entry.incomingCell = key;
        entry.hasIncoming = true;
        Adjust(key, PopulationActivity::Idle, +1, true);
    }

    void ClearIncoming(uint64 bot)
    {
        auto itr = _bots.find(bot);
        if (itr == _bots.end() || !itr->second.hasIncoming)
            return;
        Adjust(itr->second.incomingCell, PopulationActivity::Idle, -1, true);
        itr->second.hasIncoming = false;
    }

    void Remove(uint64 bot)
    {
        auto itr = _bots.find(bot);
        if (itr == _bots.end())
            return;
        if (itr->second.hasPresence)
            Adjust(itr->second.presenceCell, itr->second.activity, -1, false);
        if (itr->second.hasIncoming)
            Adjust(itr->second.incomingCell, PopulationActivity::Idle, -1, true);
        _bots.erase(itr);
    }

    PopulationCell At(uint32 mapId, float x, float y) const
    {
        auto itr = _cells.find(KeyFor(mapId, x, y));
        return itr == _cells.end() ? PopulationCell() : itr->second;
    }

    // Sum over the (2r+1)^2 cells centred on the point's cell.
    PopulationCell Around(uint32 mapId, float x, float y, int32 radiusCells) const
    {
        PopulationCell total;
        int32 cx = CellIndex(x);
        int32 cy = CellIndex(y);
        for (int32 dx = -radiusCells; dx <= radiusCells; ++dx)
        {
            for (int32 dy = -radiusCells; dy <= radiusCells; ++dy)
            {
                auto itr = _cells.find(Key(mapId, cx + dx, cy + dy));
                if (itr != _cells.end())
                    total += itr->second;
            }
        }
        return total;
    }

    size_t CellCount() const { return _cells.size(); }
    size_t BotCount() const { return _bots.size(); }

    // What the grid currently counts for one bot (debug output and tests).
    struct BotStatus
    {
        bool present = false;
        bool incoming = false;
        PopulationActivity activity = PopulationActivity::Idle;
    };

    BotStatus StatusOf(uint64 bot) const
    {
        BotStatus status;
        auto itr = _bots.find(bot);
        if (itr == _bots.end())
            return status;
        status.present = itr->second.hasPresence;
        status.incoming = itr->second.hasIncoming;
        status.activity = itr->second.activity;
        return status;
    }

private:
    struct BotEntry
    {
        uint64 presenceCell = 0;
        uint64 incomingCell = 0;
        PopulationActivity activity = PopulationActivity::Idle;
        bool hasPresence = false;
        bool hasIncoming = false;
    };

    int32 CellIndex(float v) const
    {
        return int32(std::floor(v / _cellSize));
    }

    // Map id in the top 16 bits, then 24 bits per axis with an offset so negative coordinates
    // pack cleanly. WoW coordinates stay within +-17067 yards, far inside that range.
    static uint64 Key(uint32 mapId, int32 cx, int32 cy)
    {
        constexpr int32 OFFSET = 1 << 23;
        return (uint64(mapId & 0xFFFF) << 48) | (uint64(uint32(cx + OFFSET) & 0xFFFFFF) << 24)
            | uint64(uint32(cy + OFFSET) & 0xFFFFFF);
    }

    uint64 KeyFor(uint32 mapId, float x, float y) const
    {
        return Key(mapId, CellIndex(x), CellIndex(y));
    }

    void Adjust(uint64 key, PopulationActivity activity, int32 delta, bool incoming)
    {
        PopulationCell& cell = _cells[key];
        auto bump = [delta](uint16& field) { field = uint16(std::max<int32>(0, int32(field) + delta)); };
        if (incoming)
            bump(cell.incomingBots);
        else
        {
            bump(cell.activeBots);
            if (activity == PopulationActivity::Questing)
                bump(cell.questingBots);
            else if (activity == PopulationActivity::Gathering)
                bump(cell.gatheringBots);
        }
        if (cell.Empty())
            _cells.erase(key);
    }

    float _cellSize;
    std::unordered_map<uint64, PopulationCell> _cells;
    std::unordered_map<uint64, BotEntry> _bots;
};

#endif // COA_PLAYERBOTS_POPULATION_GRID_H
