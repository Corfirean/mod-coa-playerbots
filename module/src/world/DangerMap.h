/*
 * mod-coa-playerbots
 *
 * Which parts of the open world a bot of a given level should walk around. Two sources:
 *  - static: the creature spawns of the world database, kept per 40-yard cell as the highest effective level of any
 *    creature that attacks players of a team on sight (elites count a few levels higher);
 *  - dynamic: where bots have died lately (NoteDeath), which catches what the static data cannot know (scaled levels,
 *    packs, roaming patrols).
 * Plan() turns that into a route of waypoints around the worst cells, for travel legs of a few hundred yards or more.
 * It only suggests: a waypoint the movement layer cannot reach is dropped by the caller and the bot goes straight.
 */

#ifndef COA_PLAYERBOTS_WORLD_DANGER_MAP_H
#define COA_PLAYERBOTS_WORLD_DANGER_MAP_H

#include "Define.h"
#include <vector>

namespace DangerMap
{
    struct Waypoint
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    // Builds the static grid from the loaded creature data (call once after the world is loaded).
    void Initialize();

    // A bot died here: cells around it count as dangerous for a while.
    void NoteDeath(uint32 mapId, float x, float y);

    // Risk of standing near (x, y) for a bot of this level and team (TEAM_ALLIANCE = 0, TEAM_HORDE = 1): 0 is safe, each
    // point is roughly one level above the bot beyond a margin of three; deaths add to it.
    float Risk(uint32 mapId, float x, float y, uint8 level, uint8 team);

    // Waypoints from (fromX, fromY) to (toX, toY) that avoid risky cells, not including the start and the goal. Empty
    // when the straight line is acceptable or no clearly better route exists.
    std::vector<Waypoint> Plan(uint32 mapId, float fromX, float fromY, float toX, float toY, uint8 level, uint8 team);

    // One line of numbers for logs: cells known, dynamic cells.
    char const* Summary();
}

#endif // COA_PLAYERBOTS_WORLD_DANGER_MAP_H
