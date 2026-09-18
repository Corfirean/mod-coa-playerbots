#include "BotQuestTracker.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include <cmath>
#include <limits>

BotQuestTracker* BotQuestTracker::instance()
{
    static BotQuestTracker inst;
    return &inst;
}

void BotQuestTracker::Initialize()
{
    if (_initialized)
        return;

    // Index all creature spawns by entry
    for (auto const& pair : sObjectMgr->GetAllCreatureData())
    {
        CreatureData const& data = pair.second;
        if (data.id)
            _creatureSpawns[data.id].push_back({data.mapid, data.posX, data.posY, data.posZ});
        if (data.id2)
            _creatureSpawns[data.id2].push_back({data.mapid, data.posX, data.posY, data.posZ});
        if (data.id3)
            _creatureSpawns[data.id3].push_back({data.mapid, data.posX, data.posY, data.posZ});
    }

    // Index all gameobject spawns by entry
    for (auto const& pair : sObjectMgr->GetAllGOData())
    {
        GameObjectData const& data = pair.second;
        if (data.id)
            _gameObjectSpawns[data.id].push_back({data.mapid, data.posX, data.posY, data.posZ});
    }

    // Index creature quest givers and turn-in NPCs
    if (QuestRelations* givers = sObjectMgr->GetCreatureQuestRelationMap())
    {
        for (auto const& pair : *givers)
            _questGiverCreatures[pair.second].push_back(pair.first);
    }

    if (QuestRelations* involved = sObjectMgr->GetCreatureQuestInvolvedRelationMap())
    {
        for (auto const& pair : *involved)
            _questTurnInCreatures[pair.second].push_back(pair.first);
    }

    // Populate per-map quest giver index for O(1) map-scoped lookups
    _questGiversByMap.clear();
    for (auto const& pair : _questGiverCreatures)
    {
        uint32 questId = pair.first;
        std::unordered_set<uint16> mapsSeen;
        for (uint32 npcEntry : pair.second)
        {
            auto sItr = _creatureSpawns.find(npcEntry);
            if (sItr != _creatureSpawns.end())
            {
                for (BotSpawnPoint const& sp : sItr->second)
                {
                    if (mapsSeen.insert(sp.mapId).second)
                        _questGiversByMap[sp.mapId].push_back(questId);
                }
            }
        }
    }

    _initialized = true;
}

bool BotQuestTracker::FindNearestQuestTurnIn(Player* bot, Position& outPos, uint32& outCreatureEntry)
{
    if (!_initialized)
        Initialize();

    uint16 botMap = bot->GetMapId();
    float bx = bot->GetPositionX();
    float by = bot->GetPositionY();
    float bz = bot->GetPositionZ();

    float bestDistSq = std::numeric_limits<float>::max();
    bool found = false;

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;

        if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE && !bot->CanCompleteQuest(questId))
            continue;

        auto itr = _questTurnInCreatures.find(questId);
        if (itr == _questTurnInCreatures.end())
            continue;

        for (uint32 npcEntry : itr->second)
        {
            auto sItr = _creatureSpawns.find(npcEntry);
            if (sItr == _creatureSpawns.end())
                continue;

            for (BotSpawnPoint const& sp : sItr->second)
            {
                if (sp.mapId != botMap)
                    continue;

                float dx = sp.x - bx;
                float dy = sp.y - by;
                float dz = sp.z - bz;
                float dSq = dx * dx + dy * dy + dz * dz;
                if (dSq < bestDistSq)
                {
                    bestDistSq = dSq;
                    outPos.Relocate(sp.x, sp.y, sp.z);
                    outCreatureEntry = npcEntry;
                    found = true;
                }
            }
        }
    }

    return found;
}

bool BotQuestTracker::FindNearestQuestObjective(Player* bot, Position& outPos, uint32& outEntry, bool& isGameObject)
{
    if (!_initialized)
        Initialize();

    uint16 botMap = bot->GetMapId();
    float bx = bot->GetPositionX();
    float by = bot->GetPositionY();
    float bz = bot->GetPositionZ();

    float bestDistSq = std::numeric_limits<float>::max();
    bool found = false;

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (!questId || bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 reqEntry = quest->RequiredNpcOrGo[i];
            if (!reqEntry)
                continue;

            if (bot->GetQuestSlotCounter(slot, i) >= quest->RequiredNpcOrGoCount[i])
                continue;

            if (reqEntry > 0)
            {
                uint32 cEntry = uint32(reqEntry);
                auto sItr = _creatureSpawns.find(cEntry);
                if (sItr != _creatureSpawns.end())
                {
                    for (BotSpawnPoint const& sp : sItr->second)
                    {
                        if (sp.mapId != botMap)
                            continue;

                        float dx = sp.x - bx;
                        float dy = sp.y - by;
                        float dz = sp.z - bz;
                        float dSq = dx * dx + dy * dy + dz * dz;
                        if (dSq < bestDistSq)
                        {
                            bestDistSq = dSq;
                            outPos.Relocate(sp.x, sp.y, sp.z);
                            outEntry = cEntry;
                            isGameObject = false;
                            found = true;
                        }
                    }
                }
            }
            else
            {
                uint32 goEntry = uint32(-reqEntry);
                auto sItr = _gameObjectSpawns.find(goEntry);
                if (sItr != _gameObjectSpawns.end())
                {
                    for (BotSpawnPoint const& sp : sItr->second)
                    {
                        if (sp.mapId != botMap)
                            continue;

                        float dx = sp.x - bx;
                        float dy = sp.y - by;
                        float dz = sp.z - bz;
                        float dSq = dx * dx + dy * dy + dz * dz;
                        if (dSq < bestDistSq)
                        {
                            bestDistSq = dSq;
                            outPos.Relocate(sp.x, sp.y, sp.z);
                            outEntry = goEntry;
                            isGameObject = true;
                            found = true;
                        }
                    }
                }
            }
        }
    }

    return found;
}

bool BotQuestTracker::FindNearestQuestGiver(Player* bot, Position& outPos, uint32& outCreatureEntry)
{
    if (!_initialized)
        Initialize();

    uint16 botMap = bot->GetMapId();
    float bx = bot->GetPositionX();
    float by = bot->GetPositionY();
    float bz = bot->GetPositionZ();
    uint32 botLevel = bot->GetLevel();

    float bestDistSq = std::numeric_limits<float>::max();
    bool found = false;

    auto mapItr = _questGiversByMap.find(botMap);
    if (mapItr == _questGiversByMap.end())
        return false;

    for (uint32 questId : mapItr->second)
    {
        if (bot->GetQuestStatus(questId) != QUEST_STATUS_NONE)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        if (botLevel < quest->GetMinLevel() || (quest->GetQuestLevel() > 0 && botLevel > uint32(quest->GetQuestLevel() + 5)))
            continue;

        if (!bot->CanTakeQuest(quest, false))
            continue;

        auto qgItr = _questGiverCreatures.find(questId);
        if (qgItr == _questGiverCreatures.end())
            continue;

        for (uint32 npcEntry : qgItr->second)
        {
            auto sItr = _creatureSpawns.find(npcEntry);
            if (sItr == _creatureSpawns.end())
                continue;

            for (BotSpawnPoint const& sp : sItr->second)
            {
                if (sp.mapId != botMap)
                    continue;

                float dx = sp.x - bx;
                float dy = sp.y - by;
                float dz = sp.z - bz;
                float dSq = dx * dx + dy * dy + dz * dz;
                if (dSq < 500.0f * 500.0f && dSq < bestDistSq)
                {
                    bestDistSq = dSq;
                    outPos.Relocate(sp.x, sp.y, sp.z);
                    outCreatureEntry = npcEntry;
                    found = true;
                }
            }
        }
    }

    return found;
}
