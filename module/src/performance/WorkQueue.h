#ifndef COA_BOTS_WORK_QUEUE_H
#define COA_BOTS_WORK_QUEUE_H

#include <cstdint>
#include <unordered_map>
#include <set>
#include <tuple>
#include <optional>

namespace CoaBotScheduling
{
    class WorkQueue
    {
    public:
        using Order = std::tuple<unsigned, unsigned, std::uint64_t, std::uint64_t>;
        struct Waiting { std::uint64_t generation, since, seen; unsigned priority; std::uint64_t readyAt = 0; };

        void BeginFrame(std::uint64_t now, std::uint64_t budgetUs)
        {
            _used = 0;
            _budget = budgetUs;
            for (auto it = _waiting.begin(); it != _waiting.end();)
            {
                if (now - it->second.seen > 2000)
                {
                    EraseIndices(it->first, it->second);
                    it = _waiting.erase(it);
                }
                else ++it;
            }
            while (!_due.empty() && _due.begin()->first <= now)
            {
                auto guid = _due.begin()->second;
                _due.erase(_due.begin());
                auto const& value = _waiting.at(guid);
                _priority.emplace(value.priority, value.since, guid);
                _age.emplace(value.since, guid);
            }
        }

        bool Acquire(std::uint64_t guid, std::uint64_t generation, unsigned priority, std::uint64_t now)
        {
            auto [it, fresh] = _waiting.try_emplace(guid, Waiting{generation, now, now, priority});
            if (!fresh)
            {
                EraseIndices(guid, it->second);
                if (it->second.generation != generation) it->second = {generation, now, now, priority};
                it->second.seen = now;
                it->second.priority = priority;
            }
            _priority.emplace(it->second.priority, it->second.since, guid);
            _age.emplace(it->second.since, guid);
            _waitingAge.emplace(it->second.since, guid);
            if (_used >= _budget) return false;
            std::uint64_t best = now - _age.begin()->first >= 5000 ? _age.begin()->second : std::get<2>(*_priority.begin());
            if (best != guid) return false;
            Cancel(guid);
            return true;
        }

        void Charge(std::uint64_t us) { _used += us; }
        // A caller that retries on its next AI deadline must not block ready callers in between.
        void DeferUntil(std::uint64_t guid, std::uint64_t deadline)
        {
            auto it = _waiting.find(guid);
            if (it == _waiting.end()) return;
            auto& value = it->second;
            _priority.erase({value.priority, value.since, guid});
            _age.erase({value.since, guid});
            _due.erase({value.readyAt, guid});
            value.readyAt = deadline;
            _due.emplace(deadline, guid);
        }
        void Cancel(std::uint64_t guid)
        {
            auto it = _waiting.find(guid);
            if (it == _waiting.end()) return;
            EraseIndices(guid, it->second);
            _waiting.erase(it);
        }
        std::size_t Size() const { return _waiting.size(); }
        std::uint64_t Used() const { return _used; }
        std::optional<Order> PendingOrder(std::uint64_t guid, std::uint64_t now) const
        {
            auto it = _waiting.find(guid);
            if (it == _waiting.end()) return std::nullopt;
            auto const& value = it->second;
            bool aged = now >= value.since && now - value.since >= 5000;
            return Order{aged ? 0u : 1u, aged ? 0u : value.priority, value.since, guid};
        }
        std::uint64_t OldestAge(std::uint64_t now) const
        {
            return _waitingAge.empty() ? 0 : now - _waitingAge.begin()->first;
        }
    private:
        void EraseIndices(std::uint64_t guid, Waiting const& value)
        {
            _priority.erase({value.priority, value.since, guid});
            _age.erase({value.since, guid});
            _waitingAge.erase({value.since, guid});
            _due.erase({value.readyAt, guid});
        }
        std::set<std::tuple<unsigned, std::uint64_t, std::uint64_t>> _priority;
        std::set<std::pair<std::uint64_t, std::uint64_t>> _age;
        std::set<std::pair<std::uint64_t, std::uint64_t>> _waitingAge, _due;
        std::uint64_t _used = 0, _budget = 0;
        std::unordered_map<std::uint64_t, Waiting> _waiting;
    };
}
#endif
