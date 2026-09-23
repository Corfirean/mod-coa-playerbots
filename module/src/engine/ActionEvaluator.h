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
#include "engine/SpecStrategy.h"
#include <vector>

namespace BotAI
{
    struct BotAction
    {
        uint32 spellId = 0;
        // The profile-authored root spell id this resolved from (see SpellResolver) -- distinct
        // from spellId once rank resolution is involved. Internal throttle must key off this, not
        // the resolved rank, so a rank-up doesn't silently reset an ability's own throttle.
        uint32 rootSpellId = 0;
        Unit* target = nullptr;
        float score = -1.0f;
        AbilityTag tags = AbilityTag::None;
        char const* name = "";
        char const* reason = "";
        // Copied from the winning AbilityDescriptor so the caller can apply throttle to exactly
        // this one ability after a successful cast, instead of every throttled ability in the
        // profile (see item 1 of the combat-engine rework).
        uint32 internalThrottleMs = 0;

        bool IsValid() const { return spellId != 0 && target != nullptr && score > 0.0f; }
    };

    class ActionEvaluator
    {
    public:
        // Evaluates all abilities in the profile against ctx and returns the single highest scoring action.
        static BotAction EvaluateBestAction(CombatContext const& ctx, std::vector<AbilityDescriptor> const& abilities);

        // Strategy-aware overload: applies form gating and phase-based score modifiers
        // from the SpecStrategy before the normal scoring pipeline.
        static BotAction EvaluateBestAction(CombatContext const& ctx, std::vector<AbilityDescriptor> const& abilities, SpecStrategy const* strategy);

        // Validates all cast prerequisites (known, cooldown, failure backoff, power, item, range, auras).
        static bool CanCast(CombatContext const& ctx, AbilityDescriptor const& desc, uint32 resolvedSpellId, Unit* target);

        // Calculates priority score for an ability based on context and tags.
        static float ScoreAbility(CombatContext const& ctx, AbilityDescriptor const& desc, Unit* target);

        // Resolves the destination Unit* based on TargetType. `resolvedSpellId` (the already-
        // resolved rank, see SpellResolver) lets the ally-triage target types (AnyInjuredAlly/
        // LowestHealthAlly/TankAlly) pick the best-urgency candidate that this specific spell can
        // actually reach (range/LOS/map) instead of the single best-urgency candidate group-wide
        // -- review finding #3 on the Phase 2 fixup pass: picking an ally out of this spell's
        // range meant CanCast rejected it and the whole ability entry was skipped, even when a
        // slightly-less-urgent ally was reachable. Pass 0 when no spell is resolved yet (falls
        // back to an unfiltered pick, same as before).
        static Unit* ResolveTarget(CombatContext const& ctx, TargetType targetType, AbilityDescriptor const& desc, uint32 resolvedSpellId = 0);

        // Internal throttle tracking
        static bool IsThrottled(ObjectGuid botGuid, uint32 rootSpellId);
        static void SetThrottle(ObjectGuid botGuid, uint32 rootSpellId, uint32 durationMs);
        static void ClearThrottles(ObjectGuid botGuid);
    };
}

#endif // COA_PLAYERBOTS_ACTION_EVALUATOR_H
