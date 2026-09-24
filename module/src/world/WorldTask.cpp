#include "WorldTask.h"
#include "WorldMetrics.h"

char const* WorldGoalName(WorldGoal goal)
{
    switch (goal)
    {
        case WorldGoal::Questing:  return "Questing";
        case WorldGoal::Gathering: return "Gathering";
        case WorldGoal::Fishing:   return "Fishing";
        case WorldGoal::Grinding:  return "Grinding";
        case WorldGoal::Exploring: return "Exploring";
        case WorldGoal::Traveling: return "Traveling";
        case WorldGoal::Break:     return "Break";
        default:                   return "None";
    }
}

char const* WorldTaskTypeName(WorldTaskType type)
{
    switch (type)
    {
        case WorldTaskType::QuestAccept:    return "QuestAccept";
        case WorldTaskType::QuestObjective: return "QuestObjective";
        case WorldTaskType::QuestTurnIn:    return "QuestTurnIn";
        case WorldTaskType::Gather:         return "Gather";
        case WorldTaskType::Grind:          return "Grind";
        case WorldTaskType::Explore:        return "Explore";
        case WorldTaskType::Vendor:         return "Vendor";
        case WorldTaskType::Repair:         return "Repair";
        case WorldTaskType::Travel:         return "Travel";
        case WorldTaskType::Social:         return "Social";
        default:                            return "None";
    }
}

char const* TaskPhaseName(TaskPhase phase)
{
    switch (phase)
    {
        case TaskPhase::Planning:     return "Planning";
        case TaskPhase::TravelToArea: return "TravelToArea";
        case TaskPhase::Search:       return "Search";
        case TaskPhase::Approach:     return "Approach";
        case TaskPhase::Execute:      return "Execute";
        case TaskPhase::Combat:       return "Combat";
        case TaskPhase::Loot:         return "Loot";
        case TaskPhase::Verify:       return "Verify";
        case TaskPhase::Recover:      return "Recover";
        case TaskPhase::Completed:    return "Completed";
        case TaskPhase::Failed:       return "Failed";
        default:                      return "?";
    }
}

char const* ObjectiveTypeName(ObjectiveType type)
{
    switch (type)
    {
        case ObjectiveType::KillCreature:     return "Kill";
        case ObjectiveType::UseGameObject:    return "UseObject";
        case ObjectiveType::CollectItem:      return "CollectItem";
        case ObjectiveType::LootGameObject:   return "LootObject";
        case ObjectiveType::UseItemSource:    return "UseObjectForItem";
        case ObjectiveType::Explore:          return "Explore";
        case ObjectiveType::CastOnCreature:   return "UseItemOnCreature";
        case ObjectiveType::CastOnGameObject: return "UseItemOnObject";
        case ObjectiveType::TalkTo:           return "TalkTo";
        case ObjectiveType::Escort:           return "Escort/Event";
        case ObjectiveType::Other:            return "Other";
        default:                              return "None";
    }
}

char const* FailureReasonName(FailureReason reason)
{
    switch (reason)
    {
        case FailureReason::Timeout:        return "timeout";
        case FailureReason::Unreachable:    return "unreachable";
        case FailureReason::NoTargets:      return "no targets";
        case FailureReason::TargetLost:     return "target lost";
        case FailureReason::Evaded:         return "evaded";
        case FailureReason::TargetTaken:    return "taken by someone else";
        case FailureReason::CastFailed:     return "cast failed";
        case FailureReason::InteractFailed: return "interaction failed";
        case FailureReason::Unsupported:    return "unsupported";
        case FailureReason::QuestGone:      return "quest gone";
        case FailureReason::MapChanged:     return "map changed";
        case FailureReason::Died:           return "died";
        case FailureReason::Replaced:       return "replaced";
        case FailureReason::Suspended:      return "suspended";
        case FailureReason::NoProgress:     return "no progress";
        default:                            return "none";
    }
}

char const* WorldDirectiveName(WorldDirective directive)
{
    switch (directive)
    {
        case WorldDirective::Busy:    return "Busy";
        case WorldDirective::Gather:  return "Gather";
        case WorldDirective::Fish:    return "Fish";
        case WorldDirective::Grind:   return "Grind";
        case WorldDirective::Ambient: return "Ambient";
        default:                      return "Idle";
    }
}

namespace WorldMetricsGlobal
{
    WorldMetrics& Get()
    {
        static WorldMetrics metrics;
        return metrics;
    }
}
