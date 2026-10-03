#include "BotGear.h"
#include "BotGearPolicy.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "BotSpawnRandom.h"
#include "Chat.h"
#include "ClientDBC.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Mail.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldSession.h"
#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace BotGear
{
namespace
{
std::unordered_set<uint32> excluded;
std::unordered_set<uint32> missingEquipmentDisplays;
bool catalogueReady = false;
bool catalogueLoaded = false;
bool queueLoaded = false;
std::deque<uint32> pending;
std::unordered_set<uint64> archivedEntries;
uint32 throttle = 0;

bool LoadCatalogue()
{
    if (catalogueLoaded)
        return catalogueReady;
    catalogueLoaded = true;
    std::ifstream modelCatalogue(GetClientDBCPath("coa_missing_equipment_displays.txt"));
    if (modelCatalogue)
    {
        uint32 display = 0;
        while (modelCatalogue >> display)
            missingEquipmentDisplays.insert(display);
        LOG_INFO("module.coa-playerbots", "Bot gear: excluded {} missing equipment displays.",
            missingEquipmentDisplays.size());
    }
    else
        LOG_WARN("module.coa-playerbots", "Bot gear: client equipment model audit unavailable.");
    ClientDBC vanity;
    if (!vanity.Load(GetClientDBCPath("VanityCollection.dbc"), 77))
    {
        LOG_ERROR("module.coa-playerbots", "Bot gear: vanity catalogue unavailable; automatic equipment disabled.");
        return false;
    }
    for (uint32 row = 0; row < vanity.GetRecordCount(); ++row)
        excluded.insert(vanity.GetRecord(row).GetUInt32(1));
    auto add = [](QueryResult result)
    {
        if (result)
            do { excluded.insert(result->Fetch()[0].Get<uint32>()); } while (result->NextRow());
    };
    add(WorldDatabase.Query("SELECT item FROM game_event_npc_vendor"));
    add(WorldDatabase.Query("SELECT l.Item FROM creature_loot_template l JOIN creature_template t "
        "ON t.lootid=l.Entry JOIN creature c ON c.id=t.entry JOIN game_event_creature e ON e.guid=c.guid"));
    add(WorldDatabase.Query("SELECT l.Item FROM gameobject_loot_template l JOIN gameobject_template t "
        "ON t.Data1=l.Entry JOIN gameobject g ON g.id=t.entry JOIN game_event_gameobject e ON e.guid=g.guid "
        "WHERE t.type IN (3,25)"));
    std::deque<uint32> references;
    if (QueryResult result = WorldDatabase.Query(
        "SELECT l.Reference FROM creature_loot_template l JOIN creature_template t ON t.lootid=l.Entry "
        "JOIN creature c ON c.id=t.entry JOIN game_event_creature e ON e.guid=c.guid WHERE l.Reference>0 "
        "UNION SELECT l.Reference FROM gameobject_loot_template l JOIN gameobject_template t ON t.Data1=l.Entry "
        "JOIN gameobject g ON g.id=t.entry JOIN game_event_gameobject e ON e.guid=g.guid "
        "WHERE t.type IN (3,25) AND l.Reference>0"))
        do { references.push_back(result->Fetch()[0].Get<uint32>()); } while (result->NextRow());
    std::unordered_map<uint32, std::vector<std::pair<uint32, uint32>>> loot;
    if (QueryResult result = WorldDatabase.Query("SELECT Entry,Item,Reference FROM reference_loot_template"))
        do
        {
            Field* f = result->Fetch();
            loot[f[0].Get<uint32>()].emplace_back(f[1].Get<uint32>(), f[2].Get<uint32>());
        } while (result->NextRow());
    std::unordered_set<uint32> visited;
    while (!references.empty())
    {
        uint32 reference = references.front();
        references.pop_front();
        if (!visited.insert(reference).second)
            continue;
        for (auto const& [item, next] : loot[reference])
        {
            if (item)
                excluded.insert(item);
            if (next)
                references.push_back(next);
        }
    }
    if (QueryResult result = WorldDatabase.Query(
        "SELECT q.RewardItem1,q.RewardItem2,q.RewardItem3,q.RewardItem4,"
        "q.RewardChoiceItemID1,q.RewardChoiceItemID2,q.RewardChoiceItemID3,"
        "q.RewardChoiceItemID4,q.RewardChoiceItemID5,q.RewardChoiceItemID6 FROM quest_template q "
        "JOIN (SELECT questId AS quest FROM game_event_seasonal_questrelation UNION "
        "SELECT quest FROM game_event_creature_quest UNION SELECT quest FROM game_event_gameobject_quest) e ON e.quest=q.ID"))
        do
        {
            for (unsigned column = 0; column < 10; ++column)
                excluded.insert(result->Fetch()[column].Get<uint32>());
        } while (result->NextRow());
    catalogueReady = true;
    return true;
}

void LoadQueue()
{
    if (queueLoaded)
        return;
    queueLoaded = true;
    CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS coa_bot_gear_queue "
        "(guid INT UNSIGNED NOT NULL PRIMARY KEY)");
    CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS coa_bot_gear_history "
        "(guid INT UNSIGNED NOT NULL, slot TINYINT UNSIGNED NOT NULL, item_guid INT UNSIGNED NOT NULL, "
        "entry INT UNSIGNED NOT NULL, replaced_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP, "
        "PRIMARY KEY (guid,slot,item_guid))");
    if (QueryResult result = CharacterDatabase.Query("SELECT guid,entry FROM coa_bot_gear_history"))
        do
        {
            Field* f = result->Fetch();
            archivedEntries.insert((uint64(f[0].Get<uint32>()) << 32) | f[1].Get<uint32>());
        } while (result->NextRow());
    if (QueryResult result = CharacterDatabase.Query("SELECT guid FROM coa_bot_gear_queue ORDER BY guid"))
        do { pending.push_back(result->Fetch()[0].Get<uint32>()); } while (result->NextRow());
}

unsigned ItemPower(ItemTemplate const* item, bool pvp)
{
    unsigned power = 0;
    for (auto const& spell : item->Spells)
        if (spell.SpellTrigger == ITEM_SPELLTRIGGER_ON_EQUIP)
            power += pvp ? BotGearPolicy::PvpPower(spell.SpellId) : BotGearPolicy::PvePower(spell.SpellId);
    return power;
}
}

bool Ready()
{
    return LoadCatalogue();
}

bool AllowedAtLevel(uint8 level, ItemTemplate const* item)
{
    if (!item || !BotGearPolicy::FitsLevel(level, item->RequiredLevel, item->ItemLevel, item->Quality) ||
        item->HolidayId || item->Map || item->Area || BotAI::IsProfessionTool(item) ||
        !LoadCatalogue() || excluded.count(item->ItemId))
        return false;
    if ((item->InventoryType == INVTYPE_HEAD || item->InventoryType == INVTYPE_SHOULDERS) &&
        (!item->DisplayInfoID || missingEquipmentDisplays.count(item->DisplayInfoID)))
        return false;
    unsigned powerCap = level < 60 ? 0 : level < 80 ? 15 : 25;
    if (ItemPower(item, true) > powerCap || ItemPower(item, false) > powerCap)
        return false;
    std::string name = item->Name1;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
    for (char const* marker : {"rpgitem", "[ph]", " ph ", "test", "monster - ", "deprecated", "[dnd]", "npc "})
        if (name.find(marker) != std::string::npos)
            return false;
    return true;
}

bool Allowed(Player* bot, ItemTemplate const* item)
{
    if (!bot || !AllowedAtLevel(bot->GetLevel(), item) || bot->CanUseItem(item) != EQUIP_ERR_OK)
        return false;
    bool pvp = bot->InBattleground() || bot->InArena() || bot->GetPlayerSetting("coa.bot.gear", 0).value == 1;
    return !ItemPower(item, !pvp) || ItemPower(item, pvp);
}

float PowerScore(Player* bot, ItemTemplate const* item)
{
    bool pvp = bot->InBattleground() || bot->InArena() || bot->GetPlayerSetting("coa.bot.gear", 0).value == 1;
    return (float(ItemPower(item, pvp)) - float(ItemPower(item, !pvp))) * 10.0f;
}

bool PreserveSlot(Player* bot, uint8 slot)
{
    Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!item)
        return true;
    ItemPosCountVec dest;
    bool const storeInBags = bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) == EQUIP_ERR_OK;
    LoadQueue();
    archivedEntries.insert((uint64(bot->GetGUID().GetCounter()) << 32) | item->GetEntry());
    CharacterDatabase.DirectExecute("INSERT IGNORE INTO coa_bot_gear_history (guid,slot,item_guid,entry) VALUES ({},{},{},{})",
        bot->GetGUID().GetCounter(), uint32(slot), item->GetGUID().GetCounter(), item->GetEntry());
    LOG_INFO("module.coa-playerbots", "Bot gear: {} slot {} preserving item {} (guid {}, required level {}, ilvl {}).",
        bot->GetName(), uint32(slot), item->GetEntry(), item->GetGUID().GetCounter(),
        item->GetTemplate()->RequiredLevel, item->GetTemplate()->ItemLevel);
    if (storeInBags)
    {
        bot->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
        bot->StoreItem(dest, item, true);
    }
    else
    {
        CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();
        bot->MoveItemFromInventory(INVENTORY_SLOT_BAG_0, slot, true);
        item->DeleteFromInventoryDB(transaction);
        if (item->GetState() == ITEM_UNCHANGED)
            item->FSetState(ITEM_CHANGED);
        item->SaveToDB(transaction);
        MailDraft("Previous companion equipment", "Saved during automatic equipment repair.")
            .AddItem(item).SendMailTo(transaction, MailReceiver(bot), MailSender(bot));
        bot->SaveToDB(transaction, false, false);
        CharacterDatabase.CommitTransaction(transaction);
    }
    return true;
}

bool Archived(Item const* item)
{
    if (!item)
        return false;
    LoadQueue();
    return archivedEntries.count((uint64(item->GetOwnerGUID().GetCounter()) << 32) | item->GetEntry()) != 0;
}

void Command(ChatHandler* handler, std::string const& args)
{
    LoadQueue();
    if (args == "status")
    {
        handler->PSendSysMessage("Bot gear: {} bots pending; one idle online bot per 2 seconds.", pending.size());
        return;
    }
    if (args == "cancel")
    {
        CharacterDatabase.DirectExecute("DELETE FROM coa_bot_gear_queue");
        pending.clear();
        handler->SendSysMessage("Bot gear: pending repairs cancelled; completed repairs are kept.");
        return;
    }
    if (!args.empty())
    {
        handler->SendSysMessage("Usage: .fixbotgear [status|cancel]");
        return;
    }
    if (!LoadCatalogue())
    {
        handler->SendSysMessage("Bot gear: cannot start without VanityCollection.dbc.");
        return;
    }
    std::unordered_set<uint32> queued(pending.begin(), pending.end());
    std::unordered_map<uint32, bool> botAccounts;
    std::string values;
    if (QueryResult result = CharacterDatabase.Query("SELECT guid,account FROM characters ORDER BY guid"))
        do
        {
            Field* fields = result->Fetch();
            uint32 guid = fields[0].Get<uint32>();
            uint32 account = fields[1].Get<uint32>();
            if (!botAccounts.count(account))
                botAccounts[account] = BotMgr::IsBotAccountId(account);
            if (!botAccounts[account] || !queued.insert(guid).second)
                continue;
            if (!values.empty())
                values += ',';
            values += '(' + std::to_string(guid) + ')';
            pending.push_back(guid);
        } while (result->NextRow());
    if (!values.empty())
        CharacterDatabase.DirectExecute("INSERT IGNORE INTO coa_bot_gear_queue (guid) VALUES " + values);
    handler->PSendSysMessage("Bot gear: {} bots queued. Busy bots wait; queue survives restarts. Use status or cancel.", pending.size());
}

void Queue(Player* bot)
{
    if (!bot || !BotMgr::IsBotAccountId(bot->GetSession()->GetAccountId()))
        return;
    LoadQueue();
    uint32 guid = bot->GetGUID().GetCounter();
    if (std::find(pending.begin(), pending.end(), guid) != pending.end())
        return;
    CharacterDatabase.DirectExecute("INSERT IGNORE INTO coa_bot_gear_queue (guid) VALUES ({})", guid);
    pending.push_back(guid);
}

void Update(uint32 diff)
{
    LoadQueue();
    if (pending.empty())
        return;
    if (throttle > diff)
    {
        throttle -= diff;
        return;
    }
    throttle = 2000;
    Player* bot = nullptr;
    uint32 guid = 0;
    size_t attempts = pending.size();
    while (attempts--)
    {
        guid = pending.front();
        pending.pop_front();
        bot = sBotMgr->FindBotPlayer(guid);
        if (bot && bot->IsAlive() && !bot->IsInCombat() && !bot->IsBeingTeleported() && !bot->GetTradeData() &&
            !bot->InBattleground() && !bot->InArena())
            break;
        pending.push_back(guid);
        bot = nullptr;
    }
    if (!bot)
        return;
    if (!BotMgr::IsBotAccountId(bot->GetSession()->GetAccountId()))
    {
        CharacterDatabase.DirectExecute("DELETE FROM coa_bot_gear_queue WHERE guid={}", guid);
        return;
    }
    if (!Ready())
    {
        pending.push_back(guid);
        return;
    }
    BotSpawn::RepairGear(bot);
    bool remaining = false;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (!BotAI::IsProfessionTool(item->GetTemplate()) && !Allowed(bot, item->GetTemplate()))
                remaining = true;
    CharacterDatabaseTransaction transaction = CharacterDatabase.BeginTransaction();
    bot->SaveToDB(transaction, false, false);
    if (remaining)
        pending.push_back(guid);
    else
        transaction->Append("DELETE FROM coa_bot_gear_queue WHERE guid={}", guid);
    CharacterDatabase.CommitTransaction(transaction);
}
}
