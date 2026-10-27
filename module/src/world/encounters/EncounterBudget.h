/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- budget & cooldown manager (§3 Encounter Budget).
 *
 * Enforces:
 *   - Global active encounter cap
 *   - Per-map active encounter cap
 *   - Per-real-player active encounter cap
 *   - Per-real-player personal encounter cooldown
 *   - Per-type global encounter cooldown (including long cooldowns for Bounty & Mystery)
 *   - Per-bot participation cooldown (preventing the same bot from being reused constantly)
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_BUDGET_H
#define COA_PLAYERBOTS_ENCOUNTER_BUDGET_H

#include "EncounterTypes.h"
#include <array>
#include <unordered_map>
#include <vector>

class EncounterBudgetTracker
{
public:
    bool CanCreateEncounter(
        EncounterType type,
        uint32 mapId,
        ObjectGuid targetPlayerGuid,
        EncounterConfig const& cfg,
        uint32 nowMs) const
    {
        if (!cfg.IsTypeEnabled(type))
            return false;

        // 1. Global active limit
        if (_globalActive >= cfg.maxGlobalActive)
            return false;

        // 2. Per-map active limit
        auto mapIt = _mapActive.find(mapId);
        if (mapIt != _mapActive.end() && mapIt->second >= cfg.maxMapActive)
            return false;

        // 3. Per-type cooldown
        size_t typeIdx = static_cast<size_t>(type);
        if (typeIdx >= ENCOUNTER_TYPE_COUNT)
            return false;
        if (_typeCooldownUntilMs[typeIdx] > nowMs)
            return false;

        // 4. Per-player limits and cooldowns (if targeting/attached to a real player)
        if (!targetPlayerGuid.IsEmpty())
        {
            auto activeIt = _playerActive.find(targetPlayerGuid);
            if (activeIt != _playerActive.end() && activeIt->second >= cfg.maxPerPlayerActive)
                return false;

            if (IsPlayerTargetedEncounter(type))
            {
                auto cdIt = _playerPersonalCooldownUntilMs.find(targetPlayerGuid);
                if (cdIt != _playerPersonalCooldownUntilMs.end() && cdIt->second > nowMs)
                    return false;
            }

            if (type == EncounterType::BountyHunter)
            {
                auto bcdIt = _playerBountyCooldownUntilMs.find(targetPlayerGuid);
                if (bcdIt != _playerBountyCooldownUntilMs.end() && bcdIt->second > nowMs)
                    return false;
            }
        }

        return true;
    }

    bool IsBotOnCooldown(ObjectGuid botGuid, uint32 nowMs) const
    {
        auto it = _botCooldownUntilMs.find(botGuid);
        return it != _botCooldownUntilMs.end() && it->second > nowMs;
    }

    bool IsPlayerOnPersonalCooldown(ObjectGuid playerGuid, uint32 nowMs) const
    {
        auto it = _playerPersonalCooldownUntilMs.find(playerGuid);
        return it != _playerPersonalCooldownUntilMs.end() && it->second > nowMs;
    }

    bool IsPlayerOnBountyCooldown(ObjectGuid playerGuid, uint32 nowMs) const
    {
        auto it = _playerBountyCooldownUntilMs.find(playerGuid);
        return it != _playerBountyCooldownUntilMs.end() && it->second > nowMs;
    }

    void OnEncounterStarted(EncounterType type, uint32 mapId, ObjectGuid targetPlayerGuid, EncounterConfig const* cfg = nullptr, uint32 nowMs = 0)
    {
        ++_globalActive;
        ++_mapActive[mapId];
        if (!targetPlayerGuid.IsEmpty())
        {
            ++_playerActive[targetPlayerGuid];
            if (type == EncounterType::BountyHunter && cfg != nullptr && nowMs != 0)
                _playerBountyCooldownUntilMs[targetPlayerGuid] = nowMs + cfg->bountyCooldownMs;
        }
        (void)type;
    }

    void OnEncounterFinished(
        EncounterType type,
        uint32 mapId,
        ObjectGuid targetPlayerGuid,
        std::vector<ObjectGuid> const& participantBotGuids,
        EncounterConfig const& cfg,
        uint32 nowMs)
    {
        if (_globalActive > 0)
            --_globalActive;

        auto mapIt = _mapActive.find(mapId);
        if (mapIt != _mapActive.end())
        {
            if (mapIt->second > 1)
                --mapIt->second;
            else
                _mapActive.erase(mapIt);
        }

        if (!targetPlayerGuid.IsEmpty())
        {
            auto pIt = _playerActive.find(targetPlayerGuid);
            if (pIt != _playerActive.end())
            {
                if (pIt->second > 1)
                    --pIt->second;
                else
                    _playerActive.erase(pIt);
            }

            if (IsPlayerTargetedEncounter(type))
                _playerPersonalCooldownUntilMs[targetPlayerGuid] = nowMs + cfg.playerPersonalCooldownMs;

            if (type == EncounterType::BountyHunter)
                _playerBountyCooldownUntilMs[targetPlayerGuid] = nowMs + cfg.bountyCooldownMs;
        }

        size_t typeIdx = static_cast<size_t>(type);
        if (typeIdx > 0 && typeIdx < ENCOUNTER_TYPE_COUNT)
        {
            uint32 typeCd = cfg.globalTypeCooldownMs;
            if (type == EncounterType::MysteryEncounter)
                typeCd = cfg.mysteryCooldownMs;
            else if (type == EncounterType::BountyHunter)
                typeCd = cfg.bountyCooldownMs;

            _typeCooldownUntilMs[typeIdx] = nowMs + typeCd;
        }

        for (ObjectGuid const& botGuid : participantBotGuids)
        {
            if (!botGuid.IsEmpty())
                _botCooldownUntilMs[botGuid] = nowMs + cfg.botCooldownMs;
        }
    }

    void ForgetBot(ObjectGuid botGuid)
    {
        _botCooldownUntilMs.erase(botGuid);
    }

    void ForgetPlayer(ObjectGuid playerGuid)
    {
        _playerPersonalCooldownUntilMs.erase(playerGuid);
        _playerBountyCooldownUntilMs.erase(playerGuid);
    }

    uint32 GetGlobalActive() const { return _globalActive; }

    uint32 GetMapActive(uint32 mapId) const
    {
        auto it = _mapActive.find(mapId);
        return it != _mapActive.end() ? it->second : 0;
    }

    uint32 GetPlayerActive(ObjectGuid playerGuid) const
    {
        auto it = _playerActive.find(playerGuid);
        return it != _playerActive.end() ? it->second : 0;
    }

    void Clear()
    {
        _globalActive = 0;
        _mapActive.clear();
        _playerActive.clear();
        _playerPersonalCooldownUntilMs.clear();
        _playerBountyCooldownUntilMs.clear();
        _botCooldownUntilMs.clear();
        _typeCooldownUntilMs.fill(0);
    }

private:
    uint32 _globalActive = 0;
    std::unordered_map<uint32, uint32> _mapActive;
    std::unordered_map<ObjectGuid, uint32> _playerActive;
    std::unordered_map<ObjectGuid, uint32> _playerPersonalCooldownUntilMs;
    std::unordered_map<ObjectGuid, uint32> _playerBountyCooldownUntilMs;
    std::unordered_map<ObjectGuid, uint32> _botCooldownUntilMs;
    std::array<uint32, ENCOUNTER_TYPE_COUNT> _typeCooldownUntilMs{};
};

#endif // COA_PLAYERBOTS_ENCOUNTER_BUDGET_H
