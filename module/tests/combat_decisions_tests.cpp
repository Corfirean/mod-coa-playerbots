// Tests for engine/CombatDecisions.h: the numbers-only rules behind healing economics, dispel/heal/interrupt priority and interrupt assignment.
#include "engine/CombatDecisions.h"
#include <cstdio>

using namespace BotAI::Decisions;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

// ---- helpers -------------------------------------------------------------------------------------------------------------------------
static HealGateInput Gate(uint32 raw, uint32 health, uint32 maxHealth, uint32 reserved = 0, float dps = 0.0f, uint32 castMs = 2500,
    uint32 absorb = 0, bool burst = false)
{
    HealGateInput in;
    in.forecast = ForecastDirectHeal(raw, health, maxHealth, reserved, dps, castMs, absorb);
    in.targetHpPct = maxHealth ? 100.0f * float(health) / float(maxHealth) : 0.0f;
    in.timeToDieSec = TimeToDieSec(health, absorb, dps);
    in.lethalBurstIncoming = burst;
    return in;
}

static void RawVersusEffective()
{
    // The review's example: 150 missing, 3000 heal. The clipped estimate said 100% useful; the real figure is 5%.
    HealForecast f = ForecastDirectHeal(3000, 9850, 10000, 0, 0.0f, 2500, 0);
    CHECK(f.raw == 3000 && f.effective == 150 && f.overheal == 2850);
    CHECK(!IsDirectHealWorthCasting(Gate(3000, 9850, 10000)));
    // The same target with a small heal is fine: 150 of 200 useful.
    CHECK(IsDirectHealWorthCasting(Gate(200, 9850, 10000)));
    // Full health: nothing useful.
    CHECK(!IsDirectHealWorthCasting(Gate(3000, 10000, 10000)));
    // 5% missing against a strong heal: refused (500 of 3000).
    CHECK(!IsDirectHealWorthCasting(Gate(3000, 9500, 10000)));
    // 60% missing (health 40%): danger override, even for a huge heal.
    CHECK(IsDirectHealWorthCasting(Gate(9000, 4000, 10000)));
    // 35% missing against a 3000 heal: 3500 >= 1200.
    CHECK(IsDirectHealWorthCasting(Gate(3000, 6500, 10000)));
    // Near death is always allowed.
    CHECK(IsDirectHealWorthCasting(Gate(9000, 800, 10000)));
}

static void ReservedHeals()
{
    // Two healers on one target missing 2000: the first reserves its effective part, the second sees 0 left and refuses to repeat it.
    HealForecast first = ForecastDirectHeal(2000, 8000, 10000, 0, 0.0f, 2500, 0);
    uint32 reserved = ReservationAmount(first);
    CHECK(reserved == 2000);
    HealGateInput second = Gate(2000, 8000, 10000, reserved);
    CHECK(second.forecast.effective == 0 && !IsDirectHealWorthCasting(second));
    // A huge first heal on a small deficit reserves only what lands (150), not 3000.
    CHECK(ReservationAmount(ForecastDirectHeal(3000, 9850, 10000, 0, 0.0f, 2500, 0)) == 150);
    // Second healer with a small heal while the first heal in flight still leaves a deficit.
    HealGateInput partial = Gate(1500, 7000, 10000, 1000);
    CHECK(partial.forecast.projectedMissing == 2000 && partial.forecast.effective == 1500);
    // Reservations can never exceed the deficit: no underflow when reserved > missing.
    CHECK(ForecastDirectHeal(1000, 9000, 10000, 5000, 0.0f, 0, 0).projectedMissing == 0);
}

static void IncomingDamageDuringCast()
{
    // 150 missing, 3000 heal, but the target takes 1000/s over a 2.5 s cast: 2650 of 3000 lands (88%) so the heal is worth it.
    HealForecast f = ForecastDirectHeal(3000, 9850, 10000, 0, 1000.0f, 2500, 0);
    CHECK(f.damageBeforeLanding == 2500 && f.projectedMissing == 2650 && f.effective == 2650);
    CHECK(IsDirectHealWorthCasting(Gate(3000, 9850, 10000, 0, 1000.0f, 2500)));
    // Instant heal: nothing arrives before it lands.
    HealForecast instant = ForecastDirectHeal(3000, 9850, 10000, 0, 1000.0f, 0, 0);
    CHECK(instant.damageBeforeLanding == 0 && instant.effective == 150);
    // The same damage with a strong absorb: only the part the shield does not take reaches health.
    HealForecast shielded = ForecastDirectHeal(3000, 9850, 10000, 0, 1000.0f, 2500, 2000);
    CHECK(shielded.damageReachingHealth == 500 && shielded.projectedMissing == 650);
    // Damage can not push the projection past max health.
    CHECK(ForecastDirectHeal(30000, 9000, 10000, 0, 100000.0f, 2500, 0).projectedMissing == 10000);
}

static void UnknownAmount()
{
    // A script-only heal: judged as a 15% heal. Allowed on a real deficit, refused on a nearly full target, allowed in danger.
    HealForecast known = ForecastDirectHeal(0, 7000, 10000, 0, 0.0f, 2500, 0);
    CHECK(!known.amountKnown && known.raw == 1500);
    CHECK(IsDirectHealWorthCasting(Gate(0, 7000, 10000)));
    CHECK(!IsDirectHealWorthCasting(Gate(0, 9900, 10000)));
    CHECK(IsDirectHealWorthCasting(Gate(0, 3000, 10000)));
}

static void AbsorbModel()
{
    // Not restored health: 40% HP with a strong shield still has 60% missing, the shield changes only what arrives.
    HealForecast noShield = ForecastDirectHeal(2000, 4000, 10000, 0, 0.0f, 2500, 0);
    HealForecast strongShield = ForecastDirectHeal(2000, 4000, 10000, 0, 0.0f, 2500, 5000);
    CHECK(noShield.projectedMissing == 6000 && strongShield.projectedMissing == 6000);
    CHECK(IsDirectHealWorthCasting(Gate(2000, 4000, 10000, 0, 0.0f, 2500, 5000)));
    // 95% HP with a strong shield: still a small deficit, the strong heal stays refused.
    CHECK(!IsDirectHealWorthCasting(Gate(3000, 9500, 10000, 0, 0.0f, 2500, 4000)));

    // Shield duration: a shield with 1 s left over a 4 s window counts a quarter; an unlimited one counts fully.
    AbsorbShield expiring{ 4000, 1000, ALL_SCHOOLS_MASK };
    AbsorbShield forever{ 4000, SHIELD_NO_EXPIRY, ALL_SCHOOLS_MASK };
    CHECK(UsableAbsorb(&expiring, 1, 4000, 0) == 1000);
    CHECK(UsableAbsorb(&forever, 1, 4000, 0) == 4000);
    AbsorbShield gone{ 4000, 0, ALL_SCHOOLS_MASK };
    CHECK(UsableAbsorb(&gone, 1, 4000, 0) == 0);

    // School: a physical-only shield soaks nothing of a fire hit, everything of a physical one, one seventh of damage of unknown school.
    AbsorbShield physical{ 7000, SHIELD_NO_EXPIRY, 1 };
    CHECK(UsableAbsorb(&physical, 1, 3000, 4) == 0);
    CHECK(UsableAbsorb(&physical, 1, 3000, 1) == 7000);
    CHECK(UsableAbsorb(&physical, 1, 3000, 0) == 1000);
    // A multi-school hit (fire+frost = 4|16) against a fire-only shield: half is covered.
    AbsorbShield fire{ 1000, SHIELD_NO_EXPIRY, 4 };
    CHECK(UsableAbsorb(&fire, 1, 3000, 4 | 16) == 500);

    // Overlapping shields from two healers add up (all schools).
    AbsorbShield two[2] = { { 1000, SHIELD_NO_EXPIRY, ALL_SCHOOLS_MASK }, { 1500, SHIELD_NO_EXPIRY, ALL_SCHOOLS_MASK } };
    CHECK(UsableAbsorb(two, 2, 3000, 0) == 2500);

    // Time to die counts the shield as health; a shield does not make a target "not hurt".
    CHECK(TimeToDieSec(4000, 0, 1000.0f) == 4.0f && TimeToDieSec(4000, 4000, 1000.0f) == 8.0f);
    CHECK(TimeToDieSec(4000, 4000, 0.0f) == 999.0f);

    // Burst: a 4500 hit on a full 10000-health tank leaves 5500 (fine); on 6000 health it leaves 1500 (<20%): dangerous;
    // with a 3000 shield it leaves 4500 (not dangerous); a shield smaller than the hit still leaves it dangerous.
    CHECK(!BurstIsDangerous(4500.0f, 10000, 10000, 0, 20.0f));
    CHECK(BurstIsDangerous(4500.0f, 6000, 10000, 0, 20.0f));
    CHECK(!BurstIsDangerous(4500.0f, 6000, 10000, 3000, 20.0f));
    CHECK(BurstIsDangerous(4500.0f, 6000, 10000, 300, 20.0f));
    // Imminent tankbuster on a 95% tank with no shield: 9500 - 8000 = 1500 left -> dangerous (pre-heal / pre-shield case).
    CHECK(BurstIsDangerous(8000.0f, 9500, 10000, 0, 20.0f));
    // Incoming damage larger than the remaining shield: the part beyond the shield reaches health.
    CHECK(ForecastDirectHeal(1000, 10000, 10000, 0, 2000.0f, 2500, 1000).damageReachingHealth == 4000);

    // Shield application: an equal shield already up while little is coming is redundant; with a lot coming it is not.
    CHECK(ShieldIsRedundant(3000, 3000, 1000.0f));
    CHECK(!ShieldIsRedundant(3000, 3000, 6000.0f));
    CHECK(!ShieldIsRedundant(500, 3000, 0.0f));
    CHECK(!ShieldIsRedundant(0, 0, 0.0f));
    // Heal-over-time + absorb: the HoT's own deficit logic is independent of the shield (the HoT is not a direct heal); the absorb only
    // changes damageReachingHealth, which never raises projectedMissing above the no-shield case.
    HealForecast withShield = ForecastDirectHeal(1000, 8000, 10000, 0, 800.0f, 2500, 1500);
    HealForecast withoutShield = ForecastDirectHeal(1000, 8000, 10000, 0, 800.0f, 2500, 0);
    CHECK(withShield.projectedMissing <= withoutShield.projectedMissing);
}

static void CleansePriority()
{
    CleanseFacts f;
    f.debuffedHealth = 1500.0f;
    f.healLandingMs = 2500;

    // Case A: critical ally, harmless magic debuff (no damage, no control): the heal comes first.
    CHECK(!CleanseOutranksHeal(f));
    // Case B: lethal dispellable DoT: 800/s over a 2.5 s heal is 2000 >= 1500 health, the dispel is the action that prevents the death.
    f.debuffDps = 800.0f;
    CHECK(CleanseOutranksHeal(f));
    // A DoT that the heal outlasts (200/s -> 500 < 1500) is not urgent.
    f.debuffDps = 200.0f;
    CHECK(!CleanseOutranksHeal(f));
    // A faster heal changes the arithmetic: 600/s, instant heal (500 ms with latency) -> 300 < 1500: heal.
    f.debuffDps = 600.0f; f.healLandingMs = 500;
    CHECK(!CleanseOutranksHeal(f));
    f.healLandingMs = 2500;
    CHECK(CleanseOutranksHeal(f));

    // Case C: silence on the healer itself is urgent; silence on someone else is not.
    CleanseFacts silence;
    silence.silenceMechanic = true; silence.debuffOnSelf = true; silence.debuffedHealth = 5000.0f;
    CHECK(CleanseOutranksHeal(silence));
    silence.debuffOnSelf = false;
    CHECK(!CleanseOutranksHeal(silence));

    // Control mechanics stay urgent (charm, fear, polymorph, stun) whoever has them.
    CleanseFacts control;
    control.controlMechanic = true; control.debuffedHealth = 9000.0f;
    CHECK(CleanseOutranksHeal(control));

    // A harmless debuff on a 15% ally is never urgent regardless of health.
    CleanseFacts low;
    low.debuffedHealth = 1500.0f;
    CHECK(!CleanseOutranksHeal(low));
    // Zero health or no dps do not divide or flip anything.
    CleanseFacts zero;
    zero.debuffDps = 1000.0f; zero.debuffedHealth = 0.0f;
    CHECK(!CleanseOutranksHeal(zero));
}

static void InterruptVersusHeal()
{
    // Case D: a cast that does 9000 to a tank with 2000 health when the best heal restores 4000: nothing saves it but the interrupt.
    CHECK(InterruptOutranksEmergencyHeal(9000.0f, 2000.0f, 4000.0f));
    // A 5000 hit on the same tank: a heal of 4000 on top of 2000 health survives it (6000 > 5000): the heal outranks the kick.
    CHECK(!InterruptOutranksEmergencyHeal(5000.0f, 2000.0f, 4000.0f));
    // Exactly lethal even after the heal counts as lethal.
    CHECK(InterruptOutranksEmergencyHeal(6000.0f, 2000.0f, 4000.0f));
    // No damage known: never.
    CHECK(!InterruptOutranksEmergencyHeal(0.0f, 2000.0f, 4000.0f));
}

static InterruptCandidate Cand(uint64 id, int pref, float dist, bool usable = true)
{
    InterruptCandidate c; c.id = id; c.preference = pref; c.distance = dist; c.usable = usable; return c;
}

static void InterruptAssignment()
{
    // 1. One caster, one interrupter.
    InterruptCandidate one[1] = { Cand(1, 0, 10) };
    InterruptPair p = ChooseInterrupters(one, 1, nullptr, 0);
    CHECK(p.primary == 0 && p.backup == -1);

    // 2. One caster, two interrupters: damage dealer before healer; the other is the backup.
    InterruptCandidate two[2] = { Cand(10, 3, 5), Cand(11, 0, 30) };
    p = ChooseInterrupters(two, 2, nullptr, 0);
    CHECK(p.primary == 1 && p.backup == 0);

    // 3. Two casters, two interrupters: the first cast takes the better bot; the second cast must not use a bot already taken.
    InterruptCandidate pool[2] = { Cand(20, 0, 10), Cand(21, 0, 12) };
    InterruptPair first = ChooseInterrupters(pool, 2, nullptr, 0);
    uint64 taken[1] = { pool[first.primary].id };
    InterruptPair second = ChooseInterrupters(pool, 2, taken, 1);
    CHECK(first.primary == 0 && second.primary == 1 && second.backup == -1);

    // 4-7. An unusable candidate (stunned, no line of sight, cooldown spent, dead, out of range) is never primary: the next one takes over.
    InterruptCandidate failing[3] = { Cand(30, 0, 5, false), Cand(31, 1, 8), Cand(32, 2, 9) };
    p = ChooseInterrupters(failing, 3, nullptr, 0);
    CHECK(p.primary == 1 && p.backup == 2);
    InterruptCandidate none[2] = { Cand(40, 0, 5, false), Cand(41, 0, 6, false) };
    p = ChooseInterrupters(none, 2, nullptr, 0);
    CHECK(p.primary == -1 && p.backup == -1);

    // Equal conditions give a stable answer (id breaks the tie).
    InterruptCandidate tie[2] = { Cand(51, 0, 10), Cand(50, 0, 10) };
    CHECK(ChooseInterrupters(tie, 2, nullptr, 0).primary == 1);
}

static void InterruptTimingRules()
{
    InterruptTiming t;
    t.now = 1000; t.firstSeenMs = 1000; t.finishMs = 3500; t.reactionMs = 300;
    // Primary: not before its reaction time, then due.
    CHECK(!InterruptIsDue(t));
    t.now = 1300;
    CHECK(InterruptIsDue(t));
    // Backup waits an extra 600 ms while the primary can deliver.
    t.isBackup = true;
    CHECK(!InterruptIsDue(t));
    t.now = 1900;
    CHECK(InterruptIsDue(t));
    // The primary became unusable (stun, LoS, cooldown, death, map change): the backup acts after its reaction time only.
    t.now = 1300; t.primaryUsable = false;
    CHECK(InterruptIsDue(t));
    // The primary was refused or its kick did not take effect: same.
    t.primaryUsable = true; t.primaryRefused = true;
    CHECK(InterruptIsDue(t));
    t.primaryRefused = false; t.primaryUnconfirmed = true;
    CHECK(InterruptIsDue(t));
    // 9. The cast ends before the reaction time: never due. And a kick that would land after the cast resolved is not sent.
    InterruptTiming early;
    early.now = 1100; early.firstSeenMs = 1000; early.finishMs = 1200; early.reactionMs = 300;
    CHECK(!InterruptIsDue(early));
    early.now = 1400; early.finishMs = 1500; // 100 ms left < 200 ms
    CHECK(!InterruptIsDue(early));

    // 10. Two successive casts of the same spell: the earlier one ended, the later one is a new instance.
    CHECK(IsNewCastInstance(2000, 2300));
    CHECK(!IsNewCastInstance(2000, 1900));   // still casting
    CHECK(!IsNewCastInstance(2000, 2040));   // within the 50 ms slack
    CHECK(IsNewCastInstance(2000, 2051));
}

int main()
{
    RawVersusEffective();
    ReservedHeals();
    IncomingDamageDuringCast();
    UnknownAmount();
    AbsorbModel();
    CleansePriority();
    InterruptVersusHeal();
    InterruptAssignment();
    InterruptTimingRules();
    std::printf("combat_decisions_tests: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
