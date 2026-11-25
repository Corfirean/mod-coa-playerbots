/*
 * mod-coa-playerbots
 *
 * Explicit bot & account creation policy (PB-001):
 * Implicit background systems (LFG fill, Battleground fill, cache resets, world ticks)
 * MUST NEVER create new accounts or characters. Creation is permitted only when
 * explicitly initiated by an administrator command or explicit training command.
 */

#ifndef COA_PLAYERBOTS_BOT_CREATION_POLICY_H
#define COA_PLAYERBOTS_BOT_CREATION_POLICY_H

#include <cstdint>

namespace BotCreationPolicy
{
    enum class Source : uint8_t
    {
        ImplicitSystem = 0,
        ExplicitAdminCommand = 1,
        TrainingArena = 2
    };

    struct Decision
    {
        bool allowed;
        char const* reason;
    };

    inline constexpr char const* SourceName(Source source)
    {
        switch (source)
        {
            case Source::ExplicitAdminCommand:
                return "ExplicitAdminCommand";
            case Source::TrainingArena:
                return "TrainingArena";
            case Source::ImplicitSystem:
            default:
                return "ImplicitSystem";
        }
    }

    inline constexpr Decision Evaluate(Source source, bool hasHandler, bool inExplicitBatch, bool botsEnabled)
    {
        if (!botsEnabled)
            return { false, "bots_disabled" };

        switch (source)
        {
            case Source::ImplicitSystem:
                return { false, "implicit_creation_forbidden" };

            case Source::ExplicitAdminCommand:
                if (!hasHandler && !inExplicitBatch)
                    return { false, "missing_explicit_admin_context" };
                return { true, "ok" };

            case Source::TrainingArena:
                if (!hasHandler)
                    return { false, "missing_training_handler" };
                return { true, "ok" };

            default:
                return { false, "unknown_creation_source" };
        }
    }
}

#endif
