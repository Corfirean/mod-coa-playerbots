#include "BotScheduling.h"
#include "BotPerformance.h"
#include "BotWorkBudget.h"
#include "BotAI.h"
#include "performance/DeadlineQueue.h"
#include "performance/ObserverIndex.h"
#include "Config.h"
#include "Group.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "WorldSession.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace CoaBotScheduling
{
    namespace
    {
        struct Record
        {
            ProximityState proximity;
            DamageWatch damage;
            bool recentDamage = false;
            ObjectGuid target;
            uint64 lastRun = 0, nextProximity = 0;
            uint32 map = 0, instance = 0, phase = 0;
            bool combat = false, groupCombat = false, alive = false, maintenance = true, forced = false, protectedGroup = false;
            uint64 commandAt = 0;
            Group const* group = nullptr;
        };
        ObserverIndex observers;
        std::set<Group const*> humanGroups;
        std::unordered_set<uint64> controlled, syntheticObservers;
        std::unordered_map<uint64, Record> records;
        std::mutex pendingMutex;
        struct Notification { bool maintenance = false; uint64 commandAt = 0; };
        std::unordered_map<uint64, Notification> pending;
        DeadlineQueue deadlines;
        bool enabled = false, adaptive = false;
        uint64 lastIndex = 0;
        uint32 combatInterval = 100, idleInterval = 200, soloInterval = 500;
        uint32 farIdleInterval = 0;
        bool damageWake = true;

        uint64 Now()
        {
            return uint64(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        void RebuildIndex(uint64 now, std::vector<WorldSession*> const& sessions)
        {
            observers.Clear();
            humanGroups.clear();
            controlled.clear();
            for (WorldSession* session : sessions)
                if (Player* bot = session->GetPlayer()) controlled.insert(bot->GetGUID().GetRawValue());
            for (auto const& [guid, player] : ObjectAccessor::GetPlayers())
            {
                if (!player || !player->IsInWorld() || !player->GetSession() ||
                    (player->GetSession()->IsBot() || controlled.count(guid.GetRawValue())) &&
                    !syntheticObservers.count(guid.GetRawValue())) continue;
                observers.Add(player->GetMapId(), player->GetInstanceId(), player->GetPhaseMask(),
                    player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
                if (player->GetGroup()) humanGroups.insert(player->GetGroup());
            }
            lastIndex = now;
        }

        void Classify(Player* bot, Record& record, uint64 now)
        {
            auto const [enter, leave] = observers.Query(bot->GetMapId(), bot->GetInstanceId(), bot->GetPhaseMask(),
                bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
            bool wasNear = record.proximity.nearby;
            record.proximity.Update(now, enter, leave);
            record.protectedGroup = bot->GetGroup() && humanGroups.count(bot->GetGroup());
            if (Group* group = bot->GetGroup(); group && !record.protectedGroup)
                for (GroupReference const* member = group->GetFirstMember(); member; member = member->next())
                    if (Player* player = member->GetSource(); player && player->GetSession() &&
                        !player->GetSession()->IsBot() && !controlled.count(player->GetGUID().GetRawValue()))
                    {
                        record.protectedGroup = true;
                        break;
                    }
            if (!wasNear && record.proximity.nearby) record.forced = true;
            record.nextProximity = NextDeadline(now, 1000, bot->GetGUID().GetRawValue());
        }
    }

    void AddSyntheticObserver(ObjectGuid guid)
    {
        syntheticObservers.insert(guid.GetRawValue());
        lastIndex = 0;
    }

    void Configure()
    {
        enabled = sConfigMgr->GetOption<bool>("CoaBots.Scheduling.Enable", false);
        adaptive = enabled && sConfigMgr->GetOption<bool>("CoaBots.Scheduling.Adaptive", false);
        combatInterval = std::clamp(sConfigMgr->GetOption<uint32>("CoaBots.AI.CombatIntervalMs", 100), 50u, 1000u);
        soloInterval = std::clamp(sConfigMgr->GetOption<uint32>("CoaBots.Scheduling.SoloIntervalMs", 500), 100u, 1000u);
        idleInterval = std::clamp(sConfigMgr->GetOption<uint32>("CoaBots.Scheduling.IdleIntervalMs", 200), 100u, 500u);
        farIdleInterval = sConfigMgr->GetOption<uint32>("CoaBots.Scheduling.FarIdleIntervalMs", 0);
        if (farIdleInterval) farIdleInterval = std::clamp(farIdleInterval, 500u, 2000u);
        damageWake = sConfigMgr->GetOption<bool>("CoaBots.Scheduling.DamageWake", true);
        Clear();
    }

    bool Enabled() { return enabled; }

    bool Protected(Player const* bot)
    {
        if (!bot) return false;
        auto it = records.find(bot->GetGUID().GetRawValue());
        return bot->InBattleground() || (bot->GetMap() && bot->GetMap()->IsDungeon()) ||
            (it != records.end() && (it->second.protectedGroup || it->second.recentDamage));
    }

    bool Far(Player const* bot)
    {
        if (!adaptive || !bot || bot->IsInCombat() || bot->InBattleground() || (bot->GetMap() && bot->GetMap()->IsDungeon()))
            return false;
        auto it = records.find(bot->GetGUID().GetRawValue());
        return it != records.end() && !it->second.proximity.nearby && !it->second.protectedGroup && !it->second.recentDamage;
    }

    uint32 ScanInterval(Player const* bot) { return Far(bot) ? 1500 : 500; }
    uint32 MaintenanceInterval(Player const* bot) { return Far(bot) ? 60000 : 30000; }

    void Notify(ObjectGuid guid, bool maintenance)
    {
        if (!enabled) return;
        std::lock_guard<std::mutex> lock(pendingMutex);
        if (pending.size() < 10000 || pending.count(guid.GetRawValue()))
        {
            auto& event = pending[guid.GetRawValue()];
            event.maintenance |= maintenance;
            if (!maintenance && !event.commandAt) event.commandAt = Now();
        }
    }

    bool ConsumeMaintenance(ObjectGuid guid)
    {
        auto it = records.find(guid.GetRawValue());
        if (it == records.end()) return false;
        bool value = it->second.maintenance;
        it->second.maintenance = false;
        return value;
    }

    void Forget(ObjectGuid guid)
    {
        records.erase(guid.GetRawValue());
        if (syntheticObservers.erase(guid.GetRawValue())) lastIndex = 0;
        deadlines.Cancel(guid.GetRawValue());
        std::lock_guard<std::mutex> lock(pendingMutex);
        pending.erase(guid.GetRawValue());
    }

    void Clear()
    {
        records.clear();
        deadlines = {};
        observers.Clear();
        humanGroups.clear();
        controlled.clear();
        syntheticObservers.clear();
        lastIndex = 0;
        std::lock_guard<std::mutex> lock(pendingMutex);
        pending.clear();
    }

    void Update(std::vector<WorldSession*> const& sessions, std::function<void(Player*, uint32)> const& dispatch)
    {
        uint64 now = Now();
        if (!lastIndex || now - lastIndex >= 1000) RebuildIndex(now, sessions);
        std::unordered_map<uint64, Notification> events;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            events.swap(pending);
        }
        std::unordered_map<Group const*, bool> groupCombat;
        for (WorldSession* session : sessions)
            if (Player* bot = session->GetPlayer(); bot && bot->GetGroup())
                groupCombat[bot->GetGroup()] |= bot->IsInCombat();
        for (WorldSession* session : sessions)
        {
            Player* bot = session->GetPlayer();
            if (!bot || !bot->IsInWorld()) continue;
            uint64 guid = bot->GetGUID().GetRawValue();
            auto [it, fresh] = records.try_emplace(guid);
            Record& record = it->second;
            if (fresh)
            {
                record.lastRun = now;
                record.nextProximity = now + guid % 1000;
                deadlines.Schedule(guid, now + guid % idleInterval);
            }
            auto event = events.find(guid);
            if (event != events.end())
            {
                record.forced = true;
                record.maintenance |= event->second.maintenance;
                record.commandAt = event->second.commandAt;
            }
            bool damaged = damageWake && record.damage.Update(bot->GetHealth(), now);
            bool recentDamage = damageWake && record.damage.Active(now);
            bool changed = damaged || record.recentDamage != recentDamage || record.target != bot->GetTarget() ||
                record.map != bot->GetMapId() || record.instance != bot->GetInstanceId() ||
                record.phase != bot->GetPhaseMask() || record.combat != bot->IsInCombat() ||
                record.alive != bot->IsAlive() || record.group != bot->GetGroup() ||
                record.groupCombat != (bot->GetGroup() && groupCombat[bot->GetGroup()]);
            if (changed)
            {
                record.recentDamage = recentDamage;
                record.target = bot->GetTarget();
                record.map = bot->GetMapId();
                record.instance = bot->GetInstanceId();
                record.phase = bot->GetPhaseMask();
                record.combat = bot->IsInCombat();
                record.alive = bot->IsAlive();
                record.group = bot->GetGroup();
                record.groupCombat = bot->GetGroup() && groupCombat[bot->GetGroup()];
                record.nextProximity = now;
            }
            if (now >= record.nextProximity) Classify(bot, record, now);
            if (changed || record.forced) deadlines.Schedule(guid, now);
        }
        struct ReadyUpdate
        {
            DeadlineQueue::Entry due;
            bool background = true;
            WorkOrder workOrder{2u, 0u, 0, 0};
        };
        std::vector<ReadyUpdate> ready;
        while (auto due = deadlines.Pop(now))
        {
            ReadyUpdate update{*due};
            if (BudgetsEnabled())
            {
                if (Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(due->guid)))
                    update.background = !bot->IsInCombat() && !Protected(bot);
                update.workOrder = PendingWorkOrder(ObjectGuid(due->guid), now);
            }
            ready.push_back(update);
        }
        if (BudgetsEnabled())
            std::stable_sort(ready.begin(), ready.end(), [](ReadyUpdate const& a, ReadyUpdate const& b)
            {
                return std::tie(a.background, a.workOrder) < std::tie(b.background, b.workOrder);
            });
        for (auto const& update : ready)
        {
            auto const* due = &update.due;
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(due->guid));
            auto it = records.find(due->guid);
            if (it == records.end()) continue;
            if (!bot || !bot->IsInWorld())
            {
                deadlines.Schedule(due->guid, now + idleInterval);
                continue;
            }
            Record& record = it->second;
            uint32 elapsed = uint32(std::min<uint64>(now - record.lastRun, UINT32_MAX));
            record.lastRun = now;
            record.forced = false;
            if (!update.background) CancelWork(bot->GetGUID());
            dispatch(bot, elapsed);
            uint32 interval = !bot->IsAlive() || bot->IsInCombat() || record.groupCombat || bot->InBattleground() || record.protectedGroup || record.recentDamage ||
                (bot->GetGroup() && BotAI::GetRole(bot->GetGUID()) == BotRole::Healer) ||
                (bot->GetMap() && bot->GetMap()->IsDungeon()) ? combatInterval : bot->GetGroup() ? idleInterval : soloInterval;
            if (farIdleInterval && Far(bot) && !bot->GetGroup() && !record.groupCombat)
                interval = farIdleInterval;
            auto next = NextDeadline(now, interval, due->guid);
            deadlines.Schedule(due->guid, next);
            DeferWorkUntil(bot->GetGUID(), next);
        }
        CoaBotPerf::Gauge("ai_deadlines", deadlines.Size());
        CoaBotPerf::Gauge("far_idle_interval_ms", adaptive ? farIdleInterval : 0);
    }
}
