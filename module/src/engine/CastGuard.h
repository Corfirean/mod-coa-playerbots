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
        // Skips auto-repeat (Shoot / Auto Shot) and finished instant spells. A generic cast whose
        // timer has run past its expected completion (core failed to transition it to
        // SPELL_STATE_FINISHED -- e.g. an interrupted-but-not-cleared cast, a failed LoS/target
        // recheck) is force-cleared here rather than reported as still casting, so the bot can
        // never freeze forever holding a phantom cast. Needs a non-const Player* for that clear.
        static bool IsCurrentlyCasting(Player* bot);

        // Introspection used by diagnostics and timing-aware preemption. Auto-repeat is excluded.
        static uint32 CurrentSpellId(Player* bot);
        static uint32 CurrentSpellRemainingMs(Player* bot);

        // Evaluates whether an ongoing cast must be preempted by a critical combat event.
        // Returns CastInterruptReason::None if the cast should be held/protected.
        static CastInterruptReason EvaluatePreemption(Player* bot, CombatContext const& ctx);

        // Immediately cancels current non-melee spell cast with diagnostic logging.
        static void InterruptCurrentCast(Player* bot, CastInterruptReason reason);

        // Call once per tick while BotAI is in the "holding a cast" branch, passing how long
        // CurrentSpellRemainingMs(bot) has read 0 for the SAME spellId (caller tracks this in its
        // own per-bot state -- CastGuard itself is stateless). If that has gone on longer than the
        // stale-cast grace window, force-clears the abandoned cast and returns true so the caller
        // can stop holding and fall through to normal action selection that same tick.
        static bool ForceClearIfStaleCast(Player* bot, uint32 heldZeroMs);
    };
}

#endif // COA_PLAYERBOTS_CAST_GUARD_H
