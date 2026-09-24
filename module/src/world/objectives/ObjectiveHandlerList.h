/*
 * mod-coa-playerbots
 *
 * Accessors for the handler singletons, used only by the registry (ObjectiveHandlers::For).
 */

#ifndef COA_PLAYERBOTS_OBJECTIVE_HANDLER_LIST_H
#define COA_PLAYERBOTS_OBJECTIVE_HANDLER_LIST_H

class IQuestObjectiveHandler;

IQuestObjectiveHandler& KillObjectiveHandlerInstance();

#endif // COA_PLAYERBOTS_OBJECTIVE_HANDLER_LIST_H
