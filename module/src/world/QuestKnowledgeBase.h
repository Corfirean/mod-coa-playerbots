/*
 * mod-coa-playerbots
 *
 * Everything the open-world layer knows about quests, built once at startup from data the world
 * was already loaded from, so runtime decisions are cheap lookups and never a database query or a
 * scan of every spawn in the game. Replaces BotQuestTracker, which knew spawn coordinates and
 * nothing else.
 *
 * For every quest:
 *  - who gives it and who takes it back (creatures and game objects);
 *  - what each objective actually is -- kill, use an object, collect an item, explore, use a
 *    quest item on something -- and whether a bot can do it at all;
 *  - where the objective's targets live.
 *
 * And kill-credit proxies, so "kill 8 Defias" counts every creature whose KillCredit is that entry.
 * (Item sources -- reverse loot resolution for collect quests -- arrive with the loot-quest phase.)
 *
 * Classification is deliberately conservative: a quest a bot cannot reliably finish (escorts,
 * scripted events, talk-to-NPC credit, PvP, reputation, timed) is marked unsupported and never
 * accepted -- better not taken than taken and stuck on forever.
 */

#ifndef COA_PLAYERBOTS_QUEST_KNOWLEDGE_BASE_H
#define COA_PLAYERBOTS_QUEST_KNOWLEDGE_BASE_H

#include "Define.h"
#include "WorldTask.h"
#include <string>
#include <vector>

struct SpawnPoint
{
    uint32 mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32 spawnId = 0;
    uint32 phaseMask = 0;
};

struct LootSource
{
    uint32 entry = 0;
    bool gameObject = false;
    float chance = 0.0f; // 0-1
};

// What the bot physically does for an objective. Objectives with the same action can share a trip
// ("kill kobolds" + "loot candles from kobolds"); different actions never do -- a kill task must
// not start killing the creatures another objective wants a quest item used on.
enum class ObjectiveAction : uint8
{
    None,
    KillAndLoot,
    UseObject,
    OpenObject,
    CastOnCreature,
    CastOnObject,
    Explore,
    Talk,
};

inline ObjectiveAction ActionOf(ObjectiveType type)
{
    switch (type)
    {
        case ObjectiveType::KillCreature:
        case ObjectiveType::CollectItem:      return ObjectiveAction::KillAndLoot;
        case ObjectiveType::UseGameObject:
        case ObjectiveType::UseItemSource:    return ObjectiveAction::UseObject;
        case ObjectiveType::LootGameObject:   return ObjectiveAction::OpenObject;
        case ObjectiveType::CastOnCreature:   return ObjectiveAction::CastOnCreature;
        case ObjectiveType::CastOnGameObject: return ObjectiveAction::CastOnObject;
        case ObjectiveType::Explore:          return ObjectiveAction::Explore;
        case ObjectiveType::TalkTo:           return ObjectiveAction::Talk;
        default:                              return ObjectiveAction::None;
    }
}

struct ObjectiveDef
{
    ObjectiveType type = ObjectiveType::None;
    // Index into Quest::RequiredNpcOrGo (0-3) for kill/use/cast objectives, into
    // Quest::RequiredItemId (0-5) for item objectives; unused for explore.
    uint8 slot = 0;
    uint32 targetEntry = 0;              // the entry named by the quest (creature/object)
    std::vector<uint32> targets;         // entries to actually look for in the world
    bool targetsAreGameObjects = false;
    uint32 itemId = 0;
    uint32 requiredCount = 0;
    uint32 castSpellId = 0;              // cast objectives: the quest item's use spell
    uint32 castItemId = 0;
    float bestChance = 1.0f;             // collect objectives: best drop chance among sources
    bool elite = false;                  // every creature target is elite
    bool providedByQuest = false;        // the quest hands the item out on accept (its source item)
    bool supported = false;
    char const* unsupportedReason = "";
};

struct QuestKnowledge
{
    uint32 questId = 0;
    std::vector<uint32> giverCreatures;
    std::vector<uint32> giverObjects;
    std::vector<uint32> enderCreatures;
    std::vector<uint32> enderObjects;
    std::vector<ObjectiveDef> objectives;
    std::vector<uint32> areaTriggers;
    // Would a bot take this quest (acceptance policy + what the data says bots can do)?
    bool supported = false;
    char const* unsupportedReason = "";
    // Can a bot ever finish it, whoever put it in the log? False for player kills, reputation
    // targets, no ender. (Whether each open objective has a handler is asked separately:
    // ObjectiveHandlers::CanExecute.)
    bool completable = true;
    char const* completionBlocker = "";
    bool elite = false;
    bool hasObjectives = false;          // false: a deliver / report-to quest
};

// One spawn of a quest giver, with the quests it offers.
struct GiverSpot
{
    uint32 mapId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint32 entry = 0;
    uint32 spawnId = 0;
    uint32 phaseMask = 0;
    bool gameObject = false;
    std::vector<uint32> quests;
};

// A town or camp full of quest givers: where a player goes looking for work.
struct QuestHub
{
    uint32 id = 0;
    uint32 mapId = 0;
    float x = 0.0f;           // a real giver's position (walkable), near the centre
    float y = 0.0f;
    float z = 0.0f;
    float radius = 0.0f;
    uint32 questCount = 0;
    uint8 minQuestLevel = 0;  // quest levels on offer, for a cheap first filter
    uint8 maxQuestLevel = 0;
    std::vector<GiverSpot const*> givers;
};

struct KnowledgeStats
{
    uint32 quests = 0;
    uint32 supported = 0;
    uint32 creatureEntries = 0;
    uint32 objectEntries = 0;
    uint32 lootItems = 0;
    uint32 giverSpots = 0;
    uint32 hubs = 0;
    uint32 areaTriggers = 0;
    uint32 buildMs = 0;
};

namespace QuestKB
{
    // Builds every index. Called once from the module's OnStartup, after the world is loaded.
    void Initialize();
    bool IsReady();

    QuestKnowledge const* Get(uint32 questId);

    std::vector<SpawnPoint> const* CreatureSpawns(uint32 entry);
    std::vector<SpawnPoint> const* GameObjectSpawns(uint32 entry);
    std::vector<LootSource> const* ItemSources(uint32 itemId);

    // Giver spawns on `mapId` within `radius` (2D) of the point.
    void GiversNear(uint32 mapId, float x, float y, float radius, std::vector<GiverSpot const*>& out);

    std::vector<QuestHub> const* Hubs(uint32 mapId);

    KnowledgeStats const& Stats();

    // The unsupported reason (or "supported") and objective summary, for `.botcmd brain`.
    std::string DescribeQuest(uint32 questId);
}

#endif // COA_PLAYERBOTS_QUEST_KNOWLEDGE_BASE_H
