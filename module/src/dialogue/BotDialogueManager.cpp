/*
 * mod-coa-playerbots
 *
 * Bot Dialogue System - Central Management Implementation.
 */

#include "BotDialogueManager.h"
#include <chrono>

#ifndef PLAYERBOT_TEST_HARNESS
#include "ObjectAccessor.h"
#include "Player.h"
#include "Group.h"
#include "SharedDefines.h"
#include "Log.h"
#else
#include <cstdio>
#define LOG_DEBUG(filter, ...) ((void)0)
#define LOG_INFO(filter, ...)  ((void)0)
#define LANG_UNIVERSAL 0
#endif

namespace
{
    uint32 GetCurrentTimeMs()
    {
        using namespace std::chrono;
        return static_cast<uint32>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }
}

BotDialogueManager& BotDialogueManager::Instance()
{
    static BotDialogueManager instance;
    return instance;
}

BotDialogueManager::BotDialogueManager()
{
    _pool.LoadDefaults();
}

void BotDialogueManager::Initialize(std::string const& dataDir)
{
    _dataDir = dataDir;
    Reload();
}

void BotDialogueManager::LoadAllData(std::string const& dir)
{
    if (!dir.empty())
        _dataDir = dir;
    Reload();
}

void BotDialogueManager::Reload()
{
    _pool.Clear();
    _pool.LoadDefaults();

    if (!_dataDir.empty())
    {
        std::string err;
        _pool.LoadFromDirectory(_dataDir, err);
    }
}

bool BotDialogueManager::Emit(DialogueBeat beat, DialogueContext const& inCtx, uint32 nowMs)
{
    if (!_enabled)
        return false;

    if (nowMs == 0)
        nowMs = GetCurrentTimeMs();

    DialogueContext ctx = inCtx;
#ifndef PLAYERBOT_TEST_HARNESS
    if (ctx.speaker)
    {
        if (ctx.speakerGuid.IsEmpty())
            ctx.speakerGuid = ctx.speaker->GetGUID();
        if (ctx.speakerName.empty())
            ctx.speakerName = ctx.speaker->GetName();
        ctx.speakerRace = ctx.speaker->getRace();
        ctx.currentHealthPct = ctx.speaker->GetHealthPct();
        if (ctx.speaker->GetMaxPower(POWER_MANA) > 0)
            ctx.currentManaPct = (float(ctx.speaker->GetPower(POWER_MANA)) / float(ctx.speaker->GetMaxPower(POWER_MANA))) * 100.0f;
    }
    if (ctx.listener)
    {
        if (ctx.targetGuid.IsEmpty())
            ctx.targetGuid = ctx.listener->GetGUID();
        if (ctx.targetName.empty())
            ctx.targetName = ctx.listener->GetName();
        ctx.targetRace = ctx.listener->getRace();
    }
#endif
    if (ctx.channel != DialogueChannel::Say && ctx.preferredChannel == DialogueChannel::Say)
        ctx.preferredChannel = ctx.channel;

    std::string suppressionReason;
    DialogueLine const* line = DialogueSelector::SelectLine(
        _pool, _memory, ctx, beat, nowMs, suppressionReason);

    if (!line)
    {
        if (_debug)
        {
            LOG_DEBUG("playerbots", "[Dialogue] Suppressed beat '{}' for bot {}: {}",
                DialogueUtils::BeatToString(beat), ctx.speakerGuid.ToString(), suppressionReason);
        }
        return false;
    }

    // Format final localized text with placeholders
    std::string formatted = DialogueSelector::FormatLineText(*line, ctx);

    // Record line, bot, group, and beat cooldowns into memory
    _memory.RecordSpoken(
        ctx.speakerGuid,
        ctx.groupLow,
        line->id,
        beat,
        line->cooldownSec,
        _perBotCooldownSec,
        _groupCooldownSec,
        _beatCooldownSec,
        nowMs);

    // World Delivery
#ifndef PLAYERBOT_TEST_HARNESS
    Player* speaker = ObjectAccessor::FindConnectedPlayer(ctx.speakerGuid);
    Player* target = ctx.targetGuid.IsEmpty() ? nullptr : ObjectAccessor::FindConnectedPlayer(ctx.targetGuid);

    if (speaker && speaker->IsInWorld() && !formatted.empty())
    {
        Deliver(speaker, target, ctx.preferredChannel, formatted);
    }
#endif

    if (_debug)
    {
        LOG_INFO("playerbots", "[Dialogue] Bot {} spoke [{}] '{}'",
            ctx.speakerGuid.ToString(), line->id, formatted);
    }

    return true;
}

bool BotDialogueManager::EmitGroupExchange(
    DialogueBeat initialBeat,
    DialogueBeat responseBeat,
    DialogueContext const& ctxA,
    DialogueContext const& ctxB,
    uint32 nowMs)
{
    if (!_enabled)
        return false;

    if (nowMs == 0)
        nowMs = GetCurrentTimeMs();

    // First line
    bool spokeA = Emit(initialBeat, ctxA, nowMs);
    if (!spokeA)
        return false;

    // Second response line with small stagger
    bool spokeB = Emit(responseBeat, ctxB, nowMs + 100u);
    return spokeA && spokeB;
}

void BotDialogueManager::Deliver(
    Player* speaker,
    Player* target,
    DialogueChannel channel,
    std::string const& text)
{
#ifndef PLAYERBOT_TEST_HARNESS
    if (!speaker || text.empty())
        return;

    switch (channel)
    {
        case DialogueChannel::Say:
            speaker->Say(text, LANG_UNIVERSAL);
            break;
        case DialogueChannel::Yell:
            speaker->Yell(text, LANG_UNIVERSAL);
            break;
        case DialogueChannel::Party:
            if (speaker->GetGroup())
            {
                speaker->Say(text, LANG_UNIVERSAL);
            }
            else
            {
                speaker->Say(text, LANG_UNIVERSAL);
            }
            break;
        case DialogueChannel::Whisper:
            if (target)
            {
                speaker->Whisper(text, LANG_UNIVERSAL, target);
            }
            else
            {
                speaker->Say(text, LANG_UNIVERSAL);
            }
            break;
        case DialogueChannel::Emote:
            speaker->TextEmote(text);
            break;
        case DialogueChannel::PrivateNarrator:
            if (target && target->GetSession())
            {
                speaker->Whisper(text, LANG_UNIVERSAL, target);
            }
            break;
    }
#else
    (void)speaker;
    (void)target;
    (void)channel;
    (void)text;
#endif
}
