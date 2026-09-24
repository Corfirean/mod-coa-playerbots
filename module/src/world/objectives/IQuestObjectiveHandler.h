/*
 * mod-coa-playerbots
 *
 * One handler per kind of quest objective (kill so far; collect, use, loot, explore, cast and talk
 * arrive in later phases). The quest executor picks the handler for the task's objective and lets
 * it drive the phases; the handler keeps no state of its own -- everything lives in the WorldTask
 * -- so one instance serves every bot. Adding a new objective kind (escort, say) is a new handler
 * plus a line in the registry, not another branch in a thousand-line switch.
 */

#ifndef COA_PLAYERBOTS_I_QUEST_OBJECTIVE_HANDLER_H
#define COA_PLAYERBOTS_I_QUEST_OBJECTIVE_HANDLER_H

#include "QuestKnowledgeBase.h"
#include "WorldBrainState.h"

class Player;

struct ObjectiveContext
{
    Player* bot;
    BrainState& state;
    WorldTask& task;
    QuestKnowledge const& quest;
    ObjectiveDef const& def;
    WorldBrainConfig const& cfg;
    uint32 now;
    uint32 diff;
};

enum class ObjectiveResult : uint8
{
    Running,
    Completed,
    Failed,
};

class IQuestObjectiveHandler
{
public:
    virtual ~IQuestObjectiveHandler() = default;

    virtual char const* Name() const = 0;
    // Whether this handler takes the objective. A handler decides for itself whether it also
    // takes objectives the knowledge base marked unsupported (none does yet).
    virtual bool CanHandle(ObjectiveDef const& def) const = 0;

    // Advances the task by (at most) one phase transition. Sets task.quest.lastFailure before
    // returning Failed.
    virtual ObjectiveResult Update(ObjectiveContext& ctx) = 0;
};

namespace ObjectiveHandlers
{
    // The handler for this objective, or nullptr when no handler can do it.
    IQuestObjectiveHandler* For(ObjectiveDef const& def);

    // The one answer to "can this build make progress on this objective": there is something to
    // do (a quest-provided item is not an action -- the quest handed it out, and nothing can bring
    // it back once it is gone) and a handler takes it. The planner, the quest log cleanup,
    // HasQuestWork and quest acceptance all ask this, so no part of the brain can think an
    // objective is workable while another could never create a task for it. The knowledge base's
    // `supported` flag is about what the data says; acceptance additionally requires it.
    bool CanExecute(ObjectiveDef const& def);
}

#endif // COA_PLAYERBOTS_I_QUEST_OBJECTIVE_HANDLER_H
