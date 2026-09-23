/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpecStrategyRegistry
 * Stores and retrieves per-spec strategic policies, parallel to ProfileRegistry.
 * Manages runtime state, pull readiness, form state evaluations, and startup validation.
 */

#ifndef COA_PLAYERBOTS_SPEC_STRATEGY_REGISTRY_H
#define COA_PLAYERBOTS_SPEC_STRATEGY_REGISTRY_H

#include "Define.h"
#include "BotAI.h"
#include "ObjectGuid.h"
#include "engine/SpecStrategy.h"
#include <vector>

class Group;

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

        static std::vector<SpecStrategy> const& GetAllStrategies();

        // Runtime lifecycle management
        static SpecStrategyRuntime& GetRuntime(ObjectGuid botGuid);
        static void ForgetBot(ObjectGuid botGuid);
        static void ClearAllRuntimes();

        // State Machine & Form Dancing: evaluates if bot is in baseline, temporary, or needs recovery
        static CombatStateStatus EvaluateCombatState(Player* bot, SpecStrategy const* strategy, CombatContext const& ctx, BotAction* outRecoveryAction = nullptr);

        // Pre-pull autonomous preparation pipeline: shifts into baseline forms, summons pets, prepares out-of-combat
        static PrePullResult ExecutePrePullStrategy(Player* bot, Group* group, uint32 diff);

        // Action callback for tracking cast successes and state transitions
        static void OnActionCastResult(Player* bot, BotAction const& action, bool success);

        // Pull readiness pipeline (TankReady, HealerReady, group recovery)
        static PullReadinessInfo EvaluatePullReadiness(Player* bot, CombatContext const* ctx = nullptr);
        static PullReadinessInfo EvaluateGroupPullReadiness(Player* tank, Group* group);

        // Startup Census & Duplicate Validation
        static void Validate();
    };
}

#endif // COA_PLAYERBOTS_SPEC_STRATEGY_REGISTRY_H
