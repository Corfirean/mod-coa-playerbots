#include "BotCheckpoint.h"
#include "BotPerformance.h"
#include "BotMgr.h"
#include "Bag.h"
#include "Config.h"
#include "Item.h"
#include "Log.h"
#include "Player.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>

namespace CoaBotPerf
{
    void WriteCheckpoint()
    {
        if (!Enabled()) return;
        auto bots = sBotMgr->GetOnlineBots();
        std::sort(bots.begin(), bots.end(), [](Player* a, Player* b) { return a->GetGUID() < b->GetGUID(); });
        std::ostringstream out;
        out << std::setprecision(9) << "{\"schema\":1,\"players\":[";
        unsigned count = 0;
        for (Player* bot : bots)
        {
            if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsInFlight() || bot->IsBeingTeleported()) continue;
            if (count) out << ',';
            out << "{\"guid\":" << bot->GetGUID().GetCounter() << ",\"level\":" << unsigned(bot->GetLevel())
                << ",\"class\":" << unsigned(bot->getClass()) << ",\"race\":" << unsigned(bot->getRace())
                << ",\"spec\":" << bot->GetPlayerSetting("core.ascension_active_spec", 0).value
                << ",\"xp\":" << bot->GetUInt32Value(PLAYER_XP) << ",\"money\":" << bot->GetMoney()
                << ",\"map\":" << bot->GetMapId() << ",\"x\":" << bot->GetPositionX()
                << ",\"y\":" << bot->GetPositionY() << ",\"z\":" << bot->GetPositionZ() << ",\"quests\":[";
            bool first = true;
            for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 quest = bot->GetQuestSlotQuestId(slot);
                if (!quest) continue;
                if (!first) out << ',';
                first = false;
                out << '[' << quest << ',' << unsigned(bot->GetQuestStatus(quest)) << ']';
            }
            out << "],\"items\":[";
            std::map<uint32, std::pair<uint32, uint32>> items;
            auto remember = [&items](Item* item)
            {
                if (!item || !item->GetTemplate() || (item->GetTemplate()->Flags & ITEM_FLAG_CONJURED) ||
                    item->GetUInt32Value(ITEM_FIELD_DURATION)) return;
                items.emplace(item->GetGUID().GetCounter(), std::make_pair(item->GetEntry(), item->GetCount()));
            };
            for (uint8 slot = 0; slot < PLAYER_SLOTS_COUNT; ++slot)
            {
                if (slot >= BUYBACK_SLOT_START && slot < BUYBACK_SLOT_END) continue;
                Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                remember(item);
                if (Bag* bag = item ? item->ToBag() : nullptr)
                    for (uint32 inside = 0; inside < std::min(bag->GetBagSize(), 256u); ++inside)
                        remember(bag->GetItemByPos(uint8(inside)));
            }
            first = true;
            for (auto const& [guid, value] : items)
            {
                if (!first) out << ',';
                first = false;
                out << '[' << guid << ',' << value.first << ',' << value.second << ']';
            }
            out << "]}";
            if (++count >= 20) break;
        }
        out << "]}\n";
        try
        {
            std::filesystem::path path = sConfigMgr->GetOption<std::string>("CoaBots.Perf.OutputDir", "bot-performance");
            std::filesystem::create_directories(path);
            std::ofstream file(path / "checkpoint.json", std::ios::binary);
            file << out.str();
            file.close();
            if (!file) LOG_ERROR("module.coa-playerbots", "CoaBotPerf checkpoint write failed");
        }
        catch (...) { LOG_ERROR("module.coa-playerbots", "CoaBotPerf checkpoint write failed"); }
    }
}
