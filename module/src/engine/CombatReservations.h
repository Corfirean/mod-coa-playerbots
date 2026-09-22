/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatReservations
 *
 * Short-lived coordination so a group of bots sharing interrupt (and, later, heal) capability
 * don't all spend it on the same event in the same tick -- see item 4 of the combat-engine
 * rework. Deliberately not an encounter database: a reservation is keyed on the enemy currently
 * being interrupted, not on any notion of "this specific pull" or "this specific cast type,"
 * and expires on its own shortly after the interruptible cast would have finished. That's enough
 * to solve the actual observed problem (three bots each spending their own kick on the same
 * cast) without building out real per-encounter tactical data this early.
 */

#ifndef COA_PLAYERBOTS_COMBAT_RESERVATIONS_H
#define COA_PLAYERBOTS_COMBAT_RESERVATIONS_H

#include "Define.h"
#include "ObjectGuid.h"

namespace BotAI
{
    struct InterruptReservation
    {
        ObjectGuid enemyGuid;
        uint32 spellId = 0;
        ObjectGuid botGuid;
        uint32 expiresAt = 0;
    };

    // Item 11 of the combat-engine rework: several healer bots shouldn't all commit a big heal to
    // the same critical ally in the same tick. `expectedHeal` is a deliberately rough estimate
    // (see HealEvaluator's own comment on why) -- good enough to stop redundant overhealing, not
    // a real combat-log-accurate prediction.
    struct HealReservation
    {
        ObjectGuid healerGuid;
        ObjectGuid targetGuid;
        uint32 spellId = 0;
        uint32 expectedHeal = 0;
        uint32 expiresAt = 0;
    };

    class CombatReservations
    {
    public:
        // True when some other bot already holds a live reservation to interrupt enemyGuid's
        // current cast. `askingBot` is excluded from the check -- a bot never blocks itself.
        static bool IsInterruptReserved(ObjectGuid enemyGuid, ObjectGuid askingBot);

        // Claims the interrupt on enemyGuid for botGuid, replacing any reservation that's expired
        // or already belongs to this bot. Returns false (no-op) if a different bot's reservation
        // is still live -- callers should treat that as "don't interrupt, someone else has it."
        static bool TryReserveInterrupt(ObjectGuid enemyGuid, uint32 enemySpellId, ObjectGuid botGuid, uint32 durationMs);

        // Drops the reservation on enemyGuid outright -- called once the interrupt actually
        // lands, the cast finishes/breaks on its own, or the target dies, so a stale reservation
        // never blocks a later, unrelated cast from the same enemy.
        static void ClearInterruptReservation(ObjectGuid enemyGuid);

        // Drops any reservation held on this enemy if it no longer matches spellId currently in
        // progress (or the enemy isn't casting `spellId` anymore) -- called once per Utility Layer
        // tick per bot so a reservation for a cast that already resolved doesn't linger until its
        // timeout. Cheap: a single map lookup, no scanning.
        static void ReconcileInterruptReservation(ObjectGuid enemyGuid, uint32 currentCastSpellId);

        // Sum of live (unexpired) expected-heal reservations on targetGuid, excluding any held by
        // `excludingHealer` -- HealEvaluator subtracts this from a candidate's missing health so a
        // second healer doesn't also commit a big heal to someone already about to be topped off.
        static uint32 GetReservedIncomingHeal(ObjectGuid targetGuid, ObjectGuid excludingHealer);

        // Records that `healerGuid` is about to land roughly `expectedHeal` on `targetGuid`.
        // Multiple simultaneous reservations from different healers on the same target are
        // allowed to coexist (a genuinely critical target may legitimately need two heals at
        // once) -- this is additive, not a single-slot claim like the interrupt reservation.
        static void ReserveHeal(ObjectGuid healerGuid, ObjectGuid targetGuid, uint32 spellId, uint32 expectedHeal, uint32 durationMs);

        // Drops this healer's own pending reservation -- called once its cast resolves (success
        // or failure) so a reservation never outlives the cast it was made for by more than the
        // cast's own duration.
        static void ClearHealReservation(ObjectGuid healerGuid);

        // Drops all reservations this bot holds, as either the interrupting/healing bot -- called
        // on despawn (see BotAI::Forget) so the maps don't grow across repeated spawn/despawn
        // cycles.
        static void ForgetBot(ObjectGuid botGuid);
    };
}

#endif // COA_PLAYERBOTS_COMBAT_RESERVATIONS_H
