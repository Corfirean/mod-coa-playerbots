/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- EncounterNarrator (§6 & §8).
 *
 * Provides rare, contextual, non-debug atmospheric messages and bot dialogue
 * (/say, /yell, /whisper, /emote, and anonymous player-facing atmospheric senses).
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_NARRATOR_H
#define COA_PLAYERBOTS_ENCOUNTER_NARRATOR_H

#include "EncounterTypes.h"
#include <unordered_map>
#include <vector>

class Player;

enum class NarratorChannel : uint8
{
    PlayerAtmosphere = 0, // Anonymous sensory text to target player ("Кажется, за мной кто-то следит...")
    BotSay = 1,           // Bot /say in local range
    BotYell = 2,          // Bot /yell
    BotWhisper = 3,       // Bot /whisper to target player
    BotTextEmote = 4      // Bot /me text emote
};

// Pure contextual & cooldown gate for unit-testing narrator rules without a running worldserver.
class EncounterNarratorGate
{
public:
    bool CanSendToPlayer(
        ObjectGuid playerGuid,
        EncounterType type,
        uint32 botMapId,
        uint32 playerMapId,
        float distanceYards,
        uint32 stalkElapsedMs,
        EncounterConfig const& cfg,
        uint32 nowMs) const
    {
        if (!cfg.narratorEnabled || playerGuid.IsEmpty())
            return false;
        if (botMapId != playerMapId)
            return false;
        if (distanceYards > cfg.narratorMaxDistanceYards)
            return false;

        // Assassin narrator rule (§8): only when assassin is in close proximity and has stalked for a while
        if (type == EncounterType::AssassinContract)
        {
            if (distanceYards > 45.0f || stalkElapsedMs < 3000)
                return false;
        }
        else if (type == EncounterType::TheWatcher)
        {
            if (distanceYards > 55.0f || stalkElapsedMs < 2000)
                return false;
        }

        auto it = _playerNextAllowedMs.find(playerGuid);
        if (it != _playerNextAllowedMs.end() && it->second > nowMs)
            return false;

        return true;
    }

    bool CanBotSpeak(
        ObjectGuid botGuid,
        uint32 botMapId,
        uint32 listenerMapId,
        float distanceToListenerYards,
        EncounterConfig const& cfg,
        uint32 nowMs) const
    {
        if (!cfg.narratorEnabled || botGuid.IsEmpty())
            return false;
        if (botMapId != listenerMapId)
            return false;
        if (distanceToListenerYards > cfg.narratorMaxDistanceYards)
            return false;

        auto it = _botNextAllowedMs.find(botGuid);
        if (it != _botNextAllowedMs.end() && it->second > nowMs)
            return false;

        return true;
    }

    void RecordPlayerMessage(ObjectGuid playerGuid, EncounterConfig const& cfg, uint32 nowMs)
    {
        if (!playerGuid.IsEmpty())
            _playerNextAllowedMs[playerGuid] = nowMs + cfg.narratorPlayerCooldownMs;
    }

    void RecordBotSpeech(ObjectGuid botGuid, EncounterConfig const& cfg, uint32 nowMs)
    {
        if (!botGuid.IsEmpty())
            _botNextAllowedMs[botGuid] = nowMs + cfg.narratorBotCooldownMs;
    }

    void ForgetBot(ObjectGuid botGuid)
    {
        _botNextAllowedMs.erase(botGuid);
    }

    void ForgetPlayer(ObjectGuid playerGuid)
    {
        _playerNextAllowedMs.erase(playerGuid);
    }

    void Clear()
    {
        _playerNextAllowedMs.clear();
        _botNextAllowedMs.clear();
    }

private:
    std::unordered_map<ObjectGuid, uint32> _playerNextAllowedMs;
    std::unordered_map<ObjectGuid, uint32> _botNextAllowedMs;
};

namespace EncounterNarrator
{
    EncounterNarratorGate& Gate();

    // Sends an anonymous sensory/atmospheric line to `targetPlayer` if contextual & cooldown rules pass.
    // Never reveals the bot's name.
    bool TrySendPlayerAtmosphere(
        Player* targetPlayer,
        Player const* sourceBot,
        EncounterType type,
        uint32 stalkElapsedMs,
        uint32 variantSeed);

    // Makes `bot` utter a contextual line via /say, /yell, /whisper, or emote if a real player is nearby
    // (or to `specificPlayer` if provided) and cooldowns permit.
    bool TryBotSpeak(
        Player* bot,
        Player* specificListener,
        EncounterType type,
        NarratorChannel channel,
        uint32 variantSeed);

    // Returns predefined Russian atmospheric text for a given encounter type and channel.
    char const* SelectLine(EncounterType type, NarratorChannel channel, uint32 variantSeed);
}

#endif // COA_PLAYERBOTS_ENCOUNTER_NARRATOR_H
