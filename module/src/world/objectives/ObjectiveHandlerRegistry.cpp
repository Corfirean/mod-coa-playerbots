#include "IQuestObjectiveHandler.h"
#include "ObjectiveHandlerList.h"
#include <array>

namespace ObjectiveHandlers
{
    IQuestObjectiveHandler* For(ObjectiveDef const& def)
    {
        static std::array<IQuestObjectiveHandler*, 1> const handlers =
        {
            &KillObjectiveHandlerInstance(),
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
