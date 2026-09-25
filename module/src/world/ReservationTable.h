/*
 * mod-coa-playerbots
 *
 * The data structure behind WorldReservations: who has called dibs on what, until when.
 *
 * Two shapes of claim, because the world has two shapes of contention:
 *  - Exclusive: one mob, one quest object, one herb. Two bots on the same kill-quest mob is the
 *    exact pile-up this exists to prevent -- the second bot must go find another one.
 *  - Shared: an objective area can hold several bots, it just shouldn't hold all of them.
 *    Occupancy is a count the planner reads as a crowding cost, never a hard lock.
 *
 * Every claim carries an expiry, so a bot that dies, disconnects or simply forgets to release
 * can never hold anything for longer than one ttl. Explicit release is the fast path, expiry is
 * the guarantee.
 *
 * Pure data structure (owners are raw 64-bit ids, time is passed in) so it is unit-tested
 * standalone in module/tests.
 */

#ifndef COA_PLAYERBOTS_RESERVATION_TABLE_H
#define COA_PLAYERBOTS_RESERVATION_TABLE_H

#include "Define.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

enum class ReservationKind : uint8
{
    Creature,
    GameObject,
    GatherNode,
    QuestCluster,
    SocialGroup,
    Count,
};

class ReservationTable
{
public:
    // Claims `key` for `owner` unless someone else holds a live claim on it. Re-claiming something
    // you already hold just refreshes the expiry. Returns false on a conflict.
    bool TryReserve(ReservationKind kind, uint64 key, uint64 owner, uint32 now, uint32 ttlMs)
    {
        auto& map = _exclusive[size_t(kind)];
        auto itr = map.find(key);
        if (itr != map.end() && itr->second.owner != owner && itr->second.expiresAt > now)
        {
            ++_conflicts;
            return false;
        }

        bool fresh = itr == map.end() || itr->second.owner != owner;
        map[key] = Hold{ owner, now + ttlMs };
        if (fresh)
            Index(owner, kind, key, false);
        return true;
    }

    bool IsHeldByOther(ReservationKind kind, uint64 key, uint64 owner, uint32 now) const
    {
        auto const& map = _exclusive[size_t(kind)];
        auto itr = map.find(key);
        return itr != map.end() && itr->second.owner != owner && itr->second.expiresAt > now;
    }

    // Live holder of an exclusive claim, or 0.
    uint64 HolderOf(ReservationKind kind, uint64 key, uint32 now) const
    {
        auto const& map = _exclusive[size_t(kind)];
        auto itr = map.find(key);
        return itr != map.end() && itr->second.expiresAt > now ? itr->second.owner : 0;
    }

    // Drops the claim only if `owner` is the one holding it.
    void Release(ReservationKind kind, uint64 key, uint64 owner)
    {
        auto& map = _exclusive[size_t(kind)];
        auto itr = map.find(key);
        if (itr != map.end() && itr->second.owner == owner)
            map.erase(itr);
        Unindex(owner, kind, key, false);
    }

    // Shared occupancy: `owner` counts toward `key` until it leaves or the ttl lapses.
    void Join(ReservationKind kind, uint64 key, uint64 owner, uint32 now, uint32 ttlMs)
    {
        auto& holds = _shared[size_t(kind)][key];
        for (Hold& hold : holds)
        {
            if (hold.owner == owner)
            {
                hold.expiresAt = now + ttlMs;
                return;
            }
        }
        holds.push_back(Hold{ owner, now + ttlMs });
        Index(owner, kind, key, true);
    }

    void Leave(ReservationKind kind, uint64 key, uint64 owner)
    {
        auto& map = _shared[size_t(kind)];
        auto itr = map.find(key);
        if (itr != map.end())
        {
            auto& holds = itr->second;
            holds.erase(std::remove_if(holds.begin(), holds.end(),
                [owner](Hold const& hold) { return hold.owner == owner; }), holds.end());
            if (holds.empty())
                map.erase(itr);
        }
        Unindex(owner, kind, key, true);
    }

    // Live occupants of `key`, optionally not counting `exclude` (the bot asking).
    uint32 Occupancy(ReservationKind kind, uint64 key, uint32 now, uint64 exclude = 0) const
    {
        auto const& map = _shared[size_t(kind)];
        auto itr = map.find(key);
        if (itr == map.end())
            return 0;
        uint32 count = 0;
        for (Hold const& hold : itr->second)
            if (hold.expiresAt > now && hold.owner != exclude)
                ++count;
        return count;
    }

    // Everything `owner` holds, exclusive and shared -- logout, death, task change.
    void ReleaseAll(uint64 owner)
    {
        auto itr = _byOwner.find(owner);
        if (itr == _byOwner.end())
            return;

        std::vector<OwnedKey> keys;
        keys.swap(itr->second);
        _byOwner.erase(itr);

        for (OwnedKey const& owned : keys)
        {
            if (owned.shared)
            {
                auto& map = _shared[owned.kind];
                auto sItr = map.find(owned.key);
                if (sItr == map.end())
                    continue;
                auto& holds = sItr->second;
                holds.erase(std::remove_if(holds.begin(), holds.end(),
                    [owner](Hold const& hold) { return hold.owner == owner; }), holds.end());
                if (holds.empty())
                    map.erase(sItr);
            }
            else
            {
                auto& map = _exclusive[owned.kind];
                auto eItr = map.find(owned.key);
                if (eItr != map.end() && eItr->second.owner == owner)
                    map.erase(eItr);
            }
        }
    }

    // Drops expired claims. Correctness never depends on this -- every read already ignores
    // expired entries -- it only keeps memory bounded, so it runs on a slow global timer.
    void Sweep(uint32 now)
    {
        for (auto& map : _exclusive)
            for (auto itr = map.begin(); itr != map.end();)
                itr = itr->second.expiresAt <= now ? map.erase(itr) : std::next(itr);

        for (auto& map : _shared)
        {
            for (auto itr = map.begin(); itr != map.end();)
            {
                auto& holds = itr->second;
                holds.erase(std::remove_if(holds.begin(), holds.end(),
                    [now](Hold const& hold) { return hold.expiresAt <= now; }), holds.end());
                itr = holds.empty() ? map.erase(itr) : std::next(itr);
            }
        }

        // The owner index may still point at swept keys; those entries are harmless (release
        // checks the holder) but are trimmed here so the index can't grow without bound.
        for (auto itr = _byOwner.begin(); itr != _byOwner.end();)
        {
            auto& keys = itr->second;
            uint64 owner = itr->first;
            keys.erase(std::remove_if(keys.begin(), keys.end(), [&](OwnedKey const& owned)
            {
                if (owned.shared)
                {
                    auto const& map = _shared[owned.kind];
                    auto sItr = map.find(owned.key);
                    if (sItr == map.end())
                        return true;
                    return std::none_of(sItr->second.begin(), sItr->second.end(),
                        [owner](Hold const& hold) { return hold.owner == owner; });
                }
                auto const& map = _exclusive[owned.kind];
                auto eItr = map.find(owned.key);
                return eItr == map.end() || eItr->second.owner != owner;
            }), keys.end());
            itr = keys.empty() ? _byOwner.erase(itr) : std::next(itr);
        }
    }

    size_t ExclusiveCount() const
    {
        size_t total = 0;
        for (auto const& map : _exclusive)
            total += map.size();
        return total;
    }

    size_t SharedCount() const
    {
        size_t total = 0;
        for (auto const& map : _shared)
            for (auto const& [key, holds] : map)
                total += holds.size();
        return total;
    }

    uint64 Conflicts() const { return _conflicts; }

    // Exclusive keys `owner` currently holds of `kind` (debug output).
    std::vector<uint64> HeldBy(uint64 owner, ReservationKind kind, uint32 now) const
    {
        std::vector<uint64> out;
        auto itr = _byOwner.find(owner);
        if (itr == _byOwner.end())
            return out;
        for (OwnedKey const& owned : itr->second)
            if (!owned.shared && owned.kind == uint8(kind) && HolderOf(kind, owned.key, now) == owner)
                out.push_back(owned.key);
        return out;
    }

private:
    struct Hold
    {
        uint64 owner;
        uint32 expiresAt;
    };

    struct OwnedKey
    {
        uint8 kind;
        bool shared;
        uint64 key;
    };

    void Index(uint64 owner, ReservationKind kind, uint64 key, bool shared)
    {
        auto& keys = _byOwner[owner];
        for (OwnedKey const& owned : keys)
            if (owned.kind == uint8(kind) && owned.shared == shared && owned.key == key)
                return;
        keys.push_back(OwnedKey{ uint8(kind), shared, key });
    }

    void Unindex(uint64 owner, ReservationKind kind, uint64 key, bool shared)
    {
        auto itr = _byOwner.find(owner);
        if (itr == _byOwner.end())
            return;
        auto& keys = itr->second;
        keys.erase(std::remove_if(keys.begin(), keys.end(), [&](OwnedKey const& owned)
        {
            return owned.kind == uint8(kind) && owned.shared == shared && owned.key == key;
        }), keys.end());
        if (keys.empty())
            _byOwner.erase(itr);
    }

    std::array<std::unordered_map<uint64, Hold>, size_t(ReservationKind::Count)> _exclusive;
    std::array<std::unordered_map<uint64, std::vector<Hold>>, size_t(ReservationKind::Count)> _shared;
    std::unordered_map<uint64, std::vector<OwnedKey>> _byOwner;
    uint64 _conflicts = 0;
};

#endif // COA_PLAYERBOTS_RESERVATION_TABLE_H
