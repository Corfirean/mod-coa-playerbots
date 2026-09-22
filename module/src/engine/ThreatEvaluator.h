/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: ThreatEvaluator (item 9/#3, Phase 2)
 *
 * `FindAllyThreatenedTarget` in BotAI.cpp (kept as-is, see item 23) just returns the first enemy
 * found attacking a groupmate -- a tank shouldn't drop a boss for a loose add that tapped a
 * full-HP DPS once. This scores every candidate and picks the one that actually matters most,
 * without needing this fork's real threat-table API (predates it -- see BotAI.cpp's own comment
 * on why victim-based proxies are used throughout this AI).
 */

#ifndef COA_PLAYERBOTS_THREAT_EVALUATOR_H
#define COA_PLAYERBOTS_THREAT_EVALUATOR_H

#include "Define.h"

class Player;
class Unit;

namespace BotAI
{
    // Item 8 of the Phase 2 fixup pass: a plain nullptr target conflated two very different
    // situations -- "I found real threats and none of them warrant switching" vs. "I have
    // nothing to work with at all" (no group, nothing nearby). A caller needs to tell these
    // apart: the old first-match FindAllyThreatenedTarget fallback only makes sense for the
    // second case -- falling back to it after the first would silently overrule a deliberate,
    // already-scored "don't switch" decision with an unscored first match.
    struct ThreatDecision
    {
        Unit* target = nullptr;
        bool hadCandidates = false; // true once at least one real "attacking a groupmate" candidate was scored
    };

    class ThreatEvaluator
    {
    public:
        // True when `candidate` is attacking a groupmate other than `tank` at all -- i.e. a real
        // pickup candidate exists, independent of how it ultimately scores. Split out from
        // ScoreThreatTarget (review finding #2 on the Phase 2 fixup pass): ScoreThreatTarget's
        // own re-taunt-war and distance penalties can legitimately push a genuine candidate's
        // score below 0 (e.g. another tank already holds it, or it's at the edge of `range`) --
        // using that same "score < 0" check to also mean "not a candidate at all" made
        // SelectThreatDecision report hadCandidates=false for those cases, which then let the old
        // unscored first-match FindAllyThreatenedTarget fallback re-pick exactly the target the
        // scorer had deliberately deprioritized.
        static bool IsThreatCandidate(Player* tank, Unit* candidate);

        // Higher is better among real candidates (see IsThreatCandidate) -- ranks how much a
        // legitimate pickup target actually matters right now. Meaningless (and not guaranteed
        // sign-consistent) when called on something IsThreatCandidate would reject.
        static float ScoreThreatTarget(Player* tank, Unit* candidate);

        // Best enemy for `tank` to pick up right now among ones attacking a groupmate within
        // range -- see ThreatDecision's own comment on why hadCandidates matters to the caller.
        static ThreatDecision SelectThreatDecision(Player* tank, float range = 30.0f);
    };
}

#endif // COA_PLAYERBOTS_THREAT_EVALUATOR_H
