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
#include "engine/CombatContext.h"
#include <vector>

namespace BotAI
{
    struct BotAction
    {
        uint32 spellId = 0;
        Unit* target = nullptr;
        float score = -1.0f;
        AbilityTag tags = AbilityTag::None;
        char const* name = "";
        char const* reason = "";

        bool IsValid() const { return spellId != 0 && target != nullptr && score > 0.0f; }
    };

    class ActionEvaluator
    {
    public:
        // Evaluates all abilities in the profile against ctx and returns the single highest scoring action.
        static BotAction EvaluateBestAction(CombatContext const& ctx, std::vector<AbilityDescriptor> const& abilities);

        // Validates all cast prerequisites (known, cooldown, failure backoff, power, item, range, auras).
        static bool CanCast(CombatContext const& ctx, AbilityDescriptor const& desc, uint32 resolvedSpellId, Unit* target);

        // Calculates priority score for an ability based on context and tags.
        static float ScoreAbility(CombatContext const& ctx, AbilityDescriptor const& desc, Unit* target);

        // Resolves the destination Unit* based on TargetType.
        static Unit* ResolveTarget(CombatContext const& ctx, TargetType targetType, AbilityDescriptor const& desc);

        // Internal throttle tracking
        static bool IsThrottled(ObjectGuid botGuid, uint32 rootSpellId);
        static void SetThrottle(ObjectGuid botGuid, uint32 rootSpellId, uint32 durationMs);
        static void ClearThrottles(ObjectGuid botGuid);
    };
}

#endif // COA_PLAYERBOTS_ACTION_EVALUATOR_H
