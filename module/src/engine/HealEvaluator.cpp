/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: HealEvaluator implementation
 */

#include "engine/HealEvaluator.h"
#include "engine/CombatReservations.h"
#include "engine/DamageTracker.h"
#include "engine/SpellPredicates.h"
#include "BotAI.h"
#include "Group.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <unordered_map>

namespace BotAI
{
    namespace
    {
        struct RangeCacheEntry
        {
            float range = 0.0f;
            uint32 expiresAt = 0;
        };
        // [botGuid] -> cached BestKnownHealRange result. A spellbook scan is too heavy to repeat
        // every healer decision tick; the spellbook itself changes rarely (level up, learn a new
        // rank), so a short TTL is plenty fresh without re-scanning constantly.
        std::unordered_map<ObjectGuid, RangeCacheEntry> s_healRangeCache;
        constexpr uint32 HEAL_RANGE_CACHE_TTL_MS = 15000;
    }

    namespace
    {
        // Below this missing-HP%, a target is only worth healing if something else makes it
        // urgent (see ShouldConsiderHealing) -- otherwise a healer would chase a 98%-HP ally for
        // a routine top-off, which is what the old flat `score > 5` threshold effectively did.
        constexpr float MIN_MISSING_HP_PCT = 5.0f;
        // Incoming damage expressed as a fraction of max health per second -- gear/level
        // independent, unlike a raw incomingDps number (see item 10's own comment).
        constexpr float SIGNIFICANT_INCOMING_PCT_PER_SEC = 2.0f;
        constexpr float URGENT_TTD_SEC = 8.0f;
    }

    bool HealEvaluator::ShouldConsiderHealing(Player* healer, Player* candidate)
    {
        if (!healer || !candidate || !candidate->IsAlive() || !candidate->IsInWorld())
            return false;
        if (candidate->GetMap() != healer->GetMap())
            return false;

        float maxHealth = float(candidate->GetMaxHealth());
        if (maxHealth <= 0.0f)
            return false;

        if (100.0f - candidate->GetHealthPct() >= MIN_MISSING_HP_PCT)
            return true;

        float incomingDps = DamageTracker::SampleIncomingDps(candidate);
        if (incomingDps > 1.0f)
        {
            if ((incomingDps / maxHealth) * 100.0f >= SIGNIFICANT_INCOMING_PCT_PER_SEC)
                return true;
            if (float(candidate->GetHealth()) / incomingDps < URGENT_TTD_SEC)
                return true;
        }

        if (candidate->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) || candidate->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE_PERCENT))
            return true;

        return false;
    }

    float HealEvaluator::ScoreHealUrgency(Player* healer, Player* candidate)
    {
        if (!healer || !candidate || !candidate->IsAlive() || !candidate->IsInWorld())
            return -1.0f;
        if (candidate->GetMap() != healer->GetMap())
            return -1.0f;

        float maxHealth = float(candidate->GetMaxHealth());
        if (maxHealth <= 0.0f)
            return -1.0f;

        // Missing health, net of what other healers have already committed to landing on this
        // target (item 11) -- a second healer shouldn't also chase a target that's about to be
        // topped off by someone else's heal already in flight.
        float missingHealth = maxHealth - float(candidate->GetHealth());
        uint32 reserved = CombatReservations::GetReservedIncomingHeal(candidate->GetGUID(), healer->GetGUID());
        missingHealth = std::max(0.0f, missingHealth - float(reserved));
        float missingPct = missingHealth / maxHealth * 100.0f;

        float score = missingPct * 2.0f;

        // Incoming-damage trend (cheap rolling estimate, see DamageTracker), normalized to
        // %-of-max-health-per-second (item 10, Phase 2 fixup) -- a raw incomingDps number scales
        // with gear/level (a huge-HP-pool tank takes huge raw hits at the same relative danger as
        // a squishy DPS), so a tank with a big health pool no longer automatically dominates
        // scoring just by having more absolute HP to lose per second.
        float incomingDps = DamageTracker::SampleIncomingDps(candidate);
        float incomingPctPerSecond = (incomingDps / maxHealth) * 100.0f;
        score += incomingPctPerSecond * 5.0f;

        // TTD stays in real seconds (not normalized) -- explicitly called out as its own
        // important factor regardless of scale: a 3-second TTD matters the same whether it's a
        // huge tank or a squishy DPS about to die.
        float timeToDieSec = (incomingDps > 1.0f) ? (float(candidate->GetHealth()) / incomingDps) : 999.0f;
        if (timeToDieSec < URGENT_TTD_SEC)
            score += (URGENT_TTD_SEC - timeToDieSec) * 30.0f;

        if (BotAI::GetRole(candidate->GetGUID()) == BotRole::Tank)
            score *= 1.3f;

        // An enemy is actively engaged with this ally right now (not just "took damage at some
        // point") -- getAttackers() is who's currently in melee combat with them.
        if (!candidate->getAttackers().empty())
            score += 40.0f;

        // A real damage-over-time debuff (not just any negative aura, which would also fire on
        // ordinary armor/stat debuffs that aren't urgent) -- a dangerous, stacking threat.
        if (candidate->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) || candidate->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE_PERCENT))
            score += 15.0f;

        score -= healer->GetDistance(candidate) * 0.5f;

        return score;
    }

    Player* HealEvaluator::SelectBestHealTarget(Player* healer, float maxRange)
    {
        if (!healer)
            return nullptr;

        Player* best = nullptr;
        float bestScore = -1.0f;

        auto consider = [&](Player* candidate)
        {
            if (!candidate || !candidate->IsAlive() || !candidate->IsInWorld() || candidate->GetMap() != healer->GetMap())
                return;
            if (healer->GetDistance(candidate) > maxRange)
                return;
            // Eligibility gate first (item 11) -- scoring only ranks candidates that already
            // passed it, so there's no separate magic score threshold to keep in sync with it.
            if (!ShouldConsiderHealing(healer, candidate))
                return;

            float score = ScoreHealUrgency(healer, candidate);
            if (score > bestScore)
            {
                bestScore = score;
                best = candidate;
            }
        };

        consider(healer);
        if (Group* group = healer->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                consider(ref->GetSource());

        return best;
    }

    float HealEvaluator::BestKnownHealRange(Player* healer, float fallback)
    {
        if (!healer)
            return fallback;

        ObjectGuid guid = healer->GetGUID();
        uint32 now = getMSTime();
        auto itr = s_healRangeCache.find(guid);
        if (itr != s_healRangeCache.end() && now < itr->second.expiresAt)
            return itr->second.range;

        float maxRange = 0.0f;
        for (auto const& [spellId, playerSpell] : healer->GetSpellMap())
        {
            if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                continue;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!IsUsableHealSpell(spellInfo))
                continue;

            float range = spellInfo->GetMaxRange(true, healer);
            if (range > maxRange)
                maxRange = range;
        }

        float result = maxRange > 0.0f ? maxRange : fallback;
        s_healRangeCache[guid] = RangeCacheEntry{ result, now + HEAL_RANGE_CACHE_TTL_MS };
        return result;
    }

    void HealEvaluator::ForgetBot(ObjectGuid botGuid)
    {
        s_healRangeCache.erase(botGuid);
    }
}
