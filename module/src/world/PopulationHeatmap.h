/*
 * mod-coa-playerbots
 *
 * The one shared PopulationGrid (see PopulationGrid.h) the planner reads crowding costs from.
 * Bots report where they are (staggered, only on cell change) and where they are going (when a
 * task picks a destination); logout removes them.
 */

#ifndef COA_PLAYERBOTS_POPULATION_HEATMAP_H
#define COA_PLAYERBOTS_POPULATION_HEATMAP_H

#include "ObjectGuid.h"
#include "PopulationGrid.h"

namespace PopulationHeatmap
{
    void UpdatePresence(ObjectGuid bot, uint32 mapId, float x, float y, PopulationActivity activity);
    void SetIncoming(ObjectGuid bot, uint32 mapId, float x, float y);
    void ClearIncoming(ObjectGuid bot);
    void Remove(ObjectGuid bot);

    // Bots in, or heading to, the 3x3 cells around the point.
    PopulationCell Around(uint32 mapId, float x, float y);

    PopulationGrid const& Grid();
}

#endif // COA_PLAYERBOTS_POPULATION_HEATMAP_H
