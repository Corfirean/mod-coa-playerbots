/*
 * mod-coa-playerbots
 *
 * WorldBrain: the single top-level owner of what an ungrouped bot does in the open world, and why.
 *
 * Before this, five systems each decided on their own to take the bot somewhere -- SoloIntent's
 * activity order, the quest scans, the long-distance quest tracker, the grind anchor, the ambient
 * errands -- and whichever issued a MovePoint last won that tick. The brain replaces that with one
 * decision per bot:
 *
 *   WorldBrain         what am I doing and why (goal, current task, interruptions, suspension)
 *     WorldPlanner     choose the next task by utility (quests, turn-ins, new quests)
 *     WorldExecutor    advance the task a phase at a time (travel, search, approach, act, verify)
 *       QuestExecutor + objective handlers (kill, collect, loot)
 *
 * Activities that still live in BotAI.cpp (gathering, fishing, grinding) and the ambient errands
 * (BotWorldBehavior) now run only when the brain hands them the tick through a WorldDirective, so
 * two of them can never fight over the bot.
 *
 * Combat is not the brain's business. When a task engages a mob it calls Attack() and steps back;
 * the existing combat engine fights it, and the brain picks the task up again (loot, verify, next
 * target) once the bot is out of combat. Grouped bots, manual commands, battlegrounds and dungeons
 * suspend the brain entirely.
 */

#ifndef COA_PLAYERBOTS_WORLD_BRAIN_H
#define COA_PLAYERBOTS_WORLD_BRAIN_H

#include "ObjectGuid.h"
#include "WorldTask.h"

class ChatHandler;
class Player;

namespace WorldBrain
{
    // Loads configuration and builds the quest knowledge base. Called once from OnStartup.
    void Initialize();

    // World-thread tick shared by all bots (reservation sweep). Called from BotMgr::Update.
    void GlobalUpdate(uint32 diff);

    // One tick for an ungrouped, idle bot (no combat target, not looting, not resting). Returns
    // what BotAI should do with the rest of the tick.
    WorldDirective Update(Player* bot, uint32 diff, WorldPersona const& persona);

    // BotAI ran the activity the brain asked for; `started` is whether it found anything to do.
    void ReportActivity(Player* bot, WorldDirective directive, bool started);

    // The ambient layer took the tick (a repair/vendor need, a flight): the current task pauses.
    void NotifyAmbientBusy(Player* bot);

    // Something else owns the bot now (a group, a manual command, a battleground, a dungeon).
    // Releases every claim; the task is dropped and re-planned when the brain is next updated.
    void Suspend(Player* bot, SuspendReason reason);

    // The bot died: drop the live target claim, keep the task, re-evaluate the area on return.
    void OnDeath(Player* bot);

    // Whether the bot has quest work it can do on its current map -- zone progression asks before
    // relocating a bot that is in the middle of something.
    bool HasQuestWork(Player* bot);

    WorldGoal GetGoal(ObjectGuid botGuid);

    // `.botcmd brain` and `.botcmd worldstats`.
    void Describe(Player* bot, ChatHandler* handler);
    void DescribeGlobal(ChatHandler* handler);

    // Logout/despawn: drops all state, claims, reservations and heatmap presence.
    void Forget(ObjectGuid botGuid);
}

#endif // COA_PLAYERBOTS_WORLD_BRAIN_H
