#pragma once

#include "DialogueTypes.h"
#include <cstdint>

class Player;
struct EncounterParticipant;

enum class VisualEffectType : uint8
{
    AppearSmoke = 0,
    DisappearSmoke,
    ShadowBurst,
    RuneGround,
    MysteryPulse
};

enum class AppearancePresetType : uint8
{
    None = 0,
    Watcher,
    MysteryCultist
};

class EncounterVisualResolver
{
public:
    static EncounterVisualResolver& Instance();

    // Trigger visual effect on bot (via SendPlaySpellVisual or built-in safe cosmetic spell)
    void PlayVisualEffect(Player* bot, VisualEffectType effect);

    // Apply appearance preset (morph / clothing styling) saving original display ID for clean cleanup
    void ApplyAppearancePreset(Player* bot, EncounterParticipant& part, AppearancePresetType preset);

    // Cleanly revert any appearance preset applied to the bot
    void RestoreAppearance(Player* bot, EncounterParticipant& part);

    // Safe 3.3.5a spell visual IDs
    static constexpr uint32 SPELL_VISUAL_SMOKE_PUFF    = 1784;  // Vanish / Stealth smoke puff
    static constexpr uint32 SPELL_VISUAL_SHADOW_BURST  = 30206; // Shadow burst effect
    static constexpr uint32 SPELL_VISUAL_RUNE_GROUND   = 5106;  // Scrying arcane/shadow rune ground visual
    static constexpr uint32 SPELL_VISUAL_MYSTERY_PULSE = 67556; // Dark void pulse

    // Display IDs for presets (0 = preserve natural bot player character model and gear)
    static constexpr uint32 DISPLAY_ID_WATCHER_HOOD   = 0;
    static constexpr uint32 DISPLAY_ID_MYSTERY_CULTIST = 0;

private:
    EncounterVisualResolver() = default;
};

#define sEncounterVisualResolver EncounterVisualResolver::Instance()
