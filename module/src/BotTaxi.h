/*
 * mod-coa-playerbots
 *
 * Flight paths for bots, the way a player uses them: a bot learns a flight master's node by walking
 * up to it, and flies from a flight master it is standing at to the known node nearest where it
 * wants to go, stopping over at other known nodes on the way. Before this, the only long-distance
 * travel a bot had was BotZoneProgression teleporting it straight into its next hub.
 *
 * Routing is done here because the core has no taxi route search -- a real client computes the
 * multi-hop route itself and sends the node list; ActivateTaxiPathTo only checks that each
 * consecutive pair is a direct path. The graph is the DBC's own sTaxiPathSetBySource, restricted to
 * nodes the bot knows and its faction can use, which is exactly what a player's map would offer.
 */

#ifndef COA_PLAYERBOTS_BOT_TAXI_H
#define COA_PLAYERBOTS_BOT_TAXI_H

#include "Define.h"
#include <vector>

class Player;

namespace BotTaxi
{
    // Learns the node of a flight master within talking range, as speaking to one does. Cheap: a
    // lookup in the POI index, not a grid scan. Returns true when a new node was learned.
    bool DiscoverNearbyNode(Player* bot);

    // Gives a freshly created bot the nodes a character of its level would already have: the one
    // at each zone hub for its level and faction, from the starting area up.
    void GrantNodesForLevel(Player* bot);

    // Where the nearest usable flight master within `radius` stands, if any.
    bool FindFlightMaster(Player* bot, float radius, float& x, float& y, float& z);

    // Shortest route over known, usable nodes from `sourceNode` to the known node nearest the
    // destination on the same map. Empty when there is none, or when that node is the source itself.
    std::vector<uint32> PlanRoute(Player* bot, uint32 sourceNode, float destX, float destY, float destZ);

    // Whether flying from the flight master at (masterX, masterY, masterZ) toward the destination is a
    // real plan: a route over known nodes exists and its landing node is well closer than the bot is.
    bool WorthFlying(Player* bot, float masterX, float masterY, float masterZ, float destX, float destY,
        float destZ);

    // The taxi node of the flight master the bot is standing at, or 0.
    uint32 NodeHere(Player* bot);

    // Takes off from the flight master the bot is standing at toward the destination. Returns true
    // once the bot is actually in flight.
    bool FlyToward(Player* bot, float destX, float destY, float destZ);
}

#endif // COA_PLAYERBOTS_BOT_TAXI_H
