#include "BotPerformance.h"
#include "performance/Histogram.h"
#include "Config.h"
#include "Log.h"
#include "UpdateTime.h"
#if __has_include("SpellCandidateFilter.h")
#include "SpellCandidateFilter.h"
#define COA_SPELL_CANDIDATE_FILTER 1
#endif
#if __has_include("WorldTickObserver.h")
#include "WorldTickObserver.h"
#define COA_WORLD_PERIOD_OBSERVER 1
#endif
#include "BotMgr.h"
#include "BotMovement.h"
#include "BotScheduling.h"
#include "engine/SpellResolver.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Map.h"
#include "Player.h"
#include "world/WorldMetrics.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <map>
#include <mutex>
#include <sstream>
#include <cstring>
#include <set>
#include <algorithm>
#include "Creature.h"
#include "GameObject.h"

namespace CoaBotPerf
{
    namespace
    {
        constexpr std::array<char const*, unsigned(Metric::Count)> Names{
            "manager", "ai", "offensive", "healer", "avoidance", "follow", "target_scan", "solo_world",
            "progression", "economy", "planner", "navigation", "world_period", "world_processing", "command_dispatch", "core_sessions", "core_maps", "core_callbacks", "core_scripts",
            "map_total", "map_sessions", "map_players", "map_nonplayers", "map_packets", "map_visibility",
            "combat_context", "target_refinement", "threat_selection", "action_evaluation",
            "sample_creature", "sample_player", "sample_creature_unit", "sample_player_unit", "sample_core_path",
            "manager_gear", "manager_population", "manager_world", "manager_encounters", "manager_sessions", "manager_trades",
            "gear_repair", "gear_pool", "gear_equip", "gear_archive", "gear_save"};
        std::atomic<bool> enabled{false};
        bool reference = false;
        bool detailed = true;
        bool combatProfile = false;
        std::mutex mutex;
        std::array<Histogram, unsigned(Metric::Count)> histograms;
        std::map<std::string, std::uint64_t> counters, gauges;
        std::map<std::uint64_t, std::chrono::steady_clock::time_point> commands;
        std::future<bool> writer;
        std::filesystem::path directory;
        std::uint64_t sequence = 0, epoch = 0;
        auto started = std::chrono::steady_clock::now();
        auto lastTick = started;
        auto lastDump = started;
        bool haveTick = false;

        bool Collecting() { return enabled.load(std::memory_order_relaxed) || reference; }

        void WriteMap(std::ostringstream& out, std::map<std::string, std::uint64_t> const& values)
        {
            bool first = true;
            out << '{';
            for (auto const& [key, value] : values)
            {
                if (!first) out << ',';
                first = false;
                out << '"' << key << "\":" << value;
            }
            out << '}';
        }

        std::string Snapshot()
        {
            auto copied = std::make_unique<decltype(histograms)>();
            decltype(counters) copiedCounters, copiedGauges;
            std::uint64_t copiedEpoch, elapsed;
            {
                std::lock_guard<std::mutex> lock(mutex);
                *copied = histograms;
                copiedCounters = counters;
                copiedGauges = gauges;
                copiedEpoch = epoch;
                elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
            }
            std::ostringstream out;
            out << "{\"schema\":1,\"epoch\":" << copiedEpoch << ",\"elapsed_us\":"
                << elapsed
                << ",\"histograms\":{";
            for (unsigned i = 0; i < unsigned(Metric::Count); ++i)
            {
                if (i) out << ',';
                Histogram const& h = (*copied)[i];
                out << '"' << Names[i] << "\":{\"count\":" << h.count << ",\"sum_us\":" << h.sumUs
                    << ",\"max_us\":" << h.maxUs << ",\"p50_us\":" << h.Percentile(50)
                    << ",\"p95_us\":" << h.Percentile(95) << ",\"p99_us\":" << h.Percentile(99)
                    << ",\"over\":[";
                for (unsigned j = 0; j < h.over.size(); ++j) { if (j) out << ','; out << h.over[j]; }
                out << "],\"bins\":[";
                for (unsigned j = 0; j < h.bins.size(); ++j) { if (j) out << ','; out << h.bins[j]; }
                out << "]}";
            }
            out << "},\"counters\":";
            WriteMap(out, copiedCounters);
            out << ",\"gauges\":";
            WriteMap(out, copiedGauges);
            out << '}';
            return out.str();
        }
    }

    void MarkCommand(std::uint64_t guid)
    {
        if (!Enabled()) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (commands.size() < 10000 || commands.count(guid))
            commands.try_emplace(guid, std::chrono::steady_clock::now());
    }

    void MarkDispatch(std::uint64_t guid)
    {
        if (!Enabled()) return;
        std::lock_guard<std::mutex> lock(mutex);
        auto it = commands.find(guid);
        if (it == commands.end()) return;
        histograms[unsigned(Metric::CommandDispatch)].Observe(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - it->second).count());
        commands.erase(it);
    }

    bool DetailedEnabled() { return Enabled() && detailed; }
    bool CombatProfileEnabled() { return combatProfile && DetailedEnabled(); }

    bool Enabled() { return enabled.load(std::memory_order_relaxed); }

    void Configure()
    {
        detailed = sConfigMgr->GetOption<bool>("CoaBots.Perf.Detailed", true);
        combatProfile = sConfigMgr->GetOption<bool>("CoaBots.Perf.ProfileCombat", false);
        directory = sConfigMgr->GetOption<std::string>("CoaBots.Perf.OutputDir", "bot-performance");
        enabled.store(sConfigMgr->GetOption<bool>("CoaBots.Perf.Enable", false), std::memory_order_relaxed);
        auto fixture = std::filesystem::path(sConfigMgr->GetOption<std::string>("CoaBots.Benchmark.Root", ""));
        reference = !Enabled() && !fixture.empty() &&
            sConfigMgr->GetOption<bool>("CoaBots.Benchmark.ReferenceMetrics", false) &&
            std::filesystem::is_regular_file(fixture / ".disposable-benchmark");
#ifdef COA_WORLD_PROCESSING_OBSERVER
        sWorldUpdateTime.SetProcessingObserver(Collecting() ? +[](uint64 us) { Observe(Metric::WorldProcessing, us); } : nullptr);
        sWorldUpdateTime.SetPhaseObserver(DetailedEnabled() ? +[](char const* name, uint64 us)
        {
            static constexpr std::array<std::pair<char const*, Metric>, 5> sampleNames{{
                {"Creature sample", Metric::CreatureSample}, {"Player sample", Metric::PlayerSample},
                {"Creature unit sample", Metric::CreatureUnitSample}, {"Player unit sample", Metric::PlayerUnitSample},
                {"Core path sample", Metric::CorePathSample}}};
            if (name[0] == 'C' || name[0] == 'P')
                for (auto const& [sampleName, metric] : sampleNames)
                    if (std::strcmp(name, sampleName) == 0) { Observe(metric, us); return; }
            static constexpr std::array<char const*, 10> phaseNames{"Update sessions", "Update maps", "Process query callbacks", "Update world scripts",
                "Map total", "Map sessions", "Map players", "Map nonplayers", "Map packets", "Map visibility"};
            for (unsigned i = 0; i < phaseNames.size(); ++i)
                if (std::strcmp(name, phaseNames[i]) == 0)
                {
                    Observe(Metric(unsigned(Metric::CoreSessions) + i), us);
                    break;
                }
        } : nullptr);
#endif
#ifdef COA_WORLD_PERIOD_OBSERVER
        WorldTickObserver::SetPeriodObserver(Collecting() ? +[](uint64 us) { Observe(Metric::WorldPeriod, us); } : nullptr);
#endif
        Reset();
    }

    void Observe(Metric metric, std::uint64_t us)
    {
        if (!Enabled() && !(reference && (metric == Metric::WorldPeriod || metric == Metric::WorldProcessing))) return;
        std::lock_guard<std::mutex> lock(mutex);
        histograms[unsigned(metric)].Observe(us);
    }

    void Increment(char const* name, std::uint64_t amount)
    {
        if (!Enabled()) return;
        std::lock_guard<std::mutex> lock(mutex);
        counters[name] += amount;
    }

    void Gauge(char const* name, std::uint64_t value)
    {
        if (!Enabled()) return;
        std::lock_guard<std::mutex> lock(mutex);
        gauges[name] = value;
    }

    void SampleWorld();

    void Reset()
    {
        {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& histogram : histograms) histogram = {};
        counters.clear();
        commands.clear();
        gauges.clear();
        ++epoch;
        started = std::chrono::steady_clock::now();
        lastDump = started;
        haveTick = false;
        }
        SampleWorld();
    }

    std::string Status()
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto const& h = histograms[unsigned(Metric::Manager)];
        auto samples = h.count;
#ifdef COA_WORLD_PROCESSING_OBSERVER
        samples = histograms[unsigned(Metric::WorldProcessing)].count;
#endif
        std::ostringstream out;
        out << "CoaBotPerf enabled=" << Enabled() << " epoch=" << epoch << " samples="
            << samples << " reference=" << reference
            << " manager_p95_us=" << h.Percentile(95) << " manager_p99_us=" << h.Percentile(99);
        return out.str();
    }

    bool RequestDump()
    {
        if (!Collecting()) return false;
        if (writer.valid())
        {
            if (writer.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return false;
            if (!writer.get())
                LOG_ERROR("module.coa-playerbots", "CoaBotPerf: previous dump failed");
        }
        SampleWorld();
        auto path = directory / ("metrics-" + std::to_string(++sequence) + ".json");
        auto snapshot = Snapshot();
        writer = std::async(std::launch::async, [path, snapshot = std::move(snapshot)]
        {
            try
            {
                std::filesystem::create_directories(path.parent_path());
                std::ofstream out(path, std::ios::binary);
                out << snapshot << '\n';
                out.close();
                return bool(out);
            }
            catch (...) { return false; }
        });
        return true;
    }

    void SampleWorld()
    {
        if (reference)
        {
            std::uint64_t online = 0;
            for (Player* bot : sBotMgr->GetOnlineBots()) online += bot && bot->IsInWorld();
            auto const& world = WorldMetricsGlobal::Get();
            std::lock_guard<std::mutex> lock(mutex);
            gauges["instrumentation_reference"] = 1;
            gauges["online_in_world"] = online;
            gauges["quests_accepted"] = world.questsAccepted;
            gauges["quests_turned_in"] = world.questsTurnedIn;
            gauges["objective_progress"] = world.objectiveProgress;
            gauges["movement_stuck"] = world.movementStuck;
            gauges["tasks_completed"] = world.tasksCompleted;
            gauges["tasks_failed"] = world.tasksFailed;
#ifdef COA_WORLD_PERIOD_OBSERVER
            gauges["world_period_update_start"] = 1;
#endif
#ifdef COA_WORLD_PROCESSING_OBSERVER
            gauges["world_processing_available"] = 1;
#endif
            return;
        }
        if (!Enabled()) return;
        Gauge("instrumentation_reference", 0);
        Gauge("profile_combat_enabled", CombatProfileEnabled());
        Gauge("learned_rank_cache_enabled", BotAI::SpellResolver::LearnedRankCacheEnabled());
#ifdef COA_DIRECT_OBJECT_FIELD_READS
        Gauge("direct_object_field_reads_enabled", Object::DirectFieldReadsEnabled());
#endif
#ifdef COA_SPELL_CANDIDATE_FILTER
        Gauge("spell_candidate_filter_enabled", SpellCandidateFilter::Enabled());
#endif
        uint32 configuredRate = sConfigMgr->GetOption<uint32>("World.Performance.EntitySampleRate", 0);
        Gauge("profile_entity_sample_rate", configuredRate ? std::clamp(configuredRate, 64u, 65536u) : 0u);
#ifdef COA_WORLD_PERIOD_OBSERVER
        Gauge("world_period_update_start", 1);
#else
        Gauge("world_period_update_start", 0);
#endif
#ifdef COA_WORLD_PROCESSING_OBSERVER
        Gauge("world_processing_available", 1);
#else
        Gauge("world_processing_available", 0);
#endif
        auto const navigation = BotMovement::Stats();
        Gauge("navigation_shared_no_path_hits", navigation.sharedNoPathHits);
        Gauge("navigation_path_queries", navigation.pathQueries);
        Gauge("navigation_redundant_skipped", navigation.redundantSkipped);
        Gauge("navigation_switchbacks_handled", navigation.legSwitchbacksHandled);
        Gauge("navigation_vertical_rejections", navigation.verticalRejections);
        Gauge("navigation_repaths", navigation.repaths);
        Gauge("navigation_stuck", navigation.stuckEvents);
        std::uint64_t online = 0, combat = 0, dead = 0, grouped = 0, battleground = 0, moving = 0, adaptiveFar = 0;
        std::map<std::uint32_t, std::uint64_t> maps;
        std::set<Map*> loadedMaps;
        for (Player* bot : sBotMgr->GetOnlineBots())
        {
            if (!bot || !bot->IsInWorld()) continue;
            ++online;
            combat += bot->IsInCombat();
            dead += !bot->IsAlive();
            grouped += bot->GetGroup() != nullptr;
            battleground += bot->InBattleground();
            moving += bot->isMoving();
            adaptiveFar += CoaBotScheduling::Far(bot);
            ++maps[bot->GetMapId()];
            loadedMaps.insert(bot->GetMap());
        }
        Gauge("online_in_world", online);
        Gauge("combat", combat);
        Gauge("dead", dead);
        Gauge("grouped", grouped);
        Gauge("battleground", battleground);
        Gauge("moving", moving);
        Gauge("adaptive_far", adaptiveFar);
        Gauge("maps_with_bots", maps.size());
        std::uint64_t creatures = 0, objects = 0;
        for (Map* map : loadedMaps)
        {
            creatures += map->GetObjectsStore().Size<Creature>();
            objects += map->GetObjectsStore().Size<GameObject>();
        }
        Gauge("loaded_creatures_on_bot_maps", creatures);
        Gauge("loaded_gameobjects_on_bot_maps", objects);
#ifdef COA_INTERLEAVED_WORLD_UPDATES
        std::uint64_t interleavedMaps = 0;
        for (Map* map : loadedMaps) interleavedMaps += map->UsesInterleavedWorldUpdates();
        Gauge("interleaved_world_maps", interleavedMaps);
#endif
        Gauge("db_character_queue", CharacterDatabase.QueueSize());
        Gauge("db_world_queue", WorldDatabase.QueueSize());
        Gauge("db_login_queue", LoginDatabase.QueueSize());
        auto const& world = WorldMetricsGlobal::Get();
        Gauge("quests_accepted", world.questsAccepted);
        Gauge("quests_turned_in", world.questsTurnedIn);
        Gauge("objective_progress", world.objectiveProgress);
        Gauge("movement_stuck", world.movementStuck);
        Gauge("tasks_completed", world.tasksCompleted);
        Gauge("tasks_failed", world.tasksFailed);
    }

    void Tick()
    {
        if (!Collecting()) return;
        auto now = std::chrono::steady_clock::now();
#ifndef COA_WORLD_PERIOD_OBSERVER
        if (haveTick)
            Observe(Metric::WorldPeriod, std::chrono::duration_cast<std::chrono::microseconds>(now - lastTick).count());
        haveTick = true;
        lastTick = now;
#endif
        if (now - lastDump >= std::chrono::seconds(10))
        {
            RequestDump();
            lastDump = now;
        }
    }

    void Shutdown()
    {
#ifdef COA_WORLD_PERIOD_OBSERVER
        WorldTickObserver::SetPeriodObserver(nullptr);
#endif
#ifdef COA_WORLD_PROCESSING_OBSERVER
        sWorldUpdateTime.SetProcessingObserver(nullptr);
        sWorldUpdateTime.SetPhaseObserver(nullptr);
#endif
        if (writer.valid()) writer.wait();
        RequestDump();
        if (writer.valid() && !writer.get())
            LOG_ERROR("module.coa-playerbots", "CoaBotPerf: final dump failed");
    }
}
