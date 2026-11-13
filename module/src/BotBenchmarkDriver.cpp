#include "BotBenchmarkDriver.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "BotMovement.h"
#include "BotPerformance.h"
#include "BotScheduling.h"
#include "BotBattlegroundFill.h"
#include "BotLfgFill.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "TemporarySummon.h"
#include "DBCStores.h"
#include "Group.h"
#include "LFGMgr.h"
#include "Map.h"
#include "Player.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace CoaBotBenchmark
{
    namespace
    {
        std::string scenario;
        uint32 expected = 0, tickAge = 0, readyAge = 0, started = 0, rounds = 0;
        bool initialized = false;
        std::vector<ObjectGuid> actors, enemies;
        uint32 anchorMap = 0;
        float anchorX = 0, anchorY = 0, anchorZ = 0;
        uint32 dungeonRequests = 0, bgRequests = 0;

        bool WorldActor(Player* bot)
        {
            return bot && bot->IsInWorld() && bot->IsAlive() && !bot->IsBeingTeleported() &&
                !bot->IsInFlight() && bot->GetMap() && !bot->GetMap()->IsDungeon() && !bot->InBattleground();
        }

        void Initialize(std::vector<Player*> const& bots)
        {
            for (Player* bot : bots)
            {
                if (!WorldActor(bot)) continue;
                if (scenario == "cluster" || scenario == "navigation" || scenario == "observer")
                {
                    if (bot->GetGroup()) continue;
                    actors.push_back(bot->GetGUID());
                    bot->SetGameMaster(true);
                    bot->SetGMVisible(false);
                    BotAI::SetManualCommand(bot->GetGUID(), BotManualCommand::Stay);
                    if (scenario == "navigation") BotAI::SetSuspended(bot->GetGUID(), true);
                    if (actors.size() >= (scenario == "cluster" ? 300u : scenario == "navigation" ? 10u : 1u)) break;
                }
            }
            if (scenario == "dense-combat")
            {
                for (Player* bot : bots)
                    if (WorldActor(bot) && !bot->GetGroup() && bot->GetLevel() >= 60)
                    {
                        actors.push_back(bot->GetGUID());
                        BotAI::SetManualCommand(bot->GetGUID(), BotManualCommand::Stay);
                        if (actors.size() >= 200) break;
                    }
                enemies.resize(actors.size());
            }
            if (scenario == "observer" && !actors.empty()) CoaBotScheduling::AddSyntheticObserver(actors.front());
            if (scenario == "group-content")
            {
                for (Player* bot : bots)
                {
                    if (!WorldActor(bot) || bot->GetGroup() || bot->GetLevel() != 80) continue;
                    if (dungeonRequests < 5 && BotAI::GetRole(bot->GetGUID()) == BotRole::Tank)
                    {
                        auto const locks = sLFGMgr->GetLockedDungeons(bot->GetGUID());
                        for (uint32 id = 1; id < sLFGDungeonStore.GetNumRows(); ++id)
                        {
                            LFGDungeonEntry const* entry = sLFGDungeonStore.LookupEntry(id);
                            if (!entry || entry->TypeID != 1 || entry->MinLevel > bot->GetLevel() ||
                                entry->MaxLevel < bot->GetLevel() || locks.count(id)) continue;
                            lfg::LfgDungeonSet dungeons;
                            dungeons.insert(id);
                            sLFGMgr->JoinLfg(bot, lfg::PLAYER_ROLE_TANK, dungeons, "");
                            BotLfgFill::WatchForProposal(bot);
                            ++dungeonRequests;
                            break;
                        }
                    }
                    else if (bgRequests < 2 && BotAI::GetRole(bot->GetGUID()) == BotRole::Dps)
                    {
                        if (BotBGFill::JoinBotToBattlegroundQueue(bot, bgRequests ? 2 : 3, nullptr)) ++bgRequests;
                    }
                    if (dungeonRequests >= 5 && bgRequests >= 2) break;
                }
            }
            initialized = true;
            CoaBotPerf::Gauge("benchmark_ready", 1);
            CoaBotPerf::Gauge("benchmark_actors", actors.size());
            CoaBotPerf::Gauge("benchmark_dungeon_requests", dungeonRequests);
            CoaBotPerf::Gauge("benchmark_bg_requests", bgRequests);
        }
    }

    void Configure()
    {
        scenario = sConfigMgr->GetOption<std::string>("CoaBots.Benchmark.Scenario", "distributed");
        if (scenario == "distributed" || scenario == "login" || scenario == "save")
        {
            scenario.clear();
            return;
        }
        std::filesystem::path root = sConfigMgr->GetOption<std::string>("CoaBots.Benchmark.Root", "");
        if (root.empty() || !std::filesystem::is_regular_file(root / ".disposable-benchmark"))
        {
            CoaBotPerf::Gauge("benchmark_setup_error", 1);
            scenario.clear();
            return;
        }
        expected = sConfigMgr->GetOption<uint32>("CoaBots.AutoLogin.MaxCount", 0);
        anchorMap = sConfigMgr->GetOption<uint32>("CoaBots.Benchmark.AnchorMap", 0);
        anchorX = sConfigMgr->GetOption<float>("CoaBots.Benchmark.AnchorX", 0);
        anchorY = sConfigMgr->GetOption<float>("CoaBots.Benchmark.AnchorY", 0);
        anchorZ = sConfigMgr->GetOption<float>("CoaBots.Benchmark.AnchorZ", 0);
        actors.clear();
        enemies.clear();
        tickAge = readyAge = started = rounds = dungeonRequests = bgRequests = 0;
        initialized = false;
    }

    void Update(uint32 diff)
    {
        if (scenario.empty()) return;
        tickAge += diff;
        started += diff;
        if (tickAge < 1000) return;
        tickAge = 0;
        auto bots = sBotMgr->GetOnlineBots();
        if (bots.size() < expected) return;
        std::sort(bots.begin(), bots.end(), [](Player* a, Player* b) { return a->GetGUID() < b->GetGUID(); });
        if (!initialized)
        {
            readyAge += 1000;
            if (readyAge < 5000) return;
            Initialize(bots);
        }

        CoaBotPerf::Gauge("benchmark_ready", 1);
        CoaBotPerf::Gauge("benchmark_actors", actors.size());
        uint32 combat = 0, clustered = 0;
        std::set<std::pair<uint32, uint32>> dungeons, battlegrounds;
        for (Player* bot : bots)
        {
            if (!bot->IsInWorld()) continue;
            combat += bot->IsInCombat();
            if (bot->InBattleground()) battlegrounds.emplace(bot->GetMapId(), bot->GetInstanceId());
            else if (bot->GetMap() && bot->GetMap()->IsDungeon())
            {
                dungeons.emplace(bot->GetMapId(), bot->GetInstanceId());
                if (scenario == "group-content" && bot->GetGroup() &&
                    !sBotMgr->IsAutoDungeonModeEnabled(bot->GetGroup()->GetLeaderGUID()))
                    sBotMgr->SetAutoDungeonMode(bot->GetGroup()->GetLeaderGUID(), true);
            }
            float dx = bot->GetPositionX() - anchorX, dy = bot->GetPositionY() - anchorY;
            clustered += bot->GetMapId() == anchorMap && dx * dx + dy * dy <= 150.0f * 150.0f;
        }
        CoaBotPerf::Gauge("benchmark_combat_pct", bots.empty() ? 0 : 100 * combat / bots.size());
        CoaBotPerf::Gauge("benchmark_clustered", clustered);
        CoaBotPerf::Gauge("benchmark_dungeons", dungeons.size());
        CoaBotPerf::Gauge("benchmark_battlegrounds", battlegrounds.size());
        unsigned denseCombat = 0;
        for (ObjectGuid guid : actors)
            if (Player* bot = sBotMgr->FindBotPlayer(guid.GetCounter()); bot && bot->IsInCombat()) ++denseCombat;
        CoaBotPerf::Gauge("benchmark_dense_combatants", denseCombat);
        ++rounds;

        if (scenario == "combat")
        {
            unsigned attempted = 0;
            for (Player* bot : bots)
            {
                if (!WorldActor(bot) || bot->IsInCombat()) continue;
                sBotMgr->AttackNearestHostile(bot->GetGUID().GetCounter(), 40, nullptr);
                if (++attempted >= bots.size() / 5) break;
            }
        }
        else if (scenario == "dense-combat" && rounds % 20 == 1)
        {
            for (unsigned i = 0; i < actors.size(); ++i)
            {
                Player* bot = sBotMgr->FindBotPlayer(actors[i].GetCounter());
                if (!WorldActor(bot)) continue;
                float angle = float(i + 1) * 2.399963f;
                float radius = 7.0f * std::sqrt(float(i + 1));
                float dx = bot->GetPositionX() - anchorX, dy = bot->GetPositionY() - anchorY;
                if (bot->GetMapId() != anchorMap || dx * dx + dy * dy > 150.0f * 150.0f)
                {
                    bot->TeleportTo(anchorMap, anchorX + std::cos(angle) * radius, anchorY + std::sin(angle) * radius, anchorZ + 1, 0);
                    sBotMgr->QueueTeleportAck(bot->GetSession());
                    continue;
                }
                Creature* enemy = bot->GetMap()->GetCreature(enemies[i]);
                if (!enemy || !enemy->IsAlive())
                {
                    enemy = bot->SummonCreature(36, bot->GetPositionX() + 4, bot->GetPositionY(), bot->GetPositionZ(), 0,
                        TEMPSUMMON_TIMED_DESPAWN, 3600000);
                    if (!enemy) { CoaBotPerf::Increment("benchmark_spawn_errors"); continue; }
                    enemies[i] = enemy->GetGUID();
                    enemy->SetFaction(14);
                    enemy->SetLevel(55);
                    enemy->SetMaxHealth(10000000);
                    enemy->SetHealth(10000000);
                    enemy->SetReactState(REACT_AGGRESSIVE);
                }
                if (enemy->AI()) enemy->AI()->AttackStart(bot);
                bot->Attack(enemy, true);
            }
        }
        else if (scenario == "group-content" && rounds % 20 == 1)
        {
            unsigned queuedTanks = 0;
            for (Player* bot : bots)
                if (WorldActor(bot) && !bot->GetGroup() && BotAI::GetRole(bot->GetGUID()) == BotRole::Tank &&
                    sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE) ++queuedTanks;
            unsigned requested = 0;
            for (Player* bot : bots)
            {
                if (!WorldActor(bot) || bot->GetGroup() || bot->GetLevel() != 80 || bot->IsInCombat() ||
                    bot->InBattlegroundQueue()) continue;
                if (dungeons.size() + queuedTanks + requested < 5 && BotAI::GetRole(bot->GetGUID()) == BotRole::Tank &&
                    sLFGMgr->GetState(bot->GetGUID()) == lfg::LFG_STATE_NONE)
                {
                    auto const locks = sLFGMgr->GetLockedDungeons(bot->GetGUID());
                    for (uint32 id = 1; id < sLFGDungeonStore.GetNumRows(); ++id)
                    {
                        auto const* entry = sLFGDungeonStore.LookupEntry(id);
                        if (!entry || entry->TypeID != 1 || entry->MinLevel > bot->GetLevel() ||
                            entry->MaxLevel < bot->GetLevel() || locks.count(id)) continue;
                        lfg::LfgDungeonSet selected;
                        selected.insert(id);
                        sLFGMgr->JoinLfg(bot, lfg::PLAYER_ROLE_TANK, selected, "");
                        BotLfgFill::WatchForProposal(bot);
                        ++requested;
                        CoaBotPerf::Increment("benchmark_dungeon_requeues");
                        break;
                    }
                }
            }
            if (battlegrounds.size() < 2)
            {
                bool ab = false, wsg = false;
                for (auto const& location : battlegrounds)
                {
                    ab |= location.first == 529;
                    wsg |= location.first == 489;
                }
                unsigned submitted = 0;
                for (Player* bot : bots)
                    if (WorldActor(bot) && !bot->GetGroup() && bot->GetLevel() == 80 && !bot->IsInCombat() &&
                        !bot->InBattlegroundQueue() && BotAI::GetRole(bot->GetGUID()) == BotRole::Dps)
                    {
                        uint32 type = !ab ? 3 : !wsg ? 2 : 0;
                        if (!type) break;
                        if (BotBGFill::JoinBotToBattlegroundQueue(bot, type, nullptr))
                        {
                            if (type == 3) ab = true; else wsg = true;
                            CoaBotPerf::Increment("benchmark_bg_requeues");
                            if (++submitted >= 2) break;
                        }
                    }
            }
        }
        else if (scenario == "cluster" && rounds % 20 == 1)
        {
            unsigned index = 0;
            for (ObjectGuid guid : actors)
                if (Player* bot = sBotMgr->FindBotPlayer(guid.GetCounter()); WorldActor(bot))
                {
                    float angle = float(++index) * 2.399963f;
                    float radius = 3.0f * std::sqrt(float(index));
                    bot->TeleportTo(anchorMap, anchorX + std::cos(angle) * radius, anchorY + std::sin(angle) * radius, anchorZ + 1, 0);
                    sBotMgr->QueueTeleportAck(bot->GetSession());
                }
        }
        else if (scenario == "observer" && !actors.empty())
        {
            Player* observer = sBotMgr->FindBotPlayer(actors.front().GetCounter());
            Player* target = bots[(rounds / 20) % bots.size()];
            if (observer && WorldActor(target) && target != observer && rounds % 20 == 1)
            {
                observer->TeleportTo(target->GetMapId(), target->GetPositionX() + 10, target->GetPositionY(), target->GetPositionZ() + 1, 0);
                sBotMgr->QueueTeleportAck(observer->GetSession());
                CoaBotPerf::Increment("benchmark_observer_visits");
            }
            if (target != observer)
            {
                BotAI::SetManualCommand(target->GetGUID(), rounds % 2 ? BotManualCommand::Stay : BotManualCommand::Follow);
                CoaBotPerf::Increment("benchmark_commands");
            }
            CoaBotPerf::Gauge("benchmark_synthetic_observer", 1);
        }
        else if (scenario == "navigation")
        {
            for (ObjectGuid guid : actors)
                if (Player* bot = sBotMgr->FindBotPlayer(guid.GetCounter()); bot && bot->IsInWorld())
                {
                    if (!bot->IsAlive())
                    {
                        bot->ResurrectPlayer(0.5f, false);
                        bot->SpawnCorpseBones();
                        bot->SetGameMaster(true);
                        CoaBotPerf::Increment("benchmark_revives");
                        continue;
                    }
                    if (!WorldActor(bot)) continue;
                    if (rounds % 60 == 0)
                    {
                        bot->SetGameMaster(false);
                        Unit::Kill(bot, bot);
                        CoaBotPerf::Increment("benchmark_death_cycles");
                        continue;
                    }
                    if (rounds % 40 == 0)
                        for (Player* target : bots)
                            if (WorldActor(target) && target->GetMapId() != bot->GetMapId())
                            {
                                bot->TeleportTo(target->GetMapId(), target->GetPositionX(), target->GetPositionY(), target->GetPositionZ() + 1, 0);
                                sBotMgr->QueueTeleportAck(bot->GetSession());
                                CoaBotPerf::Increment("benchmark_map_transfers");
                                break;
                            }
                    if (bot->IsBeingTeleported()) continue;
                    BotMovement::Update(bot, 1000);
                    float offset = rounds % 100 >= 40 && rounds % 100 < 80 ? 99999.0f : rounds % 20 < 10 ? 15.0f : -15.0f;
                    BotMovement::Navigate(bot, MoveOwner::Quest, 100000 + rounds / 10,
                        bot->GetPositionX() + offset, bot->GetPositionY() + 5, bot->GetPositionZ(), 2);
                    if (rounds % 30 == 0) BotMovement::ForceStopOwner(bot, MoveOwner::Quest);
                    CoaBotPerf::Increment("benchmark_navigation_attempts");
                }
        }
    }
}
