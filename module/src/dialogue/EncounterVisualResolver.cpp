#include "EncounterVisualResolver.h"
#include "EncounterParticipant.h"

#ifndef PLAYERBOT_TEST_HARNESS
#include "Player.h"
#endif

EncounterVisualResolver& EncounterVisualResolver::Instance()
{
    static EncounterVisualResolver instance;
    return instance;
}

void EncounterVisualResolver::PlayVisualEffect(Player* bot, VisualEffectType effect)
{
    if (!bot)
        return;

#ifndef PLAYERBOT_TEST_HARNESS
    switch (effect)
    {
        case VisualEffectType::AppearSmoke:
        case VisualEffectType::DisappearSmoke:
            bot->SendPlaySpellVisual(SPELL_VISUAL_SMOKE_PUFF);
            break;
        case VisualEffectType::ShadowBurst:
            bot->SendPlaySpellVisual(SPELL_VISUAL_SHADOW_BURST);
            break;
        case VisualEffectType::RuneGround:
            bot->SendPlaySpellVisual(SPELL_VISUAL_RUNE_GROUND);
            break;
        case VisualEffectType::MysteryPulse:
            bot->SendPlaySpellVisual(SPELL_VISUAL_MYSTERY_PULSE);
            break;
    }
#endif
}

void EncounterVisualResolver::ApplyAppearancePreset(Player* bot, EncounterParticipant& part, AppearancePresetType preset)
{
#ifndef PLAYERBOT_TEST_HARNESS
    if (!bot)
        return;
#endif

    if (preset == AppearancePresetType::None)
        return;

#ifndef PLAYERBOT_TEST_HARNESS
    if (!part.morphApplied)
    {
        if (part.originalDisplayId == 0)
            part.originalDisplayId = bot->GetDisplayId();

        uint32 displayId = 0;
        if (preset == AppearancePresetType::Watcher)
            displayId = DISPLAY_ID_WATCHER_HOOD;
        else if (preset == AppearancePresetType::MysteryCultist)
            displayId = DISPLAY_ID_MYSTERY_CULTIST;

        if (displayId != 0)
        {
            bot->SetDisplayId(displayId);
            part.morphApplied = true;
        }
    }
#else
    (void)bot;
    if (!part.morphApplied)
    {
        if (part.originalDisplayId == 0)
            part.originalDisplayId = 1;

        part.morphApplied = true;
    }
#endif
}

void EncounterVisualResolver::RestoreAppearance(Player* bot, EncounterParticipant& part)
{
#ifndef PLAYERBOT_TEST_HARNESS
    if (!bot)
        return;

    if (part.morphApplied && part.originalDisplayId != 0)
    {
        bot->SetDisplayId(part.originalDisplayId);
        part.morphApplied = false;
    }
#else
    (void)bot;
    if (part.morphApplied)
    {
        part.morphApplied = false;
    }
#endif
}
