/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- Bounty notoriety & Nemesis-lite memory (§7.5 & §7.6).
 *
 * Header-only and core-independent so standalone unit tests can verify score accumulation,
 * threshold triggers, grudge scoring, periodic decay, TTL expiration, and bounded history.
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_MEMORY_H
#define COA_PLAYERBOTS_ENCOUNTER_MEMORY_H

#include "EncounterTypes.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

struct BountyRecord
{
    ObjectGuid playerGuid;
    uint32 score = 0;
    uint32 botsKilled = 0;
    uint32 lastKillAtMs = 0;
    uint32 lastDecayAtMs = 0;
};

struct NemesisRecord
{
    ObjectGuid botGuid;
    ObjectGuid playerGuid;
    uint32 timesBotKilledPlayer = 0;
    uint32 timesPlayerKilledBot = 0;
    uint32 lastEncounterTimeMs = 0;
    uint32 lastDecayAtMs = 0;
    uint32 grudgeScore = 0;
};

class EncounterMemoryStore
{
public:
    static constexpr size_t MAX_NEMESIS_PER_PLAYER = 16;

    // =========================================================================
    // Bounty Notoriety (7.5)
    // =========================================================================

    void RecordPlayerKilledBot(
        ObjectGuid playerGuid,
        ObjectGuid botGuid,
        EncounterConfig const& cfg,
        uint32 nowMs)
    {
        if (playerGuid.IsEmpty() || botGuid.IsEmpty())
            return;

        ApplyDecayForPlayer(playerGuid, cfg, nowMs);

        BountyRecord& b = _bounties[playerGuid];
        b.playerGuid = playerGuid;
        if (b.lastDecayAtMs == 0)
            b.lastDecayAtMs = nowMs;
        b.lastKillAtMs = nowMs;
        ++b.botsKilled;
        b.score = std::min<uint32>(500u, b.score + cfg.bountyKillScore);

        // Also update Nemesis-lite memory for (playerGuid, botGuid)
        NemesisRecord& nem = GetOrCreateNemesis(playerGuid, botGuid, nowMs);
        ++nem.timesPlayerKilledBot;
        nem.lastEncounterTimeMs = nowMs;
        nem.grudgeScore = std::min<uint32>(20u, nem.grudgeScore + 2u);
    }

    void RecordBotKilledPlayer(
        ObjectGuid botGuid,
        ObjectGuid playerGuid,
        EncounterConfig const& cfg,
        uint32 nowMs)
    {
        if (playerGuid.IsEmpty() || botGuid.IsEmpty())
            return;

        ApplyDecayForPlayer(playerGuid, cfg, nowMs);

        NemesisRecord& nem = GetOrCreateNemesis(playerGuid, botGuid, nowMs);
        ++nem.timesBotKilledPlayer;
        nem.lastEncounterTimeMs = nowMs;
        nem.grudgeScore = std::min<uint32>(20u, nem.grudgeScore + 1u);
    }

    void RecordEncounterConflict(
        ObjectGuid botGuid,
        ObjectGuid playerGuid,
        EncounterConfig const& cfg,
        uint32 nowMs)
    {
        if (playerGuid.IsEmpty() || botGuid.IsEmpty())
            return;

        ApplyDecayForPlayer(playerGuid, cfg, nowMs);

        NemesisRecord& nem = GetOrCreateNemesis(playerGuid, botGuid, nowMs);
        nem.lastEncounterTimeMs = nowMs;
        nem.grudgeScore = std::min<uint32>(20u, nem.grudgeScore + 1u);
    }

    uint32 GetBountyScore(ObjectGuid playerGuid, EncounterConfig const& cfg, uint32 nowMs)
    {
        ApplyDecayForPlayer(playerGuid, cfg, nowMs);
        auto it = _bounties.find(playerGuid);
        return it != _bounties.end() ? it->second.score : 0;
    }

    bool IsBountyThresholdMet(ObjectGuid playerGuid, EncounterConfig const& cfg, uint32 nowMs)
    {
        return GetBountyScore(playerGuid, cfg, nowMs) >= cfg.bountyThreshold;
    }

    void ConsumeBountyForAttempt(ObjectGuid playerGuid)
    {
        auto it = _bounties.find(playerGuid);
        if (it != _bounties.end())
            it->second.score = 0;
    }

    void SetBountyScoreForTest(ObjectGuid playerGuid, uint32 score, uint32 nowMs)
    {
        BountyRecord& b = _bounties[playerGuid];
        b.playerGuid = playerGuid;
        b.score = score;
        b.lastKillAtMs = nowMs;
        b.lastDecayAtMs = nowMs;
    }

    // =========================================================================
    // Nemesis-lite Memory (7.6)
    // =========================================================================

    NemesisRecord const* GetNemesisRecord(
        ObjectGuid playerGuid,
        ObjectGuid botGuid,
        EncounterConfig const& cfg,
        uint32 nowMs)
    {
        ApplyDecayForPlayer(playerGuid, cfg, nowMs);
        auto pIt = _nemesisByPlayer.find(playerGuid);
        if (pIt == _nemesisByPlayer.end())
            return nullptr;

        for (NemesisRecord const& rec : pIt->second)
        {
            if (rec.botGuid == botGuid)
                return &rec;
        }
        return nullptr;
    }

    uint32 GetGrudgeScore(
        ObjectGuid playerGuid,
        ObjectGuid botGuid,
        EncounterConfig const& cfg,
        uint32 nowMs)
    {
        NemesisRecord const* rec = GetNemesisRecord(playerGuid, botGuid, cfg, nowMs);
        return rec ? rec->grudgeScore : 0;
    }

    bool IsNemesisRival(
        ObjectGuid playerGuid,
        ObjectGuid botGuid,
        EncounterConfig const& cfg,
        uint32 nowMs)
    {
        return GetGrudgeScore(playerGuid, botGuid, cfg, nowMs) >= cfg.nemesisMinGrudgeScore;
    }

    // Multiplier applied to candidate bot weight when selecting for player-targeted encounters.
    float GetSelectionWeightMultiplier(
        ObjectGuid playerGuid,
        ObjectGuid botGuid,
        EncounterConfig const& cfg,
        uint32 nowMs)
    {
        uint32 grudge = GetGrudgeScore(playerGuid, botGuid, cfg, nowMs);
        if (grudge == 0)
            return 1.0f;
        return 1.0f + std::min<float>(2.5f, float(grudge) * 0.45f);
    }

    std::vector<NemesisRecord> GetPlayerNemesisList(
        ObjectGuid playerGuid,
        EncounterConfig const& cfg,
        uint32 nowMs)
    {
        ApplyDecayForPlayer(playerGuid, cfg, nowMs);
        auto it = _nemesisByPlayer.find(playerGuid);
        if (it == _nemesisByPlayer.end())
            return {};
        return it->second;
    }

    void ApplyDecayForPlayer(ObjectGuid playerGuid, EncounterConfig const& cfg, uint32 nowMs)
    {
        // 1. Decay Bounty score
        auto bIt = _bounties.find(playerGuid);
        if (bIt != _bounties.end())
        {
            BountyRecord& b = bIt->second;
            if (b.lastDecayAtMs == 0)
                b.lastDecayAtMs = nowMs;

            if (nowMs > b.lastDecayAtMs && cfg.bountyDecayPerMinute > 0)
            {
                uint32 elapsedMinutes = (nowMs - b.lastDecayAtMs) / 60000u;
                if (elapsedMinutes > 0)
                {
                    uint32 decay = elapsedMinutes * cfg.bountyDecayPerMinute;
                    b.score = (b.score > decay) ? (b.score - decay) : 0;
                    b.lastDecayAtMs += elapsedMinutes * 60000u;
                }
            }
        }

        // 2. Decay & TTL prune Nemesis records
        auto nIt = _nemesisByPlayer.find(playerGuid);
        if (nIt != _nemesisByPlayer.end())
        {
            auto& vec = nIt->second;
            for (auto it = vec.begin(); it != vec.end();)
            {
                // TTL expiration check
                if (cfg.nemesisTtlMs > 0 && nowMs > it->lastEncounterTimeMs &&
                    (nowMs - it->lastEncounterTimeMs) >= cfg.nemesisTtlMs)
                {
                    it = vec.erase(it);
                    continue;
                }

                // Periodic grudge decay
                if (it->lastDecayAtMs == 0)
                    it->lastDecayAtMs = it->lastEncounterTimeMs;

                if (cfg.nemesisDecayIntervalMs > 0 && nowMs > it->lastDecayAtMs)
                {
                    uint32 steps = (nowMs - it->lastDecayAtMs) / cfg.nemesisDecayIntervalMs;
                    if (steps > 0)
                    {
                        it->grudgeScore = (it->grudgeScore > steps) ? (it->grudgeScore - steps) : 0;
                        it->lastDecayAtMs += steps * cfg.nemesisDecayIntervalMs;
                    }
                }

                if (it->grudgeScore == 0 && (nowMs - it->lastEncounterTimeMs) >= cfg.nemesisDecayIntervalMs)
                {
                    it = vec.erase(it);
                    continue;
                }

                ++it;
            }

            if (vec.empty())
                _nemesisByPlayer.erase(nIt);
        }
    }

    void ForgetBot(ObjectGuid botGuid)
    {
        for (auto it = _nemesisByPlayer.begin(); it != _nemesisByPlayer.end();)
        {
            auto& vec = it->second;
            vec.erase(
                std::remove_if(vec.begin(), vec.end(), [&](NemesisRecord const& r) { return r.botGuid == botGuid; }),
                vec.end());
            if (vec.empty())
                it = _nemesisByPlayer.erase(it);
            else
                ++it;
        }
    }

    void ForgetPlayer(ObjectGuid playerGuid)
    {
        _bounties.erase(playerGuid);
        _nemesisByPlayer.erase(playerGuid);
    }

    void Clear()
    {
        _bounties.clear();
        _nemesisByPlayer.clear();
    }

private:
    NemesisRecord& GetOrCreateNemesis(ObjectGuid playerGuid, ObjectGuid botGuid, uint32 nowMs)
    {
        auto& vec = _nemesisByPlayer[playerGuid];
        for (NemesisRecord& rec : vec)
        {
            if (rec.botGuid == botGuid)
            {
                if (rec.lastDecayAtMs == 0)
                    rec.lastDecayAtMs = nowMs;
                return rec;
            }
        }

        // Enforce bounded history per player: evict oldest/lowest-grudge entry if full
        if (vec.size() >= MAX_NEMESIS_PER_PLAYER)
        {
            auto evictIt = std::min_element(vec.begin(), vec.end(), [](NemesisRecord const& a, NemesisRecord const& b) {
                if (a.grudgeScore != b.grudgeScore)
                    return a.grudgeScore < b.grudgeScore;
                return a.lastEncounterTimeMs < b.lastEncounterTimeMs;
            });
            vec.erase(evictIt);
        }

        NemesisRecord fresh;
        fresh.botGuid = botGuid;
        fresh.playerGuid = playerGuid;
        fresh.lastEncounterTimeMs = nowMs;
        fresh.lastDecayAtMs = nowMs;
        vec.push_back(fresh);
        return vec.back();
    }

    std::unordered_map<ObjectGuid, BountyRecord> _bounties;
    std::unordered_map<ObjectGuid, std::vector<NemesisRecord>> _nemesisByPlayer;
};

#endif // COA_PLAYERBOTS_ENCOUNTER_MEMORY_H
