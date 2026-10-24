# Training results

All runs: sandbox world (core 12f459cff + all modules except stock Zyth playerbots), level-60 dedicated actors in the gear the live gear-up gives them
(`train livegear <guid> new`), three or five seeds per episode, paired by (scenario, candidate, seed). Pilot team: Venomancer Fortitude (tank, 106035),
Venomancer Vizier (healer, 106036), Templar Zealot (melee dps, 106037), Tinker Mechanics (ranged dps, 106038), Stormbringer Wind (support, 106039).
Tools: `tools/combat-intelligence/ci_plan_run.py`, `ci_compare.py` (paired differences, 95% interval from the paired spread; a difference whose interval
spans zero is noise). Results folders live in `.agents/training-live/ci-results/` (not committed: gear, characters and traces are local).

## Run register

| Label | Binary | What differs | Episodes |
|---|---|---|---|
| baseline | `worldserver-base.exe` | module `966e054` + cast telemetry | 21 |
| cand1 | `worldserver-cand1.exe` | + heal-waste gate, healer-first cleanse/kick, Fortitude profile fix | 21 |
| base2 | `worldserver-base2.exe` | baseline + `CoaBots.Training.RealisticDamage` | 21 |
| cand2 | `worldserver-cand2.exe` | cand1 + group interrupt director + `IsEatingOrDrinking` | 21 |
| allspecs-cand2 | cand2 | `ST_STATIC` 30 s, one seed, every one of the 70 specs | 70 |
| burst-off / burst-on / burst-on15 | cand3 / cand3 / cand4 | `TANKBUSTER_TELEGRAPH`, burst prediction off / on (25%) / on (15%) | 5 each |

## Findings

**Healer (Vizier), HEAL_RAMP + HEAL_POOL, 6 pairs.** baseline vs cand1: HPS -3.8% (+-9%), no out-of-mana time either side. base2 vs cand2: HPS +4.7% (+-10%),
deaths -0.17 and critical-person-seconds -1.1 (+-1.3). **No significant difference.** 25% of the Vizier's accepted casts land on allies at 95% health or
more in both builds, all heal-over-time upkeep (see F-001).

**Tank (Fortitude), SURVIVAL + THREAT_STRESS, 6 pairs.** The new defensives are used (Regrow Exoskeleton and Lifeblood once per 60 s episode) but
`SURVIVAL`'s `tankbuster` has no cast bar, so there is nothing to react to and damage taken is identical (6284 with realistic damage, 7200 without). DPS -6.9% (+-15%).
Realistic damage turns 7200 into 6284: armor and damage-taken auras now apply (F-018).

**Telegraphed tankbuster, 5 seeds.** Prediction off: no defensive cast, first hit ~1639. Prediction on at the default 25%: the hit is 19-20% of this tank's health, below
the threshold, so identical to off. Prediction on at 15%: Regrow Exoskeleton and Lifeblood are cast once before the first hit and it lands ~1353 (-17%); the next three hits
(cooldowns 60 s / 90 s) are unmitigated. **The mechanism works; the budgeting of cooldowns across several busters is not built.**

**Interrupts, 27 interruptible boss casts per build.** Identical in both builds (18 interrupted, median reaction 110-115 ms). Group episodes interrupt every cast in both
builds because the old reservation already handled one boss; single-candidate episodes interrupt only what the candidate's own kick cooldown allows. The director's advantage
(several casters) is not exercised by any current scenario.

**All 70 specs, 30 s smoke.** Every spec loads its profile, casts, and finishes without an engine error (`SPEC_VALIDATION_MATRIX.md`). 202 descriptors were dead at level 60
(not in the spellbook, passive, or an id Spell.dbc does not contain). Healer dummy DPS is 0-351 (Vizier 0, Flameweaving 17, Moon Priest 96, Blessings 104): they hold
their damage back on a solo dummy; in group scenarios the Vizier does 35-40.

## Not measured yet
Mana sustainability beyond 30 s (needs `RESOURCE_ENDURANCE` or 180 s runs), threat margin and aggro recovery (THREAT_STRESS recovery 1-5 s, no change made), interrupts with two casters,
anything in a real dungeon with these changes.

## Role-wide first look (roles-cand4: one seed, 60 s, level 60, every tank / healer / support spec)

Not a paired test: one run per spec to find outliers.

**Tanks, THREAT_STRESS** (a damage dealer gets +1000 threat per second; losses of the boss in 60 s): Vanguard 0, Fortitude 1, Eternal 1, Defiance 4, Oathkeeper 4,
Mountain King 4, Seraphim 5, Tyrant 5, Dreadnought 5, Domination 6, Black Knight 6, Moon Guard 6. The first recovery takes about 1 s for everyone; every later one takes 4-6 s,
which is the taunts' 8 s cooldown, so recovery time is set by the spell, not by the AI. Specs with the lowest threat output (Moon Guard 188 dps, Black Knight 214, Domination 208) lose
the boss most. No AI idle time explains the gap (every tank fills its global cooldowns).

**Tanks, TANKBUSTER_TELEGRAPH** (default 25% threshold, the hits are ~19% of the tank): damage taken 4400-6446 for ten of twelve specs, Black Knight 1875 (its own shield/avoidance
handles the cast), no deaths.

**Healers, HEAL_RAMP** (ceiling = first party death): Heretic, Invention and Blessings survive the full 60 s; Vizier and Moon Priest die at hit ~2600, Brewing, Time and Flameweaving at ~2800.
All within 10%: no outlier. **HEAL_POOL throughput (HPS):** Blessings 1564, Vizier 1065, Moon Priest 965, Brewing 664, Heretic 623, Invention 609, Flameweaving 412, Time 384. Time and Flameweaving
reject their main heals as "can't afford" in 35% of decisions: they run out of mana first (in-combat regeneration in this core only comes from MP5 on gear).

**Supports, SUPPORT_GROUP:** every support spec places 2-4 distinct auras on all four party members (Wind: 707652 and 572399 on all four) - the earlier "zero Wind coverage" is gone.
