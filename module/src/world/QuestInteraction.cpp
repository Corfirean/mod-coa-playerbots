#include "QuestInteraction.h"
#include "BotAI.h"
#include "Creature.h"
#include "GameObject.h"
#include "IQuestObjectiveHandler.h"
#include "ItemTemplate.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "ObjectiveCommon.h"
#include "Opcodes.h"
#include "Player.h"
#include "QuestDef.h"
#include "QuestKnowledgeBase.h"
#include "QuestPackets.h"
#include "QuestPolicy.h"
#include "WorldPacket.h"
#include "WorldPlanner.h"
#include "WorldSession.h"
#include <cmath>

using namespace WorldBrainInternal;

namespace
{
    // Reading the quest text and clicking through the dialog: a player doesn't turn in and accept
    // in the same frame they arrive, and neither does a bot.
    constexpr uint32 READ_MIN_MS = 800;
    constexpr uint32 READ_MAX_MS = 2600;
    constexpr uint32 NPC_SEARCH_MS = 20000;
    constexpr float NPC_SEARCH_RADIUS = 50.0f;

    ExecResult FailNpc(Player* bot, BrainState& state, FailureReason reason)
    {
        WorldTask& task = state.task;
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        if (task.npcSpawnId)
            state.failures.Remember(FailKind::Npc, task.npcSpawnId, NowMs(), cfg.npcFailMs, uint8(reason));
        task.quest.lastFailure = reason;
        LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' could not deal with quest npc {} (spawn {}): {}.", bot->GetName(),
            task.npcEntry, task.npcSpawnId, FailureReasonName(reason));
        return ExecResult::Failed;
    }

    WorldObject* ResolveNpc(Player* bot, WorldTask const& task)
    {
        if (task.npcGuid.IsEmpty())
            return nullptr;
        if (task.npcIsGameObject)
            return ObjectAccessor::GetGameObject(*bot, task.npcGuid);
        Creature* creature = ObjectAccessor::GetCreature(*bot, task.npcGuid);
        return creature && creature->IsAlive() ? creature : nullptr;
    }

    QuestRelationBounds InvolvedBounds(Object* giver)
    {
        return giver->GetTypeId() == TYPEID_UNIT ? sObjectMgr->GetCreatureQuestInvolvedRelationBounds(giver->GetEntry())
            : sObjectMgr->GetGOQuestInvolvedRelationBounds(giver->GetEntry());
    }

    QuestRelationBounds OfferedBounds(Object* giver)
    {
        return giver->GetTypeId() == TYPEID_UNIT ? sObjectMgr->GetCreatureQuestRelationBounds(giver->GetEntry())
            : sObjectMgr->GetGOQuestRelationBounds(giver->GetEntry());
    }
}

namespace QuestInteraction
{
    uint32 ActiveQuestCount(Player* bot)
    {
        uint32 count = 0;
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            if (bot->GetQuestSlotQuestId(slot))
                ++count;
        return count;
    }

    bool WouldAccept(Player* bot, BrainState& state, Quest const* quest, char const*& reason)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 questId = quest->GetQuestId();
        QuestKnowledge const* info = QuestKB::Get(questId);
        if (!info)
        {
            reason = "unknown quest";
            return false;
        }
        if (!info->supported)
        {
            reason = info->unsupportedReason;
            return false;
        }
        if (info->elite && !cfg.acceptElite)
        {
            reason = "elite";
            return false;
        }
        if (state.failures.Has(FailKind::Quest, questId, NowMs()))
        {
            reason = "suspended";
            return false;
        }
        if (bot->GetQuestStatus(questId) != QUEST_STATUS_NONE)
        {
            reason = "already taken";
            return false;
        }

        int32 level = int32(bot->GetLevel());
        int32 questLevel = quest->GetQuestLevel() > 0 ? quest->GetQuestLevel() : level;
        if (questLevel + cfg.questLevelBelow < level)
        {
            reason = "too low";
            return false;
        }
        if (questLevel > level + cfg.questLevelAbove)
        {
            reason = "too high";
            return false;
        }
        if (ActiveQuestCount(bot) >= cfg.maxActiveQuests)
        {
            reason = "quest log full";
            return false;
        }
        if (!bot->CanTakeQuest(quest, false) || !bot->CanAddQuest(quest, false))
        {
            reason = "not eligible";
            return false;
        }
        int32 money = quest->GetRewOrReqMoney(bot->GetLevel());
        if (money < 0 && bot->GetMoney() < uint32(-money))
        {
            reason = "costs money";
            return false;
        }
        for (ObjectiveDef const& def : info->objectives)
        {
            // Handed out on accept: nothing to do for it.
            if (def.providedByQuest)
                continue;
            if (!def.supported || !ObjectiveHandlers::CanExecute(def))
            {
                reason = "an objective this build cannot do";
                return false;
            }
            if (!WorldPlanner::ObjectiveReachable(bot, state, *info, def))
            {
                reason = "objective out of reach";
                return false;
            }
        }
        if (!WorldPlanner::EnderReachable(bot, *info))
        {
            reason = "handed in elsewhere";
            return false;
        }
        return true;
    }

    bool Workable(Player* bot, uint32 questId, QuestKnowledge const& info, char const** why)
    {
        uint32 open = 0;
        uint32 executable = 0;
        for (ObjectiveDef const& def : info.objectives)
        {
            if (ObjectiveCommon::IsDone(bot, questId, def))
                continue;
            ++open;
            if (ObjectiveHandlers::CanExecute(def))
                ++executable;
        }
        bool workable = QuestPolicy::Workable(info.completable, open, executable);
        if (!workable && why)
            *why = !info.completable ? info.completionBlocker
                 : "an open objective this build cannot do (no handler, or a quest-provided item that is gone)";
        return workable;
    }

    uint32 PickRewardIndex(Player* bot, Quest const* quest)
    {
        if (quest->GetRewChoiceItemsCount() <= 1)
            return 0;

        BotRole role = BotAI::GetRole(bot->GetGUID());
        uint32 bestIndex = 0;
        float bestScore = -1e9f;

        for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        {
            uint32 itemId = quest->RewardChoiceItemId[i];
            if (!itemId)
                continue;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
                continue;

            // Unusable (wrong armour type, wrong weapon, class-locked): only worth its vendor price.
            float score = float(proto->SellPrice) / 100.0f;
            if (proto->InventoryType != INVTYPE_NON_EQUIP && bot->CanUseItem(proto) == EQUIP_ERR_OK)
            {
                uint8 slot = bot->FindEquipSlot(proto, NULL_SLOT, true);
                if (slot != NULL_SLOT)
                {
                    float candidate = BotAI::ScoreItemForBot(bot, proto, role);
                    float current = 0.0f;
                    if (Item* equipped = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                        if (ItemTemplate const* equippedProto = equipped->GetTemplate())
                            current = BotAI::ScoreItemForBot(bot, equippedProto, role);
                    // Usable items always beat vendor trash; among them the biggest upgrade wins.
                    score = 100000.0f + (candidate - current);
                }
            }

            if (score > bestScore)
            {
                bestScore = score;
                bestIndex = i;
            }
        }
        return bestIndex;
    }

    GiverResult ProcessGiver(Player* bot, BrainState& state, Object* giver)
    {
        GiverResult result;

        QuestRelationBounds involved = InvolvedBounds(giver);
        for (auto itr = involved.first; itr != involved.second; ++itr)
        {
            uint32 questId = itr->second;
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest || bot->GetQuestStatus(questId) == QUEST_STATUS_NONE || bot->GetQuestRewardStatus(questId))
                continue;

            // Report/deliver quests only become complete here: the client's quest dialog does
            // exactly this when it opens on the ender.
            if (bot->CanCompleteQuest(questId))
                bot->CompleteQuest(questId);
            if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
                continue;

            uint32 reward = PickRewardIndex(bot, quest);
            if (!bot->CanRewardQuest(quest, reward, false))
            {
                ++result.rewardBlocked;
                continue;
            }

            bot->RewardQuest(quest, reward, giver, true);
            ++result.turnedIn;
            Count(state.metrics, &WorldMetrics::questsTurnedIn);
            state.questSuspensions.erase(questId);
            LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' QuestTurnedIn {} ('{}'), reward choice {}.", bot->GetName(), questId,
                quest->GetTitle(), reward);
        }

        QuestRelationBounds offered = OfferedBounds(giver);
        for (auto itr = offered.first; itr != offered.second; ++itr)
        {
            uint32 questId = itr->second;
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest)
                continue;

            char const* reason = "";
            if (!WouldAccept(bot, state, quest, reason))
            {
                LOG_TRACE("module.coa-playerbots.quest", "Bot '{}' passes on quest {} ('{}'): {}.", bot->GetName(), questId,
                    quest->GetTitle(), reason);
                continue;
            }

            bot->AddQuestAndCheckCompletion(quest, giver);
            if (uint32 spell = quest->GetSrcSpell())
                bot->CastSpell(bot, spell, true);
            ++result.accepted;
            Count(state.metrics, &WorldMetrics::questsAccepted);
            LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' QuestAccepted {} ('{}', level {}).", bot->GetName(), questId,
                quest->GetTitle(), quest->GetQuestLevel());
        }

        return result;
    }

    void Abandon(Player* bot, uint32 questId)
    {
        uint16 slot = bot->FindQuestSlot(questId);
        if (slot >= MAX_QUEST_LOG_SIZE)
            return;

        WorldPacket data(CMSG_QUESTLOG_REMOVE_QUEST, 1);
        WorldPackets::Quest::QuestLogRemoveQuest packet(std::move(data));
        packet.Slot = uint8(slot);
        bot->GetSession()->HandleQuestLogRemoveQuest(packet);
        LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' abandoned quest {}.", bot->GetName(), questId);
    }

    uint32 PhaseBudgetMs(BrainState const& state)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        switch (state.task.phase)
        {
            case TaskPhase::TravelToArea: return cfg.travelTimeoutMs;
            case TaskPhase::Search:       return NPC_SEARCH_MS;
            case TaskPhase::Approach:     return cfg.approachTimeoutMs;
            default:                      return 0;
        }
    }

    ExecResult UpdateNpcTask(Player* bot, BrainState& state)
    {
        WorldTask& task = state.task;
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        uint32 now = NowMs();

        switch (task.phase)
        {
            case TaskPhase::Planning:
            case TaskPhase::Recover:
                SetPhase(bot, state, TaskPhase::TravelToArea, "heading to quest npc");
                [[fallthrough]];
            case TaskPhase::TravelToArea:
            {
                float dist = std::hypot(bot->GetPositionX() - task.x, bot->GetPositionY() - task.y);
                if (bot->GetMapId() == task.mapId && dist <= 20.0f)
                {
                    SetPhase(bot, state, TaskPhase::Search, "at the npc's spot");
                    return ExecResult::Running;
                }
                if (PhaseElapsed(state) > cfg.travelTimeoutMs)
                    return FailNpc(bot, state, FailureReason::Timeout);
                NavStatus status = WorldExecutor::TravelTo(bot, state, WorldGoalSub::Npc, task.x, task.y, task.z, 12.0f);
                if (status == NavStatus::Arrived)
                    SetPhase(bot, state, TaskPhase::Search, "at the npc's spot");
                else if (status == NavStatus::Stuck)
                {
                    Count(state.metrics, &WorldMetrics::movementStuck);
                    return FailNpc(bot, state, FailureReason::Unreachable);
                }
                return ExecResult::Running;
            }
            case TaskPhase::Search:
            {
                WorldObject* npc = task.npcIsGameObject
                    ? static_cast<WorldObject*>(bot->FindNearestGameObject(task.npcEntry, NPC_SEARCH_RADIUS))
                    : static_cast<WorldObject*>(bot->FindNearestCreature(task.npcEntry, NPC_SEARCH_RADIUS, true));
                if (npc)
                {
                    task.npcGuid = npc->GetGUID();
                    SetPhase(bot, state, TaskPhase::Approach, "found the npc");
                }
                else if (PhaseElapsed(state) > NPC_SEARCH_MS)
                    return FailNpc(bot, state, FailureReason::NoTargets);
                return ExecResult::Running;
            }
            case TaskPhase::Approach:
            {
                WorldObject* npc = ResolveNpc(bot, task);
                if (!npc)
                {
                    SetPhase(bot, state, TaskPhase::Search, "npc gone");
                    return ExecResult::Running;
                }
                float dist = bot->GetDistance(npc);
                if (dist <= INTERACTION_DISTANCE - 1.0f)
                {
                    BotMovement::Release(bot, MoveOwner::Quest);
                    if (bot->isMoving())
                    {
                        bot->StopMoving();
                        return ExecResult::Running;
                    }
                    WorldExecutor::Dismount(bot, state);
                    bot->SetFacingToObject(npc);
                    task.waitUntilMs = now + RollRange(state, 0x4ead, READ_MIN_MS, READ_MAX_MS);
                    SetPhase(bot, state, TaskPhase::Execute, "talking");
                    return ExecResult::Running;
                }
                if (PhaseElapsed(state) > cfg.approachTimeoutMs)
                    return FailNpc(bot, state, FailureReason::Unreachable);
                NavStatus status = WorldExecutor::TravelTo(bot, state, WorldGoalSub::Npc, npc->GetPositionX(), npc->GetPositionY(),
                    npc->GetPositionZ(), INTERACTION_DISTANCE - 2.0f, npc->GetGUID().GetCounter(), dist > 60.0f);
                if (status == NavStatus::Stuck)
                    return FailNpc(bot, state, FailureReason::Unreachable);
                return ExecResult::Running;
            }
            case TaskPhase::Execute:
            {
                if (now < task.waitUntilMs)
                    return ExecResult::Running;

                Object* giver = task.npcIsGameObject
                    ? static_cast<Object*>(bot->GetGameObjectIfCanInteractWith(task.npcGuid, GAMEOBJECT_TYPE_QUESTGIVER))
                    : static_cast<Object*>(bot->GetNPCIfCanInteractWith(task.npcGuid, UNIT_NPC_FLAG_QUESTGIVER));
                if (!giver)
                {
                    if (task.quest.retryCount++ < 2)
                    {
                        SetPhase(bot, state, TaskPhase::Approach, "not in reach to talk");
                        return ExecResult::Running;
                    }
                    return FailNpc(bot, state, FailureReason::InteractFailed);
                }

                GiverResult result = ProcessGiver(bot, state, giver);
                NoteEvent(state, Acore::StringFormat("at quest npc {}: {} handed in, {} accepted", task.npcEntry, result.turnedIn,
                    result.accepted));

                if (!result.turnedIn && !result.accepted)
                {
                    if (result.rewardBlocked && task.quest.questId)
                        state.failures.Remember(FailKind::TurnIn, task.quest.questId, now, 120000, uint8(FailureReason::InteractFailed));
                    return FailNpc(bot, state, FailureReason::InteractFailed);
                }

                task.waitUntilMs = now + RollRange(state, 0x60de, 600, 2200);
                SetPhase(bot, state, TaskPhase::Verify, "done talking");
                return ExecResult::Running;
            }
            case TaskPhase::Verify:
                return now < task.waitUntilMs ? ExecResult::Running : ExecResult::Completed;
            default:
                return ExecResult::Failed;
        }
    }
}
