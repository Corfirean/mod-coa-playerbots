#include "BotTaxi.h"
#include "BotZoneProgression.h"
#include "DBCStores.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "BotWorldPoi.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <unordered_map>

namespace
{
    // A flight master stands right next to its node; farther than this and the bot is not "at" it.
    constexpr float AT_FLIGHT_MASTER = 10.0f;
    constexpr float NODE_NEAR_FLIGHT_MASTER = 40.0f;
    // How much closer to its destination a flight must land a bot than it already stands.
    constexpr float MIN_FLIGHT_GAIN = 400.0f;

    bool InNetwork(uint32 nodeId)
    {
        uint8 field = uint8((nodeId - 1) / 32);
        uint32 submask = 1u << ((nodeId - 1) % 32);
        return field < TaxiMaskSize && (sTaxiNodesMask[field] & submask);
    }

    // Same faction rule ObjectMgr::GetNearestTaxiNode and ActivateTaxiPathTo apply: a node without a
    // mount for this team is not one this team can fly from or to.
    TaxiNodesEntry const* UsableNode(Player const* bot, uint32 nodeId)
    {
        TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(nodeId);
        if (!node || !InNetwork(nodeId))
            return nullptr;
        if (!node->MountCreatureID[bot->GetTeamId() == TEAM_ALLIANCE ? 1 : 0])
            return nullptr;
        return node;
    }

    float NodeDistance(TaxiNodesEntry const* a, TaxiNodesEntry const* b)
    {
        return std::sqrt((a->x - b->x) * (a->x - b->x) + (a->y - b->y) * (a->y - b->y) + (a->z - b->z) * (a->z - b->z));
    }

    bool IsFriendlyFlightMaster(Player const* bot, Poi const& poi)
    {
        FactionTemplateEntry const* mine = bot->GetFactionTemplateEntry();
        FactionTemplateEntry const* theirs = poi.faction ? sFactionTemplateStore.LookupEntry(poi.faction) : nullptr;
        return !theirs || (mine && !mine->IsHostileTo(*theirs) && !theirs->IsHostileTo(*mine));
    }
}

namespace BotTaxi
{
    uint32 NodeHere(Player* bot)
    {
        std::vector<Poi const*> masters;
        BotWorldPoi::Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), AT_FLIGHT_MASTER,
            PoiKind::FlightMaster, masters);

        for (Poi const* master : masters)
        {
            if (!IsFriendlyFlightMaster(bot, *master))
                continue;

            uint32 nodeId = sObjectMgr->GetNearestTaxiNode(master->x, master->y, master->z, bot->GetMapId(),
                bot->GetTeamId());
            TaxiNodesEntry const* node = nodeId ? UsableNode(bot, nodeId) : nullptr;
            if (node && std::hypot(node->x - master->x, node->y - master->y) <= NODE_NEAR_FLIGHT_MASTER)
                return nodeId;
        }
        return 0;
    }

    bool DiscoverNearbyNode(Player* bot)
    {
        uint32 nodeId = NodeHere(bot);
        if (!nodeId || bot->m_taxi.IsTaximaskNodeKnown(nodeId))
            return false;

        bot->m_taxi.SetTaximaskNode(nodeId);
        TaxiNodesEntry const* node = sTaxiNodesStore.LookupEntry(nodeId);
        LOG_INFO("module.coa-playerbots.world", "Bot '{}' discovered flight path {} ({}).", bot->GetName(), nodeId,
            node ? node->name[0] : "?");
        return true;
    }

    void GrantNodesForLevel(Player* bot)
    {
        uint32 granted = 0;
        std::vector<BotZoneProgression::ZoneHub const*> hubs = BotZoneProgression::HubsUpToLevel(bot->GetLevel(),
            bot->getRace());
        for (BotZoneProgression::ZoneHub const* hub : hubs)
        {
            uint32 nodeId = sObjectMgr->GetNearestTaxiNode(hub->x, hub->y, hub->z, hub->mapId, bot->GetTeamId());
            if (nodeId && UsableNode(bot, nodeId) && bot->m_taxi.SetTaximaskNode(nodeId))
                ++granted;
        }

        if (granted)
            LOG_INFO("module.coa-playerbots", "BotTaxi: '{}' knows {} flight path(s) for level {}.", bot->GetName(),
                granted, bot->GetLevel());
    }

    bool FindFlightMaster(Player* bot, float radius, float& x, float& y, float& z)
    {
        std::vector<Poi const*> masters;
        BotWorldPoi::Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), radius, PoiKind::FlightMaster,
            masters);

        Poi const* nearest = nullptr;
        float nearestDist = 0.0f;
        for (Poi const* master : masters)
        {
            if (!IsFriendlyFlightMaster(bot, *master))
                continue;
            float d = std::hypot(master->x - bot->GetPositionX(), master->y - bot->GetPositionY());
            if (!nearest || d < nearestDist)
            {
                nearest = master;
                nearestDist = d;
            }
        }

        if (!nearest)
            return false;
        x = nearest->x;
        y = nearest->y;
        z = nearest->z;
        return true;
    }

    std::vector<uint32> PlanRoute(Player* bot, uint32 sourceNode, float destX, float destY, float destZ)
    {
        // The source counts as known even before the bot has spoken to its flight master: walking up
        // to it learns it, so a plan made from afar must not be refused for that.
        auto known = [bot, sourceNode](uint32 nodeId) -> TaxiNodesEntry const*
        {
            TaxiNodesEntry const* node = UsableNode(bot, nodeId);
            return node && (nodeId == sourceNode || bot->m_taxi.IsTaximaskNodeKnown(nodeId)) ? node : nullptr;
        };

        TaxiNodesEntry const* source = known(sourceNode);
        if (!source)
            return {};

        uint32 target = 0;
        float targetDist = 0.0f;
        for (uint32 nodeId = 1; nodeId < sTaxiNodesStore.GetNumRows(); ++nodeId)
        {
            TaxiNodesEntry const* node = known(nodeId);
            if (!node || node->map_id != source->map_id)
                continue;
            float d = std::sqrt((node->x - destX) * (node->x - destX) + (node->y - destY) * (node->y - destY) +
                (node->z - destZ) * (node->z - destZ));
            if (!target || d < targetDist)
            {
                target = nodeId;
                targetDist = d;
            }
        }

        if (!target || target == sourceNode)
            return {};

        // Dijkstra by flight distance over direct paths between known nodes.
        using Entry = std::pair<float, uint32>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
        std::unordered_map<uint32, float> best;
        std::unordered_map<uint32, uint32> previous;
        best[sourceNode] = 0.0f;
        open.emplace(0.0f, sourceNode);

        while (!open.empty())
        {
            auto [cost, current] = open.top();
            open.pop();
            if (current == target)
                break;
            if (cost > best[current])
                continue;

            auto edges = sTaxiPathSetBySource.find(current);
            if (edges == sTaxiPathSetBySource.end())
                continue;

            TaxiNodesEntry const* from = sTaxiNodesStore.LookupEntry(current);
            for (auto const& [next, path] : edges->second)
            {
                TaxiNodesEntry const* to = known(next);
                if (!to || !path)
                    continue;
                float nextCost = cost + NodeDistance(from, to);
                auto itr = best.find(next);
                if (itr != best.end() && itr->second <= nextCost)
                    continue;
                best[next] = nextCost;
                previous[next] = current;
                open.emplace(nextCost, next);
            }
        }

        if (!previous.count(target))
            return {};

        std::vector<uint32> route;
        for (uint32 node = target; node != sourceNode; node = previous[node])
            route.push_back(node);
        route.push_back(sourceNode);
        std::reverse(route.begin(), route.end());
        return route;
    }

    bool WorthFlying(Player* bot, float masterX, float masterY, float masterZ, float destX, float destY,
        float destZ)
    {
        uint32 source = sObjectMgr->GetNearestTaxiNode(masterX, masterY, masterZ, bot->GetMapId(), bot->GetTeamId());
        if (!source || !UsableNode(bot, source))
            return false;

        std::vector<uint32> route = PlanRoute(bot, source, destX, destY, destZ);
        if (route.size() < 2)
            return false;

        // Landing must leave the bot clearly closer than it is now; otherwise the walk to the flight
        // master plus the walk from the landing node is no better than walking straight there.
        TaxiNodesEntry const* last = sTaxiNodesStore.LookupEntry(route.back());
        float fromLanding = std::hypot(last->x - destX, last->y - destY);
        float fromHere = std::hypot(bot->GetPositionX() - destX, bot->GetPositionY() - destY);
        return fromLanding + MIN_FLIGHT_GAIN < fromHere;
    }

    bool FlyToward(Player* bot, float destX, float destY, float destZ)
    {
        uint32 here = NodeHere(bot);
        if (!here)
        {
            LOG_INFO("module.coa-playerbots.world", "Bot '{}' cannot fly: no flight-master node here (map {}, {:.0f}, {:.0f}).",
                bot->GetName(), bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY());
            return false;
        }

        // Standing at the flight master is talking to it: the node is learned before anything else.
        DiscoverNearbyNode(bot);

        std::vector<uint32> route = PlanRoute(bot, here, destX, destY, destZ);
        if (route.size() < 2)
        {
            LOG_INFO("module.coa-playerbots.world", "Bot '{}' cannot fly: no known route from node {} toward ({:.0f}, {:.0f}).",
                bot->GetName(), here, destX, destY);
            return false;
        }

        if (bot->IsMounted())
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);

        if (!bot->ActivateTaxiPathTo(route))
        {
            LOG_INFO("module.coa-playerbots.world", "Bot '{}' cannot fly: the engine refused the {}-node route from {}.",
                bot->GetName(), route.size(), here);
            return false;
        }

        TaxiNodesEntry const* last = sTaxiNodesStore.LookupEntry(route.back());
        LOG_INFO("module.coa-playerbots.world", "Bot '{}' took a flight to {} ({} hop(s)).", bot->GetName(),
            last ? last->name[0] : "?", route.size() - 1);
        return true;
    }
}
