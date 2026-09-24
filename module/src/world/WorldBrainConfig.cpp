#include "WorldBrainConfig.h"
#include "Config.h"
#include <algorithm>

namespace
{
    WorldBrainConfig _config;

    template <typename T>
    T Opt(char const* name, T fallback)
    {
        return sConfigMgr->GetOption<T>(std::string("CoaBots.WorldBrain.") + name, fallback);
    }
}

namespace WorldBrainSettings
{
    void Load()
    {
        WorldBrainConfig c;

        c.enabled = Opt<bool>("Enable", c.enabled);
        c.plannerMinMs = Opt<uint32>("PlannerMinMs", c.plannerMinMs);
        c.plannerMaxMs = std::max(c.plannerMinMs, Opt<uint32>("PlannerMaxMs", c.plannerMaxMs));
        c.scanMinMs = std::max<uint32>(200, Opt<uint32>("ScanMinMs", c.scanMinMs));
        c.scanMaxMs = std::max(c.scanMinMs, Opt<uint32>("ScanMaxMs", c.scanMaxMs));

        c.searchRadius = Opt<float>("SearchRadius", c.searchRadius);
        c.engageRange = Opt<float>("EngageRange", c.engageRange);
        c.searchTimeoutMs = Opt<uint32>("SearchTimeoutMs", c.searchTimeoutMs);
        c.approachTimeoutMs = Opt<uint32>("ApproachTimeoutMs", c.approachTimeoutMs);
        c.travelTimeoutMs = Opt<uint32>("TravelTimeoutMs", c.travelTimeoutMs);

        c.maxActiveQuests = std::clamp<uint32>(Opt<uint32>("MaxActiveQuests", c.maxActiveQuests), 1, 25);
        c.maxObjectiveDistance = Opt<float>("MaxObjectiveDistance", c.maxObjectiveDistance);
        c.giverSearchRadius = Opt<float>("GiverSearchRadius", c.giverSearchRadius);
        c.questLevelBelow = Opt<int32>("QuestLevelBelow", c.questLevelBelow);
        c.questLevelAbove = Opt<int32>("QuestLevelAbove", c.questLevelAbove);
        c.maxLevelAbove = Opt<uint32>("MaxMobLevelAbove", c.maxLevelAbove);
        c.acceptElite = Opt<bool>("AcceptEliteQuests", c.acceptElite);
        c.maxBadClusters = std::max<uint32>(1, Opt<uint32>("MaxBadAreasPerObjective", c.maxBadClusters));
        c.questSuspendMs = std::max<uint32>(10000, Opt<uint32>("QuestSuspendMs", c.questSuspendMs));
        c.questSuspendMaxMs = std::max(c.questSuspendMs, Opt<uint32>("QuestSuspendMaxMs", c.questSuspendMaxMs));

        c.mountDistanceMin = Opt<float>("MountDistanceMin", c.mountDistanceMin);
        c.mountDistanceMax = std::max(c.mountDistanceMin, Opt<float>("MountDistanceMax", c.mountDistanceMax));
        c.taxiMinDistance = Opt<float>("TaxiMinDistance", c.taxiMinDistance);

        c.utility.travelPer100Yards = Opt<float>("Weight.TravelPer100Yards", c.utility.travelPer100Yards);
        c.utility.crowdPerBot = Opt<float>("Weight.CrowdPerBot", c.utility.crowdPerBot);
        c.utility.overlapBonus = Opt<float>("Weight.Overlap", c.utility.overlapBonus);
        c.utility.turnInBase = Opt<float>("Weight.TurnIn", c.utility.turnInBase);
        c.utility.acceptBase = Opt<float>("Weight.Accept", c.utility.acceptBase);
        c.cluster.occupancy = Opt<float>("Weight.AreaOccupancy", c.cluster.occupancy);
        c.target.reserved = Opt<float>("Weight.TargetReserved", c.target.reserved);

        _config = c;
    }

    WorldBrainConfig const& Get()
    {
        return _config;
    }
}
