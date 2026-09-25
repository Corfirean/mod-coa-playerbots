/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CastGuard
 * Manages current spell cast monitoring, self-interruption, and priority overrides.
 */

#ifndef COA_PLAYERBOTS_CAST_GUARD_H
#define COA_PLAYERBOTS_CAST_GUARD_H

#include "Define.h"

class Player;

namespace BotAI
{
    struct CombatContext;
    enum class CastInterruptReason : uint8
    {
        None = 0,
        LethalGroundHazard,
        LethalBossMechanic,
        EmergencyTankSave,
        HighPriorityInterrupt,
        CriticalTaunt,
        ManualOverride
    };

    class CastGuard
    {
    public:
        // Returns true if the bot is currently in the middle of a non-melee spell cast or channel.
        // Skips auto-repeat (Shoot / Auto Shot) and finished instant spells.
        static bool IsCurrentlyCasting(Player const* bot);

        // Introspection used by diagnostics and timing-aware preemption. Auto-repeat is excluded.
        static uint32 CurrentSpellId(Player const* bot);
        static uint32 CurrentSpellRemainingMs(Player const* bot);

        // Evaluates whether an ongoing cast must be preempted by a critical combat event.
        // Returns CastInterruptReason::None if the cast should be held/protected.
        static CastInterruptReason EvaluatePreemption(Player* bot, CombatContext const& ctx);

        // Immediately cancels current non-melee spell cast with diagnostic logging.
        static void InterruptCurrentCast(Player* bot, CastInterruptReason reason);
    };
}

#endif // COA_PLAYERBOTS_CAST_GUARD_H
