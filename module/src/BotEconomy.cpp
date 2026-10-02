#include "BotEconomy.h"

#include "AuctionHouseMgr.h"
#include "Bag.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "GameTime.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "StringFormat.h"
#include "WorldSession.h"
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
    } g_settings;

    uint32 g_settingsAgeMs = 1000000;
    bool g_tablesReady = false;
    std::unordered_set<uint32> g_protected;

    // Lowest buyout per single item over every auction house, refreshed every few minutes.
    std::unordered_map<uint32, uint32> g_market;
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
        LoadProtected();
        g_tablesReady = true;
    }

    void RefreshMarket(uint32 diff)
    {
        g_marketAgeMs += diff;
        if (g_marketAgeMs < MARKET_REFRESH_MS)
            return;
        g_marketAgeMs = 0;
        g_market.clear();
        for (AuctionHouseId house : { AuctionHouseId::Alliance, AuctionHouseId::Horde, AuctionHouseId::Neutral })
        {
            AuctionHouseObject* auctions = sAuctionMgr->GetAuctionsMapByHouseId(house);
            if (!auctions)
                continue;
            for (auto const& pair : auctions->GetAuctions())
            {
                AuctionEntry const* a = pair.second;
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
}

namespace BotEconomy
{
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
            if (total < g_settings.minTripCopper)
                continue;

            uint64 fingerprint = 1469598103934665603ull;
            for (Line const& l : plan)
                for (uint64 v : { uint64(l.entry), uint64(l.count), uint64(l.verdict) })
                    fingerprint = (fingerprint ^ v) * 1099511628211ull;
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
            handler->PSendSysMessage("Economy: {} (plan only, nothing is sold yet). Looked at {} bots, {} with something to sell; journal rows written: {}.",
                g_settings.enable ? "on" : "off - set CoaBots.Economy.Enable = 1", g_totals.botsScanned, g_totals.botsWithSales, g_totals.journalRows);
            handler->PSendSysMessage("Would sell to vendors: {}; to the auction: {}. Auctions known to the market cache: {} items. Protected items: {}.",
                Money(g_totals.vendorCopper), Money(g_totals.auctionCopper), g_market.size(), g_protected.size());
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

        handler->SendSysMessage("Usage: .botcmd economy [status | plan <bot guid> | journal [n] | protect <item id> | unprotect <item id>]");
    }
}
