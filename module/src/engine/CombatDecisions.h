/*
 * mod-coa-playerbots
 *
 * Combat decisions that need numbers only: no core types, so production code and module/tests/combat_decisions_tests.cpp compile the very same
 * rules. The engine gathers the facts (health, absorbs, reservations, cast times) and asks these functions; nothing here reads game state.
 *
 *  - Healing economics: raw heal, effective heal, overheal, absorb handling (HealEvaluator / ActionEvaluator).
 *  - Dispel vs heal priority (CombatUtility).
 *  - Interrupt vs heal priority and interrupt-assignment timing (CombatUtility / GroupDirector).
 */

#ifndef COA_PLAYERBOTS_COMBAT_DECISIONS_H
#define COA_PLAYERBOTS_COMBAT_DECISIONS_H

#include "Define.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace BotAI::Decisions
{
    // ---------------------------------------------------------------------------------------------------------------------------------
    // Absorb shields
    // ---------------------------------------------------------------------------------------------------------------------------------
    // An absorb is not restored health: the deficit stays after the shield is gone. It only keeps part of the damage that arrives while it
    // lasts away from health. Everything below therefore uses the shield to reduce *damage that reaches health*, never missing health.

    constexpr uint32 ALL_SCHOOLS_MASK = 127;
    constexpr int32 SHIELD_NO_EXPIRY = -1;

    struct AbsorbShield
    {
        uint32 amount = 0;
        int32 remainingMs = SHIELD_NO_EXPIRY; // aura duration left; SHIELD_NO_EXPIRY for a shield without a timer
        uint32 schoolMask = ALL_SCHOOLS_MASK; // schools the shield soaks (MiscValue of SPELL_AURA_SCHOOL_ABSORB)
    };

    inline uint32 SchoolCount(uint32 mask)
    {
        mask &= ALL_SCHOOLS_MASK;
        uint32 n = 0;
        while (mask) { n += mask & 1u; mask >>= 1; }
        return n;
    }

    // How much of the shields can be counted on within the next `horizonMs`, against damage of `incomingSchoolMask` (0 = school unknown).
    // - A shield that expires before the horizon only counts for the share of the window it lasts (damage is assumed spread evenly).
    // - A shield soaking only some schools counts fully against damage of a school it covers, not at all against one it does not, and
    //   by the share of schools it covers when the incoming school is not known (so a physical-only shield is never taken for an all-school one).
    inline uint32 UsableAbsorb(AbsorbShield const* shields, size_t count, uint32 horizonMs, uint32 incomingSchoolMask)
    {
        double total = 0.0;
        for (size_t i = 0; i < count; ++i)
        {
            AbsorbShield const& s = shields[i];
            if (s.amount == 0 || s.remainingMs == 0)
                continue;
            double life = 1.0;
            if (s.remainingMs > 0 && horizonMs > 0 && uint32(s.remainingMs) < horizonMs)
                life = double(s.remainingMs) / double(horizonMs);
            double school;
            uint32 covered = s.schoolMask & ALL_SCHOOLS_MASK;
            if (incomingSchoolMask & ALL_SCHOOLS_MASK)
            {
                uint32 incoming = incomingSchoolMask & ALL_SCHOOLS_MASK;
                school = double(SchoolCount(covered & incoming)) / double(SchoolCount(incoming));
            }
            else
                school = double(SchoolCount(covered)) / 7.0;
            total += double(s.amount) * life * school;
        }
        return total >= 4294967295.0 ? 4294967295u : uint32(total);
    }

    // ---------------------------------------------------------------------------------------------------------------------------------
    // Direct heals
    // ---------------------------------------------------------------------------------------------------------------------------------

    // The share of the target's max health a heal of unknown size (script-only, no static amount) is assumed to restore. An explicit, mid-sized
    // assumption: such a heal is judged as if it were a 15% heal, so it is allowed on a real deficit and refused on a nearly full target.
    constexpr float UNKNOWN_HEAL_ASSUMED_FRACTION = 0.15f;
    // Smallest share of a direct heal that has to be useful (land on missing health) for the cast to be worth its mana while the target is safe.
    constexpr float MIN_USEFUL_HEAL_FRACTION = 0.4f;
    // Targets below this health are in danger: the heal is allowed whatever the arithmetic says.
    constexpr float HEAL_DANGER_HP_PCT = 60.0f;
    constexpr float HEAL_DANGER_TTD_SEC = 8.0f;

    struct HealForecast
    {
        uint32 raw = 0;                  // what the spell restores if all of it lands on missing health
        uint32 damageBeforeLanding = 0;  // damage the target is expected to take before the heal lands
        uint32 damageReachingHealth = 0; // ... after the usable absorb has taken its share
        uint32 projectedMissing = 0;     // missing health when the heal lands, net of heals already in flight
        uint32 effective = 0;            // min(raw, projectedMissing): the useful part
        uint32 overheal = 0;             // raw - effective
        bool amountKnown = true;
    };

    // `raw` 0 means the amount is unknown: the heal is then forecast with UNKNOWN_HEAL_ASSUMED_FRACTION of max health (amountKnown = false).
    // `reservedIncoming` is the effective healing other healers already have in flight on this target.
    inline HealForecast ForecastDirectHeal(uint32 raw, uint32 health, uint32 maxHealth, uint32 reservedIncoming, float incomingDps,
        uint32 castMs, uint32 usableAbsorb)
    {
        HealForecast f;
        f.amountKnown = raw > 0;
        f.raw = raw > 0 ? raw : uint32(double(maxHealth) * UNKNOWN_HEAL_ASSUMED_FRACTION);
        double dmg = incomingDps > 0.0f ? double(incomingDps) * double(castMs) / 1000.0 : 0.0;
        dmg = std::min(dmg, double(maxHealth));
        f.damageBeforeLanding = uint32(dmg);
        f.damageReachingHealth = f.damageBeforeLanding > usableAbsorb ? f.damageBeforeLanding - usableAbsorb : 0;
        uint32 missingNow = maxHealth > health ? maxHealth - health : 0;
        uint64 projected = std::min<uint64>(maxHealth, uint64(missingNow) + f.damageReachingHealth);
        f.projectedMissing = projected > reservedIncoming ? uint32(projected - reservedIncoming) : 0;
        f.effective = std::min(f.raw, f.projectedMissing);
        f.overheal = f.raw - f.effective;
        return f;
    }

    // What a healer reserves for others to see: the part of its heal that will land on health, never the raw spell amount (an over-sized heal on
    // a nearly full target would otherwise hide a real deficit from the next healer) and never the clipped-at-now amount (a heal landing in 3 s
    // on a target taking damage restores more than the deficit of this moment).
    inline uint32 ReservationAmount(HealForecast const& f) { return f.effective; }

    struct HealGateInput
    {
        HealForecast forecast;
        float targetHpPct = 100.0f;
        float timeToDieSec = 999.0f;
        bool lethalBurstIncoming = false;
    };

    // True when casting the direct heal is worth it. Danger always allows it; otherwise at least MIN_USEFUL_HEAL_FRACTION of the *raw* heal has to
    // be useful, so a 3000-point heal on a 150-point deficit is refused (5%) where a clipped estimate would have shown 100%.
    inline bool IsDirectHealWorthCasting(HealGateInput const& in)
    {
        if (in.targetHpPct < HEAL_DANGER_HP_PCT || in.timeToDieSec < HEAL_DANGER_TTD_SEC || in.lethalBurstIncoming)
            return true;
        HealForecast const& f = in.forecast;
        if (f.raw == 0)
            return true;
        return float(f.effective) >= float(f.raw) * MIN_USEFUL_HEAL_FRACTION;
    }

    // ---------------------------------------------------------------------------------------------------------------------------------
    // Time to die with absorbs, shield value
    // ---------------------------------------------------------------------------------------------------------------------------------

    // Seconds until health runs out at `incomingDps`, counting the usable absorb as extra health; 999 when nothing is hitting.
    inline float TimeToDieSec(uint32 health, uint32 usableAbsorb, float incomingDps)
    {
        if (incomingDps <= 1.0f)
            return 999.0f;
        return float(double(health) + double(usableAbsorb)) / incomingDps;
    }

    // True when a burst of `damage` leaves the target below `lethalMarginPct` of max health, counting what the usable absorb takes.
    inline bool BurstIsDangerous(float damage, uint32 health, uint32 maxHealth, uint32 usableAbsorb, float lethalMarginPct)
    {
        if (maxHealth == 0)
            return false;
        float reaching = std::max(0.0f, damage - float(usableAbsorb));
        return float(health) - reaching < float(maxHealth) * lethalMarginPct / 100.0f;
    }

    // A new absorb is redundant when shields already on the target cover what is expected to arrive during the shield's life and are at least
    // half as large as the new one; a target about to take a lethal hit is never "covered" by a shield that does not cover that hit.
    inline bool ShieldIsRedundant(uint32 existingUsable, uint32 addedEstimate, float expectedDamageInHorizon)
    {
        if (addedEstimate == 0)
            return false;
        return float(existingUsable) >= expectedDamageInHorizon && float(existingUsable) >= float(addedEstimate) * 0.5f;
    }

    // ---------------------------------------------------------------------------------------------------------------------------------
    // Dispel vs heal
    // ---------------------------------------------------------------------------------------------------------------------------------

    struct CleanseFacts
    {
        bool controlMechanic = false;     // stun, fear, charm, sleep, polymorph, disorient, horror, banish, shackle
        bool silenceMechanic = false;     // silence
        bool debuffOnSelf = false;        // the debuffed unit is the bot that would cast the cleanse
        float debuffDps = 0.0f;           // damage per second the debuff itself deals to the unit (0 when it deals none)
        float debuffedHealth = 0.0f;      // current health of the debuffed unit
        uint32 healLandingMs = 2500;      // how long a heal cast now takes to land (cast time + latency)
    };

    // True when removing the debuff cannot wait behind a heal for someone who is in danger:
    //  - it controls the unit (a feared, charmed or incapacitated groupmate acts against the group), or
    //  - it silences the bot itself (the heal cannot be cast at all until it is gone), or
    //  - the debuff alone would kill the unit before a heal could land.
    // A harmless debuff on a critical ally is NOT urgent: the heal comes first.
    inline bool CleanseOutranksHeal(CleanseFacts const& f)
    {
        if (f.controlMechanic)
            return true;
        if (f.silenceMechanic && f.debuffOnSelf)
            return true;
        if (f.debuffDps > 0.0f && f.debuffedHealth > 0.0f)
            return double(f.debuffDps) * double(f.healLandingMs) / 1000.0 >= double(f.debuffedHealth);
        return false;
    }

    // ---------------------------------------------------------------------------------------------------------------------------------
    // Interrupt vs heal
    // ---------------------------------------------------------------------------------------------------------------------------------

    // With a groupmate in danger, the healer's GCD goes to the heal unless the enemy cast is going to kill someone no heal can save: the
    // damage is at least what the target has plus the best heal the healer could land first. A cast that the heal can outlast stays a heal job.
    inline bool InterruptOutranksEmergencyHeal(float castDamage, float victimHealth, float bestHealEffective)
    {
        return castDamage > 0.0f && castDamage >= victimHealth + bestHealEffective;
    }

    // ---------------------------------------------------------------------------------------------------------------------------------
    // Interrupt assignment (GroupDirector)
    // ---------------------------------------------------------------------------------------------------------------------------------

    constexpr uint32 INTERRUPT_REACTION_BASE_MS = 250;
    constexpr uint32 INTERRUPT_REACTION_SPREAD_MS = 350;
    constexpr uint32 INTERRUPT_BACKUP_DELAY_MS = 600;
    constexpr uint32 INTERRUPT_MIN_REMAINING_MS = 200;
    // A bot the director has not heard from for this long is not taking part in the fight (not ticked, loading, frozen).
    constexpr uint32 INTERRUPT_BOT_STALE_MS = 1500;
    // An accepted kick that left the enemy cast running this long afterwards did not take effect (resisted, immune, wrong target).
    constexpr uint32 INTERRUPT_CONFIRM_MS = 450;

    struct InterruptCandidate
    {
        uint64 id = 0;
        int preference = 0;     // lower is better (damage dealer 0 ... healer 3)
        float distance = 0.0f;
        bool usable = false;    // alive, in world, same map, not casting, can interrupt this enemy right now
    };

    struct InterruptPair
    {
        int primary = -1; // index into the candidates, -1 none
        int backup = -1;
    };

    // Primary and backup among `candidates`, skipping those in `taken` (bots that are already primary on another cast: one bot can only do
    // one thing at a time). Order: preference, then distance, then id, so equal conditions give the same answer.
    inline InterruptPair ChooseInterrupters(InterruptCandidate const* candidates, size_t count, uint64 const* taken, size_t takenCount)
    {
        int order[64];
        size_t n = 0;
        for (size_t i = 0; i < count && n < 64; ++i)
        {
            if (!candidates[i].usable)
                continue;
            bool skip = false;
            for (size_t t = 0; t < takenCount; ++t)
                if (taken[t] == candidates[i].id)
                    skip = true;
            if (!skip)
                order[n++] = int(i);
        }
        std::sort(order, order + n, [&](int a, int b)
        {
            InterruptCandidate const& x = candidates[a];
            InterruptCandidate const& y = candidates[b];
            if (x.preference != y.preference) return x.preference < y.preference;
            if (x.distance != y.distance) return x.distance < y.distance;
            return x.id < y.id;
        });
        InterruptPair pair;
        if (n > 0) pair.primary = order[0];
        if (n > 1) pair.backup = order[1];
        return pair;
    }

    // A cast seen again under the same (enemy, spell) key is the same cast only while the earlier one had not ended; otherwise it is a new
    // cast that needs its own reaction time and its own assignment. Pushback moves the finish time but never past `now` before the cast ends.
    inline bool IsNewCastInstance(uint32 previousFinishMs, uint32 nowMs)
    {
        return previousFinishMs + 50 < nowMs;
    }

    struct InterruptTiming
    {
        uint32 now = 0;
        uint32 firstSeenMs = 0;
        uint32 finishMs = 0;
        uint32 reactionMs = 0;           // the bot's own stable reaction time
        bool isBackup = false;           // acting as backup (and not also the primary)
        bool primaryUsable = true;       // the primary can still do it
        bool primaryRefused = false;     // the primary's kick was refused by the game
        bool primaryUnconfirmed = false; // the primary's kick was accepted but the enemy is still casting
    };

    // May the bot act on this cast now? The backup waits for the primary only while the primary can still be expected to deliver.
    inline bool InterruptIsDue(InterruptTiming const& t)
    {
        if (t.finishMs <= t.now + INTERRUPT_MIN_REMAINING_MS)
            return false;
        uint32 due = t.firstSeenMs + t.reactionMs;
        if (t.isBackup && t.primaryUsable && !t.primaryRefused && !t.primaryUnconfirmed)
            due += INTERRUPT_BACKUP_DELAY_MS;
        return t.now >= due;
    }
}

#endif // COA_PLAYERBOTS_COMBAT_DECISIONS_H
