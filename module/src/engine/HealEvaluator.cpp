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
        // How far ahead a shield has to last to be counted when judging how long an ally survives (about two heal casts).
        constexpr uint32 ABSORB_HORIZON_MS = 4000;
        constexpr uint32 MAX_TRACKED_SHIELDS = 16;
        // Pre-heal rules: only for a cast landing within this window, and only when the ally would end under this share of its health.
        constexpr uint32 BURST_LOOKAHEAD_MS = 4500;
        constexpr float LETHAL_MARGIN_PCT = 20.0f;
        constexpr float BURST_URGENCY_BONUS = 150.0f;
    }

    namespace
    {
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
                uint32 schoolMask = 0;
                if (!GetDamagingCastOn(attacker, ally, damage, remainingMs, &schoolMask) || remainingMs > BURST_LOOKAHEAD_MS)
                    continue;
                // A shield that is still up when the cast lands takes its share of the hit; one that expires first does not.
                uint32 absorb = HealEvaluator::UsableAbsorb(ally, std::max<uint32>(remainingMs, 1), schoolMask);
                if (Decisions::BurstIsDangerous(damage, ally->GetHealth(), maxHealth, absorb, LETHAL_MARGIN_PCT))
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

        // Missing health is missing health, shield or not: an absorb is not restored health, the deficit is still there when it is gone.
        if (100.0f - candidate->GetHealthPct() >= MIN_MISSING_HP_PCT)
            return true;

        // Pre-heal: a damaging cast is about to land on this ally and would leave it dangerously low.
        if (BurstWouldBeDangerous(candidate))
            return true;

        float incomingDps = DamageTracker::SampleIncomingDps(candidate);
        if (incomingDps > 1.0f)
        {
            if ((incomingDps / maxHealth) * 100.0f >= SIGNIFICANT_INCOMING_PCT_PER_SEC)
                return true;
            // Shields count as extra health for how long the ally lasts.
            if (Decisions::TimeToDieSec(candidate->GetHealth(), UsableAbsorb(candidate, ABSORB_HORIZON_MS, 0), incomingDps) < URGENT_TTD_SEC)
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
        float timeToDieSec = Decisions::TimeToDieSec(candidate->GetHealth(), UsableAbsorb(candidate, ABSORB_HORIZON_MS, 0), incomingDps);
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

    uint32 HealEvaluator::EstimateRawHeal(Player* healer, Unit* target, SpellInfo const* spellInfo)
    {
        if (!healer || !target || !spellInfo)
            return 0;
        uint64 expected = 0;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            SpellEffectInfo const& effect = spellInfo->Effects[i];
            if (effect.Effect == SPELL_EFFECT_HEAL_MAX_HEALTH)
            {
                expected += target->GetMaxHealth();
                continue;
            }
            int32 base = effect.CalcValue(healer);
            if (base <= 0)
                continue;
            if (effect.Effect == SPELL_EFFECT_HEAL)
            {
                uint32 amount = healer->SpellHealingBonusDone(target, spellInfo, uint32(base), HEAL, i);
                expected += target->SpellHealingBonusTaken(healer, spellInfo, amount, HEAL);
            }
            else if (effect.Effect == SPELL_EFFECT_HEAL_PCT)
                expected += uint64(target->GetMaxHealth()) * uint32(base) / 100;
        }
        return uint32(std::min<uint64>(expected, 0xFFFFFFFF));
    }

    uint32 HealEvaluator::UsableAbsorb(Unit const* unit, uint32 horizonMs, uint32 schoolMask)
    {
        if (!unit)
            return 0;
        Decisions::AbsorbShield shields[MAX_TRACKED_SHIELDS];
        size_t count = 0;
        for (AuraEffect const* effect : unit->GetAuraEffectsByType(SPELL_AURA_SCHOOL_ABSORB))
        {
            if (count >= MAX_TRACKED_SHIELDS)
                break; // bounded: further shields are ignored, which can only make the estimate conservative
            if (!effect || effect->GetAmount() <= 0)
                continue;
            Aura const* aura = effect->GetBase();
            shields[count].amount = uint32(effect->GetAmount());
            shields[count].remainingMs = aura ? aura->GetDuration() : Decisions::SHIELD_NO_EXPIRY;
            shields[count].schoolMask = uint32(effect->GetMiscValue());
            ++count;
        }
        return Decisions::UsableAbsorb(shields, count, horizonMs, schoolMask);
    }

    Decisions::HealForecast HealEvaluator::ForecastHeal(Player* healer, Unit* target, SpellInfo const* spellInfo)
    {
        uint32 raw = EstimateRawHeal(healer, target, spellInfo);
        uint32 castMs = spellInfo ? spellInfo->CalcCastTime(healer) : 0;
        uint32 reserved = CombatReservations::GetReservedIncomingHeal(target->GetGUID(), healer->GetGUID());
        // The shield has to last through the cast to take its share of what arrives meanwhile.
        uint32 absorb = UsableAbsorb(target, std::max<uint32>(castMs, 1), 0);
        return Decisions::ForecastDirectHeal(raw, target->GetHealth(), target->GetMaxHealth(), reserved,
            DamageTracker::SampleIncomingDps(target), castMs, absorb);
    }

    bool HealEvaluator::IsDirectHealWorthCasting(Player* healer, Player* target, SpellInfo const* spellInfo)
    {
        if (!ShouldConsiderHealing(healer, target))
            return false;

        Decisions::HealGateInput in;
        in.forecast = ForecastHeal(healer, target, spellInfo);
        in.targetHpPct = target->GetHealthPct();
        in.timeToDieSec = Decisions::TimeToDieSec(target->GetHealth(), UsableAbsorb(target, ABSORB_HORIZON_MS, 0),
            DamageTracker::SampleIncomingDps(target));
        in.lethalBurstIncoming = BurstWouldBeDangerous(target);
        return Decisions::IsDirectHealWorthCasting(in);
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
