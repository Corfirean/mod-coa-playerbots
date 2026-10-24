/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: HealEvaluator implementation
 */

#include "engine/HealEvaluator.h"
#include "Config.h"
#include "engine/CombatReservations.h"
#include "engine/DamageTracker.h"
#include "engine/SpellPredicates.h"
#include "BotAI.h"
#include "Group.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"
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
        // A direct heal above this health is judged on how much of it would be useful; below it the heal is always allowed.
        constexpr float OVERHEAL_EXEMPT_HP_PCT = 60.0f;
        // Smallest share of a direct heal that has to land on missing health (after heals already in flight) for the
        // cast to be worth its mana, while the target is not in danger.
        constexpr float MIN_USEFUL_HEAL_FRACTION = 0.4f;
        // Pre-heal rules: only for a cast landing within this window, and only when the ally would end under this share of its health.
        constexpr uint32 BURST_LOOKAHEAD_MS = 4500;
        constexpr float LETHAL_MARGIN_PCT = 20.0f;
        constexpr float BURST_URGENCY_BONUS = 150.0f;
    }

    namespace
    {
        // What the ally's absorb shields still soak (damage that will not reach its health): a shield already on a target is healing that
        // has been done, so it must not be healed again as if the health were missing.
        uint32 RemainingAbsorb(Unit const* unit)
        {
            int64 total = 0;
            for (AuraEffect const* effect : unit->GetAuraEffectsByType(SPELL_AURA_SCHOOL_ABSORB))
                if (effect->GetAmount() > 0)
                    total += effect->GetAmount();
            return uint32(std::min<int64>(total, 0xFFFFFFFF));
        }

        // True when a damaging cast that an enemy is aiming at `ally` would leave the ally below LETHAL_MARGIN_PCT of its health
        // if nothing heals it first: the case for a pre-heal before a tankbuster, when the tank is not yet hurt.
        bool BurstWouldBeDangerous(Player* ally)
        {
            static bool const predictBurst = sConfigMgr->GetOption<bool>("CoaBots.Combat.BurstPrediction", true);
            if (!predictBurst)
                return false;
            uint32 maxHealth = ally->GetMaxHealth();
            if (!maxHealth)
                return false;
            for (Unit* attacker : ally->getAttackers())
            {
                float damage = 0.0f;
                uint32 remainingMs = 0;
                if (!GetDamagingCastOn(attacker, ally, damage, remainingMs) || remainingMs > BURST_LOOKAHEAD_MS)
                    continue;
                if (float(ally->GetHealth()) - damage < float(maxHealth) * LETHAL_MARGIN_PCT / 100.0f)
                    return true;
            }
            return false;
        }
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
        {
            // ...unless a shield on the ally already covers what is missing.
            float missing = maxHealth - float(candidate->GetHealth());
            if (float(RemainingAbsorb(candidate)) < missing)
                return true;
        }

        // Pre-heal: a damaging cast is about to land on this ally and would leave it dangerously low.
        if (BurstWouldBeDangerous(candidate))
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
        missingHealth = std::max(0.0f, missingHealth - float(reserved) - float(RemainingAbsorb(candidate)));
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

        // A cast that would nearly kill this ally is already on its way: heal it before the hit, not after.
        if (BurstWouldBeDangerous(candidate))
            score += BURST_URGENCY_BONUS;

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

    uint32 HealEvaluator::EstimateDirectHeal(Player* healer, Unit* target, SpellInfo const* spellInfo)
    {
        if (!healer || !target || !spellInfo)
            return 0;
        uint32 expected = 0;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            SpellEffectInfo const& effect = spellInfo->Effects[i];
            int32 base = effect.CalcValue(healer);
            if (base <= 0)
                continue;
            if (effect.Effect == SPELL_EFFECT_HEAL)
            {
                uint32 amount = healer->SpellHealingBonusDone(target, spellInfo, uint32(base), HEAL, i);
                expected += target->SpellHealingBonusTaken(healer, spellInfo, amount, HEAL);
            }
            else if (effect.Effect == SPELL_EFFECT_HEAL_PCT)
                expected += uint32(uint64(target->GetMaxHealth()) * uint32(base) / 100);
        }
        uint32 missing = target->GetMaxHealth() > target->GetHealth() ? target->GetMaxHealth() - target->GetHealth() : 0;
        return std::min(expected, missing);
    }

    bool HealEvaluator::IsDirectHealWorthCasting(Player* healer, Player* target, uint32 expectedHeal)
    {
        if (!ShouldConsiderHealing(healer, target))
            return false;
        if (expectedHeal == 0)
            return true;

        // A target in real danger is always worth a heal, whatever the numbers say.
        if (target->GetHealthPct() < OVERHEAL_EXEMPT_HP_PCT)
            return true;
        float incomingDps = DamageTracker::SampleIncomingDps(target);
        if (incomingDps > 1.0f && float(target->GetHealth()) / incomingDps < URGENT_TTD_SEC)
            return true;

        // What is still missing after the heals other healers already have in flight; a heal that would
        // fill less than MIN_USEFUL_HEAL_FRACTION of itself is mostly overheal.
        uint32 missing = target->GetMaxHealth() > target->GetHealth() ? target->GetMaxHealth() - target->GetHealth() : 0;
        uint32 reserved = CombatReservations::GetReservedIncomingHeal(target->GetGUID(), healer->GetGUID());
        uint32 netMissing = missing > reserved ? missing - reserved : 0;
        return float(netMissing) >= float(expectedHeal) * MIN_USEFUL_HEAL_FRACTION;
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
