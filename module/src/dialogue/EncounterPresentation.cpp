#include "EncounterPresentation.h"
#include "BotDialogueManager.h"
#include "EncounterVisualResolver.h"
#include "EncounterParticipant.h"

#ifndef PLAYERBOT_TEST_HARNESS
#include "Player.h"
#include "SharedDefines.h"
#endif

EncounterPresentation& EncounterPresentation::Instance()
{
    static EncounterPresentation instance;
    return instance;
}

void EncounterPresentation::ApplyWatcherObservation(Player* bot, EncounterParticipant& part, Player* targetPlayer, uint32 elapsedMs, uint32 nowMs)
{
    if (!bot)
        return;

    sEncounterVisualResolver.ApplyAppearancePreset(bot, part, AppearancePresetType::Watcher);

#ifndef PLAYERBOT_TEST_HARNESS
    if (targetPlayer)
        bot->SetFacingToObject(targetPlayer);
#endif

    if (!part.narratorSpoken && elapsedMs >= 4000)
    {
        DialogueContext ctx;
        ctx.speaker = bot;
        ctx.listener = targetPlayer;
        ctx.channel = DialogueChannel::Say;
        ctx.priority = DialoguePriority::Normal;
        if (sBotDialogueMgr.Emit(DialogueBeat::WatcherObserved, ctx, nowMs))
            part.narratorSpoken = true;
    }
}

void EncounterPresentation::ApplyWatcherVanish(Player* bot, EncounterParticipant& part, Player* targetPlayer, uint32 nowMs)
{
    if (!bot)
        return;

    sEncounterVisualResolver.PlayVisualEffect(bot, VisualEffectType::DisappearSmoke);

    DialogueContext ctx;
    ctx.speaker = bot;
    ctx.listener = targetPlayer;
    ctx.channel = DialogueChannel::Say;
    ctx.priority = DialoguePriority::High;
    sBotDialogueMgr.Emit(DialogueBeat::WatcherSpotted, ctx, nowMs);
}

void EncounterPresentation::ApplyMysteryPresentation(Player* bot, EncounterParticipant& part, Player* targetPlayer, uint8 variantIndex, uint32 elapsedMs, uint32 nowMs)
{
    if (!bot)
        return;

    sEncounterVisualResolver.ApplyAppearancePreset(bot, part, AppearancePresetType::MysteryCultist);

#ifndef PLAYERBOT_TEST_HARNESS
    if (targetPlayer)
        bot->SetFacingToObject(targetPlayer);
#endif

    if (!part.emoteDone && elapsedMs >= 3500)
    {
        part.emoteDone = true;
        uint8 var = variantIndex % 5;

#ifndef PLAYERBOT_TEST_HARNESS
        switch (var)
        {
            case 0: // Variant A: Kneel & Scrying Rune
                bot->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);
                sEncounterVisualResolver.PlayVisualEffect(bot, VisualEffectType::RuneGround);
                break;
            case 1: // Variant B: Dark Incantation
                bot->HandleEmoteCommand(EMOTE_ONESHOT_SPELL_CAST_OMNI);
                sEncounterVisualResolver.PlayVisualEffect(bot, VisualEffectType::MysteryPulse);
                break;
            case 2: // Variant C: Silent Vigil & Ominous Stare
                bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
                break;
            case 3: // Variant D: Cursed Ground / Offering
                bot->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
                sEncounterVisualResolver.PlayVisualEffect(bot, VisualEffectType::ShadowBurst);
                break;
            case 4: // Variant E: Aberration Burst
                bot->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
                sEncounterVisualResolver.PlayVisualEffect(bot, VisualEffectType::AppearSmoke);
                break;
        }
#endif

        DialogueContext ctx;
        ctx.speaker = bot;
        ctx.listener = targetPlayer;
        ctx.channel = DialogueChannel::Say;
        ctx.priority = DialoguePriority::High;

        DialogueBeat beat = DialogueBeat::MysteryShathYarWhisper;
        if (var == 1) beat = DialogueBeat::MysteryShathYarIncantation;
        else if (var == 2) beat = DialogueBeat::MysteryWhisper;
        else if (var == 3) beat = DialogueBeat::MysteryShathYarThreat;
        else if (var == 4) beat = DialogueBeat::MysteryRune;

        sBotDialogueMgr.Emit(beat, ctx, nowMs);
    }
}

void EncounterPresentation::ApplyMysteryDeparture(Player* bot, EncounterParticipant& /*part*/, Player* targetPlayer, uint32 nowMs)
{
    if (!bot)
        return;

    sEncounterVisualResolver.PlayVisualEffect(bot, VisualEffectType::DisappearSmoke);

    DialogueContext ctx;
    ctx.speaker = bot;
    ctx.listener = targetPlayer;
    ctx.channel = DialogueChannel::Say;
    ctx.priority = DialoguePriority::High;
    sBotDialogueMgr.Emit(DialogueBeat::MysteryShathYarDeparture, ctx, nowMs);
}

void EncounterPresentation::ApplyRoadDuelChallenge(Player* challenger, Player* challenged, uint32 nowMs)
{
    if (!challenger)
        return;

    DialogueContext ctx;
    ctx.speaker = challenger;
    ctx.listener = challenged;
    ctx.channel = DialogueChannel::Say;
    ctx.priority = DialoguePriority::High;
    sBotDialogueMgr.Emit(DialogueBeat::RoadDuelChallenge, ctx, nowMs);
}

void EncounterPresentation::ApplyRoadDuelVictory(Player* winner, Player* loser, uint32 nowMs)
{
    if (!winner)
        return;

    DialogueContext ctx;
    ctx.speaker = winner;
    ctx.listener = loser;
    ctx.channel = DialogueChannel::Say;
    ctx.priority = DialoguePriority::Normal;
    sBotDialogueMgr.Emit(DialogueBeat::RoadDuelVictory, ctx, nowMs);
}

void EncounterPresentation::ApplyHunterThreat(Player* hunter, Player* prey, uint32 nowMs)
{
    if (!hunter)
        return;

    DialogueContext ctx;
    ctx.speaker = hunter;
    ctx.listener = prey;
    ctx.channel = DialogueChannel::Yell;
    ctx.priority = DialoguePriority::High;
    sBotDialogueMgr.Emit(DialogueBeat::HunterThreat, ctx, nowMs);
}

void EncounterPresentation::ApplyPreyPanic(Player* prey, Player* hunter, uint32 nowMs)
{
    if (!prey)
        return;

    DialogueContext ctx;
    ctx.speaker = prey;
    ctx.listener = hunter;
    ctx.channel = DialogueChannel::Yell;
    ctx.priority = DialoguePriority::High;
    sBotDialogueMgr.Emit(DialogueBeat::PreyPanic, ctx, nowMs);
}

void EncounterPresentation::ApplyNemesisGreeting(Player* nemesis, Player* targetPlayer, uint32 nowMs)
{
    if (!nemesis)
        return;

    DialogueContext ctx;
    ctx.speaker = nemesis;
    ctx.listener = targetPlayer;
    ctx.channel = DialogueChannel::Say;
    ctx.priority = DialoguePriority::High;
    sBotDialogueMgr.Emit(DialogueBeat::NemesisGreeting, ctx, nowMs);
}

void EncounterPresentation::ApplyNemesisTaunt(Player* nemesis, Player* targetPlayer, uint32 nowMs)
{
    if (!nemesis)
        return;

    DialogueContext ctx;
    ctx.speaker = nemesis;
    ctx.listener = targetPlayer;
    ctx.channel = DialogueChannel::Yell;
    ctx.priority = DialoguePriority::High;
    sBotDialogueMgr.Emit(DialogueBeat::NemesisTaunt, ctx, nowMs);
}

void EncounterPresentation::RestoreParticipantPresentation(Player* bot, EncounterParticipant& part)
{
    sEncounterVisualResolver.RestoreAppearance(bot, part);
}
