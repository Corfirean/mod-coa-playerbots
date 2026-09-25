/*
 * mod-coa-playerbots
 *
 * Chooses the bot's next WorldTask by utility (see WorldUtility.h), never by a fixed script:
 *  - objectives of quests already in the log, each placed in its best objective area (spawn
 *    count, distance, how many bots are already there, what failed recently);
 *  - turn-ins of completed quests, batched per quest ender;
 *  - quest givers nearby with work suitable for the bot;
 *  - a new quest hub when the current area has nothing left.
 * Objectives that share targets or lie next to each other are bundled into one trip, and the rest
 * of the candidates are ordered into a short route (nearest-next) for `.botcmd brain`.
 *
 * Runs rarely (a staggered few seconds when idle, and after each finished task), never per tick.
 */

#ifndef COA_PLAYERBOTS_WORLD_PLANNER_H
#define COA_PLAYERBOTS_WORLD_PLANNER_H

#include "ObjectiveAreas.h"
#include "WorldBrainState.h"

class Player;
struct QuestHub;

struct AreaPick
{
    ObjectiveArea const* area = nullptr;
    float score = 0.0f;
    float distance = 0.0f;
    float dropChance = 1.0f;
};

namespace WorldPlanner
{
    WorldTask ChooseTask(Player* bot, BrainState& state);

    // The best objective area for this objective on the bot's map, skipping areas that failed
    // recently. `excludeAreaId` also skips the area the bot is giving up on right now.
    AreaPick PickArea(Player* bot, BrainState& state, ObjectiveDef const& def, uint32 excludeAreaId = 0);

    // Nearest quest area trigger of an explore objective on the bot's map (0 when none).
    uint32 PickAreaTrigger(Player* bot, QuestKnowledge const& quest);

    // Could the bot do this objective from where it is (an area on this map within reach)?
    bool ObjectiveReachable(Player* bot, BrainState& state, QuestKnowledge const& quest, ObjectiveDef const& def);

    // Could it hand the quest in on this map?
    bool EnderReachable(Player* bot, QuestKnowledge const& quest);

    // Supported, not suspended, incomplete quest work on this map.
    bool HasQuestWork(Player* bot, BrainState& state);

    // A hub on this map with quests for the bot's level, scored against distance and crowding.
    QuestHub const* PickHub(Player* bot, BrainState& state);
}

#endif // COA_PLAYERBOTS_WORLD_PLANNER_H
