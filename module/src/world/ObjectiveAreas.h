/*
 * mod-coa-playerbots
 *
 * Objective areas: the places where a quest target lives, built by clustering its static spawns
 * (see SpawnClustering.h for why places, never single spawn coordinates). Computed lazily the
 * first time any bot needs an entry and cached for the life of the process, so a population of
 * thousands with the same quest pays for one clustering run.
 *
 * Static spawns are world knowledge only -- "there is a kobold camp here". What a bot fights,
 * loots or clicks is always a live Creature/GameObject found by scanning around it once it is in
 * the area.
 */

#ifndef COA_PLAYERBOTS_OBJECTIVE_AREAS_H
#define COA_PLAYERBOTS_OBJECTIVE_AREAS_H

#include "Define.h"
#include "QuestKnowledgeBase.h"
#include <vector>

struct ObjectiveArea
{
    uint32 id = 0;
    uint32 mapId = 0;
    bool gameObject = false;
    uint32 entry = 0;
    float x = 0.0f;           // anchor: a real spawn next to the centre (walkable)
    float y = 0.0f;
    float z = 0.0f;
    float cx = 0.0f;          // centroid
    float cy = 0.0f;
    float radius = 0.0f;
    uint32 spawnCount = 0;
    uint32 phaseMask = 0;     // the phase every member spawns in (areas never mix phases)
    std::vector<SpawnPoint> points; // members to wander between while searching (capped)
};

namespace ObjectiveAreas
{
    // Every area of this creature/object entry, on every map.
    std::vector<uint32> const& ForEntry(bool gameObject, uint32 entry);

    ObjectiveArea const* Get(uint32 areaId);

    size_t CachedAreas();
}

#endif // COA_PLAYERBOTS_OBJECTIVE_AREAS_H
