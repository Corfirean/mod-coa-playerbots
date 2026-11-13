#ifndef COA_BOTS_PERFORMANCE_H
#define COA_BOTS_PERFORMANCE_H

#include <chrono>
#include <cstdint>
#include <string>

namespace CoaBotPerf
{
    enum class Metric : unsigned
    {
        Manager, AI, Offensive, Healer, Avoidance, Follow, TargetScan, SoloWorld,
        Progression, Economy, Planner, Navigation, WorldPeriod, WorldProcessing, CommandDispatch, CoreSessions, CoreMaps, CoreCallbacks, CoreScripts,
        MapTotal, MapSessions, MapPlayers, MapNonplayers, MapPackets, MapVisibility,
        CombatContext, TargetRefinement, ThreatSelection, ActionEvaluation,
        CreatureSample, PlayerSample, CreatureUnitSample, PlayerUnitSample, CorePathSample,
        ManagerGear, ManagerPopulation, ManagerWorld, ManagerEncounters, ManagerSessions, ManagerTrades,
        GearRepair, GearPool, GearEquip, GearArchive, GearSave, Count
    };

    bool Enabled();
    void Configure();
    void Observe(Metric metric, std::uint64_t us);
    void Increment(char const* name, std::uint64_t amount = 1);
    void Gauge(char const* name, std::uint64_t value);
    void Reset();
    std::string Status();
    bool RequestDump();
    void Shutdown();
    void Tick();
    void MarkCommand(std::uint64_t guid);
    void MarkDispatch(std::uint64_t guid);
    bool DetailedEnabled();
    bool CombatProfileEnabled();

    class Scope
    {
    public:
        explicit Scope(Metric metric, bool active = true) : _metric(metric), _enabled(active && DetailedEnabled())
        {
            if (_enabled)
                _start = std::chrono::steady_clock::now();
        }
        ~Scope() { Stop(); }
        void Stop()
        {
            if (_enabled)
            {
                _enabled = false;
                Observe(_metric, std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - _start).count());
            }
        }
        Scope(Scope const&) = delete;
        Scope& operator=(Scope const&) = delete;
    private:
        Metric _metric;
        bool _enabled;
        std::chrono::steady_clock::time_point _start;
    };
}
#endif
