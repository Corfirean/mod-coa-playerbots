#include "WorldReservations.h"
#include "GameTime.h"

namespace
{
    ReservationTable _table;
    uint32 _sweepTimerMs = 0;
    constexpr uint32 SWEEP_INTERVAL_MS = 30000;

    uint32 NowMs()
    {
        return uint32(GameTime::GetGameTimeMS().count());
    }
}

namespace WorldReservations
{
    bool TryReserve(ReservationKind kind, uint64 key, ObjectGuid bot, uint32 ttlMs)
    {
        return _table.TryReserve(kind, key, bot.GetRawValue(), NowMs(), ttlMs);
    }

    bool IsHeldByOther(ReservationKind kind, uint64 key, ObjectGuid bot)
    {
        return _table.IsHeldByOther(kind, key, bot.GetRawValue(), NowMs());
    }

    void Release(ReservationKind kind, uint64 key, ObjectGuid bot)
    {
        _table.Release(kind, key, bot.GetRawValue());
    }

    void Join(ReservationKind kind, uint64 key, ObjectGuid bot, uint32 ttlMs)
    {
        _table.Join(kind, key, bot.GetRawValue(), NowMs(), ttlMs);
    }

    void Leave(ReservationKind kind, uint64 key, ObjectGuid bot)
    {
        _table.Leave(kind, key, bot.GetRawValue());
    }

    uint32 Occupancy(ReservationKind kind, uint64 key, ObjectGuid exclude)
    {
        return _table.Occupancy(kind, key, NowMs(), exclude.GetRawValue());
    }

    void ReleaseAll(ObjectGuid bot)
    {
        _table.ReleaseAll(bot.GetRawValue());
    }

    void Update(uint32 diff)
    {
        _sweepTimerMs += diff;
        if (_sweepTimerMs < SWEEP_INTERVAL_MS)
            return;
        _sweepTimerMs = 0;
        _table.Sweep(NowMs());
    }

    ReservationTable const& Table()
    {
        return _table;
    }
}
