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

        // Avoid a pointless re-taunt war between two tank bots -- if a different Tank-role
        // groupmate already has this exact enemy as ITS victim, someone's already handling it.
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != tank && member->GetVictim() == candidate &&
                BotAI::GetRole(member->GetGUID()) == BotRole::Tank)
            {
                score -= 250.0f;
                break;
            }
        }

        score -= tank->GetDistance(candidate) * 1.5f;

        return score;
    }

    Unit* ThreatEvaluator::SelectThreatTarget(Player* tank, float range)
    {
        if (!tank || !tank->GetGroup())
            return nullptr;

        std::vector<Unit*> enemies;
        GetNearbyEnemies(tank, tank, range, enemies);

        Unit* best = nullptr;
        float bestScore = 0.0f;
        for (Unit* candidate : enemies)
        {
            float score = ScoreThreatTarget(tank, candidate);
            if (score > bestScore)
            {
                bestScore = score;
                best = candidate;
            }
        }
        return best;
    }
}
