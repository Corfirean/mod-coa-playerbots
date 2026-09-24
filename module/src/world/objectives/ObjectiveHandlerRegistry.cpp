#include "IQuestObjectiveHandler.h"
#include "ObjectiveHandlerList.h"
#include <array>

namespace ObjectiveHandlers
{
    IQuestObjectiveHandler* For(ObjectiveDef const& def)
    {
        static std::array<IQuestObjectiveHandler*, 8> const handlers =
        {
            &KillObjectiveHandlerInstance(),
            &LootItemObjectiveHandlerInstance(),
            &CastObjectiveHandlerInstance(),
            &UseGameObjectObjectiveHandlerInstance(),
            &LootGameObjectObjectiveHandlerInstance(),
            &CastOnObjectObjectiveHandlerInstance(),
            &ExploreObjectiveHandlerInstance(),
            &TalkObjectiveHandlerInstance(),
        };

        for (IQuestObjectiveHandler* handler : handlers)
            if (handler->CanHandle(def))
                return handler;
        return nullptr;
    }

    bool CanExecute(ObjectiveDef const& def)
    {
        return !def.providedByQuest && For(def) != nullptr;
    }
}
