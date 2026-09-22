/*
 * mod-coa-playerbots
 *
 * Spatial index of the places an ungrouped bot might want to go: service NPCs, mailboxes,
 * gathering nodes and hostile spawns. It is what lets the ambient layer answer "where could I go
 * next" without a grid scan, because the scans the rest of BotAI.cpp uses (Cell::VisitObjects) only
 * see loaded grids within a few dozen yards -- exactly why a bot parked at a game_tele hub never
 * found anything to do.
 *
 * Built lazily per map from ObjectMgr's in-memory spawn data, the same records the world was loaded
 * from, so it costs no database query and is shared by every bot on that map: three thousand bots
 * in Stormwind pay for one build. Entries are bucketed into fixed-size cells so a radius query only
 * touches the handful of cells it overlaps.
 */

#ifndef COA_PLAYERBOTS_BOT_WORLD_POI_H
#define COA_PLAYERBOTS_BOT_WORLD_POI_H

#include "Define.h"
#include <vector>

enum class PoiKind : uint8
{
    Vendor,
    Repair,
    Banker,
    Auctioneer,
    Innkeeper,
    ProfessionTrainer,
    FlightMaster,
    Mailbox,
    Herb,
    Ore,
    Hostile,
    Count,
};

struct Poi
{
    PoiKind kind;
    uint32 entry;
    uint32 spawnId;
    float x;
    float y;
    float z;
    // Creature template faction; 0 for game objects. Friendliness depends on who is asking, so it is
    // resolved at query time rather than baked in.
    uint32 faction;
    uint8 minLevel;
    uint8 maxLevel;
    // Skill value a gathering node's lock demands; 0 for everything else.
    uint16 requiredSkill;
};

namespace BotWorldPoi
{
    // Appends every indexed entry of `kind` whose 2D distance from (x, y) on `mapId` is within
    // `radius`. Pointers stay valid for the life of the process: a map's index is built once and
    // never rebuilt or shrunk.
    void Query(uint32 mapId, float x, float y, float radius, PoiKind kind, std::vector<Poi const*>& out);

    char const* KindName(PoiKind kind);
}

#endif // COA_PLAYERBOTS_BOT_WORLD_POI_H
