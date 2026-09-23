/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpecStrategyRegistry
 * Stores and retrieves per-spec strategic policies, parallel to ProfileRegistry.
 */

#ifndef COA_PLAYERBOTS_SPEC_STRATEGY_REGISTRY_H
#define COA_PLAYERBOTS_SPEC_STRATEGY_REGISTRY_H

#include "Define.h"
#include "BotAI.h"
#include "engine/SpecStrategy.h"

namespace BotAI
{
    class SpecStrategyRegistry
    {
    public:
        static void RegisterStrategy(SpecStrategy strategy);

        // Find the strategy for a given (classId, specId, role) triple.
        // Returns nullptr if no strategy is registered.
        static SpecStrategy const* FindStrategy(uint8 classId, uint32 specId, BotRole role);

        // Convenience: find by classId + specId only (ignores role).
        // Returns the first match. Useful when the caller doesn't know the role.
        static SpecStrategy const* FindStrategy(uint8 classId, uint32 specId);

        static bool HasStrategy(uint8 classId, uint32 specId, BotRole role);
    };
}

#endif // COA_PLAYERBOTS_SPEC_STRATEGY_REGISTRY_H
