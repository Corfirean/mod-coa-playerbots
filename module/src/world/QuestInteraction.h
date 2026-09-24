/*
 * mod-coa-playerbots
 *
 * Talking to quest givers the way a player does: walk up to the NPC (or the wanted poster), hand
 * in whatever is finished -- picking the reward that is actually an upgrade instead of always the
 * first one -- and take the quests worth taking. "Worth taking" is where most of the old stuck
 * bots came from: everything on offer was accepted, including escorts, scripted events and quests
 * on another continent. Now a quest is only accepted when the knowledge base says a bot can do
 * every objective, its level fits, its objectives and its ender are reachable on this map, and the
 * quest log has room.
 *
 * All real Player APIs, the same calls the client's quest dialog ends in (CanTakeQuest,
 * AddQuestAndCheckCompletion, CanRewardQuest, RewardQuest). Report/deliver quests complete through
 * the ender exactly as the client's dialog makes them complete; nothing else is ever completed
 * without being done.
 */

#ifndef COA_PLAYERBOTS_QUEST_INTERACTION_H
#define COA_PLAYERBOTS_QUEST_INTERACTION_H

#include "WorldBrainState.h"
#include "WorldExecutor.h"

class Object;
class Player;
class Quest;

namespace QuestInteraction
{
    struct GiverResult
    {
        uint32 turnedIn = 0;
        uint32 accepted = 0;
        uint32 rewardBlocked = 0; // finished but not rewardable (bags full, money short)
    };

    // Hands in everything finished at this giver, then accepts what the bot would do.
    GiverResult ProcessGiver(Player* bot, BrainState& state, Object* giver);

    // Would the bot take this quest from this spot? `reason` explains a refusal.
    bool WouldAccept(Player* bot, BrainState& state, Quest const* quest, char const*& reason);

    // Best reward choice: an upgrade the bot can use, else the most valuable to sell.
    uint32 PickRewardIndex(Player* bot, Quest const* quest);

    uint32 ActiveQuestCount(Player* bot);

    // Removes a quest the bot gave up on, through the same handler the quest log's Abandon button
    // uses.
    void Abandon(Player* bot, uint32 questId);

    // Executes QuestAccept / QuestTurnIn tasks: travel to the giver's spawn, find the live NPC,
    // walk up, interact.
    ExecResult UpdateNpcTask(Player* bot, BrainState& state);
}

#endif // COA_PLAYERBOTS_QUEST_INTERACTION_H
