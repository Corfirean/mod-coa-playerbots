/*
 * mod-coa-playerbots
 *
 * Per-bot, short-lived memory of things that just failed: a mob the bot could not reach, a
 * spawn cluster where nothing lived, a quest giver that was not where the spawn table said, a
 * quest that turned out to be impossible. Without it every open-world loop in this module had the
 * same shape of bug -- walk there, fail, give up, and two seconds later pick the exact same place
 * again because nothing remembered the failure (the gathering "8040 of 8042 casts failed on one
 * node" incident in AGENTS.md is the canonical example).
 *
 * Entries expire on their own. A thing that fails again while still remembered earns a strike,
 * and every strike stretches its next expiry, so one transient failure costs a minute while a
 * genuinely broken spot stays out of the rotation for a long time.
 *
 * Pure data structure: time is passed in, nothing touches the core, so it is unit-tested
 * standalone (module/tests).
 */

#ifndef COA_PLAYERBOTS_FAILURE_MEMORY_H
#define COA_PLAYERBOTS_FAILURE_MEMORY_H

#include "Define.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <unordered_map>

enum class FailKind : uint8
{
    Target,     // a live creature/GO guid
    Spawn,      // a static spawn id used as a destination
    Cluster,    // an objective area (SpawnCluster id)
    GameObject, // a live GO guid used for an objective
    Npc,        // a quest giver / ender spawn id
    Quest,      // a quest id: objective work set aside, the planner leaves it alone
    Hub,        // a quest hub id
    TurnIn,     // a quest id whose reward could not be taken (bags full): hand-in retried later
    DeadEnd,    // a quest id noted as a dead end (logging / debug only; planning asks Workable)
    Unworkable, // a quest id a handler tried and found this bot cannot do (talk gave no credit,
                // trigger gave nothing, the quest item is gone): no work on it, a dead end
    Count,
};

struct FailureEntry
{
    uint32 expiresAt = 0;
    uint8 reason = 0;
    uint8 strikes = 0;
};

class FailureMemory
{
public:
    // Strikes stretch the ttl up to this multiple.
    static constexpr uint32 MAX_STRIKE_MULTIPLIER = 4;

    void Remember(FailKind kind, uint64 id, uint32 now, uint32 ttlMs, uint8 reason)
    {
        auto& map = _entries[size_t(kind)];
        FailureEntry& entry = map[id];
        entry.strikes = entry.expiresAt > now ? uint8(std::min<uint32>(255, entry.strikes + 1u)) : uint8(1);
        entry.expiresAt = now + ttlMs * std::min<uint32>(entry.strikes, MAX_STRIKE_MULTIPLIER);
        entry.reason = reason;

        if (map.size() > PRUNE_THRESHOLD)
            Prune(now);
    }

    bool Has(FailKind kind, uint64 id, uint32 now) const
    {
        auto const& map = _entries[size_t(kind)];
        auto itr = map.find(id);
        return itr != map.end() && itr->second.expiresAt > now;
    }

    uint32 RemainingMs(FailKind kind, uint64 id, uint32 now) const
    {
        auto const& map = _entries[size_t(kind)];
        auto itr = map.find(id);
        return itr != map.end() && itr->second.expiresAt > now ? itr->second.expiresAt - now : 0;
    }

    // Strikes still count while the entry is live; an expired entry has none.
    uint8 Strikes(FailKind kind, uint64 id, uint32 now) const
    {
        auto const& map = _entries[size_t(kind)];
        auto itr = map.find(id);
        return itr != map.end() && itr->second.expiresAt > now ? itr->second.strikes : uint8(0);
    }

    void Forget(FailKind kind, uint64 id)
    {
        _entries[size_t(kind)].erase(id);
    }

    void Prune(uint32 now)
    {
        for (auto& map : _entries)
            for (auto itr = map.begin(); itr != map.end();)
                itr = itr->second.expiresAt <= now ? map.erase(itr) : std::next(itr);
    }

    void Clear()
    {
        for (auto& map : _entries)
            map.clear();
    }

    size_t Size() const
    {
        size_t total = 0;
        for (auto const& map : _entries)
            total += map.size();
        return total;
    }

    // Visits every live entry: fn(FailKind, uint64 id, uint32 remainingMs, uint8 reason, uint8 strikes).
    template <typename Fn>
    void ForEachLive(uint32 now, Fn&& fn) const
    {
        for (size_t kind = 0; kind < _entries.size(); ++kind)
            for (auto const& [id, entry] : _entries[kind])
                if (entry.expiresAt > now)
                    fn(FailKind(kind), id, entry.expiresAt - now, entry.reason, entry.strikes);
    }

private:
    // A bot remembers a handful of things at a time; pruning on write once a map grows past this
    // keeps it bounded without any per-tick sweep across thousands of bots.
    static constexpr size_t PRUNE_THRESHOLD = 64;

    std::array<std::unordered_map<uint64, FailureEntry>, size_t(FailKind::Count)> _entries;
};

inline char const* FailKindName(FailKind kind)
{
    switch (kind)
    {
        case FailKind::Target:     return "target";
        case FailKind::Spawn:      return "spawn";
        case FailKind::Cluster:    return "cluster";
        case FailKind::GameObject: return "object";
        case FailKind::Npc:        return "npc";
        case FailKind::Quest:      return "quest";
        case FailKind::Hub:        return "hub";
        case FailKind::TurnIn:     return "turn-in";
        case FailKind::DeadEnd:    return "dead end";
        case FailKind::Unworkable: return "unworkable";
        default:                   return "?";
    }
}

#endif // COA_PLAYERBOTS_FAILURE_MEMORY_H
