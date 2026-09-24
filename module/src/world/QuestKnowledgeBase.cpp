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
    std::unordered_map<uint32, uint32> _openingSpellByLockType;

    // Giver spawns live in a deque so the pointers handed out by the grid and the hubs stay valid.
    std::deque<GiverSpot> _giverSpots;
    constexpr float GIVER_CELL = 250.0f;
    std::unordered_map<uint32, std::unordered_map<uint64, std::vector<GiverSpot const*>>> _giverGrid;
    std::unordered_map<uint32, std::vector<QuestHub>> _hubs;

    // Quest givers this close together are one hub; a town wider than this is split.
    constexpr float HUB_LINK_YARDS = 90.0f;
    constexpr float HUB_MAX_RADIUS = 220.0f;

    // Loot templates use Chance == 0 to mean "equal chance among this group's zero-chance rows",
    // not "no chance" or some fixed percentage -- the real per-row probability depends on how many
    // siblings share the group, which this reverse index doesn't track. Treating it as a guessed
    // ~20% (as an earlier version of this code did) made dry-attempt accounting actively dangerous:
    // DryAttemptLimit() derives its patience from this value, so a confident-but-wrong guess could
    // exhaust the limit on ordinary bad luck and get a perfectly good source blacklisted. Use a
    // conservative floor instead -- low enough that DryAttemptLimit() clamps to its maximum, i.e.
    // "don't know the odds, so be as patient as we ever are" rather than asserting a false one.
    constexpr float UNKNOWN_DROP_CHANCE = 0.02f;

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

    // The generic, profession-free "Opening" spells a player casts on locked objects, indexed by
    // the lock type they open. Picked from the spell store rather than hard-coded ids so custom
    // realm data can't silently break it.
    void IndexOpeningSpells()
    {
        for (uint32 id = 1; id < sSpellMgr->GetSpellInfoStoreSize(); ++id)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
            if (!info || info->IsPassive() || info->ManaCost || info->RequiresSpellFocus || info->EquippedItemClass >= 0)
                continue;
            if (info->Reagent[0] > 0 || info->Totem[0] || info->SpellFamilyName)
                continue;

            for (uint8 eff = 0; eff < MAX_SPELL_EFFECTS; ++eff)
            {
                if (info->Effects[eff].Effect != SPELL_EFFECT_OPEN_LOCK)
                    continue;
                uint32 lockType = uint32(info->Effects[eff].MiscValue);
                if (SkillByLockType(LockType(lockType)) != SKILL_NONE)
                    continue;
                auto itr = _openingSpellByLockType.find(lockType);
                if (itr == _openingSpellByLockType.end() || id < itr->second)
                    _openingSpellByLockType[lockType] = id;
            }
        }
    }

    // Every item some quest asks the player to bring: the only items worth a reverse loot lookup.
    std::unordered_set<uint32> CollectWantedItems()
    {
        std::unordered_set<uint32> items;
        for (auto const& [questId, quest] : sObjectMgr->GetQuestTemplates())
            for (uint32 item : quest->RequiredItemId)
                if (item)
                    items.insert(item);
        return items;
    }

    float NormaliseChance(float chance)
    {
        if (chance <= 0.0f)
            return UNKNOWN_DROP_CHANCE;
        return std::min(1.0f, chance / 100.0f);
    }

    std::string JoinIds(std::vector<uint32> const& ids, size_t begin, size_t end)
    {
        std::string out;
        out.reserve((end - begin) * 7);
        for (size_t i = begin; i < end; ++i)
        {
            if (i != begin)
                out += ',';
            out += std::to_string(ids[i]);
        }
        return out;
    }

    // One loot table, restricted to the rows that can produce a wanted item, directly or through a
    // reference template. Fills lootId -> (item, chance).
    void ReadLootTable(char const* table, std::vector<uint32> const& items,
        std::unordered_map<uint32, std::vector<std::pair<uint32, float>>> const& itemsByReference,
        std::unordered_map<uint32, std::vector<std::pair<uint32, float>>>& out)
    {
        constexpr size_t CHUNK = 800;
        for (size_t begin = 0; begin < items.size(); begin += CHUNK)
        {
            size_t end = std::min(items.size(), begin + CHUNK);
            QueryResult result = WorldDatabase.Query(Acore::StringFormat(
                "SELECT Entry, Item, Chance FROM {} WHERE Reference = 0 AND Item IN ({})", table, JoinIds(items, begin, end)));
            if (!result)
                continue;
            do
            {
                Field* fields = result->Fetch();
                out[fields[0].Get<uint32>()].emplace_back(fields[1].Get<uint32>(), NormaliseChance(fields[2].Get<float>()));
            } while (result->NextRow());
        }

        if (itemsByReference.empty())
            return;

        std::vector<uint32> refs;
        refs.reserve(itemsByReference.size());
        for (auto const& [ref, list] : itemsByReference)
            refs.push_back(ref);

        for (size_t begin = 0; begin < refs.size(); begin += CHUNK)
        {
            size_t end = std::min(refs.size(), begin + CHUNK);
            QueryResult result = WorldDatabase.Query(Acore::StringFormat(
                "SELECT Entry, Reference, Chance FROM {} WHERE Reference IN ({})", table, JoinIds(refs, begin, end)));
            if (!result)
                continue;
            do
            {
                Field* fields = result->Fetch();
                uint32 lootId = fields[0].Get<uint32>();
                uint32 ref = uint32(std::abs(fields[1].Get<int32>()));
                float refChance = NormaliseChance(fields[2].Get<float>());
                auto itr = itemsByReference.find(ref);
                if (itr == itemsByReference.end())
                    continue;
                for (auto const& [item, chance] : itr->second)
                    out[lootId].emplace_back(item, refChance * chance);
            } while (result->NextRow());
        }
    }

    void IndexLoot(std::unordered_set<uint32> const& wanted)
    {
        std::vector<uint32> items(wanted.begin(), wanted.end());
        std::sort(items.begin(), items.end());
        if (items.empty())
            return;

        // Reference templates holding a wanted item, resolved to a fixed point: the game's own
        // loot roller (LootTemplate::Process) follows a Reference to another Reference just as
        // readily as it follows one straight to an Item, and reference_loot_template has the same
        // Entry/Reference/Chance shape as every other loot table, so a chain (A -> B -> wanted item)
        // is real, not hypothetical. Each round below finds the reference_loot_template rows that
        // point *at* a reference already known to lead to a wanted item, one level further out from
        // the wanted items than the previous round. A reference already present in itemsByReference
        // is skipped rather than re-expanded, which both dedups repeat discovery and makes a cycle
        // (A -> B -> A) terminate harmlessly instead of growing forever. MAX_REFERENCE_DEPTH is a
        // sanity cap, not a modeled limit -- real chains here are expected to be shallow.
        constexpr int MAX_REFERENCE_DEPTH = 8;
        std::unordered_map<uint32, std::vector<std::pair<uint32, float>>> itemsByReference;
        ReadLootTable("reference_loot_template", items, {}, itemsByReference);
        for (int depth = 0; depth < MAX_REFERENCE_DEPTH; ++depth)
        {
            std::unordered_map<uint32, std::vector<std::pair<uint32, float>>> nextLevel;
            ReadLootTable("reference_loot_template", {}, itemsByReference, nextLevel);

            bool grew = false;
            for (auto& [entry, list] : nextLevel)
            {
                if (itemsByReference.count(entry))
                    continue;
                itemsByReference.emplace(entry, std::move(list));
                grew = true;
            }
            if (!grew)
                break;
        }

        std::unordered_map<uint32, std::vector<std::pair<uint32, float>>> creatureLoot;
        std::unordered_map<uint32, std::vector<std::pair<uint32, float>>> objectLoot;
        ReadLootTable("creature_loot_template", items, itemsByReference, creatureLoot);
        ReadLootTable("gameobject_loot_template", items, itemsByReference, objectLoot);

        auto addSource = [](uint32 item, uint32 entry, bool gameObject, float chance)
        {
            auto& sources = _itemSources[item];
            for (LootSource& source : sources)
            {
                if (source.entry == entry && source.gameObject == gameObject)
                {
                    source.chance = std::max(source.chance, chance);
                    return;
                }
            }
            sources.push_back(LootSource{ entry, gameObject, chance });
        };

        for (auto const& [entry, tmpl] : *sObjectMgr->GetCreatureTemplates())
        {
            if (!tmpl.lootid)
                continue;
            auto itr = creatureLoot.find(tmpl.lootid);
            if (itr == creatureLoot.end())
                continue;
            for (auto const& [item, chance] : itr->second)
                addSource(item, entry, false, chance);
        }

        for (auto const& [entry, tmpl] : *sObjectMgr->GetGameObjectTemplates())
        {
            uint32 lootId = tmpl.GetLootId();
            if (!lootId)
                continue;
            auto itr = objectLoot.find(lootId);
            if (itr == objectLoot.end())
                continue;
            for (auto const& [item, chance] : itr->second)
                addSource(item, entry, true, chance);
        }

        // Objects whose use spell creates a wanted item (a pile you click to pick up a crate):
        // recorded as game-object sources with chance 1, the use handler deals with them.
        for (auto const& [entry, tmpl] : *sObjectMgr->GetGameObjectTemplates())
        {
            if (tmpl.type != GAMEOBJECT_TYPE_GOOBER || !tmpl.goober.spellId)
                continue;
            SpellInfo const* spell = sSpellMgr->GetSpellInfo(tmpl.goober.spellId);
            if (!spell)
                continue;
            for (uint8 eff = 0; eff < MAX_SPELL_EFFECTS; ++eff)
                if (spell->Effects[eff].Effect == SPELL_EFFECT_CREATE_ITEM && wanted.count(spell->Effects[eff].ItemType))
                    addSource(spell->Effects[eff].ItemType, entry, true, 1.0f);
        }

        _stats.lootItems = uint32(_itemSources.size());
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

        // Delivery quests: the quest hands the item out itself. Only the source item is handed out
        // on accept (Player::GiveQuestSourceItem); ItemDrop lists items that drop *during* the
        // quest, which still have to be collected like any other.
        if (def.itemId == quest->GetSrcItemId())
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
            // A gameobject only counts as a usable source if a handler actually exists for its
            // real interaction type: goober (click, credited via its use-spell) or chest (open via
            // the lock's Opening spell, see LootGameObjectObjectiveHandler). Anything else -- a
            // fishing hole, a door, a generic goober with no CREATE_ITEM effect that still carries a
            // loot table, etc. -- is indexed by IndexLoot() but no bot code can ever open it, so
            // advertising it here would accept a quest the bot can never actually complete.
            if (source.gameObject)
            {
                GameObjectTemplate const* go = sObjectMgr->GetGameObjectTemplate(source.entry);
                if (!go || (go->type != GAMEOBJECT_TYPE_CHEST && go->type != GAMEOBJECT_TYPE_GOOBER))
                    continue;
            }
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

    // Not only never taken: never finishable by a bot, whoever put it in the log.
    void MarkUncompletable(QuestKnowledge& info, char const* reason)
    {
        MarkQuest(info, reason);
        info.completable = false;
        info.completionBlocker = reason;
    }

    void ClassifyQuest(Quest const* quest, QuestKnowledge& info, std::vector<uint32> const* triggers)
    {
        info.supported = true;
        info.unsupportedReason = "";
        info.completable = true;
        info.completionBlocker = "";

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

        // Acceptance policy and completability are different things: a daily or an item-started
        // quest a bot already has is perfectly finishable, it is just not one a bot picks up on its
        // own. Player kills, reputation targets and a missing ender can never be finished here.
        if (quest->GetPlayersSlain())
            MarkUncompletable(info, "player kills");
        else if (quest->GetRepObjectiveFaction() || quest->GetRepObjectiveFaction2())
            MarkUncompletable(info, "reputation objective");
        else if (info.enderCreatures.empty() && info.enderObjects.empty())
            MarkUncompletable(info, "no quest ender");
        else if (quest->GetTimeAllowed())
            MarkQuest(info, "timed");
        else if (quest->IsDailyOrWeekly() || quest->IsMonthly() || quest->IsSeasonal())
            MarkQuest(info, "daily/weekly/seasonal");
        else if (info.giverCreatures.empty() && info.giverObjects.empty())
            MarkQuest(info, "no quest giver (item or script started)");
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
        IndexOpeningSpells();
        IndexLoot(CollectWantedItems());
        IndexQuests(ReadQuestAreaTriggers());
        IndexGivers();
        IndexHubs();

        _stats.buildMs = GetMSTimeDiffToNow(started);
        _ready = true;

        LOG_INFO("module.coa-playerbots.quest", ">> QuestKB: {} quests ({} supported), {} creature and {} object entries spawned, "
            "{} quest items with loot sources, {} giver spawns in {} hubs, {} quest area triggers, {} opening spells ({} ms).",
            _stats.quests, _stats.supported, _stats.creatureEntries, _stats.objectEntries, _stats.lootItems,
            _stats.giverSpots, _stats.hubs, _stats.areaTriggers, _openingSpellByLockType.size(), _stats.buildMs);
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

    uint32 OpeningSpellFor(GameObjectTemplate const* go)
    {
        if (!go)
            return 0;

        uint32 lockId = go->GetLockId();
        if (!lockId)
        {
            // An unlocked chest opens with any opening spell (Spell::CanOpenLock accepts lock 0).
            auto itr = _openingSpellByLockType.find(LOCKTYPE_OPEN);
            if (itr != _openingSpellByLockType.end())
                return itr->second;
            return _openingSpellByLockType.empty() ? 0 : _openingSpellByLockType.begin()->second;
        }

        LockEntry const* lock = sLockStore.LookupEntry(lockId);
        if (!lock)
            return 0;

        for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
        {
            if (lock->Type[i] != LOCK_KEY_SKILL)
                continue;
            auto itr = _openingSpellByLockType.find(lock->Index[i]);
            if (itr != _openingSpellByLockType.end())
                return itr->second;
        }
        return 0;
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

        std::string out = info->supported ? "supported" : Acore::StringFormat("not taken: {}", info->unsupportedReason);
        if (!info->completable)
            out += Acore::StringFormat(", can never be completed ({})", info->completionBlocker);
        if (info->elite)
            out += ", elite";
        for (ObjectiveDef const& def : info->objectives)
            out += Acore::StringFormat("; {} x{} ({}{})", ObjectiveTypeName(def.type), def.requiredCount,
                def.itemId ? def.itemId : def.targetEntry, def.supported ? "" : ", unsupported");
        return out;
    }
}
