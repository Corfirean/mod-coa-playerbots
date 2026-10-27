/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- Cleanup Invariant Validator & Runtime Diagnostics (§2 & §3).
 *
 * Verifies that after any encounter finishes or is aborted (for any reason: normal
 * completion, timeout, death, logout, map transfer, teleport, dungeon/BG entry,
 * group join, manual command, unreachable nav, PvP loss, or GM stop), every
 * participating bot has zero residual encounter state and cleanly resumes WorldBrain.
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_CLEANUP_DIAGNOSTICS_H
#define COA_PLAYERBOTS_ENCOUNTER_CLEANUP_DIAGNOSTICS_H

#include "BotMovementPrimitives.h"
#include "EncounterParticipant.h"
#include "EncounterTypes.h"
#include <string>
#include <vector>

struct EncounterCleanupSnapshot
{
    ObjectGuid botGuid;

    // 1. Ownership & participant records
    bool hasEncounterOwnership = false;
    bool inAnyEncounterParticipantList = false;

    // 2. Target & PvP combat state
    bool hasStaleSelectionTarget = false;
    bool hasStalePvPCombatTarget = false;

    // 3. Grouping
    bool hasTemporaryEncounterGroup = false;

    // 4. Movement & Navigation
    MoveOwner currentMoveOwner = MoveOwner::None;
    bool hasEncounterNavRequest = false;
    bool moveTokenStillValid = false;

    // 5. Visual / Aura / Stand / Object overrides
    bool hasResidualEncounterStealth = false;
    bool hasResidualEncounterMorph = false;
    bool hasResidualSitOrKneelState = false;
    bool hasResidualSpellVisual = false;
    bool hasResidualTemporaryGameObject = false;
    bool hasResidualFactionOverride = false;
    bool hasResidualEncounterFlags = false;

    // 6. WorldBrain integration state
    bool pausedByEncounterFlag = false;
    bool worldTaskStuckPaused = false; // True if task.paused == true with no active pause owner
};

struct EncounterCleanupVerdict
{
    bool passed = true;
    uint32 violationCount = 0;
    std::string summary = "PASS";

    void AddViolation(char const* name)
    {
        if (passed)
        {
            passed = false;
            summary = "FAIL(";
        }
        else
        {
            summary += ",";
        }
        summary += name;
        ++violationCount;
    }

    void Finalize()
    {
        if (!passed)
            summary += ")";
    }
};

class EncounterCleanupValidator
{
public:
    static EncounterCleanupVerdict Validate(EncounterCleanupSnapshot const& snap)
    {
        EncounterCleanupVerdict v;

        if (snap.hasEncounterOwnership)
            v.AddViolation("EncounterOwnership");
        if (snap.inAnyEncounterParticipantList)
            v.AddViolation("StaleParticipantRecord");
        if (snap.hasStaleSelectionTarget)
            v.AddViolation("StaleSelectionTarget");
        if (snap.hasStalePvPCombatTarget)
            v.AddViolation("StalePvPCombatTarget");
        if (snap.hasTemporaryEncounterGroup)
            v.AddViolation("TemporaryEncounterGroup");
        if (snap.currentMoveOwner == MoveOwner::Encounter)
            v.AddViolation("MoveOwnerEncounter");
        if (snap.hasEncounterNavRequest)
            v.AddViolation("EncounterNavRequest");
        if (snap.moveTokenStillValid)
            v.AddViolation("ParticipantMoveToken");
        if (snap.hasResidualEncounterStealth)
            v.AddViolation("ResidualStealth");
        if (snap.hasResidualEncounterMorph)
            v.AddViolation("ResidualMorph");
        if (snap.hasResidualSitOrKneelState)
            v.AddViolation("ResidualSitOrKneel");
        if (snap.hasResidualSpellVisual)
            v.AddViolation("ResidualSpellVisual");
        if (snap.hasResidualTemporaryGameObject)
            v.AddViolation("ResidualTemporaryGO");
        if (snap.hasResidualFactionOverride)
            v.AddViolation("ResidualFactionOverride");
        if (snap.hasResidualEncounterFlags)
            v.AddViolation("ResidualEncounterFlags");
        if (snap.pausedByEncounterFlag)
            v.AddViolation("PausedByEncounterStuck");
        if (snap.worldTaskStuckPaused)
            v.AddViolation("WorldTaskStuckPaused");

        v.Finalize();
        return v;
    }
};

#endif // COA_PLAYERBOTS_ENCOUNTER_CLEANUP_DIAGNOSTICS_H
