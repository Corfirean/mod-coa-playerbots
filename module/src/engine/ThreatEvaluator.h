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
    class ThreatEvaluator
    {
    public:
        // Higher is better. Only meaningful for an enemy currently attacking a groupmate other
        // than `tank` -- see SelectThreatTarget, which is the actual entry point.
        static float ScoreThreatTarget(Player* tank, Unit* candidate);

        // Best enemy for `tank` to pick up right now among ones attacking a groupmate within
        // range, or nullptr if none qualify -- same "nothing to react to" outcome as the old
        // first-match FindAllyThreatenedTarget, just choosing among candidates by score instead
        // of by scan order when more than one groupmate is under threat.
        static Unit* SelectThreatTarget(Player* tank, float range = 30.0f);
    };
}

#endif // COA_PLAYERBOTS_THREAT_EVALUATOR_H
