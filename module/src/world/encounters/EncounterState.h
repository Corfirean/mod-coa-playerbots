/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- finite state machine:
 *   Candidate -> Prepare -> Active -> Resolve -> Cooldown (-> Terminated)
 *
 * Enforces monotonic forward transitions, per-phase timeout budgets, and stop reasons.
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_STATE_H
#define COA_PLAYERBOTS_ENCOUNTER_STATE_H

#include "EncounterTypes.h"

struct EncounterStateMachine
{
    EncounterPhase phase = EncounterPhase::Candidate;
    EncounterStopReason stopReason = EncounterStopReason::None;
    uint32 createdAtMs = 0;
    uint32 phaseEnteredAtMs = 0;
    uint32 prepareTimeoutMs = 90000;
    uint32 activeTimeoutMs = 120000;
    uint32 resolveTimeoutMs = 10000;
    uint32 cooldownUntilMs = 0;

    void Initialize(uint32 nowMs, uint32 prepTimeout, uint32 actTimeout, uint32 resTimeout)
    {
        phase = EncounterPhase::Candidate;
        stopReason = EncounterStopReason::None;
        createdAtMs = nowMs;
        phaseEnteredAtMs = nowMs;
        prepareTimeoutMs = prepTimeout;
        activeTimeoutMs = actTimeout;
        resolveTimeoutMs = resTimeout;
        cooldownUntilMs = 0;
    }

    static bool IsValidTransition(EncounterPhase from, EncounterPhase to)
    {
        if (from == to)
            return false;
        if (from == EncounterPhase::Terminated)
            return false;
        if (to == EncounterPhase::Terminated || to == EncounterPhase::Cooldown)
            return from != EncounterPhase::Terminated;

        switch (from)
        {
            case EncounterPhase::Candidate:
                return to == EncounterPhase::Prepare;
            case EncounterPhase::Prepare:
                return to == EncounterPhase::Active || to == EncounterPhase::Resolve;
            case EncounterPhase::Active:
                return to == EncounterPhase::Resolve;
            case EncounterPhase::Resolve:
                return to == EncounterPhase::Cooldown;
            case EncounterPhase::Cooldown:
                return to == EncounterPhase::Terminated;
            default:
                return false;
        }
    }

    bool TransitionTo(EncounterPhase nextPhase, uint32 nowMs)
    {
        if (!IsValidTransition(phase, nextPhase))
            return false;
        phase = nextPhase;
        phaseEnteredAtMs = nowMs;
        return true;
    }

    void Terminate(EncounterStopReason reason, uint32 nowMs, uint32 cooldownDurationMs = 0)
    {
        if (stopReason == EncounterStopReason::None)
            stopReason = reason;
        phase = EncounterPhase::Cooldown;
        phaseEnteredAtMs = nowMs;
        cooldownUntilMs = nowMs + cooldownDurationMs;
    }

    void MarkFullyRetired(uint32 nowMs)
    {
        phase = EncounterPhase::Terminated;
        phaseEnteredAtMs = nowMs;
    }

    uint32 PhaseElapsedMs(uint32 nowMs) const
    {
        return nowMs >= phaseEnteredAtMs ? (nowMs - phaseEnteredAtMs) : 0;
    }

    uint32 CurrentPhaseBudgetMs() const
    {
        switch (phase)
        {
            case EncounterPhase::Prepare: return prepareTimeoutMs;
            case EncounterPhase::Active:  return activeTimeoutMs;
            case EncounterPhase::Resolve: return resolveTimeoutMs;
            default:                      return 0;
        }
    }

    uint32 RemainingPhaseTimeoutMs(uint32 nowMs) const
    {
        uint32 budget = CurrentPhaseBudgetMs();
        if (budget == 0)
            return 0;
        uint32 elapsed = PhaseElapsedMs(nowMs);
        return elapsed < budget ? (budget - elapsed) : 0;
    }

    bool IsPhaseTimedOut(uint32 nowMs) const
    {
        uint32 budget = CurrentPhaseBudgetMs();
        return budget > 0 && PhaseElapsedMs(nowMs) >= budget;
    }

    bool IsRunning() const
    {
        return phase == EncounterPhase::Candidate ||
               phase == EncounterPhase::Prepare ||
               phase == EncounterPhase::Active ||
               phase == EncounterPhase::Resolve;
    }
};

#endif // COA_PLAYERBOTS_ENCOUNTER_STATE_H
