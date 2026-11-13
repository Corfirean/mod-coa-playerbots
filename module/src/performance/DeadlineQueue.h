#ifndef COA_BOTS_DEADLINE_QUEUE_H
#define COA_BOTS_DEADLINE_QUEUE_H

#include <cstdint>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>

namespace CoaBotScheduling
{
    struct DamageWatch
    {
        std::optional<std::uint32_t> health;
        std::uint64_t until = 0;
        bool Update(std::uint32_t value, std::uint64_t now)
        {
            bool dropped = health && value < *health;
            health = value;
            if (dropped) until = now + 3000;
            return dropped;
        }
        bool Active(std::uint64_t now) const { return now < until; }
    };

    inline std::uint64_t NextDeadline(std::uint64_t now, std::uint32_t interval, std::uint64_t guid)
    {
        std::uint64_t const phase = (guid * 37u) % interval;
        std::uint64_t next = now - now % interval + phase;
        return next > now ? next : next + interval;
    }

    class DeadlineQueue
    {
    public:
        struct Entry
        {
            std::uint64_t due, guid, generation;
            bool operator<(Entry const& other) const
            {
                return std::tie(due, guid, generation) < std::tie(other.due, other.guid, other.generation);
            }
        };

        std::uint64_t Schedule(std::uint64_t guid, std::uint64_t due)
        {
            Cancel(guid);
            Entry entry{due, guid, ++_generation};
            _entries.insert(entry);
            _byGuid.emplace(guid, entry);
            return entry.generation;
        }

        void Cancel(std::uint64_t guid)
        {
            auto it = _byGuid.find(guid);
            if (it == _byGuid.end()) return;
            _entries.erase(it->second);
            _byGuid.erase(it);
        }

        std::optional<Entry> Pop(std::uint64_t now)
        {
            if (_entries.empty() || _entries.begin()->due > now) return std::nullopt;
            Entry entry = *_entries.begin();
            Cancel(entry.guid);
            return entry;
        }

        bool Current(Entry const& entry) const
        {
            auto it = _byGuid.find(entry.guid);
            return it != _byGuid.end() && it->second.generation == entry.generation;
        }

        std::size_t Size() const { return _entries.size(); }

    private:
        std::uint64_t _generation = 0;
        std::set<Entry> _entries;
        std::unordered_map<std::uint64_t, Entry> _byGuid;
    };

    struct ProximityState
    {
        bool nearby = true;
        std::optional<std::uint64_t> outsideSince;

        bool Update(std::uint64_t now, bool insideEnter, bool insideLeave)
        {
            if (insideEnter)
            {
                nearby = true;
                outsideSince.reset();
            }
            else if (nearby)
            {
                if (insideLeave) outsideSince.reset();
                else
                {
                    if (!outsideSince) outsideSince = now;
                    if (now - *outsideSince >= 10000) nearby = false;
                }
            }
            return nearby;
        }
    };
}
#endif
