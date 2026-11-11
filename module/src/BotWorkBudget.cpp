#include "BotWorkBudget.h"
#include "BotScheduling.h"
#include "BotPerformance.h"
#include "performance/WorkQueue.h"
#include "Config.h"
#include "Group.h"
#include "Player.h"
#include <algorithm>
#include <array>
#include <mutex>
#include <thread>

namespace CoaBotScheduling
{
    namespace
    {
        std::mutex workMutex;
        bool budgetsEnabled = false;
        std::thread::id worldThread;
        std::array<WorkQueue, unsigned(Work::Count)> queues;
        std::array<std::uint64_t, unsigned(Work::Count)> budgets{3000, 2000, 4000};
        constexpr std::array<char const*, unsigned(Work::Count)> queueNames{"planner_waiting", "scan_waiting", "navigation_waiting"};
        constexpr std::array<char const*, unsigned(Work::Count)> ageNames{"planner_oldest_ms", "scan_oldest_ms", "navigation_oldest_ms"};
        constexpr std::array<char const*, unsigned(Work::Count)> chargeNames{"planner_charged_us", "scan_charged_us", "navigation_charged_us"};
        constexpr std::array<char const*, unsigned(Work::Count)> doneNames{"planner_completed", "scan_completed", "navigation_completed"};
        constexpr std::array<char const*, unsigned(Work::Count)> overNames{"planner_over_budget_updates", "scan_over_budget_updates", "navigation_over_budget_updates"};
        std::uint64_t Now()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    void ConfigureBudgets()
    {
        worldThread = std::this_thread::get_id();
        budgetsEnabled = sConfigMgr->GetOption<bool>("CoaBots.Budgets.Enable", false);
        budgets = {std::clamp(sConfigMgr->GetOption<unsigned>("CoaBots.Budgets.PlannerUs", 3000), 100u, 50000u),
            std::clamp(sConfigMgr->GetOption<unsigned>("CoaBots.Budgets.ScanUs", 2000), 100u, 50000u),
            std::clamp(sConfigMgr->GetOption<unsigned>("CoaBots.Budgets.NavigationUs", 4000), 100u, 50000u)};
        std::lock_guard<std::mutex> lock(workMutex);
        queues = {};
    }

    void BeginBudgetFrame()
    {
        if (!budgetsEnabled) return;
        std::lock_guard<std::mutex> lock(workMutex);
        for (unsigned i = 0; i < queues.size(); ++i)
        {
            CoaBotPerf::Gauge(queueNames[i], queues[i].Size());
            CoaBotPerf::Gauge(ageNames[i], queues[i].OldestAge(Now()));
            CoaBotPerf::Increment(chargeNames[i], queues[i].Used());
            if (queues[i].Used() > budgets[i]) CoaBotPerf::Increment(overNames[i]);
            queues[i].BeginFrame(Now(), budgets[i]);
        }
    }

    bool BudgetsEnabled() { return budgetsEnabled; }

    WorkOrder PendingWorkOrder(ObjectGuid guid, std::uint64_t nowMs)
    {
        WorkOrder best{2u, 0u, 0, 0};
        if (!budgetsEnabled) return best;
        if (!nowMs) nowMs = Now();
        std::lock_guard<std::mutex> lock(workMutex);
        for (auto const& queue : queues)
            if (auto order = queue.PendingOrder(guid.GetRawValue(), nowMs); order && *order < best) best = *order;
        return best;
    }

    void CancelWork(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(workMutex);
        for (auto& queue : queues) queue.Cancel(guid.GetRawValue());
    }

    void DeferWorkUntil(ObjectGuid guid, std::uint64_t deadlineMs)
    {
        if (!budgetsEnabled) return;
        std::lock_guard<std::mutex> lock(workMutex);
        for (auto& queue : queues) queue.DeferUntil(guid.GetRawValue(), deadlineMs);
    }

    void DeferWorkFor(ObjectGuid guid, std::uint32_t intervalMs)
    {
        DeferWorkUntil(guid, Now() + intervalMs);
    }

    WorkScope::WorkScope(Work work, Player* bot, std::uint64_t generation) : _work(work)
    {
        if (!budgetsEnabled || !bot || std::this_thread::get_id() != worldThread) return;
        if (bot->IsInCombat() || Protected(bot))
        {
            std::lock_guard<std::mutex> lock(workMutex);
            queues[unsigned(work)].Cancel(bot->GetGUID().GetRawValue());
            return;
        }
        unsigned priority = Protected(bot) ? 0u : Far(bot) ? 3u : 2u;
        {
            std::lock_guard<std::mutex> lock(workMutex);
            _accepted = queues[unsigned(work)].Acquire(bot->GetGUID().GetRawValue(), generation, priority, Now());
        }
        _charged = _accepted;
        if (_accepted) _started = std::chrono::steady_clock::now();
        else CoaBotPerf::Increment("work_deferred");
    }

    WorkScope::~WorkScope()
    {
        if (!_charged) return;
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - _started).count();
        std::lock_guard<std::mutex> lock(workMutex);
        queues[unsigned(_work)].Charge(us);
        CoaBotPerf::Increment(doneNames[unsigned(_work)], 1);
        if (std::uint64_t(us) > budgets[unsigned(_work)]) CoaBotPerf::Increment("single_work_over_budget");
    }
}
