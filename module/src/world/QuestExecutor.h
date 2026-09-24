/*
 * mod-coa-playerbots
 *
 * Runs a QuestObjective task: checks the quest is still in the log and the objective still open,
 * hands the tick to the objective's handler (objectives/), and turns the handler's outcome into
 * task completion or failure. It deliberately holds no objective-specific logic -- that is what
 * the handlers are for.
 */

#ifndef COA_PLAYERBOTS_QUEST_EXECUTOR_H
#define COA_PLAYERBOTS_QUEST_EXECUTOR_H

#include "WorldExecutor.h"

namespace QuestExecutor
{
    ExecResult Update(Player* bot, BrainState& state, uint32 diff);

    // Name of the handler that would run this task, for `.botcmd brain`.
    char const* HandlerName(WorldTask const& task);
}

#endif // COA_PLAYERBOTS_QUEST_EXECUTOR_H
