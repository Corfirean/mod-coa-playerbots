/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: BotDebugLog implementation
 */

#include "engine/BotDebugLog.h"
#include <unordered_map>

namespace BotAI::BotDebugLog
{
    namespace
    {
        std::unordered_map<ObjectGuid, uint8> g_fleetSlot;

        std::string const& SharedLoggerName()
        {
            static std::string const name = "module.coa-playerbots";
            return name;
        }

        // Precomputed once per possible slot (1-21) rather than built on every call.
        std::string const& FleetLoggerName(uint8 slot)
        {
            static std::unordered_map<uint8, std::string> names = []
            {
                std::unordered_map<uint8, std::string> m;
                for (uint8 s = 1; s <= MAX_FLEET_SLOTS; ++s)
                    m.emplace(s, "module.coa-playerbots.fleet.bot" + std::to_string(s));
                return m;
            }();
            return names.at(slot);
        }
    }

    void Clear()
    {
        g_fleetSlot.clear();
    }

    void Register(ObjectGuid guid, uint8 slot)
    {
        if (slot < 1 || slot > MAX_FLEET_SLOTS)
            return;
        g_fleetSlot[guid] = slot;
    }

    bool IsFleetBot(ObjectGuid guid)
    {
        return g_fleetSlot.find(guid) != g_fleetSlot.end();
    }

    std::string const& LoggerName(ObjectGuid guid)
    {
        auto it = g_fleetSlot.find(guid);
        if (it == g_fleetSlot.end())
            return SharedLoggerName();
        return FleetLoggerName(it->second);
    }
}
