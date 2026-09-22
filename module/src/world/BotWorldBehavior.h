/*
 * mod-coa-playerbots
 *
 * Ambient world behavior: the layer that gives an ungrouped, idle bot a concrete errand when its
 * own solo activities (questing, gathering, fishing, grinding -- all still in BotAI.cpp) found
 * nothing within their few dozen yards of scan range. Before this layer existed that case simply
 * ended in the bot standing still, which is what made bots at town hubs and in capitals look like
 * NPCs rather than players.
 *
 * Two layers with different lifetimes, deliberately not merged. BotAI's SoloIntent is "what kind
 * of player am I this half hour" and is chosen from the persistent personality; the WorldIntent
 * here is "which concrete errand am I on right now" and lasts seconds to a few minutes. SoloIntent
 * only biases which errand gets picked (AmbientLean below), so the existing personality machinery
 * is reused rather than duplicated.
 *
 * Priority against the rest of BotAI: combat, death, rest, loot and any in-flight quest/gather/
 * fish walk always run before this layer is even consulted. Needs (repair, selling) preempt the
 * solo activity scans; cosmetic errands only start after those scans have come up empty for a
 * while, and never interrupt them. Grouped bots, Stay, battlegrounds and dungeons never reach it.
 */

#ifndef COA_PLAYERBOTS_BOT_WORLD_BEHAVIOR_H
#define COA_PLAYERBOTS_BOT_WORLD_BEHAVIOR_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>

class Player;

// Mirror of BotAI's private SoloIntent, translated at the call site so this translation unit
// doesn't depend on BotAI.cpp's internals.
enum class AmbientLean : uint8
{
    None,
    Quest,
    Gather,
    Fish,
    Grind,
    Explore,
};

// The slice of a bot's persistent personality the ambient layer scores with.
struct AmbientProfile
{
    uint32 seed = 0;
    uint8 questing = 50;
    uint8 gathering = 50;
    uint8 grinding = 50;
    uint8 patience = 50;
    uint8 sociability = 50;
    AmbientLean lean = AmbientLean::None;
};

enum class AmbientTick : uint8
{
    // Nothing active: let the solo activity scans run this tick.
    Idle,
    // An errand owns this tick (travelling, lingering, or a need that just started). The solo
    // scans must not run, or they would start a competing walk.
    Busy,
    // An errand finished this tick with the bot somewhere new. The caller must drop its grind
    // anchor, otherwise TryGrindWhenSolo would immediately walk the bot back to where it was.
    Relocated,
};

namespace BotWorldBehavior
{
    // Reads and caches configuration. Called once from the module's OnStartup.
    void LoadConfig();

    // Consulted first in the idle-solo branch, before any solo activity scan.
    AmbientTick UpdateBeforeSolo(Player* bot, AmbientProfile const& profile);

    // Consulted after the solo activity scans ran without an active errand. soloStartedSomething
    // is whether any of them started work (a walk, a cast, an attack) this tick.
    void UpdateAfterSolo(Player* bot, AmbientProfile const& profile, bool soloStartedSomething);

    // Asks the bot to travel to a destination on its current map by flight path: walk to the nearest
    // flight master, fly to the known node nearest the destination, walk the rest. Returns false
    // without changing anything when that cannot work (different map, no flight master in reach, no
    // known node near the destination), so the caller can fall back to teleporting.
    bool RequestTravel(Player* bot, uint32 mapId, float x, float y, float z);

    // One line for `.botcmd profile`.
    std::string Describe(ObjectGuid botGuid);

    void Forget(ObjectGuid botGuid);
}

#endif // COA_PLAYERBOTS_BOT_WORLD_BEHAVIOR_H
