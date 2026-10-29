/*
 * mod-coa-playerbots -- CoA World Map Intelligence
 * BotMapService: Server-side query processor, filtering, activity resolution and event provider.
 */

#ifndef COA_PLAYERBOTS_BOT_MAP_SERVICE_H
#define COA_PLAYERBOTS_BOT_MAP_SERVICE_H

#include "BotMapProtocol.h"
#include "BotMapFilter.h"
#include "WorldMapEventSnapshot.h"
#include <memory>
#include <unordered_map>
#include <vector>

class Player;

class BotMapService
{
public:
    static BotMapService* instance();

    // Query Handlers
    std::vector<std::string> HandleMapQuery(Player* commander, std::vector<std::string> const& parts);
    std::vector<std::string> HandleMapDetail(Player* commander, std::vector<std::string> const& parts);
    std::vector<std::string> HandleMapEvents(Player* commander, std::vector<std::string> const& parts);

    // Event Provider Integration
    void SetEventProvider(std::shared_ptr<IWorldEventProvider> provider);
    IWorldEventProvider* GetEventProvider() const { return _eventProvider.get(); }

    // Activity resolution helper
    BotMapActivity ResolveBotActivity(Player* bot) const;

    // Build pin snapshot for a single bot
    bool BuildPinSnapshot(Player* bot, Player* commander, BotMapPinSnapshot& outPin) const;

    // Production Filtering Logic forwarding (decoupled for direct unit testing)
    static bool FilterBot(
        BotMapCommanderView const& commander,
        BotMapMode mode,
        int32 filterMapId,
        int32 filterAreaId,
        uint32 radius,
        BotMapFilterTarget const& bot,
        BotMapPinSnapshot& outPin)
    {
        return BotMapFilter::FilterBot(commander, mode, filterMapId, filterAreaId, radius, bot, outPin);
    }

    static std::vector<BotMapPinSnapshot> FilterBots(
        BotMapCommanderView const& commander,
        BotMapMode mode,
        int32 filterMapId,
        int32 filterAreaId,
        uint32 radius,
        std::vector<BotMapFilterTarget> const& bots)
    {
        return BotMapFilter::FilterBots(commander, mode, filterMapId, filterAreaId, radius, bots);
    }

    // Overloads for backwards compatibility
    static bool FilterBot(
        BotMapCommanderView const& commander,
        BotMapMode mode,
        int32 filterMapId,
        uint32 radius,
        BotMapFilterTarget const& bot,
        BotMapPinSnapshot& outPin)
    {
        return BotMapFilter::FilterBot(commander, mode, filterMapId, 0, radius, bot, outPin);
    }

    static std::vector<BotMapPinSnapshot> FilterBots(
        BotMapCommanderView const& commander,
        BotMapMode mode,
        int32 filterMapId,
        uint32 radius,
        std::vector<BotMapFilterTarget> const& bots)
    {
        return BotMapFilter::FilterBots(commander, mode, filterMapId, 0, radius, bots);
    }

private:
    BotMapService();

    void CleanStaleRateLimits(uint32 nowMs);

    std::shared_ptr<IWorldEventProvider> _eventProvider;
    uint32 _revision = 1;

    // Per-player rate limit state: playerGuid -> lastQueryGameMs
    std::unordered_map<uint64, uint32> _lastQueryTimes;
    std::unordered_map<uint64, uint32> _lastDetailTimes;
    std::unordered_map<uint64, uint32> _lastEventsTimes;
};

#define sBotMapService BotMapService::instance()

#endif // COA_PLAYERBOTS_BOT_MAP_SERVICE_H
