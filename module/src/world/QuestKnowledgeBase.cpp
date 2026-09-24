#include "QuestKnowledgeBase.h"
#include "CreatureData.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "QueryResult.h"
#include "GameObject.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "SpawnClustering.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace
{
    bool _ready = false;
    KnowledgeStats _stats;

    std::unordered_map<uint32, QuestKnowledge> _quests;
    std::unordered_map<uint32, std::vector<SpawnPoint>> _creatureSpawns;
    std::unordered_map<uint32, std::vector<SpawnPoint>> _objectSpawns;
    std::unordered_map<uint32, std::vector<LootSource>> _itemSources;
    std::unordered_map<uint32, std::vector<uint32>> _killCreditSources;

    // Giver spawns live in a deque so the pointers handed out by the grid and the hubs stay valid.
    std::deque<GiverSpot> _giverSpots;
    constexpr float GIVER_CELL = 250.0f;
    std::unordered_map<uint32, std::unordered_map<uint64, std::vector<GiverSpot const*>>> _giverGrid;
    std::unordered_map<uint32, std::vector<QuestHub>> _hubs;

    // Quest givers this close together are one hub; a town wider than this is split.
    constexpr float HUB_LINK_YARDS = 90.0f;
    constexpr float HUB_MAX_RADIUS = 220.0f;

    int32 GiverCellOf(float v)
    {
        return int32(std::floor(v / GIVER_CELL));
    }

    uint64 GiverCellKey(int32 cx, int32 cy)
    {
        return (uint64(uint32(cx)) << 32) | uint32(cy);
    }

    void AddSpawn(std::unordered_map<uint32, std::vector<SpawnPoint>>& index, uint32 entry, SpawnData const& data)
    {
        if (!entry)
            return;
        index[entry].push_back(SpawnPoint{ data.mapid, data.posX, data.posY, data.posZ, uint32(data.spawnId), data.phaseMask });
    }

    void IndexSpawns()
    {
        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            AddSpawn(_creatureSpawns, data.id, data);
            if (data.id2 && data.id2 != data.id)
                AddSpawn(_creatureSpawns, data.id2, data);
            if (data.id3 && data.id3 != data.id && data.id3 != data.id2)
                AddSpawn(_creatureSpawns, data.id3, data);
        }

        for (auto const& [spawnId, data] : sObjectMgr->GetAllGOData())
            AddSpawn(_objectSpawns, data.id, data);

        _stats.creatureEntries = uint32(_creatureSpawns.size());
        _stats.objectEntries = uint32(_objectSpawns.size());
    }

    void IndexKillCredits()
    {
        for (auto const& [entry, tmpl] : *sObjectMgr->GetCreatureTemplates())
            for (uint32 credit : tmpl.KillCredit)
                if (credit && credit != entry)
                    _killCreditSources[credit].push_back(entry);
    }

    std::unordered_map<uint32, std::vector<uint32>> ReadQuestAreaTriggers()
    {
        std::unordered_map<uint32, std::vector<uint32>> byQuest;
        QueryResult result = WorldDatabase.Query("SELECT id, quest FROM areatrigger_involvedrelation");
        if (!result)
            return byQuest;
        do
        {
            Field* fields = result->Fetch();
            uint32 trigger = fields[0].Get<uint32>();
            if (sObjectMgr->GetAreaTrigger(trigger))
                byQuest[fields[1].Get<uint32>()].push_back(trigger);
        } while (result->NextRow());
        return byQuest;
    }

    bool FriendlyToTeam(FactionTemplateEntry const* faction, uint32 mask)
    {
        return (faction->ourMask & mask) || (faction->friendlyMask & mask);
    }

    // Can a player of at least one faction fight this creature? Faction-specific enemies (a
    // Horde quest to kill Alliance guards) count; the live check per bot is IsValidAttackTarget.
    bool KillableByPlayers(CreatureTemplate const* tmpl)
    {
        FactionTemplateEntry const* faction = sFactionTemplateStore.LookupEntry(tmpl->faction);
        if (!faction)
            return true;
        return !(FriendlyToTeam(faction, FACTION_MASK_ALLIANCE) && FriendlyToTeam(faction, FACTION_MASK_HORDE));
    }

    bool IsEliteRank(uint32 rank)
    {
        return rank == CREATURE_ELITE_ELITE || rank == CREATURE_ELITE_RAREELITE || rank == CREATURE_ELITE_WORLDBOSS;
    }

    bool HasSpawns(bool gameObject, uint32 entry)
    {
        auto const& index = gameObject ? _objectSpawns : _creatureSpawns;
        return index.count(entry) != 0;
    }

    struct QuestItemSpell
    {
        uint32 itemId = 0;
        uint32 spellId = 0;
    };

    // The on-use spells of the items the quest itself hands out (its start item and its provided
    // items): the "use the Blessed Torch on..." part of a cast objective.
    std::vector<QuestItemSpell> QuestItemSpells(Quest const* quest)
    {
        std::vector<QuestItemSpell> spells;
        auto consider = [&spells](uint32 itemId)
        {
            if (!itemId)
                return;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
            if (!proto)
                return;
            for (auto const& spell : proto->Spells)
                if (spell.SpellId > 0 && spell.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE &&
                    sSpellMgr->GetSpellInfo(uint32(spell.SpellId)))
                    spells.push_back(QuestItemSpell{ itemId, uint32(spell.SpellId) });
        };
        consider(quest->GetSrcItemId());
        for (uint32 item : quest->ItemDrop)
            consider(item);
        return spells;
    }

    // Does the spell hand out kill credit for this exact entry? The strongest possible signal that
    // an objective is "use this item on X", not "kill X".
    bool SpellCreditsEntry(uint32 spellId, uint32 entry)
    {
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(spellId);
        if (!spell)
            return false;
        for (uint8 eff = 0; eff < MAX_SPELL_EFFECTS; ++eff)
        {
            uint32 effect = spell->Effects[eff].Effect;
            if ((effect == SPELL_EFFECT_KILL_CREDIT || effect == SPELL_EFFECT_KILL_CREDIT2) &&
                uint32(spell->Effects[eff].MiscValue) == entry)
                return true;
        }
        return false;
    }

    void Unsupported(ObjectiveDef& def, char const* reason)
    {
        def.supported = false;
        def.unsupportedReason = reason;
    }

    ObjectiveDef ClassifyNpcOrGo(Quest const* quest, uint8 slot, std::vector<QuestItemSpell> const& itemSpells)
    {
        ObjectiveDef def;
        def.slot = slot;
        def.requiredCount = quest->RequiredNpcOrGoCount[slot];
        int32 raw = quest->RequiredNpcOrGo[slot];

        if (raw > 0)
        {
            uint32 entry = uint32(raw);
            def.targetEntry = entry;
            CreatureTemplate const* tmpl = sObjectMgr->GetCreatureTemplate(entry);
            if (!tmpl)
            {
                def.type = ObjectiveType::Other;
                Unsupported(def, "creature template missing");
                return def;
            }

            // "Use this item on X" when an item the quest gives the player credits X directly.
            for (QuestItemSpell const& itemSpell : itemSpells)
            {
                if (!SpellCreditsEntry(itemSpell.spellId, entry))
                    continue;
                def.type = ObjectiveType::CastOnCreature;
                def.castItemId = itemSpell.itemId;
                def.castSpellId = itemSpell.spellId;
                break;
            }

            // What to actually look for: the entry itself when it is spawned, plus every creature
            // whose kill credits it (many "kill N Defias" quests credit an invisible proxy entry).
            if (HasSpawns(false, entry))
                def.targets.push_back(entry);
            auto proxies = _killCreditSources.find(entry);
            if (proxies != _killCreditSources.end())
                for (uint32 proxy : proxies->second)
                    if (HasSpawns(false, proxy))
                        def.targets.push_back(proxy);

            if (def.targets.empty())
            {
                if (def.type == ObjectiveType::None)
                    def.type = ObjectiveType::Other;
                Unsupported(def, "no spawned creature gives this credit");
                return def;
            }

            bool anyKillable = false;
            bool allElite = true;
            for (uint32 target : def.targets)
            {
                CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(target);
                if (!t)
                    continue;
                if (KillableByPlayers(t))
                    anyKillable = true;
                if (!IsEliteRank(t->rank))
                    allElite = false;
            }
            def.elite = allElite;

            if (def.type == ObjectiveType::CastOnCreature)
            {
                def.supported = true;
                return def;
            }

            if (anyKillable)
            {
                def.type = ObjectiveType::KillCreature;
                // A quest item with a use spell is remembered: if killing never advances the
                // counter, the executor switches to using it on the target instead.
                if (!itemSpells.empty())
                {
                    def.castItemId = itemSpells.front().itemId;
                    def.castSpellId = itemSpells.front().spellId;
                }
                def.supported = true;
                return def;
            }

            if (!itemSpells.empty())
            {
                def.type = ObjectiveType::CastOnCreature;
                def.castItemId = itemSpells.front().itemId;
                def.castSpellId = itemSpells.front().spellId;
                def.supported = true;
                return def;
            }

            def.type = ObjectiveType::TalkTo;
            Unsupported(def, "credit from a friendly npc (gossip/script)");
            return def;
        }

        uint32 entry = uint32(-raw);
        def.targetEntry = entry;
        def.targetsAreGameObjects = true;
        GameObjectTemplate const* tmpl = sObjectMgr->GetGameObjectTemplate(entry);
        if (!tmpl)
        {
            def.type = ObjectiveType::Other;
            Unsupported(def, "object template missing");
            return def;
        }
        if (!HasSpawns(true, entry))
        {
            def.type = ObjectiveType::UseGameObject;
            Unsupported(def, "object is never spawned");
            return def;
        }
        def.targets.push_back(entry);

        if (tmpl->type == GAMEOBJECT_TYPE_GOOBER)
        {
            def.type = ObjectiveType::UseGameObject;
            def.supported = true;
            return def;
        }

        if (!itemSpells.empty())
        {
            def.type = ObjectiveType::CastOnGameObject;
            def.castItemId = itemSpells.front().itemId;
            def.castSpellId = itemSpells.front().spellId;
            def.supported = true;
            return def;
        }

        def.type = ObjectiveType::UseGameObject;
        Unsupported(def, "object type gives no credit on use");
        return def;
    }

    ObjectiveDef ClassifyItem(Quest const* quest, uint8 slot)
    {
        ObjectiveDef def;
        def.slot = slot;
        def.itemId = quest->RequiredItemId[slot];
        def.requiredCount = quest->RequiredItemCount[slot];
        def.type = ObjectiveType::CollectItem;

        // Delivery quests: the quest hands the item out itself.
        if (def.itemId == quest->GetSrcItemId() ||
            std::find(std::begin(quest->ItemDrop), std::end(quest->ItemDrop), def.itemId) != std::end(quest->ItemDrop))
        {
            def.providedByQuest = true;
            def.supported = true;
            return def;
        }

        auto itr = _itemSources.find(def.itemId);
        if (itr == _itemSources.end())
        {
            Unsupported(def, "no loot source (vendor, craft or script item)");
            return def;
        }

        std::vector<LootSource> creatures;
        std::vector<LootSource> objects;
        for (LootSource const& source : itr->second)
        {
            if (!HasSpawns(source.gameObject, source.entry))
                continue;
            (source.gameObject ? objects : creatures).push_back(source);
        }

        // Creatures first: killing things that drop it is the common case and needs no object
        // type juggling. Objects only when nothing alive drops it.
        std::vector<LootSource>& chosen = !creatures.empty() ? creatures : objects;
        if (chosen.empty())
        {
            Unsupported(def, "item sources are never spawned");
            return def;
        }

        std::sort(chosen.begin(), chosen.end(), [](LootSource const& a, LootSource const& b) { return a.chance > b.chance; });
        // Keep the sources worth farming: the best dropper and anything at least a fifth as good.
        float best = chosen.front().chance;
        def.bestChance = best;
        for (LootSource const& source : chosen)
            if (source.chance >= best * 0.2f && def.targets.size() < 12)
                def.targets.push_back(source.entry);

        if (chosen.front().gameObject)
        {
            def.targetsAreGameObjects = true;
            GameObjectTemplate const* go = sObjectMgr->GetGameObjectTemplate(def.targets.front());
            def.type = go && go->type == GAMEOBJECT_TYPE_GOOBER ? ObjectiveType::UseItemSource : ObjectiveType::LootGameObject;
        }
        else
        {
            bool allElite = true;
            for (uint32 target : def.targets)
                if (CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(target); t && !IsEliteRank(t->rank))
                    allElite = false;
            def.elite = allElite;
        }

        def.supported = true;
        return def;
    }

    void MarkQuest(QuestKnowledge& info, char const* reason)
    {
        info.supported = false;
        info.unsupportedReason = reason;
    }

    void ClassifyQuest(Quest const* quest, QuestKnowledge& info, std::vector<uint32> const* triggers)
    {
        info.supported = true;
        info.unsupportedReason = "";

        std::vector<QuestItemSpell> itemSpells = QuestItemSpells(quest);

        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            if (quest->RequiredNpcOrGo[i] && quest->RequiredNpcOrGoCount[i])
                info.objectives.push_back(ClassifyNpcOrGo(quest, i, itemSpells));

        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            if (quest->RequiredItemId[i] && quest->RequiredItemCount[i])
                info.objectives.push_back(ClassifyItem(quest, i));

        if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT))
        {
            ObjectiveDef def;
            def.type = ObjectiveType::Explore;
            def.requiredCount = 1;
            if (triggers && !triggers->empty())
            {
                info.areaTriggers = *triggers;
                def.supported = true;
            }
            else
            {
                // No trigger means completion comes from a script or an escort event.
                def.type = ObjectiveType::Escort;
                Unsupported(def, "scripted event or escort");
            }
            info.objectives.push_back(def);
        }

        info.hasObjectives = !info.objectives.empty();
        info.elite = false;
        for (ObjectiveDef const& def : info.objectives)
        {
            if (!def.supported)
            {
                MarkQuest(info, def.unsupportedReason);
                break;
            }
            if (def.elite)
                info.elite = true;
        }

        if (quest->GetPlayersSlain())
            MarkQuest(info, "player kills");
        else if (quest->GetRepObjectiveFaction() || quest->GetRepObjectiveFaction2())
            MarkQuest(info, "reputation objective");
        else if (quest->GetTimeAllowed())
            MarkQuest(info, "timed");
        else if (quest->IsDailyOrWeekly() || quest->IsMonthly() || quest->IsSeasonal())
            MarkQuest(info, "daily/weekly/seasonal");
        else if (info.giverCreatures.empty() && info.giverObjects.empty())
            MarkQuest(info, "no quest giver (item or script started)");
        else if (info.enderCreatures.empty() && info.enderObjects.empty())
            MarkQuest(info, "no quest ender");
    }

    void IndexQuests(std::unordered_map<uint32, std::vector<uint32>> const& triggers)
    {
        auto relations = [](QuestRelations const* map, bool giver, bool gameObject)
        {
            if (!map)
                return;
            for (auto const& [entry, questId] : *map)
            {
                QuestKnowledge& info = _quests[questId];
                auto& list = giver ? (gameObject ? info.giverObjects : info.giverCreatures)
                                   : (gameObject ? info.enderObjects : info.enderCreatures);
                if (std::find(list.begin(), list.end(), entry) == list.end())
                    list.push_back(entry);
            }
        };

        relations(sObjectMgr->GetCreatureQuestRelationMap(), true, false);
        relations(sObjectMgr->GetGOQuestRelationMap(), true, true);
        relations(sObjectMgr->GetCreatureQuestInvolvedRelationMap(), false, false);
        relations(sObjectMgr->GetGOQuestInvolvedRelationMap(), false, true);

        for (auto const& [questId, quest] : sObjectMgr->GetQuestTemplates())
        {
            QuestKnowledge& info = _quests[questId];
            info.questId = questId;
            auto trig = triggers.find(questId);
            ClassifyQuest(quest, info, trig == triggers.end() ? nullptr : &trig->second);
            ++_stats.quests;
            if (info.supported)
                ++_stats.supported;
        }

        // Relation rows for quests that no longer exist would leave empty shells behind.
        for (auto itr = _quests.begin(); itr != _quests.end();)
            itr = itr->second.questId ? std::next(itr) : _quests.erase(itr);

        _stats.areaTriggers = 0;
        for (auto const& [quest, list] : triggers)
            _stats.areaTriggers += uint32(list.size());
    }

    void IndexGivers()
    {
        auto addGivers = [](QuestRelations const* map, bool gameObject)
        {
            if (!map)
                return;
            std::unordered_map<uint32, std::vector<uint32>> questsByEntry;
            for (auto const& [entry, questId] : *map)
                questsByEntry[entry].push_back(questId);

            for (auto const& [entry, quests] : questsByEntry)
            {
                auto const& index = gameObject ? _objectSpawns : _creatureSpawns;
                auto spawns = index.find(entry);
                if (spawns == index.end())
                    continue;
                for (SpawnPoint const& sp : spawns->second)
                {
                    GiverSpot& spot = _giverSpots.emplace_back();
                    spot.mapId = sp.mapId;
                    spot.x = sp.x;
                    spot.y = sp.y;
                    spot.z = sp.z;
                    spot.entry = entry;
                    spot.spawnId = sp.spawnId;
                    spot.phaseMask = sp.phaseMask;
                    spot.gameObject = gameObject;
                    spot.quests = quests;
                    _giverGrid[sp.mapId][GiverCellKey(GiverCellOf(sp.x), GiverCellOf(sp.y))].push_back(&spot);
                }
            }
        };

        addGivers(sObjectMgr->GetCreatureQuestRelationMap(), false);
        addGivers(sObjectMgr->GetGOQuestRelationMap(), true);
        _stats.giverSpots = uint32(_giverSpots.size());
    }

    void IndexHubs()
    {
        std::unordered_map<uint32, std::vector<GiverSpot const*>> byMap;
        for (GiverSpot const& spot : _giverSpots)
            byMap[spot.mapId].push_back(&spot);

        uint32 nextId = 1;
        for (auto& [mapId, spots] : byMap)
        {
            std::vector<ClusterInputPoint> points;
            points.reserve(spots.size());
            for (GiverSpot const* spot : spots)
                points.push_back(ClusterInputPoint{ spot->x, spot->y, spot->z });

            for (auto const& members : SpawnClustering::Cluster(points, HUB_LINK_YARDS, 40.0f, HUB_MAX_RADIUS))
            {
                std::unordered_set<uint32> quests;
                uint8 minLevel = 255;
                uint8 maxLevel = 0;
                QuestHub hub;
                for (uint32 i : members)
                {
                    hub.givers.push_back(spots[i]);
                    for (uint32 questId : spots[i]->quests)
                    {
                        auto info = _quests.find(questId);
                        if (info == _quests.end() || !info->second.supported || !quests.insert(questId).second)
                            continue;
                        if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
                        {
                            int32 level = quest->GetQuestLevel() > 0 ? quest->GetQuestLevel() : int32(quest->GetMinLevel());
                            uint8 clamped = uint8(std::clamp(level, 1, 255));
                            minLevel = std::min(minLevel, clamped);
                            maxLevel = std::max(maxLevel, clamped);
                        }
                    }
                }

                // A lone giver with one quest is not a hub worth travelling to.
                if (quests.size() < 3)
                    continue;

                ClusterShape shape = SpawnClustering::Shape(points, members);
                hub.id = nextId++;
                hub.mapId = mapId;
                hub.x = points[shape.anchor].x;
                hub.y = points[shape.anchor].y;
                hub.z = points[shape.anchor].z;
                hub.radius = shape.radius;
                hub.questCount = uint32(quests.size());
                hub.minQuestLevel = minLevel;
                hub.maxQuestLevel = maxLevel;
                _hubs[mapId].push_back(std::move(hub));
            }
        }

        _stats.hubs = nextId - 1;
    }
}

namespace QuestKB
{
    void Initialize()
    {
        if (_ready)
            return;

        uint32 started = getMSTime();

        IndexSpawns();
        IndexKillCredits();
        IndexQuests(ReadQuestAreaTriggers());
        IndexGivers();
        IndexHubs();

        _stats.buildMs = GetMSTimeDiffToNow(started);
        _ready = true;

        LOG_INFO("module.coa-playerbots.quest", ">> QuestKB: {} quests ({} supported), {} creature and {} object entries spawned, "
            "{} giver spawns in {} hubs, {} quest area triggers ({} ms).",
            _stats.quests, _stats.supported, _stats.creatureEntries, _stats.objectEntries,
            _stats.giverSpots, _stats.hubs, _stats.areaTriggers, _stats.buildMs);
    }

    bool IsReady()
    {
        return _ready;
    }

    QuestKnowledge const* Get(uint32 questId)
    {
        auto itr = _quests.find(questId);
        return itr == _quests.end() ? nullptr : &itr->second;
    }

    std::vector<SpawnPoint> const* CreatureSpawns(uint32 entry)
    {
        auto itr = _creatureSpawns.find(entry);
        return itr == _creatureSpawns.end() ? nullptr : &itr->second;
    }

    std::vector<SpawnPoint> const* GameObjectSpawns(uint32 entry)
    {
        auto itr = _objectSpawns.find(entry);
        return itr == _objectSpawns.end() ? nullptr : &itr->second;
    }

    std::vector<LootSource> const* ItemSources(uint32 itemId)
    {
        auto itr = _itemSources.find(itemId);
        return itr == _itemSources.end() ? nullptr : &itr->second;
    }

    void GiversNear(uint32 mapId, float x, float y, float radius, std::vector<GiverSpot const*>& out)
    {
        auto map = _giverGrid.find(mapId);
        if (map == _giverGrid.end())
            return;

        float radiusSq = radius * radius;
        for (int32 cx = GiverCellOf(x - radius); cx <= GiverCellOf(x + radius); ++cx)
        {
            for (int32 cy = GiverCellOf(y - radius); cy <= GiverCellOf(y + radius); ++cy)
            {
                auto cell = map->second.find(GiverCellKey(cx, cy));
                if (cell == map->second.end())
                    continue;
                for (GiverSpot const* spot : cell->second)
                {
                    float dx = spot->x - x;
                    float dy = spot->y - y;
                    if (dx * dx + dy * dy <= radiusSq)
                        out.push_back(spot);
                }
            }
        }
    }

    std::vector<QuestHub> const* Hubs(uint32 mapId)
    {
        auto itr = _hubs.find(mapId);
        return itr == _hubs.end() ? nullptr : &itr->second;
    }

    KnowledgeStats const& Stats()
    {
        return _stats;
    }

    std::string DescribeQuest(uint32 questId)
    {
        QuestKnowledge const* info = Get(questId);
        if (!info)
            return "unknown quest";

        std::string out = info->supported ? "supported" : Acore::StringFormat("unsupported: {}", info->unsupportedReason);
        if (info->elite)
            out += ", elite";
        for (ObjectiveDef const& def : info->objectives)
            out += Acore::StringFormat("; {} x{} ({}{})", ObjectiveTypeName(def.type), def.requiredCount,
                def.itemId ? def.itemId : def.targetEntry, def.supported ? "" : ", unsupported");
        return out;
    }
}
