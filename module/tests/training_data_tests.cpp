#include "training/TrainingData.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace CoaTraining;
    Scenario scenario;
    for (auto name : {"ST_STATIC", "RESOURCE_ENDURANCE", "ST_MOVEMENT", "BURST", "CLEAVE", "AOE",
        "SURVIVAL", "GROUND_HAZARDS", "GROUP_STRESS", "THREAT_STRESS", "SUPPORT_GROUP",
        "DISPEL_MAGIC", "RESURRECT_AREA", "RESURRECT_AFTER_PULL", "INTERRUPT_MULTI", "TANKBUSTER_PCT", "TANKBUSTER_TELEGRAPH"})
    {
        assert(Scenario::Find(name, scenario));
        assert(scenario.Timeline(1001, 5) == scenario.Timeline(1001, 5));
        auto timeline = scenario.Timeline(1001, 5);
        for (size_t i = 0; i < timeline.size(); ++i)
        {
            assert(timeline[i].timeMs < scenario.durationMs);
            assert(timeline[i].targetSlot < 5);
            if (i) assert(timeline[i].timeMs >= timeline[i - 1].timeMs);
        }
    }
    assert(Scenario::Find("GROUP_STRESS", scenario));
    assert(scenario.Timeline(1001, 5) != scenario.Timeline(1002, 5));
    assert(!Scenario::Find("BAD_SCENARIO", scenario));
    assert(Scenario::Find("RESURRECT_AREA", scenario));
    size_t scheduledDeaths = 0;
    for (auto const& event : scenario.Timeline(6000, 5))
        if (event.kind == "resurrection_death") { ++scheduledDeaths; assert(event.timeMs == 3000); }
    assert(scheduledDeaths == 1);
    assert(Json("quote\"\n\\").Dump() == "\"quote\\\"\\u000a\\\\\"");
    // INTERRUPT_MULTI: two enemies, every enemy casts together every 10 s from 10 s, no single-boss cast.
    assert(Scenario::Find("INTERRUPT_MULTI", scenario) && scenario.enemyCount == 2 && scenario.multiInterrupt);
    size_t allCasts = 0, singleCasts = 0;
    for (auto const& event : scenario.Timeline(1001, 5))
    {
        if (event.kind == "interruptible_cast_all") { ++allCasts; assert(event.timeMs >= 10000 && (event.timeMs - 10000) % 10000 == 0); }
        if (event.kind == "interruptible_cast") ++singleCasts;
    }
    assert(allCasts == 8 && singleCasts == 0);
    // Other scenarios are unchanged: one interruptible_cast stream, no multi-cast.
    assert(Scenario::Find("ST_STATIC", scenario) && !scenario.multiInterrupt);
    for (auto const& event : scenario.Timeline(1001, 5)) assert(event.kind != "interruptible_cast_all");
    // TANKBUSTER_PCT carries a percentage, TANKBUSTER_TELEGRAPH a damage value.
    assert(Scenario::Find("TANKBUSTER_PCT", scenario) && scenario.busterIsPercent && scenario.telegraphedBuster == 20);
    assert(Scenario::Find("TANKBUSTER_TELEGRAPH", scenario) && !scenario.busterIsPercent && scenario.telegraphedBuster == 1800);
    Json row;
    row["value"] = 17; row["null"] = Json(); row["bool"] = true;
    assert(row.Dump() == "{\"bool\":true,\"null\":null,\"value\":17}");
    AuraStats aura;
    aura.Apply(1000, 1); aura.Apply(2000, 2);
    assert(aura.Uptime(3000) == 2000);
    assert(aura.refreshes == 1 && aura.applies == 1 && aura.stacks == 2);
    aura.Remove(4000); aura.Remove(5000);
    assert(aura.Uptime(6000) == 3000);
    aura.Apply(7000, 1);
    assert(aura.Uptime(8000) == 4000);
    aura = AuraStats{};
    assert(aura.Uptime(10000) == 0 && !aura.active);
    std::cout << "Training schedule and JSON checks passed\n";
}
