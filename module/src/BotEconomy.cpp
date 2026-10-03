#include "BotEconomy.h"

#include "AuctionHouseMgr.h"
#include "Bag.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "BotMovement.h"
#include "BotProgression.h"
#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "StringFormat.h"
#include "World.h"
#include "WorldConfig.h"
#include "WorldSession.h"
#include "world/BotWorldBehavior.h"
#include "world/BotWorldPoi.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{
    constexpr uint32 HEARTHSTONE_ENTRY = 6948;

    // Settings, re-read every few seconds: the config manager warns on every lookup of a key the active
    // file does not have, and servers updated from older packages do not have these.
    struct Settings
    {
        bool enable = false;
        uint32 minTripCopper = 50000;      // 5 gold: below this a bot is not worth a trip to town
        uint32 keepConsumables = 10;       // potions / food of its own level range kept per item
        uint32 outdatedBelowLevels = 15;   // a consumable this many levels under the bot is outdated
        uint32 minAuctionQuality = 2;      // uncommon and better go to the auction, the rest to a vendor
        uint32 undercutPercent = 5;        // what the plan assumes below the lowest current buyout
        uint32 scanIntervalSec = 600;      // how often one bot is looked at
        uint32 botsPerTick = 2;            // bots looked at per world tick
        uint32 journalDays = 14;           // journal rows older than this are removed
        bool dryRun = true;                // plan only: nobody travels or sells
        uint32 maxTrips = 5;               // bots on their way to town at the same time
        uint32 tripCooldownMin = 30;       // minutes before the same bot is sent again
        uint32 guildSharePercent = 10;     // of what a trip earns, paid into the guild bank
        uint32 moneyReserveCopper = 50000; // a bot keeps at least this much for its own needs
        uint32 maxListings = 20;           // auctions one bot may have running
        uint32 spreadMin = 5;              // listing prices vary by this many percent ...
        uint32 spreadMax = 10;             // ... up to this many, up or down
        uint32 minListingCopper = 5000;    // a listing worth less than this is not worth the deposit: a vendor takes it
        uint32 listingMinutes = 1440;      // how long a listing runs
        bool consumeSupplies = false;      // eating and drinking use up food and drink
        uint32 supplyTarget = 20;          // how many of each a bot likes to carry
        uint32 dailyBudgetCopper = 20000;  // what one bot spends on supplies in a day
    } g_settings;

    uint32 g_settingsAgeMs = 1000000;
    bool g_tablesReady = false;
    std::unordered_set<uint32> g_protected;

    struct GoldOrder
    {
        uint32 guildId = 0;
        uint64 targetCopper = 0;
        uint64 depositedCopper = 0;
    };
    std::unordered_map<ObjectGuid, GoldOrder> g_goldOrders;

    // Stock limits per guild, the bots carrying a stock surplus to town, and one run at a time per guild.
    std::unordered_map<uint32, std::unordered_map<uint32, uint32>> g_stockLimits; // guild -> item -> keep
    std::unordered_map<ObjectGuid, std::unordered_set<uint32>> g_stockCarry;       // bot -> items it carries for sale
    std::unordered_map<uint32, ObjectGuid> g_stockRuns;                            // guild -> the bot on a run
    std::unordered_map<uint64, uint32> g_stockBlockedUntil;                        // (guild << 32 | item) -> game ms
    uint32 g_stockCheckAgeMs = 0;
    constexpr uint32 STOCK_CHECK_MS = 60 * 1000;
    uint32 g_goldCheckAgeMs = 0;
    constexpr uint32 GOLD_CHECK_MS = 60 * 1000;
    constexpr uint32 GOLD_MIN_DEPOSIT_COPPER = 10000; // 1 gold

    // Supplies: bots that found nothing to eat or drink, and what each spent today.
    std::unordered_set<ObjectGuid> g_needsSupplies;
    std::unordered_map<uint64, uint32> g_noSupplyUntil; // (bot guid * 2 + food) -> a bot with nothing to eat is not searched again for a few seconds
    struct Spending
    {
        uint32 dayStartedAt = 0;
        uint64 copper = 0;
    };
    std::unordered_map<ObjectGuid, Spending> g_spent;
    uint64 g_boughtCopper = 0;

    // Lowest buyout per single item over every auction house, refreshed every few minutes.
    std::unordered_map<uint32, uint32> g_market;
    std::unordered_map<uint32, uint32> g_listingCount; // auctions per owner (guid low), from the same scan
    uint32 g_marketAgeMs = 1000000;
    constexpr uint32 MARKET_REFRESH_MS = 5 * 60 * 1000;

    // Per bot: a fingerprint of the plan last written to the journal, so an unchanged plan is not written again.
    std::unordered_map<ObjectGuid, uint64> g_lastPlan;

    // Per bot: the game time (ms) it is next looked at.
    std::unordered_map<ObjectGuid, uint32> g_nextScanAt;
    size_t g_rotation = 0;
    uint32 g_pruneAgeMs = 0;

    // A few bots' plans are kept for `.botcmd economy status`.
    struct Totals
    {
        uint32 botsScanned = 0;
        uint32 botsWithSales = 0;
        uint64 vendorCopper = 0;
        uint64 auctionCopper = 0;
        uint32 journalRows = 0;
    } g_totals;

    void RefreshSettings(uint32 diff)
    {
        g_settingsAgeMs += diff;
        if (g_settingsAgeMs < 10000)
            return;
        g_settingsAgeMs = 0;
        g_settings.enable = sConfigMgr->GetOption<bool>("CoaBots.Economy.Enable", false);
        g_settings.minTripCopper = sConfigMgr->GetOption<uint32>("CoaBots.Economy.MinTripCopper", 50000);
        g_settings.keepConsumables = sConfigMgr->GetOption<uint32>("CoaBots.Economy.KeepConsumables", 10);
        g_settings.outdatedBelowLevels = sConfigMgr->GetOption<uint32>("CoaBots.Economy.OutdatedBelowLevels", 15);
        g_settings.minAuctionQuality = sConfigMgr->GetOption<uint32>("CoaBots.Economy.MinAuctionQuality", 2);
        g_settings.undercutPercent = std::min<uint32>(50, sConfigMgr->GetOption<uint32>("CoaBots.Economy.UndercutPercent", 5));
        g_settings.scanIntervalSec = std::max<uint32>(30, sConfigMgr->GetOption<uint32>("CoaBots.Economy.ScanIntervalSec", 600));
        g_settings.botsPerTick = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("CoaBots.Economy.BotsPerTick", 2));
        g_settings.journalDays = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("CoaBots.Economy.JournalDays", 14));
        g_settings.dryRun = sConfigMgr->GetOption<bool>("CoaBots.Economy.DryRun", true);
        g_settings.maxTrips = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("CoaBots.Economy.MaxTrips", 5));
        g_settings.tripCooldownMin = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("CoaBots.Economy.TripCooldownMin", 30));
        g_settings.guildSharePercent = std::min<uint32>(100, sConfigMgr->GetOption<uint32>("CoaBots.Economy.GuildSharePercent", 10));
        g_settings.moneyReserveCopper = sConfigMgr->GetOption<uint32>("CoaBots.Economy.MoneyReserveCopper", 50000);
        g_settings.maxListings = sConfigMgr->GetOption<uint32>("CoaBots.Economy.MaxListings", 20);
        g_settings.spreadMin = std::min<uint32>(50, sConfigMgr->GetOption<uint32>("CoaBots.Economy.PriceSpreadMinPercent", 5));
        g_settings.consumeSupplies = sConfigMgr->GetOption<bool>("CoaBots.Economy.ConsumeSupplies", false);
        g_settings.supplyTarget = std::max<uint32>(1, std::min<uint32>(200, sConfigMgr->GetOption<uint32>("CoaBots.Economy.SupplyTarget", 20)));
        g_settings.dailyBudgetCopper = sConfigMgr->GetOption<uint32>("CoaBots.Economy.DailyBudgetCopper", 20000);
        g_settings.minListingCopper = sConfigMgr->GetOption<uint32>("CoaBots.Economy.MinListingCopper", 5000);
        g_settings.listingMinutes = std::max<uint32>(5, std::min<uint32>(2880, sConfigMgr->GetOption<uint32>("CoaBots.Economy.ListingMinutes", 1440)));
        g_settings.spreadMax = std::max<uint32>(g_settings.spreadMin, std::min<uint32>(50, sConfigMgr->GetOption<uint32>("CoaBots.Economy.PriceSpreadMaxPercent", 10)));
    }

    void LoadProtected()
    {
        g_protected.clear();
        if (QueryResult result = CharacterDatabase.Query("SELECT item_entry FROM mod_coa_bot_protected_items"))
            do
                g_protected.insert((*result)[0].Get<uint32>());
            while (result->NextRow());
    }

    void EnsureTables()
    {
        if (g_tablesReady)
            return;
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS mod_coa_bot_trade_journal ("
            "  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
            "  at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "  bot_guid INT UNSIGNED NOT NULL,"
            "  bot_name VARCHAR(24) NOT NULL,"
            "  item_entry INT UNSIGNED NOT NULL,"
            "  item_count INT UNSIGNED NOT NULL,"
            "  verdict VARCHAR(8) NOT NULL,"
            "  reason VARCHAR(32) NOT NULL,"
            "  unit_copper INT UNSIGNED NOT NULL,"
            "  price_source VARCHAR(12) NOT NULL,"
            "  executed TINYINT UNSIGNED NOT NULL DEFAULT 0,"
            "  KEY idx_at (at),"
            "  KEY idx_bot (bot_guid)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS mod_coa_bot_protected_items ("
            "  item_entry INT UNSIGNED NOT NULL PRIMARY KEY"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS mod_coa_bot_gold_orders ("
            "  bot_guid INT UNSIGNED NOT NULL PRIMARY KEY,"
            "  guild_id INT UNSIGNED NOT NULL,"
            "  target_copper BIGINT UNSIGNED NOT NULL,"
            "  deposited_copper BIGINT UNSIGNED NOT NULL DEFAULT 0"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
        CharacterDatabase.DirectExecute(
            "CREATE TABLE IF NOT EXISTS mod_coa_bot_stock_limits ("
            "  guild_id INT UNSIGNED NOT NULL,"
            "  item_entry INT UNSIGNED NOT NULL,"
            "  keep_count INT UNSIGNED NOT NULL,"
            "  PRIMARY KEY (guild_id, item_entry)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
        if (QueryResult limits = CharacterDatabase.Query("SELECT guild_id, item_entry, keep_count FROM mod_coa_bot_stock_limits"))
            do
                g_stockLimits[(*limits)[0].Get<uint32>()][(*limits)[1].Get<uint32>()] = (*limits)[2].Get<uint32>();
            while (limits->NextRow());
        LoadProtected();
        if (QueryResult result = CharacterDatabase.Query("SELECT bot_guid, guild_id, target_copper, deposited_copper FROM mod_coa_bot_gold_orders"))
            do
            {
                Field* f = (*result).Fetch();
                GoldOrder o;
                o.guildId = f[1].Get<uint32>();
                o.targetCopper = f[2].Get<uint64>();
                o.depositedCopper = f[3].Get<uint64>();
                g_goldOrders[ObjectGuid::Create<HighGuid::Player>(f[0].Get<uint32>())] = o;
            } while (result->NextRow());
        g_tablesReady = true;
    }

    void RefreshMarket(uint32 diff)
    {
        g_marketAgeMs += diff;
        if (g_marketAgeMs < MARKET_REFRESH_MS)
            return;
        g_marketAgeMs = 0;
        g_market.clear();
        g_listingCount.clear();
        for (AuctionHouseId house : { AuctionHouseId::Alliance, AuctionHouseId::Horde, AuctionHouseId::Neutral })
        {
            AuctionHouseObject* auctions = sAuctionMgr->GetAuctionsMapByHouseId(house);
            if (!auctions)
                continue;
            for (auto const& pair : auctions->GetAuctions())
            {
                AuctionEntry const* a = pair.second;
                if (a)
                    ++g_listingCount[a->owner.GetCounter()];
                if (!a || !a->itemCount || !a->buyout)
                    continue;
                uint32 const unit = std::max<uint32>(1, a->buyout / a->itemCount);
                auto [it, inserted] = g_market.try_emplace(a->item_template, unit);
                if (!inserted)
                    it->second = std::min(it->second, unit);
            }
        }
    }

    // What one item would fetch at the auction: just under the lowest buyout on offer, or - when nobody
    // sells it - a markup on the vendor price.
    uint32 AuctionUnitPrice(uint32 entry, ItemTemplate const* proto, char const*& source)
    {
        auto it = g_market.find(entry);
        if (it != g_market.end())
        {
            source = "market";
            return std::max<uint32>(1, uint32(uint64(it->second) * (100 - g_settings.undercutPercent) / 100));
        }
        source = "estimate";
        return std::max<uint32>(1, proto->SellPrice * 2);
    }

    template <typename F>
    void ForEachBagItem(Player* bot, F&& fn)
    {
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                fn(item);
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        {
            Bag* pBag = bot->GetBagByPos(bag);
            if (!pBag)
                continue;
            for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                if (Item* item = pBag->GetItemByPos(j))
                    fn(item);
        }
    }

    bool IsConsumableOfInterest(ItemTemplate const* proto)
    {
        if (proto->Class != ITEM_CLASS_CONSUMABLE)
            return false;
        return proto->SubClass == ITEM_SUBCLASS_POTION || proto->SubClass == ITEM_SUBCLASS_ELIXIR || proto->SubClass == ITEM_SUBCLASS_FLASK ||
            proto->SubClass == ITEM_SUBCLASS_FOOD || proto->SubClass == ITEM_SUBCLASS_BANDAGE;
    }

    // Gear the bot cannot wear, or that does not beat what it already wears in the slot (the same 5% margin the
    // upgrade pass uses). Gear that would fill an empty slot is not dead weight: the bot will put it on.
    bool IsDeadWeightGear(Player* bot, ItemTemplate const* proto, char const*& reason)
    {
        if (BotAI::IsProfessionTool(proto))
            return false;
        uint8 const slot = bot->FindEquipSlot(proto, NULL_SLOT, true);
        if (slot == NULL_SLOT)
        {
            reason = "unusable-gear";
            return true;
        }
        Item* current = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!current)
            return false;
        BotRole const role = BotAI::GetRole(bot->GetGUID());
        reason = "outdated-gear";
        return BotAI::ScoreItemForBot(bot, proto, role) <= BotAI::ScoreItemForBot(bot, current->GetTemplate(), role) * 1.05f;
    }

    // Auction when it can be listed and is worth it, a vendor otherwise.
    void Decide(Item const* item, ItemTemplate const* proto, bool qualityGate, BotEconomy::Line& line)
    {
        bool const canList = !item->IsSoulBound() && proto->Bonding != BIND_WHEN_PICKED_UP && item->CanBeTraded() && !item->IsConjuredConsumable();
        if (canList && (!qualityGate || proto->Quality >= g_settings.minAuctionQuality))
        {
            line.verdict = BotEconomy::Verdict::Auction;
            line.unitCopper = AuctionUnitPrice(line.entry, proto, line.priceSource);
            return;
        }
        line.verdict = BotEconomy::Verdict::Vendor;
        line.unitCopper = proto->SellPrice;
        line.priceSource = "vendor";
    }

    std::string QuoteSql(std::string const& s)
    {
        std::string out;
        for (char c : s)
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
                out.push_back(c);
        return out;
    }

    std::string Money(uint64 copper)
    {
        return Acore::StringFormat("{}g {}s {}c", copper / 10000, (copper / 100) % 100, copper % 100);
    }

    void Journal(Player* bot, std::vector<BotEconomy::Line> const& lines)
    {
        for (BotEconomy::Line const& l : lines)
        {
            CharacterDatabase.Execute(Acore::StringFormat(
                "INSERT INTO mod_coa_bot_trade_journal (bot_guid, bot_name, item_entry, item_count, verdict, reason, unit_copper, price_source, executed) "
                "VALUES ({}, '{}', {}, {}, '{}', '{}', {}, '{}', 0)",
                bot->GetGUID().GetCounter(), QuoteSql(bot->GetName()), l.entry, l.count,
                l.verdict == BotEconomy::Verdict::Auction ? "auction" : "vendor", QuoteSql(l.reason), l.unitCopper, QuoteSql(l.priceSource)));
            ++g_totals.journalRows;
        }
    }

    // ---- Gold orders --------------------------------------------------------------------------------------------

    void SaveGoldOrder(ObjectGuid guid, GoldOrder const& o)
    {
        CharacterDatabase.Execute(Acore::StringFormat(
            "REPLACE INTO mod_coa_bot_gold_orders (bot_guid, guild_id, target_copper, deposited_copper) VALUES ({}, {}, {}, {})",
            guid.GetCounter(), o.guildId, o.targetCopper, o.depositedCopper));
    }

    void DeleteGoldOrder(ObjectGuid guid)
    {
        CharacterDatabase.Execute(Acore::StringFormat("DELETE FROM mod_coa_bot_gold_orders WHERE bot_guid = {}", guid.GetCounter()));
        g_goldOrders.erase(guid);
    }

    // ---- Trips to town -------------------------------------------------------------------------------------------

    struct Trip
    {
        uint32 mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint32 faction = 0; // the auctioneer's faction: decides which auction house is used
        uint32 startedAt = 0;
        uint64 goalId = 0;
        bool flightTried = false;
        bool flightRequested = false; // a flight was asked for and has not finished yet
    };

    std::unordered_map<ObjectGuid, Trip> g_trips;
    std::unordered_set<ObjectGuid> g_wantsTrip;
    std::unordered_map<ObjectGuid, uint32> g_nextTripAt;
    uint64 g_goalSeed = 0xEC000000000ull;
    constexpr float ARRIVE_RADIUS = 30.0f;
    constexpr float TAXI_MIN_DISTANCE = 900.0f;
    constexpr uint32 TRIP_TIMEOUT_MS = 25 * 60 * 1000;

    struct TripTotals
    {
        uint32 trips = 0;
        uint32 listed = 0;
        uint32 soldToVendor = 0;
        uint64 earnedCopper = 0;
        uint64 guildCopper = 0;
        uint32 noDestination = 0;
        uint32 timedOut = 0;
    } g_tripTotals;

    // The trip outranks the walks of ordinary activities (a quest walk, grinding, gathering, fishing, strolling); a
    // claim left over from one of them would keep the bot from ever starting out.
    void ReleaseOrdinaryWalks(Player* bot)
    {
        for (MoveOwner owner : { MoveOwner::Ambient, MoveOwner::Grind, MoveOwner::Gather, MoveOwner::Fish, MoveOwner::Quest })
            BotMovement::ForceReleaseOwner(bot, owner);
    }

    bool IsFriendly(Player const* bot, Poi const& poi)
    {
        FactionTemplateEntry const* mine = bot->GetFactionTemplateEntry();
        FactionTemplateEntry const* theirs = poi.faction ? sFactionTemplateStore.LookupEntry(poi.faction) : nullptr;
        return !theirs || (mine && !mine->IsHostileTo(*theirs) && !theirs->IsHostileTo(*mine));
    }

    // The nearest friendly auctioneer on the bot's map.
    Poi const* NearestAuctioneer(Player* bot)
    {
        std::vector<Poi const*> found;
        BotWorldPoi::Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), 30000.0f, PoiKind::Auctioneer, found);
        Poi const* best = nullptr;
        float bestDist = 0.0f;
        for (Poi const* poi : found)
        {
            if (!poi->faction || !IsFriendly(bot, *poi) || !AuctionHouseMgr::GetAuctionHouseEntryFromFactionTemplate(poi->faction))
                continue;
            float const d = std::hypot(poi->x - bot->GetPositionX(), poi->y - bot->GetPositionY());
            if (!best || d < bestDist)
            {
                best = poi;
                bestDist = d;
            }
        }
        return best;
    }

    // Listing price for one item: the plan's price with a random spread of a few percent either way so the market
    // looks alive, never below what a vendor would pay plus a little.
    uint32 ListingUnitPrice(BotEconomy::Line const& line, ItemTemplate const* proto)
    {
        uint32 const spread = urand(g_settings.spreadMin, g_settings.spreadMax);
        uint64 unit = line.unitCopper;
        unit = urand(0, 1) ? unit * (100 + spread) / 100 : unit * (100 - std::min<uint32>(spread, 90)) / 100;
        unit = std::max<uint64>(unit, uint64(proto->SellPrice) * 105 / 100);
        return uint32(std::max<uint64>(1, std::min<uint64>(unit, 2000000)));
    }

    bool ListOnAuction(Player* bot, Item* item, uint32 count, uint32 buyout, uint32 auctioneerFaction)
    {
        AuctionHouseEntry const* houseEntry = AuctionHouseMgr::GetAuctionHouseEntryFromFactionTemplate(auctioneerFaction);
        AuctionHouseObject* house = sAuctionMgr->GetAuctionsMap(auctioneerFaction);
        if (!houseEntry || !house || !count || count > item->GetCount() || !buyout)
            return false;
        if (sAuctionMgr->GetAItem(item->GetGUID()) || !item->CanBeTraded() || item->IsNotEmptyBag() || item->GetTemplate()->HasFlag(ITEM_FLAG_CONJURED) ||
            item->GetUInt32Value(ITEM_FIELD_DURATION))
            return false;

        uint32 const etime = g_settings.listingMinutes * MINUTE;
        uint32 const auctionTime = uint32(etime * sWorld->getRate(RATE_AUCTION_TIME));
        uint32 const deposit = sAuctionMgr->GetAuctionDeposit(houseEntry, etime, item, count);
        if (!bot->HasEnoughMoney(deposit))
            return false;

        AuctionHouseId houseId = AuctionHouseId::Neutral;
        if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
            houseId = AuctionHouseId(houseEntry->houseId);

        Item* listed = item;
        if (item->GetCount() != count)
        {
            listed = item->CloneItem(count, bot);
            if (!listed)
                return false;
        }

        bot->ModifyMoney(-int32(deposit));
        AuctionEntry* ah = new AuctionEntry;
        ah->Id = sObjectMgr->GenerateAuctionID();
        ah->houseId = houseId;
        ah->owner = bot->GetGUID();
        // The auction house suggests the first bid; the seller only chooses the buyout, a little above it.
        ah->startbid = std::max<uint32>(1, uint32(uint64(buyout) * 85 / 100));
        ah->bidder = ObjectGuid::Empty;
        ah->bid = 0;
        ah->buyout = buyout;
        ah->expire_time = GameTime::GetGameTime().count() + auctionTime;
        ah->deposit = deposit;
        ah->auctionHouseEntry = houseEntry;
        ah->item_guid = listed->GetGUID();
        ah->item_template = listed->GetEntry();
        ah->itemCount = listed->GetCount();

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        sAuctionMgr->AddAItem(listed);
        house->AddAuction(ah);
        if (listed == item)
        {
            bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
            item->DeleteFromInventoryDB(trans);
            item->SaveToDB(trans);
        }
        else
        {
            item->SetCount(item->GetCount() - count);
            item->SetState(ITEM_CHANGED, bot);
            bot->ItemRemovedQuestCheck(item->GetEntry(), count);
            listed->SaveToDB(trans);
        }
        ah->SaveToDB(trans);
        bot->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);
        ++g_listingCount[bot->GetGUID().GetCounter()];
        return true;
    }

    // Money and returned items from earlier auctions sit in the bot's mailbox; a player collects them at a mailbox,
    // a bot in town just takes them. Returns the money collected.
    uint64 CollectAuctionMail(Player* bot)
    {
        uint64 money = 0;
        time_t const now = GameTime::GetGameTime().count();
        std::vector<Mail*> mails;
        for (Mail* m : bot->GetMails())
            if (m && m->messageType == MAIL_AUCTION && m->state != MAIL_STATE_DELETED && m->deliver_time <= now && !m->COD)
                mails.push_back(m);
        if (mails.empty())
            return 0;

        bool changed = false;
        for (Mail* m : mails)
        {
            if (m->money && bot->ModifyMoney(m->money, false))
            {
                money += m->money;
                m->money = 0;
                m->state = MAIL_STATE_CHANGED;
                changed = true;
            }
            for (MailItemInfo const& info : std::vector<MailItemInfo>(m->items))
            {
                Item* it = bot->GetMItem(info.item_guid);
                if (!it)
                    continue;
                ItemPosCountVec dest;
                if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, it, false) != EQUIP_ERR_OK)
                    continue;
                m->RemoveItem(info.item_guid);
                m->removedItems.push_back(info.item_guid);
                m->state = MAIL_STATE_CHANGED;
                bot->RemoveMItem(it->GetGUID().GetCounter());
                it->SetState(ITEM_UNCHANGED);
                bot->MoveItemToInventory(dest, it, true);
                changed = true;
            }
            if (!m->money && m->items.empty())
            {
                m->state = MAIL_STATE_DELETED;
                changed = true;
            }
        }
        if (changed)
            bot->m_mailsUpdated = true; // the save at the end of the trip writes the mail, the items and the gold
        return money;
    }

    void JournalDone(Player* bot, BotEconomy::Line const& l, uint32 count, uint32 unitCopper, char const* verdict, char const* source)
    {
        CharacterDatabase.Execute(Acore::StringFormat(
            "INSERT INTO mod_coa_bot_trade_journal (bot_guid, bot_name, item_entry, item_count, verdict, reason, unit_copper, price_source, executed) "
            "VALUES ({}, '{}', {}, {}, '{}', '{}', {}, '{}', 1)",
            bot->GetGUID().GetCounter(), QuoteSql(bot->GetName()), l.entry, count, verdict, QuoteSql(l.reason), unitCopper, QuoteSql(source)));
    }

    // ---- Supplies ------------------------------------------------------------------------------------------------

    // The consumable (food: spell category 11, drink: 59) in the bot's bags that fits its level best, or null.
    Item* BestSupplyInBags(Player* bot, bool food)
    {
        Item* best = nullptr;
        uint32 bestLevel = 0;
        ForEachBagItem(bot, [&](Item* item)
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE || proto->SubClass != ITEM_SUBCLASS_FOOD)
                return;
            if (proto->Spells[0].SpellCategory != (food ? 11u : 59u) || proto->RequiredLevel > bot->GetLevel())
                return;
            if (!best || proto->RequiredLevel > bestLevel)
            {
                best = item;
                bestLevel = proto->RequiredLevel;
            }
        });
        return best;
    }

    // How many items of the level range a bot may still use (RequiredLevel within the outdated margin).
    uint32 SupplyCount(Player* bot, bool food)
    {
        uint32 count = 0;
        ForEachBagItem(bot, [&](Item* item)
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto || proto->Class != ITEM_CLASS_CONSUMABLE || proto->SubClass != ITEM_SUBCLASS_FOOD)
                return;
            if (proto->Spells[0].SpellCategory != (food ? 11u : 59u) || proto->RequiredLevel > bot->GetLevel())
                return;
            if (proto->RequiredLevel + g_settings.outdatedBelowLevels < bot->GetLevel())
                return;
            count += item->GetCount();
        });
        return count;
    }

    bool UsesMana(Player* bot)
    {
        return bot->getPowerType() == POWER_MANA;
    }

    // What a trip to town should buy: the best food (and drink for a mana user) of the bot's level up to the target
    // count, within the money the bot may spend today.
    void BuySupplies(Player* bot)
    {
        if (!g_settings.consumeSupplies)
            return;
        uint32 const now = GameTime::GetGameTimeMS().count();
        Spending& spent = g_spent[bot->GetGUID()];
        if (!spent.dayStartedAt || now - spent.dayStartedAt > 24u * 60 * 60 * 1000)
        {
            spent.dayStartedAt = now ? now : 1;
            spent.copper = 0;
        }

        for (bool food : { true, false })
        {
            if (!food && !UsesMana(bot))
                continue;
            uint32 const entry = food ? BotProgression::PickFood(bot->GetLevel()) : BotProgression::PickDrink(bot->GetLevel());
            ItemTemplate const* proto = entry ? sObjectMgr->GetItemTemplate(entry) : nullptr;
            if (!proto)
                continue;
            uint32 const have = SupplyCount(bot, food);
            if (have >= g_settings.supplyTarget)
                continue;
            uint32 want = g_settings.supplyTarget - have;
            uint32 const unit = std::max<uint32>(1, proto->BuyPrice / std::max<uint32>(1, proto->BuyCount));
            uint64 const money = bot->GetMoney() > g_settings.moneyReserveCopper ? bot->GetMoney() - g_settings.moneyReserveCopper : 0;
            uint64 const budget = spent.copper < g_settings.dailyBudgetCopper ? g_settings.dailyBudgetCopper - spent.copper : 0;
            want = uint32(std::min<uint64>(want, std::min(money, budget) / unit));
            if (!want)
                continue;
            ItemPosCountVec dest;
            if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, want) != EQUIP_ERR_OK)
                continue;
            uint64 const cost = uint64(unit) * want;
            bot->ModifyMoney(-int64(cost));
            bot->StoreNewItem(dest, entry, true);
            spent.copper += cost;
            g_boughtCopper += cost;
            BotEconomy::Line line;
            line.entry = entry;
            line.reason = food ? "supplies-food" : "supplies-drink";
            JournalDone(bot, line, want, unit, "buy", "vendor");
        }
        g_needsSupplies.erase(bot->GetGUID());
    }

    // Whether a bot that eats and drinks has run out and can afford more: worth a trip to town.
    bool NeedsSupplies(Player* bot)
    {
        if (!g_settings.consumeSupplies)
            return false;
        if (SupplyCount(bot, true) == 0)
            return true;
        return UsesMana(bot) && SupplyCount(bot, false) == 0;
    }

    // What a bot does once it has reached the auctioneers: collect what earlier auctions earned, list and sell, and
    // pay the guild its share.
    void SellInTown(Player* bot, uint32 auctioneerFaction)
    {
        uint64 gained = CollectAuctionMail(bot);

        uint32 const listingsNow = g_listingCount[bot->GetGUID().GetCounter()];
        uint32 listingsLeft = g_settings.maxListings > listingsNow ? g_settings.maxListings - listingsNow : 0;

        std::vector<BotEconomy::Line> plan = BotEconomy::PlanSale(bot);
        for (BotEconomy::Line const& line : plan)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(line.entry);
            if (!proto)
                continue;
            uint32 remaining = line.count;

            if (line.verdict == BotEconomy::Verdict::Auction)
            {
                std::vector<Item*> stacks;
                ForEachBagItem(bot, [&](Item* it) { if (it->GetEntry() == line.entry) stacks.push_back(it); });
                for (Item* it : stacks)
                {
                    if (!remaining || !listingsLeft)
                        break;
                    uint32 const n = std::min(remaining, it->GetCount());
                    uint32 const unit = ListingUnitPrice(line, proto);
                    // When a vendor would pay more than the auction nets after its 5% cut, sell to the vendor instead.
                    if (uint64(unit) * n < g_settings.minListingCopper || uint64(unit) * n * 95 / 100 <= uint64(proto->SellPrice) * n)
                        break;
                    if (!ListOnAuction(bot, it, n, unit * n, auctioneerFaction))
                        break;
                    remaining -= n;
                    --listingsLeft;
                    ++g_tripTotals.listed;
                    JournalDone(bot, line, n, unit, "auction", line.priceSource);
                }
            }

            // Whatever is left (or was meant for a vendor) is sold at the vendor price.
            if (remaining && proto->SellPrice)
            {
                uint32 const have = bot->GetItemCount(line.entry);
                uint32 const n = std::min(remaining, have);
                if (n)
                    bot->DestroyItemCount(line.entry, n, true);
                if (n && bot->GetItemCount(line.entry) + n == have)
                {
                    uint64 const copper = uint64(proto->SellPrice) * n;
                    bot->ModifyMoney(int64(copper));
                    gained += copper;
                    ++g_tripTotals.soldToVendor;
                    JournalDone(bot, line, n, proto->SellPrice, "vendor", "vendor");
                }
            }
        }

        BuySupplies(bot);

        // The guild's small share, leaving the bot its own reserve.
        if (gained && g_settings.guildSharePercent && bot->GetGuild() && !g_goldOrders.count(bot->GetGUID()))
        {
            uint64 share = gained * g_settings.guildSharePercent / 100;
            uint64 const spare = bot->GetMoney() > g_settings.moneyReserveCopper ? bot->GetMoney() - g_settings.moneyReserveCopper : 0;
            share = std::min(share, spare);
            if (share)
            {
                sBotMgr->GuildDepositMoney(bot->GetGUID().GetCounter(), uint32(std::min<uint64>(share, 4000000000ull)), nullptr);
                g_tripTotals.guildCopper += share;
            }
        }
        g_tripTotals.earnedCopper += gained;
        bot->SaveToDB(false, false);
    }
}

namespace BotEconomy
{
    void ProcessGoldOrders();
    void BalanceStock();
    void EndStockRun(ObjectGuid bot);

    std::vector<Line> PlanSale(Player* bot)
    {
        std::vector<Line> out;
        if (!bot)
            return out;

        uint8 const botLevel = bot->GetLevel();

        // Consumables are judged per item over all stacks together.
        std::unordered_map<uint32, uint32> consumableTotal;
        ForEachBagItem(bot, [&](Item* item)
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (proto && IsConsumableOfInterest(proto))
                consumableTotal[item->GetEntry()] += item->GetCount();
        });

        std::unordered_set<uint32> consumableDone;
        auto add = [&](Line line)
        {
            if (line.verdict == Verdict::Keep || !line.count)
                return;
            for (Line& existing : out)
                if (existing.entry == line.entry && existing.verdict == line.verdict && std::string(existing.reason) == line.reason)
                {
                    existing.count += line.count;
                    return;
                }
            out.push_back(std::move(line));
        };

        ForEachBagItem(bot, [&](Item* item)
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto)
                return;
            uint32 const entry = item->GetEntry();
            if (entry == HEARTHSTONE_ENTRY || g_protected.count(entry))
                return;
            // A resource taken out of the guild bank to be sold (see the Stock limits).
            if (proto->Class == ITEM_CLASS_TRADE_GOODS)
            {
                auto carried = g_stockCarry.find(bot->GetGUID());
                if (carried != g_stockCarry.end() && carried->second.count(entry) && proto->SellPrice)
                {
                    Line stock;
                    stock.entry = entry;
                    stock.name = proto->Name1;
                    stock.count = item->GetCount();
                    stock.reason = "stock-surplus";
                    Decide(item, proto, false, stock);
                    add(std::move(stock));
                    return;
                }
            }
            if (proto->Class == ITEM_CLASS_QUEST || proto->StartQuest || proto->Class == ITEM_CLASS_KEY || proto->Class == ITEM_CLASS_CONTAINER ||
                proto->Class == ITEM_CLASS_TRADE_GOODS || proto->Class == ITEM_CLASS_RECIPE || proto->Class == ITEM_CLASS_PROJECTILE ||
                proto->Class == ITEM_CLASS_QUIVER || proto->Class == ITEM_CLASS_GLYPH)
                return;
            if (proto->Bonding == BIND_QUEST_ITEM || proto->Bonding == BIND_QUEST_ITEM1)
                return;

            Line line;
            line.entry = entry;
            line.name = proto->Name1;

            // Grey junk.
            if (proto->Quality == ITEM_QUALITY_POOR)
            {
                if (proto->SellPrice == 0)
                    return;
                line.count = item->GetCount();
                line.reason = "junk";
                line.verdict = Verdict::Vendor;
                line.unitCopper = proto->SellPrice;
                line.priceSource = "vendor";
                add(std::move(line));
                return;
            }

            // Gear.
            if (proto->Class == ITEM_CLASS_ARMOR || proto->Class == ITEM_CLASS_WEAPON)
            {
                char const* reason = "";
                if (!IsDeadWeightGear(bot, proto, reason))
                    return;
                line.count = item->GetCount();
                line.reason = reason;
                Decide(item, proto, true, line);
                add(std::move(line));
                return;
            }

            // Potions, elixirs, flasks, food, bandages.
            if (IsConsumableOfInterest(proto))
            {
                if (!consumableDone.insert(entry).second)
                    return; // handled with the first stack
                uint32 const total = consumableTotal[entry];
                bool const outdated = proto->RequiredLevel + g_settings.outdatedBelowLevels < botLevel;
                bool const tooHigh = proto->RequiredLevel > botLevel;
                if (tooHigh)
                    return; // the bot will grow into it
                uint32 const keep = outdated ? 0 : g_settings.keepConsumables;
                if (total <= keep)
                    return;
                line.count = total - keep;
                line.reason = outdated ? "outdated-consumable" : "surplus-consumable";
                Decide(item, proto, false, line); // potions and food are worth listing whatever their quality
                add(std::move(line));
                return;
            }

            // Gems and other finds worth a listing.
            if ((proto->Class == ITEM_CLASS_GEM || proto->Class == ITEM_CLASS_MISC) && proto->Quality >= g_settings.minAuctionQuality)
            {
                line.count = item->GetCount();
                line.reason = "found-valuable";
                Decide(item, proto, true, line);
                if (line.verdict == Verdict::Auction)
                    add(std::move(line));
            }
        });

        // Biggest first: it is what a trip to town is for.
        std::sort(out.begin(), out.end(), [](Line const& a, Line const& b) { return a.TotalCopper() > b.TotalCopper(); });
        return out;
    }

    void Update(uint32 diff, std::vector<WorldSession*> const& sessions)
    {
        RefreshSettings(diff);
        if (!g_settings.enable || sessions.empty())
            return;
        EnsureTables();
        RefreshMarket(diff);
        g_goldCheckAgeMs += diff;
        if (g_goldCheckAgeMs >= GOLD_CHECK_MS)
        {
            g_goldCheckAgeMs = 0;
            ProcessGoldOrders();
        }
        g_stockCheckAgeMs += diff;
        if (g_stockCheckAgeMs >= STOCK_CHECK_MS)
        {
            g_stockCheckAgeMs = 0;
            BalanceStock();
        }

        // Tick every waiting bot down, then look at the ones that are due, a few per tick.
        g_pruneAgeMs += diff;
        if (g_pruneAgeMs > 3600000)
        {
            g_pruneAgeMs = 0;
            CharacterDatabase.Execute(Acore::StringFormat("DELETE FROM mod_coa_bot_trade_journal WHERE at < NOW() - INTERVAL {} DAY", g_settings.journalDays));
            if (g_lastPlan.size() > sessions.size() * 2)
            {
                std::unordered_set<ObjectGuid> online;
                for (WorldSession* s : sessions)
                    if (Player* p = s->GetPlayer())
                        online.insert(p->GetGUID());
                for (auto it = g_lastPlan.begin(); it != g_lastPlan.end();)
                    it = online.count(it->first) ? std::next(it) : g_lastPlan.erase(it);
            }
            if (g_nextScanAt.size() > sessions.size() * 2)
            {
                std::unordered_set<ObjectGuid> online;
                for (WorldSession* s : sessions)
                    if (Player* p = s->GetPlayer())
                        online.insert(p->GetGUID());
                for (auto it = g_nextScanAt.begin(); it != g_nextScanAt.end();)
                    it = online.count(it->first) ? std::next(it) : g_nextScanAt.erase(it);
            }
        }

        // Look at a slice of the bots each tick; those that are due get planned, a few per tick.
        uint32 const now = GameTime::GetGameTimeMS().count();
        uint32 looked = 0;
        size_t const n = sessions.size();
        size_t const slice = std::min<size_t>(n, 64);
        for (size_t i = 0; i < slice && looked < g_settings.botsPerTick; ++i)
        {
            WorldSession* session = sessions[(g_rotation + i) % n];
            Player* bot = session->GetPlayer();
            if (!bot || !bot->IsInWorld() || bot->IsInCombat() || !bot->IsAlive())
                continue;

            auto [it, isNew] = g_nextScanAt.try_emplace(bot->GetGUID(), now);
            if (!isNew && int32(now - it->second) < 0)
                continue;
            // Spread the next looks out so a thousand bots are not all planned at the same moment.
            it->second = now + g_settings.scanIntervalSec * 1000 + urand(0, g_settings.scanIntervalSec * 200);
            ++looked;
            ++g_totals.botsScanned;

            std::vector<Line> plan = PlanSale(bot);
            uint64 total = 0;
            for (Line const& l : plan)
                total += l.TotalCopper();
            // A bot working on a gold order goes to town for a smaller load: the sooner its finds are money, the sooner they are in the bank.
            uint64 const minTrip = g_goldOrders.count(bot->GetGUID()) ? std::min<uint64>(g_settings.minTripCopper, 5000) : g_settings.minTripCopper;
            if (total < minTrip && !(g_needsSupplies.count(bot->GetGUID()) || NeedsSupplies(bot)))
                continue;

            uint64 fingerprint = 1469598103934665603ull;
            for (Line const& l : plan)
                for (uint64 v : { uint64(l.entry), uint64(l.count), uint64(l.verdict) })
                    fingerprint = (fingerprint ^ v) * 1099511628211ull;
            if (!g_settings.dryRun && !g_trips.count(bot->GetGUID()))
                g_wantsTrip.insert(bot->GetGUID());
            auto [last, firstPlan] = g_lastPlan.try_emplace(bot->GetGUID(), fingerprint);
            if (!firstPlan && last->second == fingerprint)
                continue; // nothing new since this bot was last written down
            last->second = fingerprint;

            ++g_totals.botsWithSales;
            for (Line const& l : plan)
                (l.verdict == Verdict::Auction ? g_totals.auctionCopper : g_totals.vendorCopper) += l.TotalCopper();
            Journal(bot, plan);
            LOG_DEBUG("module.coa-playerbots", "BotEconomy: '{}' would sell {} stack(s) worth about {} (plan only).", bot->GetName(), plan.size(), Money(total));
        }
        g_rotation = (g_rotation + slice) % std::max<size_t>(1, n);
    }

    // Guild master or an officer (rank 0 or 1).
    bool IsGuildLeader(Player* player)
    {
        Guild* guild = player ? sGuildMgr->GetGuildById(player->GetGuildId()) : nullptr;
        Guild::Member const* member = guild ? guild->GetMember(player->GetGUID()) : nullptr;
        return member && member->GetRankId() <= 1;
    }

    void EndStockRun(ObjectGuid bot)
    {
        g_stockCarry.erase(bot);
        for (auto it = g_stockRuns.begin(); it != g_stockRuns.end();)
            it = it->second == bot ? g_stockRuns.erase(it) : std::next(it);
    }

    // Once a minute, for each guild with limits that is not already running a surplus out: the most valuable surplus is
    // withdrawn by a free bot that has room for it, and that bot sets off for town.
    void BalanceStock()
    {
        uint32 const now = GameTime::GetGameTimeMS().count();
        for (auto const& [guildId, limits] : g_stockLimits)
        {
            if (limits.empty() || g_stockRuns.count(guildId))
                continue;
            Guild* guild = sGuildMgr->GetGuildById(guildId);
            if (!guild)
                continue;

            auto counts = sBotMgr->GuildBankTradeGoods(guildId);
            uint32 bestEntry = 0;
            uint32 bestSurplus = 0;
            uint64 bestValue = 0;
            for (auto const& [entry, keep] : limits)
            {
                auto have = counts.find(entry);
                if (have == counts.end() || have->second <= keep)
                    continue;
                uint32 const surplus = have->second - keep;
                if (surplus < std::max<uint32>(10, keep / 10))
                    continue;
                auto blocked = g_stockBlockedUntil.find((uint64(guildId) << 32) | entry);
                if (blocked != g_stockBlockedUntil.end() && int32(now - blocked->second) < 0)
                    continue;
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
                uint64 const value = uint64(surplus) * std::max<uint32>(1, proto ? proto->SellPrice : 1);
                if (value > bestValue)
                {
                    bestValue = value;
                    bestEntry = entry;
                    bestSurplus = surplus;
                }
            }
            if (!bestEntry)
                continue;

            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(bestEntry);
            if (!proto)
                continue;
            Player* chosen = nullptr;
            for (Player* bot : sBotMgr->GetOnlineBots())
            {
                if (bot->GetGuildId() != guildId || bot->GetGroup() || !bot->IsAlive() || bot->IsInCombat() || g_trips.count(bot->GetGUID()) ||
                    g_goldOrders.count(bot->GetGUID()) || sBotMgr->GetGuildGatherOrder(bot->GetGUID()) || bot->GetFreeInventorySpace() < 6)
                    continue;
                chosen = bot;
                break;
            }
            if (!chosen)
                continue;

            uint32 const room = chosen->GetFreeInventorySpace() * std::max<uint32>(1, proto->GetMaxStackSize()) * 8 / 10;
            uint32 const take = std::min(bestSurplus, std::max<uint32>(1, room));
            uint32 const got = sBotMgr->GuildWithdrawItem(chosen->GetGUID().GetCounter(), bestEntry, take, nullptr);
            if (!got)
            {
                g_stockBlockedUntil[(uint64(guildId) << 32) | bestEntry] = now + 30 * 60 * 1000;
                continue;
            }
            g_stockCarry[chosen->GetGUID()].insert(bestEntry);
            g_stockRuns[guildId] = chosen->GetGUID();
            g_nextTripAt.erase(chosen->GetGUID());
            g_wantsTrip.insert(chosen->GetGUID());
            LOG_DEBUG("module.coa-playerbots", "BotEconomy: '{}' took {}x {} out of the guild bank to sell.", chosen->GetName(), got, proto->Name1);
        }
    }

    std::vector<std::string> GetStockLines(Player* requester)
    {
        std::vector<std::string> lines;
        Guild* guild = requester ? sGuildMgr->GetGuildById(requester->GetGuildId()) : nullptr;
        if (!guild)
            return lines;
        EnsureTables();

        auto counts = sBotMgr->GuildBankTradeGoods(guild->GetId());
        auto const limitsIt = g_stockLimits.find(guild->GetId());
        struct Row
        {
            uint32 entry;
            uint32 count;
            uint32 limit;
            std::string name;
        };
        std::vector<Row> rows;
        for (auto const& [entry, count] : counts)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
            if (!proto)
                continue;
            uint32 limit = 0;
            if (limitsIt != g_stockLimits.end())
                if (auto l = limitsIt->second.find(entry); l != limitsIt->second.end())
                    limit = l->second;
            std::string name;
            for (char c : proto->Name1)
                name.push_back(c == ':' || c == '|' || c == ',' ? ' ' : c);
            rows.push_back({ entry, count, limit, name });
        }
        std::sort(rows.begin(), rows.end(), [](Row const& x, Row const& y) { return x.name < y.name; });

        constexpr size_t BUDGET = 200;
        std::string body;
        auto flush = [&]()
        {
            if (!body.empty())
                lines.push_back("STOCK:" + body);
            body.clear();
        };
        for (Row const& r : rows)
        {
            std::string const piece = std::to_string(r.entry) + "," + std::to_string(r.count) + "," + std::to_string(r.limit) + "," + r.name;
            if (!body.empty() && body.size() + 1 + piece.size() > BUDGET)
                flush();
            if (!body.empty())
                body += "|";
            body += piece;
        }
        flush();
        return lines;
    }

    void SetStockLimit(Player* requester, uint32 itemEntry, int64 keep, ChatHandler* handler)
    {
        if (!requester || !itemEntry)
            return;
        EnsureTables();
        uint32 const guildId = requester->GetGuildId();
        if (!guildId || (!handler && !IsGuildLeader(requester)))
            return;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemEntry);
        if (!proto || proto->Class != ITEM_CLASS_TRADE_GOODS)
        {
            if (handler)
                handler->SendSysMessage("Limits can be set for resources (trade goods) only.");
            return;
        }
        if (keep < 0)
        {
            g_stockLimits[guildId].erase(itemEntry);
            CharacterDatabase.Execute(Acore::StringFormat("DELETE FROM mod_coa_bot_stock_limits WHERE guild_id = {} AND item_entry = {}", guildId, itemEntry));
            if (handler)
                handler->PSendSysMessage("Limit of {} removed.", proto->Name1);
            return;
        }
        uint32 const value = uint32(std::min<int64>(keep, 1000000));
        g_stockLimits[guildId][itemEntry] = value;
        g_stockBlockedUntil.erase((uint64(guildId) << 32) | itemEntry);
        CharacterDatabase.Execute(Acore::StringFormat("REPLACE INTO mod_coa_bot_stock_limits (guild_id, item_entry, keep_count) VALUES ({}, {}, {})", guildId, itemEntry, value));
        if (handler)
            handler->PSendSysMessage("The guild bank keeps {} of {}; the surplus is sold by the bots.", value, proto->Name1);
    }

    void ProcessGoldOrders()
    {
        std::vector<ObjectGuid> guids;
        guids.reserve(g_goldOrders.size());
        for (auto const& entry : g_goldOrders)
            guids.push_back(entry.first);
        for (ObjectGuid const& guid : guids)
        {
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (!bot)
                continue; // offline: the order waits
            GoldOrder& o = g_goldOrders[guid];
            if (!bot->GetGuild() || bot->GetGuildId() != o.guildId)
            {
                DeleteGoldOrder(guid); // the bot left the guild
                continue;
            }
            uint64 const money = bot->GetMoney();
            uint64 const spare = money > g_settings.moneyReserveCopper ? money - g_settings.moneyReserveCopper : 0;
            if (spare < GOLD_MIN_DEPOSIT_COPPER)
                continue;
            uint64 const remaining = o.targetCopper > o.depositedCopper ? o.targetCopper - o.depositedCopper : 0;
            uint64 const amount = std::min<uint64>({ spare, remaining, 4000000000ull });
            if (amount)
            {
                sBotMgr->GuildDepositMoney(guid.GetCounter(), uint32(amount), nullptr);
                o.depositedCopper += amount;
                g_tripTotals.guildCopper += amount;
            }
            if (o.depositedCopper >= o.targetCopper)
            {
                LOG_INFO("module.coa-playerbots", "BotEconomy: '{}' finished a gold order of {} for guild {}.", bot->GetName(), Money(o.targetCopper), o.guildId);
                DeleteGoldOrder(guid);
            }
            else
                SaveGoldOrder(guid, o);
        }
    }

    void PlaceGoldOrder(Player* requester, uint32 goldAmount, ChatHandler* handler)
    {
        if (!requester)
            return;
        EnsureTables();
        uint32 const guildId = requester->GetGuildId();
        auto say = [&](std::string const& text)
        {
            if (handler)
                handler->SendSysMessage(text);
        };
        if (!guildId)
        {
            say("Gold orders need a guild: the requester is not in one.");
            return;
        }
        if (!handler)
        {
            // From a player (the addon): guild master or an officer only.
            Guild* guild = sGuildMgr->GetGuildById(guildId);
            Guild::Member const* member = guild ? guild->GetMember(requester->GetGUID()) : nullptr;
            if (!member || member->GetRankId() > 1)
                return;
        }

        if (goldAmount == 0)
        {
            std::vector<ObjectGuid> mine;
            for (auto const& [guid, o] : g_goldOrders)
                if (o.guildId == guildId)
                    mine.push_back(guid);
            for (ObjectGuid const& guid : mine)
                DeleteGoldOrder(guid);
            say(Acore::StringFormat("Cancelled {} gold order(s).", mine.size()));
            return;
        }

        Player* chosen = nullptr;
        for (Player* bot : sBotMgr->GetOnlineBots())
        {
            if (bot->GetGuildId() != guildId || bot->GetGroup() || g_goldOrders.count(bot->GetGUID()))
                continue;
            if (!sBotMgr->GetGuildGatherOrder(bot->GetGUID()))
            {
                chosen = bot;
                break;
            }
            if (!chosen)
                chosen = bot;
        }
        if (!chosen)
        {
            say("No free online guild-mate bot found for a gold order.");
            return;
        }
        GoldOrder order;
        order.guildId = guildId;
        order.targetCopper = uint64(std::min<uint32>(goldAmount, 100000)) * 10000;
        g_goldOrders[chosen->GetGUID()] = order;
        SaveGoldOrder(chosen->GetGUID(), order);
        say(Acore::StringFormat("{} will earn {} for the guild.", chosen->GetName(), Money(order.targetCopper)));
    }

    std::string GoldOrderTask(Player* bot)
    {
        if (!bot)
            return std::string();
        auto it = g_goldOrders.find(bot->GetGUID());
        if (it == g_goldOrders.end())
            return std::string();
        GoldOrder const& o = it->second;
        uint64 const target = std::max<uint64>(1, o.targetCopper);
        return Acore::StringFormat("earning gold for the guild ({}/{} g - {}%)", o.depositedCopper / 10000, target / 10000, std::min<uint64>(100, o.depositedCopper * 100 / target));
    }

    void StartRestAura(Player* bot, bool food)
    {
        uint32 const spell = food ? 433 : 431;
        if (!g_settings.consumeSupplies || !g_settings.enable)
        {
            bot->CastSpell(bot, spell, true);
            return;
        }
        uint32 const now = GameTime::GetGameTimeMS().count();
        uint64 const key = uint64(bot->GetGUID().GetCounter()) * 2 + (food ? 1 : 0);
        auto wait = g_noSupplyUntil.find(key);
        if (wait != g_noSupplyUntil.end() && int32(now - wait->second) < 0)
            return;
        Item* item = BestSupplyInBags(bot, food);
        if (!item)
        {
            g_needsSupplies.insert(bot->GetGUID()); // rests the slow way until it has bought something
            g_noSupplyUntil[key] = now + 5000;
            return;
        }
        bot->DestroyItemCount(item->GetEntry(), 1, true);
        bot->CastSpell(bot, spell, true);
    }

    bool UpdateTrip(Player* bot, uint32 /*diff*/)
    {
        if (!g_settings.enable || g_settings.dryRun || !bot)
            return false;

        ObjectGuid const guid = bot->GetGUID();
        uint32 const now = GameTime::GetGameTimeMS().count();

        auto trip = g_trips.find(guid);
        if (trip == g_trips.end())
        {
            if (!g_wantsTrip.count(guid) || g_trips.size() >= g_settings.maxTrips || !bot->IsAlive() || bot->IsInFlight())
                return false;
            auto cooldown = g_nextTripAt.find(guid);
            if (cooldown != g_nextTripAt.end() && int32(now - cooldown->second) < 0)
                return false;
            g_wantsTrip.erase(guid);
            Poi const* poi = NearestAuctioneer(bot);
            if (!poi)
            {
                ++g_tripTotals.noDestination;
                g_nextTripAt[guid] = now + 20 * 60 * 1000;
                return false;
            }
            Trip t;
            t.mapId = bot->GetMapId();
            t.x = poi->x;
            t.y = poi->y;
            t.z = poi->z;
            t.faction = poi->faction;
            t.startedAt = now;
            t.goalId = ++g_goalSeed;
            trip = g_trips.emplace(guid, t).first;
            ++g_tripTotals.trips;
            ReleaseOrdinaryWalks(bot);
            LOG_DEBUG("module.coa-playerbots", "BotEconomy: '{}' sets off for the auctioneers ({:.0f}, {:.0f}).", bot->GetName(), t.x, t.y);
        }

        Trip& t = trip->second;
        auto finish = [&](bool timedOut)
        {
            BotMovement::ForceReleaseOwner(bot, MoveOwner::Travel);
            g_nextTripAt[guid] = now + g_settings.tripCooldownMin * 60 * 1000 + urand(0, 5 * 60 * 1000);
            if (timedOut)
                ++g_tripTotals.timedOut;
            g_trips.erase(guid);
            EndStockRun(guid);
        };

        if (!bot->IsAlive() || bot->GetMapId() != t.mapId || int32(now - t.startedAt) > int32(TRIP_TIMEOUT_MS))
        {
            finish(bot->IsAlive());
            return false;
        }

        // The ambient layer drives the flight (walk to the flight master, take off, land) once asked; until it is done
        // the bot is left to it. Any other ambient errand (sitting at an inn, strolling) gives way to the trip.
        if (bot->IsInFlight())
            return false;
        if (BotWorldBehavior::HasActiveErrand(guid))
        {
            if (t.flightRequested)
                return false;
            BotWorldBehavior::Forget(guid);
            BotMovement::ForceReleaseOwner(bot, MoveOwner::Ambient);
        }
        else
            t.flightRequested = false;

        float const dist = std::hypot(bot->GetPositionX() - t.x, bot->GetPositionY() - t.y);
        if (dist <= ARRIVE_RADIUS)
        {
            if (bot->IsMounted())
                BotMovement::RequestDismount(bot, MoveOwner::Travel, DismountReason::Arrival);
            SellInTown(bot, t.faction);
            finish(false);
            return false;
        }

        if (!t.flightTried && dist > TAXI_MIN_DISTANCE)
        {
            t.flightTried = true;
            if (BotWorldBehavior::RequestTravel(bot, t.mapId, t.x, t.y, t.z, false))
            {
                t.flightRequested = true;
                BotMovement::ForceReleaseOwner(bot, MoveOwner::Travel);
                return true;
            }
        }

        BotMovement::RequestMount(bot, MoveOwner::Travel, dist);

        NavStatus const status = BotMovement::Navigate(bot, MoveOwner::Travel, t.goalId, t.x, t.y, t.z, ARRIVE_RADIUS * 0.7f);
        if (status == NavStatus::Stuck)
        {
            finish(false);
            return false;
        }
        if (status == NavStatus::Blocked)
            ReleaseOrdinaryWalks(bot);
        return true;
    }

    void HandleCommand(ChatHandler* handler, std::string const& args)
    {
        EnsureTables();
        std::vector<std::string> tok;
        {
            std::string cur;
            for (char c : args)
            {
                if (c == ' ')
                {
                    if (!cur.empty())
                        tok.push_back(cur), cur.clear();
                }
                else
                    cur.push_back(c);
            }
            if (!cur.empty())
                tok.push_back(cur);
        }
        std::string const sub = tok.empty() ? "status" : tok[0];

        if (sub == "status")
        {
            handler->PSendSysMessage("Economy: {}, {}. Looked at {} bots, {} with something to sell; journal rows written: {}.",
                g_settings.enable ? "on" : "off - set CoaBots.Economy.Enable = 1", g_settings.dryRun ? "plan only (CoaBots.Economy.DryRun = 1)" : "bots trade for real",
                g_totals.botsScanned, g_totals.botsWithSales, g_totals.journalRows);
            handler->PSendSysMessage("Stock: {} guild(s) with limits, {} surplus run(s) now.", g_stockLimits.size(), g_stockRuns.size());
            handler->PSendSysMessage("Trips: {} bots on their way, {} waiting; trips made {}; listed {} auction(s); {} vendor sale(s); earned {}, paid to guilds {}; no auctioneer on the map: {}; gave up: {}.",
                g_trips.size(), g_wantsTrip.size(), g_tripTotals.trips, g_tripTotals.listed, g_tripTotals.soldToVendor, Money(g_tripTotals.earnedCopper),
                Money(g_tripTotals.guildCopper), g_tripTotals.noDestination, g_tripTotals.timedOut);
            handler->PSendSysMessage("Would sell to vendors: {}; to the auction: {}. Auctions known to the market cache: {} items. Protected items: {}.",
                Money(g_totals.vendorCopper), Money(g_totals.auctionCopper), g_market.size(), g_protected.size());
            return;
        }

        if (sub == "goldorder" && tok.size() >= 3)
        {
            Player* requester = ObjectAccessor::FindPlayerByLowGUID(uint32(std::strtoul(tok[1].c_str(), nullptr, 10)));
            if (!requester)
            {
                handler->PSendSysMessage("No online player with guid {}.", tok[1]);
                return;
            }
            PlaceGoldOrder(requester, uint32(std::strtoul(tok[2].c_str(), nullptr, 10)), handler);
            return;
        }

        if (sub == "stock" && tok.size() >= 2)
        {
            Player* requester = ObjectAccessor::FindPlayerByLowGUID(uint32(std::strtoul(tok[1].c_str(), nullptr, 10)));
            if (!requester)
            {
                handler->PSendSysMessage("No online player with guid {}.", tok[1]);
                return;
            }
            std::vector<std::string> lines = GetStockLines(requester);
            handler->PSendSysMessage("{} reply line(s) for the guild bank:", lines.size());
            for (std::string const& l : lines)
                handler->SendSysMessage(l);
            return;
        }

        if (sub == "stocklimit" && tok.size() >= 4)
        {
            Player* requester = ObjectAccessor::FindPlayerByLowGUID(uint32(std::strtoul(tok[1].c_str(), nullptr, 10)));
            if (!requester)
            {
                handler->PSendSysMessage("No online player with guid {}.", tok[1]);
                return;
            }
            SetStockLimit(requester, uint32(std::strtoul(tok[2].c_str(), nullptr, 10)), std::strtoll(tok[3].c_str(), nullptr, 10), handler);
            return;
        }

        if (sub == "trips")
        {
            uint32 const now = GameTime::GetGameTimeMS().count();
            if (g_trips.empty())
                handler->SendSysMessage("No bot is on a trip to town right now.");
            for (auto const& [guid, t] : g_trips)
            {
                Player* bot = ObjectAccessor::FindPlayer(guid);
                if (!bot)
                    continue;
                handler->PSendSysMessage("  {} (guid {}): {:.0f} yd from the auctioneers, {} s on the way{}{}{}", bot->GetName(), guid.GetCounter(),
                    std::hypot(bot->GetPositionX() - t.x, bot->GetPositionY() - t.y), (now - t.startedAt) / 1000, bot->IsInFlight() ? ", flying" : "",
                    bot->IsMounted() ? ", mounted" : "", BotWorldBehavior::HasActiveErrand(guid) ? ", doing another errand first" : "");
            }
            return;
        }

        if (sub == "plan" && tok.size() >= 2)
        {
            Player* bot = sBotMgr->FindBotPlayer(uint32(std::strtoul(tok[1].c_str(), nullptr, 10)));
            if (!bot)
            {
                handler->PSendSysMessage("BotMgr: no online bot with guid {}.", tok[1]);
                return;
            }
            g_marketAgeMs = MARKET_REFRESH_MS; // the plan below should see current prices
            RefreshMarket(0);
            std::vector<Line> plan = PlanSale(bot);
            handler->PSendSysMessage("{} would sell {} line(s):", bot->GetName(), plan.size());
            uint64 total = 0;
            for (Line const& l : plan)
            {
                total += l.TotalCopper();
                handler->PSendSysMessage("  {} x{} -> {} ({}), {} each ({}), {}", l.name, l.count, l.verdict == Verdict::Auction ? "auction" : "vendor", l.reason,
                    Money(l.unitCopper), l.priceSource, Money(l.TotalCopper()));
            }
            handler->PSendSysMessage("Total: {}.", Money(total));
            return;
        }

        if ((sub == "protect" || sub == "unprotect") && tok.size() >= 2)
        {
            uint32 const entry = uint32(std::strtoul(tok[1].c_str(), nullptr, 10));
            if (!entry || !sObjectMgr->GetItemTemplate(entry))
            {
                handler->PSendSysMessage("No item with id {}.", tok[1]);
                return;
            }
            if (sub == "protect")
            {
                CharacterDatabase.DirectExecute(Acore::StringFormat("INSERT IGNORE INTO mod_coa_bot_protected_items (item_entry) VALUES ({})", entry));
                g_protected.insert(entry);
                handler->PSendSysMessage("Item {} is protected: bots will never sell it.", entry);
            }
            else
            {
                CharacterDatabase.DirectExecute(Acore::StringFormat("DELETE FROM mod_coa_bot_protected_items WHERE item_entry = {}", entry));
                g_protected.erase(entry);
                handler->PSendSysMessage("Item {} is no longer protected.", entry);
            }
            return;
        }

        if (sub == "journal")
        {
            uint32 n = tok.size() >= 2 ? std::min<uint32>(50, uint32(std::strtoul(tok[1].c_str(), nullptr, 10))) : 15;
            if (!n)
                n = 15;
            QueryResult result = CharacterDatabase.Query(Acore::StringFormat(
                "SELECT at, bot_name, item_entry, item_count, verdict, reason, unit_copper, executed FROM mod_coa_bot_trade_journal ORDER BY id DESC LIMIT {}", n));
            if (!result)
            {
                handler->SendSysMessage("The journal is empty.");
                return;
            }
            do
            {
                Field* f = result->Fetch();
                handler->PSendSysMessage("{} {}: item {} x{} -> {} ({}), {} each{}", f[0].Get<std::string>(), f[1].Get<std::string>(), f[2].Get<uint32>(),
                    f[3].Get<uint32>(), f[4].Get<std::string>(), f[5].Get<std::string>(), Money(f[6].Get<uint32>()), f[7].Get<uint8>() ? "" : " (plan)");
            } while (result->NextRow());
            return;
        }

        handler->SendSysMessage("Usage: .botcmd economy [status | trips | goldorder <player guid> <gold> | stock <player guid> | stocklimit <player guid> <item> <keep, -1 clears> | plan <bot guid> | journal [n] | protect <item id> | unprotect <item id>]");
    }
}
