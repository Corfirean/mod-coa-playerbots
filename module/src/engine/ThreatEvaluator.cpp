/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: ThreatEvaluator implementation
 */

#include "engine/ThreatEvaluator.h"
#include "engine/SpellPredicates.h"
#include "BotAI.h"
#include "Group.h"
#include "Player.h"
#include <vector>

namespace BotAI
{
    float ThreatEvaluator::ScoreThreatTarget(Player* tank, Unit* candidate)
    {
        if (!tank || !candidate || !candidate->IsAlive())
            return -1.0f;

        Unit* victim = candidate->GetVictim();
        if (!victim || victim == tank || !victim->IsAlive())
            return -1.0f; // not threatening anyone else right now

        Player* victimPlayer = victim->ToPlayer();
        Group* group = tank->GetGroup();
        if (!victimPlayer || !group || victimPlayer->GetGroup() != group)
            return -1.0f; // ignore an enemy fighting some unrelated bystander

        float score = 100.0f;

        BotRole victimRole = BotAI::GetRole(victimPlayer->GetGUID());
        if (victimRole == BotRole::Healer)
            score += 300.0f;
        else if (victimPlayer->GetHealthPct() < 40.0f)
            score += 200.0f;
        // else: an ordinary DPS being tapped still matters at the +100 base above, just less.

        if (IsBossOrEliteTarget(candidate))
            score += 150.0f;

        // Avoid a pointless re-taunt war between two tank bots (item 9, Phase 2 fixup): the
        // signal for "someone else already has this" is the ENEMY targeting that other tank
        // (candidate->GetVictim() == member) -- two tanks can both legitimately be swinging on
        // the same boss (member->GetVictim() == candidate) without either one "holding" it, so
        // that direction must not be penalized, or an off-tank attacking the boss alongside the
        // main tank would itself look like a reason to deprioritize the boss.
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != tank && candidate->GetVictim() == member &&
                BotAI::GetRole(member->GetGUID()) == BotRole::Tank)
            {
                score -= 250.0f;
                break;
            }
        }

        score -= tank->GetDistance(candidate) * 1.5f;

        return score;
    }

    ThreatDecision ThreatEvaluator::SelectThreatDecision(Player* tank, float range)
    {
        ThreatDecision decision;
        if (!tank || !tank->GetGroup())
            return decision;

        std::vector<Unit*> enemies;
        GetNearbyEnemies(tank, tank, range, enemies);

        float bestScore = 0.0f;
        for (Unit* candidate : enemies)
        {
            float score = ScoreThreatTarget(tank, candidate);
            if (score < 0.0f)
                continue; // not attacking any groupmate at all -- not a real candidate

            decision.hadCandidates = true;
            if (score > bestScore)
            {
                bestScore = score;
                decision.target = candidate;
            }
        }
        return decision;
    }
}
