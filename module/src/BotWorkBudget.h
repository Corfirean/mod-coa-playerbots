#ifndef COA_BOTS_WORK_BUDGET_H
#define COA_BOTS_WORK_BUDGET_H

#include "ObjectGuid.h"
#include <chrono>
#include <cstdint>
#include <tuple>

class Player;

namespace CoaBotScheduling
{
    enum class Work : unsigned { Planner, Scan, Navigation, Count };
    void ConfigureBudgets();
    bool BudgetsEnabled();
    using WorkOrder = std::tuple<unsigned, unsigned, std::uint64_t, std::uint64_t>;
    WorkOrder PendingWorkOrder(ObjectGuid guid, std::uint64_t nowMs = 0);
    void BeginBudgetFrame();
    void CancelWork(ObjectGuid guid);
    void DeferWorkUntil(ObjectGuid guid, std::uint64_t deadlineMs);
    void DeferWorkFor(ObjectGuid guid, std::uint32_t intervalMs);

    class WorkScope
    {
    public:
        WorkScope(Work work, Player* bot, std::uint64_t generation = 0);
        ~WorkScope();
        explicit operator bool() const { return _accepted; }
        WorkScope(WorkScope const&) = delete;
        WorkScope& operator=(WorkScope const&) = delete;
    private:
        Work _work;
        bool _accepted = true, _charged = false;
        std::chrono::steady_clock::time_point _started;
    };
}
#endif
