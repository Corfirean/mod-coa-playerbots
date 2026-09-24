/*
 * mod-coa-playerbots
 *
 * Process-wide claims over world targets, shared by every bot (see ReservationTable.h for the
 * semantics). Keys are a creature/object guid's raw value, a gather node guid, an objective area
 * id. Everything a bot holds is dropped by WorldBrain when its task changes, when it dies, and on
 * logout -- and expires on its own regardless.
 */

#ifndef COA_PLAYERBOTS_WORLD_RESERVATIONS_H
#define COA_PLAYERBOTS_WORLD_RESERVATIONS_H

#include "ObjectGuid.h"
#include "ReservationTable.h"

namespace WorldReservations
{
    bool TryReserve(ReservationKind kind, uint64 key, ObjectGuid bot, uint32 ttlMs);
    bool IsHeldByOther(ReservationKind kind, uint64 key, ObjectGuid bot);
    void Release(ReservationKind kind, uint64 key, ObjectGuid bot);

    void Join(ReservationKind kind, uint64 key, ObjectGuid bot, uint32 ttlMs);
    void Leave(ReservationKind kind, uint64 key, ObjectGuid bot);
    uint32 Occupancy(ReservationKind kind, uint64 key, ObjectGuid exclude = ObjectGuid::Empty);

    void ReleaseAll(ObjectGuid bot);

    // Called from the world tick; sweeps expired claims on a slow timer.
    void Update(uint32 diff);

    ReservationTable const& Table();
}

#endif // COA_PLAYERBOTS_WORLD_RESERVATIONS_H
