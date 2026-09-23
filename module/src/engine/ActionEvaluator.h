/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: ActionEvaluator
 * Evaluates candidates and scores the best action for the current CombatContext.
 */

#ifndef COA_PLAYERBOTS_ACTION_EVALUATOR_H
#define COA_PLAYERBOTS_ACTION_EVALUATOR_H

#include "Define.h"
#include "engine/AbilityDescriptor.h"
#include "engine/BotAction.h"
#include "engine/CombatContext.h"
#include "engine/SpecStrategy.h"
#include <vector>

namespace BotAI
{
    class ActionEvaluator
    {
    public:
        // Evaluates all abilities in the profile against ctx and returns the single highest scoring action.
        static BotAction EvaluateBestAction(CombatContext const& ctx, std::vector<AbilityDescriptor> const& abilities);

        // Strategy-aware overload: applies form gating and phase-based score modifiers
        // from the SpecStrategy before the normal scoring pipeline.
        static BotAction EvaluateBestAction(CombatContext const& ctx, std::vector<AbilityDescriptor> const& abilities, SpecStrategy const* strategy);

        // Unified action validation pipeline: verifies that any action (Profile, StateTransition, PrePull, Utility, Legacy)
        // satisfies all cast prerequisites, cooldowns, CombatResourceEvaluator::CanAfford, and state restrictions.
        static bool ValidateAction(CombatContext const& ctx, BotAction const& action, AbilityDescriptor const* desc = nullptr);

        // Validates all cast prerequisites (known, cooldown, failure backoff, power, item, range, auras, form state, entities).
        static bool CanCast(CombatContext const& ctx, AbilityDescriptor const& desc, uint32 resolvedSpellId, Unit* target);

        // Calculates priority score for an ability based on context, tags, phase, and defensive reserves.
        static float ScoreAbility(CombatContext const& ctx, AbilityDescriptor const& desc, Unit* target, SpecStrategy const* strategy = nullptr, CombatPhase phase = CombatPhase::SingleTarget);

        // Helper to compute projected resource amount after ability resolves (handles Fixed, All, None correctly)
        static int32 ProjectResourceAfterAbility(CombatContext const& ctx, CombatResourceKey const& key, uint32 resolvedSpellId);

        // Resolves the destination Unit* based on TargetType.
        static Unit* ResolveTarget(CombatContext const& ctx, TargetType targetType, AbilityDescriptor const& desc, uint32 resolvedSpellId);

        // Internal rotation cooldown / throttle tracking for abilities without native DBC cooldowns
        static bool IsThrottled(ObjectGuid botGuid, uint32 rootSpellId);
        static void SetThrottle(ObjectGuid botGuid, uint32 rootSpellId, uint32 durationMs);
        static void ClearThrottles(ObjectGuid botGuid);
    };
}

#endif // COA_PLAYERBOTS_ACTION_EVALUATOR_H
