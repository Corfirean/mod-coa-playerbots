#pragma once

#include "DialogueTypes.h"
#include <cstdint>

class Player;
struct EncounterParticipant;

enum class MysteryVariant : uint8
{
    VariantA_KneelScrying = 0,
    VariantB_DarkIncantation,
    VariantC_SilentVigil,
    VariantD_CursedGround,
    VariantE_AberrationBurst
};

class EncounterPresentation
{
public:
    static EncounterPresentation& Instance();

    // The Watcher
    void ApplyWatcherObservation(Player* bot, EncounterParticipant& part, Player* targetPlayer, uint32 elapsedMs, uint32 nowMs);
    void ApplyWatcherVanish(Player* bot, EncounterParticipant& part, Player* targetPlayer, uint32 nowMs);

    // Mystery Cultist (Variants A..E)
    void ApplyMysteryPresentation(Player* bot, EncounterParticipant& part, Player* targetPlayer, uint8 variantIndex, uint32 elapsedMs, uint32 nowMs);
    void ApplyMysteryDeparture(Player* bot, EncounterParticipant& part, Player* targetPlayer, uint32 nowMs);

    // Road Duel
    void ApplyRoadDuelChallenge(Player* challenger, Player* challenged, uint32 nowMs);
    void ApplyRoadDuelVictory(Player* winner, Player* loser, uint32 nowMs);

    // Hunter & Prey
    void ApplyHunterThreat(Player* hunter, Player* prey, uint32 nowMs);
    void ApplyPreyPanic(Player* prey, Player* hunter, uint32 nowMs);

    // Nemesis
    void ApplyNemesisGreeting(Player* nemesis, Player* targetPlayer, uint32 nowMs);
    void ApplyNemesisTaunt(Player* nemesis, Player* targetPlayer, uint32 nowMs);

    // Cleanup appearance & lingering visual states
    void RestoreParticipantPresentation(Player* bot, EncounterParticipant& part);

private:
    EncounterPresentation() = default;
};

#define sEncounterPresentation EncounterPresentation::Instance()
