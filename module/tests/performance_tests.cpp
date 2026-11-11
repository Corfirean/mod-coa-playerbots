#include "performance/Histogram.h"
#include "performance/DeadlineQueue.h"
#include "performance/WorkQueue.h"
#include "performance/ObserverIndex.h"
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <vector>

namespace
{
    void Require(bool value)
    {
        if (!value) std::abort();
    }
}

int main()
{
    CoaBotScheduling::DamageWatch damage;
    Require(!damage.Update(100, 0) && !damage.Active(0));
    Require(!damage.Update(110, 100));
    Require(damage.Update(90, 200) && damage.Active(200));
    Require(damage.Active(3199) && !damage.Active(3200));
    Require(damage.Update(80, 3000) && damage.Active(5999) && !damage.Active(6000));
    Require(!damage.Update(100, 4000));
    CoaBotPerf::Histogram left, right;
    Require(left.Percentile(99) == 0);
    for (unsigned i = 1; i <= 100; ++i) (i <= 50 ? left : right).Observe(i * 1000);
    left.Merge(right);
    Require(left.count == 100 && left.sumUs == 5050000 && left.maxUs == 100000);
    Require(left.Percentile(50) >= 50000 && left.Percentile(50) <= 50250);
    Require(left.Percentile(95) >= 95000 && left.Percentile(95) <= 95250);
    Require(left.Percentile(99) >= 99000 && left.Percentile(99) <= 99250);
    Require(left.over[0] == 50 && left.over[1] == 0);
    left.Observe(10000000000ull);
    Require(left.Percentile(100) == 10000000000ull);
    Require(CoaBotPerf::Histogram::Index(1024000) == CoaBotPerf::Histogram::LinearBins);
    Require(CoaBotPerf::Histogram::Upper(CoaBotPerf::Histogram::Index(1023999)) >= 1023999);

    CoaBotScheduling::ObserverIndex index;
    index.Add(0, 10, 1, 0, 0, 0);
    Require(index.Query(0, 10, 1, 250, 0, 0).first);
    Require(!index.Query(0, 10, 1, 250.1f, 0, 0).first);
    Require(index.Query(0, 10, 1, 350, 0, 0).second);
    Require(!index.Query(0, 10, 1, 351, 0, 0).second);
    Require(!index.Query(1, 10, 1, 0, 0, 0).second);
    Require(!index.Query(0, 11, 1, 0, 0, 0).second);
    Require(!index.Query(0, 10, 2, 0, 0, 0).second);
    Require(index.Query(0, 10, 3, -.1f, -.1f, 0).first);
    Require(!index.Query(0, 10, 1, 0, 0, 351).second);
    index.Clear();
    Require(!index.Query(0, 10, 1, 0, 0, 0).second);

    CoaBotScheduling::DeadlineQueue queue;
    auto firstDeadline = CoaBotScheduling::NextDeadline(1500, 500, 42);
    auto lateDeadline = CoaBotScheduling::NextDeadline(4500, 500, 42);
    Require(firstDeadline > 1500 && firstDeadline <= 2000);
    Require(lateDeadline - firstDeadline == 3000);
    Require(firstDeadline % 500 == (42 * 37) % 500);
    std::vector<unsigned> classificationBuckets(1000);
    for (unsigned guid = 1; guid <= 3000; ++guid)
    {
        auto deadline = CoaBotScheduling::NextDeadline(10000, 1000, guid);
        Require(deadline > 10000 && deadline <= 11000);
        ++classificationBuckets[deadline % 1000];
        Require(CoaBotScheduling::NextDeadline(deadline, 1000, guid) == deadline + 1000);
    }
    Require(std::all_of(classificationBuckets.begin(), classificationBuckets.end(), [](unsigned count) { return count == 3; }));
    auto old = queue.Schedule(1, 100);
    auto fresh = queue.Schedule(1, 200);
    Require(fresh != old && queue.Size() == 1);
    Require(!queue.Current({100, 1, old}));
    Require(!queue.Pop(199));
    queue.Schedule(2, 200);
    Require(queue.Pop(200)->guid == 1);
    Require(queue.Pop(200)->guid == 2);
    Require(!queue.Pop(100000));
    auto sleeping = queue.Schedule(7, 1000);
    queue.Schedule(7, 125);
    Require(!queue.Current({1000, 7, sleeping}));
    Require(!queue.Pop(124));
    Require(queue.Pop(125)->guid == 7);
    Require(!queue.Pop(1000));
    for (unsigned i = 0; i < 100000; ++i) queue.Schedule(1, i);
    Require(queue.Size() == 1);
    queue.Cancel(1);
    Require(queue.Size() == 0);

    CoaBotScheduling::ProximityState proximity;
    Require(proximity.Update(0, false, false));
    Require(proximity.Update(9999, false, false));
    Require(!proximity.Update(10000, false, false));
    Require(!proximity.Update(10001, false, true));
    Require(proximity.Update(10002, true, true));
    Require(proximity.Update(20000, false, true));
    Require(proximity.Update(20001, false, false));
    Require(proximity.Update(29999, false, false));
    Require(!proximity.Update(30001, false, false));
    CoaBotScheduling::WorkQueue work;
    work.BeginFrame(0, 1000);
    Require(work.Acquire(1, 1, 3, 0));
    work.Charge(1000);
    Require(!work.Acquire(2, 1, 3, 10));
    Require(!work.Acquire(3, 1, 0, 20));
    Require(work.Size() == 2);
    work.BeginFrame(30, 1000);
    Require(!work.Acquire(2, 1, 3, 30));
    Require(work.Acquire(3, 1, 0, 30));
    Require(work.Acquire(2, 1, 3, 30));
    work.Charge(1000);
    Require(!work.Acquire(4, 1, 3, 40));
    for (unsigned t = 1000; t < 6000; t += 1000)
    {
        work.BeginFrame(t, 1);
        work.Charge(1);
        Require(!work.Acquire(4, 1, 3, t));
    }
    work.BeginFrame(6000, 1000);
    Require(!work.Acquire(5, 1, 0, 6000));
    Require(work.Acquire(4, 1, 3, 6000));
    Require(work.Acquire(5, 1, 0, 6000));
    work.Charge(1000);
    Require(!work.Acquire(6, 1, 0, 6100));
    work.BeginFrame(8200, 1000);
    Require(work.Size() == 0);
    work.BeginFrame(9000, 1);
    work.Charge(1);
    Require(!work.Acquire(10, 1, 0, 9000));
    work.DeferUntil(10, 9500);
    work.BeginFrame(9010, 1000);
    // The higher priority task cannot run until its next AI update. It must not stall this one.
    Require(work.Acquire(11, 1, 3, 9010));
    Require(work.Size() == 1 && work.OldestAge(9010) == 10);
    work.BeginFrame(9500, 1000);
    Require(!work.Acquire(12, 1, 3, 9500));
    Require(work.Acquire(10, 1, 0, 9500));
    Require(work.Acquire(12, 1, 3, 9500));
    work.Charge(1000);
    Require(!work.Acquire(13, 1, 0, 9600));
    work.DeferUntil(13, 10000);
    work.Cancel(13);
    work.BeginFrame(10000, 1000);
    Require(work.Size() == 0 && work.Acquire(14, 1, 3, 10000));

    for (auto [step, budget] : std::vector<std::pair<unsigned, unsigned>>{{10, 100}, {100, 300}, {250, 750}})
    {
        CoaBotScheduling::WorkQueue phasedWork;
        std::vector<std::uint64_t> nextRun(1001, 0);
        unsigned calls = 0, accepted = 0;
        for (std::uint64_t now = 0; now < 60000; now += step)
        {
            phasedWork.BeginFrame(now, budget);
            std::vector<unsigned> due;
            for (unsigned guid = 1; guid <= 1000; ++guid)
                if (nextRun[guid] <= now) due.push_back(guid);
            std::sort(due.begin(), due.end(), [&](unsigned a, unsigned b)
            {
                return nextRun[a] != nextRun[b] ? nextRun[a] < nextRun[b] : a < b;
            });
            std::stable_sort(due.begin(), due.end(), [&](unsigned a, unsigned b)
            {
                auto empty = CoaBotScheduling::WorkQueue::Order{2u, 0u, 0, 0};
                return phasedWork.PendingOrder(a, now).value_or(empty) < phasedWork.PendingOrder(b, now).value_or(empty);
            });
            for (unsigned guid : due)
            {
                ++calls;
                if (phasedWork.Acquire(guid, 1, 3, now))
                {
                    ++accepted;
                    phasedWork.Charge(1);
                }
                nextRun[guid] = CoaBotScheduling::NextDeadline(now, 500, guid);
                phasedWork.DeferUntil(guid, nextRun[guid]);
            }
            // The initial login burst may queue work; sufficient steady capacity must drain it.
            if (now >= 30000) Require(phasedWork.OldestAge(now) <= 500);
        }
        Require(phasedWork.Size() == 0 && accepted * 10 >= calls * 9);
    }
    std::cout << "Performance primitives passed\n";
}
