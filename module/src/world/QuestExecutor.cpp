#include "QuestExecutor.h"
#include "engine/BotDebugLog.h"
#include "IQuestObjectiveHandler.h"
#include "ObjectiveCommon.h"
#include "Player.h"
#include "QuestDef.h"
#include "QuestKnowledgeBase.h"

using namespace WorldBrainInternal;

namespace
{
    void CountCompletion(BrainState& state, ObjectiveType type)
    {
        Count(state.metrics, &WorldMetrics::objectivesCompleted);
        switch (ActionOf(type))
        {
            case ObjectiveAction::KillAndLoot:
                if (type == ObjectiveType::CollectItem)
                    Count(state.metrics, &WorldMetrics::lootObjectivesCompleted);
                break;
            case ObjectiveAction::OpenObject:
                Count(state.metrics, &WorldMetrics::lootObjectivesCompleted);
                break;
            case ObjectiveAction::UseObject:
            case ObjectiveAction::CastOnCreature:
            case ObjectiveAction::CastOnObject:
                Count(state.metrics, &WorldMetrics::useObjectivesCompleted);
                break;
            case ObjectiveAction::Explore:
                Count(state.metrics, &WorldMetrics::exploreObjectivesCompleted);
                break;
            default:
                break;
        }
    }
}

namespace QuestExecutor
{
    char const* HandlerName(WorldTask const& task)
    {
        QuestKnowledge const* quest = QuestKB::Get(task.quest.questId);
        if (!quest || task.quest.objectiveIndex >= quest->objectives.size())
            return "-";
        IQuestObjectiveHandler* handler = ObjectiveHandlers::For(quest->objectives[task.quest.objectiveIndex]);
        return handler ? handler->Name() : "none";
    }

    ExecResult Update(Player* bot, BrainState& state, uint32 diff)
    {
        WorldTask& task = state.task;
        uint32 questId = task.quest.questId;

        QuestStatus status = bot->GetQuestStatus(questId);
        if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
        {
            task.quest.lastFailure = FailureReason::QuestGone;
            LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
                "QuestExecutor: bot '{}' quest {} failed -- {} (status {}).",
                bot->GetName(), questId, FailureReasonName(task.quest.lastFailure), uint32(status));
            return ExecResult::Failed;
        }

        QuestKnowledge const* quest = QuestKB::Get(questId);
        if (!quest || task.quest.objectiveIndex >= quest->objectives.size())
        {
            task.quest.lastFailure = FailureReason::Unsupported;
            LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
                "QuestExecutor: bot '{}' quest {} failed -- {} (objective index {}).",
                bot->GetName(), questId, FailureReasonName(task.quest.lastFailure), uint32(task.quest.objectiveIndex));
            return ExecResult::Failed;
        }
        ObjectiveDef const& def = quest->objectives[task.quest.objectiveIndex];

        if (status == QUEST_STATUS_COMPLETE || ObjectiveCommon::IsDone(bot, questId, def))
        {
            CountCompletion(state, def.type);
            if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
            {
                Count(state.metrics, &WorldMetrics::questsCompleted);
                LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "Bot '{}' QuestCompleted {}.", bot->GetName(), questId);
            }
            LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "Bot '{}' ObjectiveCompleted quest {} objective {} ({}).", bot->GetName(),
                questId, uint32(task.quest.objectiveIndex), ObjectiveTypeName(def.type));
            return ExecResult::Completed;
        }

        IQuestObjectiveHandler* handler = ObjectiveHandlers::For(def);
        if (!handler)
        {
            task.quest.lastFailure = FailureReason::Unsupported;
            LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
                "QuestExecutor: bot '{}' quest {} objective {} ({}) has no handler.",
                bot->GetName(), questId, uint32(task.quest.objectiveIndex), ObjectiveTypeName(def.type));
            return ExecResult::Failed;
        }

        ObjectiveContext ctx{ bot, state, task, *quest, def, WorldBrainSettings::Get(), NowMs(), diff };
        switch (handler->Update(ctx))
        {
            case ObjectiveResult::Running:
                return ExecResult::Running;
            case ObjectiveResult::Completed:
                CountCompletion(state, def.type);
                if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
                {
                    Count(state.metrics, &WorldMetrics::questsCompleted);
                    LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "Bot '{}' QuestCompleted {}.", bot->GetName(), questId);
                }
                LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()), "Bot '{}' ObjectiveCompleted quest {} objective {} ({}).", bot->GetName(),
                    questId, uint32(task.quest.objectiveIndex), ObjectiveTypeName(def.type));
                return ExecResult::Completed;
            case ObjectiveResult::Failed:
            default:
                Count(state.metrics, &WorldMetrics::failedObjectives);
                LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
                    "QuestExecutor: bot '{}' quest {} objective {} ({}, handler '{}') failed -- {}.",
                    bot->GetName(), questId, uint32(task.quest.objectiveIndex), ObjectiveTypeName(def.type),
                    handler->Name(), FailureReasonName(task.quest.lastFailure));
                return ExecResult::Failed;
        }
    }
}
