#ifndef _BOT_QUEST_TRACKER_H
#define _BOT_QUEST_TRACKER_H

#include "Common.h"
#include "SharedDefines.h"
#include "Position.h"
#include <vector>
#include <unordered_map>

class Player;

struct BotSpawnPoint
{
    uint16 mapId;
    float x;
    float y;
    float z;
};

class BotQuestTracker
{
public:
    static BotQuestTracker* instance();

    void Initialize();

    // Finds the nearest NPC where the bot can turn in an already-completed quest on the current map.
    bool FindNearestQuestTurnIn(Player* bot, Position& outPos, uint32& outCreatureEntry);

    // Finds the nearest incomplete quest objective (kill mob or interact gameobject) on the current map.
    bool FindNearestQuestObjective(Player* bot, Position& outPos, uint32& outEntry, bool& isGameObject);

    // Finds the nearest questgiver offering an eligible quest for this bot's level on the current map.
    bool FindNearestQuestGiver(Player* bot, Position& outPos, uint32& outCreatureEntry);

    void Reset()
    {
        _initialized = false;
        _creatureSpawns.clear();
        _gameObjectSpawns.clear();
        _questGiverCreatures.clear();
        _questTurnInCreatures.clear();
        _questGiversByMap.clear();
    }

private:
    BotQuestTracker() : _initialized(false) {}

    bool _initialized;
    std::unordered_map<uint32, std::vector<BotSpawnPoint>> _creatureSpawns;
    std::unordered_map<uint32, std::vector<BotSpawnPoint>> _gameObjectSpawns;
    std::unordered_map<uint32, std::vector<uint32>> _questGiverCreatures;
    std::unordered_map<uint32, std::vector<uint32>> _questTurnInCreatures;
    std::unordered_map<uint16, std::vector<uint32>> _questGiversByMap;
};

#define sBotQuestTracker BotQuestTracker::instance()

#endif
