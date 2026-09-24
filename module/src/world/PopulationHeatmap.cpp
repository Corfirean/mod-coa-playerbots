#include "PopulationHeatmap.h"

namespace
{
    PopulationGrid _grid(100.0f);
}

namespace PopulationHeatmap
{
    void UpdatePresence(ObjectGuid bot, uint32 mapId, float x, float y, PopulationActivity activity)
    {
        _grid.UpdatePresence(bot.GetRawValue(), mapId, x, y, activity);
    }

    void SetIncoming(ObjectGuid bot, uint32 mapId, float x, float y)
    {
        _grid.SetIncoming(bot.GetRawValue(), mapId, x, y);
    }

    void ClearIncoming(ObjectGuid bot)
    {
        _grid.ClearIncoming(bot.GetRawValue());
    }

    void Remove(ObjectGuid bot)
    {
        _grid.Remove(bot.GetRawValue());
    }

    PopulationCell Around(uint32 mapId, float x, float y)
    {
        return _grid.Around(mapId, x, y, 1);
    }

    PopulationGrid const& Grid()
    {
        return _grid;
    }
}
