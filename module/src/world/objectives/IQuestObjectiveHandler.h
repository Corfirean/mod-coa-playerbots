/*
 * mod-coa-playerbots
 *
 * One handler per kind of quest objective (kill, collect, loot so far; use, explore, cast and talk
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
    virtual bool CanHandle(ObjectiveDef const& def) const = 0;

    // Advances the task by (at most) one phase transition. Sets task.quest.lastFailure before
    // returning Failed.
    virtual ObjectiveResult Update(ObjectiveContext& ctx) = 0;
};

namespace ObjectiveHandlers
{
    // The handler for this objective, or nullptr when no handler can do it.
    IQuestObjectiveHandler* For(ObjectiveDef const& def);
}

#endif // COA_PLAYERBOTS_I_QUEST_OBJECTIVE_HANDLER_H
