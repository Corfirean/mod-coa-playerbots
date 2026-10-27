/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- Encounter handler dispatch and shared world helpers.
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_HANDLERS_H
#define COA_PLAYERBOTS_ENCOUNTER_HANDLERS_H

#include "EncounterInstance.h"

class Creature;
class Player;

namespace EncounterHandlers
{
    // Shared helpers used across ambient, hostile, and mystery handlers
    bool IsStealthCapableClass(uint8 classId);
    bool TryApplyStealth(Player* bot, EncounterParticipant& part);
    void ClearStealthIfApplied(Player* bot, EncounterParticipant& part);
    void RestoreParticipantState(Player* bot, EncounterParticipant& part);
    void EngagePvPOrCreature(Player* bot, Unit* target);

    // Ambient encounters (Stage 6)
    void UpdateRoadDuel(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);
    void UpdateCaravan(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);
    void UpdateDeserter(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);
    void UpdateRivalParty(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);
    void UpdateRareHunter(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);

    // Hostile / personal / PvP encounters (Stages 7 & 8)
    void UpdateAssassinContract(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);
    void UpdateTheWatcher(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);
    void UpdateFactionSkirmish(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);
    void UpdatePatrolAmbush(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);
    void UpdateBountyHunter(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);
    void UpdateNemesis(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);
    void UpdateHunterAndPrey(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);

    // Mystery encounter (Stage 9)
    void UpdateMysteryEncounter(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs);
}

#endif // COA_PLAYERBOTS_ENCOUNTER_HANDLERS_H
