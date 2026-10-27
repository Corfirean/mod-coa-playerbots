/*
 * mod-coa-playerbots
 *
 * Bot Dialogue System - Anti-Repeat Memory, Cooldown Tracking,
 * Mechanic Deduplication, and Global Chatter Budget.
 */

#ifndef COA_PLAYERBOTS_DIALOGUE_MEMORY_H
#define COA_PLAYERBOTS_DIALOGUE_MEMORY_H

#include "DialogueTypes.h"
#include "ObjectGuid.h"
#include <deque>
#include <string>
#include <unordered_map>
#include <algorithm>

class DialogueMemory
{
public:
    static constexpr size_t MAX_RECENT_LINES_PER_BOT = 16;
    static constexpr size_t MAX_RECENT_LINES_PER_GROUP = 24;

    bool WasRecentlySpokenByBot(ObjectGuid botGuid, std::string const& lineId) const
    {
        auto it = _botRecentLines.find(botGuid);
        if (it == _botRecentLines.end())
            return false;
        return std::find(it->second.begin(), it->second.end(), lineId) != it->second.end();
    }

    bool WasRecentlySpokenInGroup(uint32 groupLow, std::string const& lineId) const
    {
        if (groupLow == 0)
            return false;
        auto it = _groupRecentLines.find(groupLow);
        if (it == _groupRecentLines.end())
            return false;
        return std::find(it->second.begin(), it->second.end(), lineId) != it->second.end();
    }

    bool IsLineOnCooldown(std::string const& lineId, uint32 nowMs) const
    {
        auto it = _lineCooldownUntilMs.find(lineId);
        return it != _lineCooldownUntilMs.end() && nowMs < it->second;
    }

    bool IsBotOnChatterCooldown(ObjectGuid botGuid, uint32 nowMs) const
    {
        auto it = _botCooldownUntilMs.find(botGuid);
        return it != _botCooldownUntilMs.end() && nowMs < it->second;
    }

    bool IsGroupOnChatterCooldown(uint32 groupLow, uint32 nowMs) const
    {
        if (groupLow == 0)
            return false;
        auto it = _groupCooldownUntilMs.find(groupLow);
        return it != _groupCooldownUntilMs.end() && nowMs < it->second;
    }

    bool IsBeatOnCooldownForBot(ObjectGuid botGuid, DialogueBeat beat, uint32 nowMs) const
    {
        uint64 key = (botGuid.GetRawValue() ^ (static_cast<uint64>(beat) << 32));
        auto it = _beatCooldownUntilMs.find(key);
        return it != _beatCooldownUntilMs.end() && nowMs < it->second;
    }

    bool IsMechanicCalloutSuppressed(std::string const& mechanicKey, uint32 nowMs) const
    {
        if (mechanicKey.empty())
            return false;
        auto it = _mechanicCalloutUntilMs.find(mechanicKey);
        return it != _mechanicCalloutUntilMs.end() && nowMs < it->second;
    }

    void RecordSpoken(
        ObjectGuid botGuid,
        uint32 groupLow,
        std::string const& lineId,
        DialogueBeat beat,
        uint32 lineCooldownSec,
        uint32 botCooldownSec,
        uint32 groupCooldownSec,
        uint32 beatCooldownSec,
        uint32 nowMs)
    {
        // 1. Record Line Cooldown
        if (lineCooldownSec > 0)
            _lineCooldownUntilMs[lineId] = nowMs + (lineCooldownSec * 1000u);

        // 2. Record Bot Cooldown
        if (botCooldownSec > 0)
            _botCooldownUntilMs[botGuid] = nowMs + (botCooldownSec * 1000u);

        // 3. Record Group Cooldown
        if (groupLow != 0 && groupCooldownSec > 0)
            _groupCooldownUntilMs[groupLow] = nowMs + (groupCooldownSec * 1000u);

        // 4. Record Beat Cooldown
        if (beatCooldownSec > 0)
        {
            uint64 key = (botGuid.GetRawValue() ^ (static_cast<uint64>(beat) << 32));
            _beatCooldownUntilMs[key] = nowMs + (beatCooldownSec * 1000u);
        }

        // 5. Anti-Repeat History for Bot
        auto& botHistory = _botRecentLines[botGuid];
        botHistory.push_back(lineId);
        if (botHistory.size() > MAX_RECENT_LINES_PER_BOT)
            botHistory.pop_front();

        // 6. Anti-Repeat History for Group
        if (groupLow != 0)
        {
            auto& grpHistory = _groupRecentLines[groupLow];
            grpHistory.push_back(lineId);
            if (grpHistory.size() > MAX_RECENT_LINES_PER_GROUP)
                grpHistory.pop_front();
        }
    }

    void RecordMechanicCallout(std::string const& mechanicKey, uint32 nowMs, uint32 durationMs = 8000u)
    {
        if (!mechanicKey.empty())
            _mechanicCalloutUntilMs[mechanicKey] = nowMs + durationMs;
    }

    void Clear()
    {
        _botRecentLines.clear();
        _groupRecentLines.clear();
        _lineCooldownUntilMs.clear();
        _botCooldownUntilMs.clear();
        _groupCooldownUntilMs.clear();
        _beatCooldownUntilMs.clear();
        _mechanicCalloutUntilMs.clear();
    }

private:
    std::unordered_map<ObjectGuid, std::deque<std::string>> _botRecentLines;
    std::unordered_map<uint32, std::deque<std::string>> _groupRecentLines;
    std::unordered_map<std::string, uint32> _lineCooldownUntilMs;
    std::unordered_map<ObjectGuid, uint32> _botCooldownUntilMs;
    std::unordered_map<uint32, uint32> _groupCooldownUntilMs;
    std::unordered_map<uint64, uint32> _beatCooldownUntilMs;
    std::unordered_map<std::string, uint32> _mechanicCalloutUntilMs;
};

#endif // COA_PLAYERBOTS_DIALOGUE_MEMORY_H
