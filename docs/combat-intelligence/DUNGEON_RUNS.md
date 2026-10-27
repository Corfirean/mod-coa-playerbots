# Dungeon runs: every specialization in auto-dungeon, three runs each

210 runs (70 specializations x 3), second campaign (`campaign6`), core `12f459cff4d1`, module build `cand18` (module commits up to `9e39e06`). Harness:
`tools/combat-intelligence/ci_dungeon_run.py`; report: `ci_dungeon_report.py`; raw per-run JSON stays local in `.agents/training-live/dungeon-results/campaign6/` (not committed: it names
characters). An earlier campaign (`campaign5`) had two confounds found afterwards and its comparisons are withdrawn (see "What changed since the first campaign").

## What was run

- **Group:** five bots, the tank as leader. The specialization under test fills its role slot (tank, healer, or a damage/support slot); the other four are a fixed team in the same
  specs for every run (tank Fortitude, healer Vizier, damage Templar-Zealot-class and Mechanics, support Wind). Eight such fixed teams ran in parallel, one dungeon instance each.
- **Level and gear:** every bot is level 60 and wears what the live gear-up gives a level-60 bot of its spec (`train livegear <guid> new`). The content scaling module maps dungeons to
  effective levels at the 60 cap (Classic 1-45, TBC 45-55, WotLK 55-60). Level-60 bots in this gear cannot make progress in the WotLK dungeons (0 bosses and 5-9 deaths per 15 minutes for the
  fixed team, healer and mana users out of mana), so the counted runs use six mid-level dungeons the team can play without wiping: Stratholme (mobs level 44), Underbog (49), Slave Pens,
  Blood Furnace (48), Uldaman (30), Zul'Farrak (35). All are far below the level-60 requirement, so no entry requirement was violated; the choice is about how hard the content is for these bots.
- **Run:** normal difficulty, auto-dungeon on (`botcmd train dauto`), ten minutes, ended early only when the dungeon is cleared, the group wipes three times, or nothing moves for four minutes.
  The group is brought to full health before it enters; nothing heals, buffs or resurrects anybody during a run.
- **Warm-up:** a bot that was just re-specced and re-equipped (`train livegear` removes every aura and resets the bot's AI state) died in its first fight in 29 of 33 runs, against 14 of 196
  for the fixed team. Each specialization therefore plays an uncounted four-minute warm-up run first.

## What changed since the first campaign

1. **Dungeon parity.** In the first campaign the dungeon rotation gave some specs only the easy half of the list and others the hard half, and "vs role" compared a spec with the median of all
   specs, so a spec's damage figure partly measured which dungeons it drew. Now each spec visits three consecutive dungeons of the list and "vs role" divides by the median of the **same role in
   the same dungeon**.
2. **Active combat seconds.** "In combat" includes minutes when a pack cannot be reached (Zul'Farrak, Slave Pens) and nobody can hurt it; those minutes counted as damage-free combat and
   deflated everybody's per-second damage. Damage and healing are now per second **in which the group actually dealt damage** (`activeSeconds`); `stalemateSeconds` is recorded separately.
3. Claims of the first campaign that depended on these two (the 20x spread between damage specs, rankings of the middle of the table) are withdrawn. The comparisons that survive are the ones
   large enough to hold under both corrections and listed below.

## Results at a glance

- **No wipes in 210 runs, 5 runs ended STUCK, 0 errors, 1 boss down in total (Uldaman).** Ten minutes is not enough for these groups to reach a boss: they spend it on trash (8-61 kills per ten
  minutes depending on the dungeon) and on resting between packs. Progress is therefore measured by kills, elite kills, deaths and what each bot did, not by bosses.
- **Dungeon difficulty for the fixed team** (group kills per ten minutes, median): Stratholme 61, Slave Pens 25, Uldaman 19, Blood Furnace 14, Underbog 14, Zul'Farrak 13. The spread is mostly
  how much trash there is and how far apart the packs are, not how hard the mobs hit.
- **Mana is the limit.** The lowest power reading is 0% in most healer runs and in many mana-using damage runs. The tank waits for 80% mana before every pull, so a group that runs dry spends most of
  its time resting. This matches the earlier finding that mana is not paced (F-007).
- **Damage specs far below the other damage specs of the same dungeons** (own damage per active second divided by the same-role median in the same dungeon): **Boltslinger 7%** (16 damage per second
  against ~300), War 40%, Demolition 43%, Archery 44%, Crusader 44%, Artificer 45%, Gladiator 54%, Sentinel 57%, Houndmaster 63%, Mechanics 63% (the fixed-team damage dealer, so its relative
  figure is also affected by the dungeon mix of the other specs), Valkyrie 64%, Inquisition 68%, Piety 69%. Boltslinger is not a rounding matter: either its rotation is almost entirely
  not casting, or its damage is not attributed to the bot (the ranged builder spells do not exist in its spellbook, finding F-016). War, Archery, Demolition, Crusader and Artificer also
  hit 0% power in their runs, so they are resource-starved rather than idle.
- **High end:** Animation 242%, Lightning 232%, Maelstrom 164%, Corruption 168%, Soul 175%, Rime 155%, Voodoo 146%, Geomancy 138%. Whether this is the spec or its pet/totem/area damage being attributed
  more completely has not been checked.
- **Tanks:** damage per second is a weak signal for a tank. Moon Guard 30%, Seraphim 34% and Domination 46% are lowest (the same three that lose aggro most in the threat-stress scenario); Eternal 194%,
  Defiance 188%, Dreadnought 154%, Fortitude 136% highest. None of the tanks wiped a group in these dungeons.
- **Healers:** healing per active second: Invention 372, Blessings 361, Brewing 336, Vizier 300, Time 293, Heretic 264, Flameweaving 228, Moon Priest 213. Flameweaving and Heretic are 22% below the
  healer median (not tested for significance; three runs each in different dungeons).
- **Support:** Fleshweaver (a healing support) did 36% of the support median damage with 169 healing per second; it is the only support that heals a lot.

## Problems the runs found

1. **Dungeon bots waited 150 seconds after every fight (fixed, `9205661`).** The tank holds the pull until everybody is above 80% mana (60% to start waiting) and 90% health; a bot only sat down to
   drink below 25-50% mana. A damage dealer at 55% mana was never asked to rest, so the group stood still for the whole wait after each pack. Rest thresholds in an auto-dungeon are now at least the tank's.
2. **The Black Knight tank (class 15, spec 97) got stuck** in 2 of 3 runs with "pull not ready: SpecStrategyReadinessFailed": the spec's readiness check required Blade Stance (aura 802002) and the bot does
   not put the stance back on after it died or was re-equipped. The check now fails only when the bot has the stance spell but is not in the stance, and the spec also requires 70% health
   (`c944614`); verified afterwards on build `cand19`: three 10-minute runs (Stratholme, Underbog, Slave Pens) ended TIMEOUT, none STUCK, with 18.6 group kills per ten minutes against 13.7 before.
3. **Three other STUCK runs:** Sanguine in Blood Furnace (nothing in reach of a Shadowmoon Adept, "made no progress"), Archery in Underbog ("pull not ready: MemberOutOfRange", the leader waits for a
   member that does not come), Piety in Slave Pens (navigation mesh has no path). 
4. **Navigation mesh refusals** in the scouting runs and in some campaign runs: Zul'Farrak, Slave Pens, Hellfire Ramparts, The Nexus, Mana-Tombs, Shattered Halls, Botanica, Uldaman, Razorfen Downs and
   Maraudon: "the navigation mesh has no path to this point". The tank stops walking toward the next boss, or a pack stays out of reach. These are map-data or route problems, not spec problems.
5. **A freshly equipped bot is much more fragile than a settled one** (see "Warm-up"); a test that swaps gear right before a run measures the swap.
6. **`dgroup locked` (gear kept as it is) halves the group's damage**, so the runs use unlocked gear. Unlocked bots loot and may upgrade their gear during a long campaign.

## Limits of these numbers

- Ten minutes, one fixed team, three dungeons per spec, one seed: per-spec figures are the median of three runs. Treat a gap of 25% or less as noise; the very low rows (Boltslinger, War, Demolition,
  Archery, Crusader, Artificer) and the very high rows are the findings.
- "Own dps" and "own hps" count what the damage and heal hooks attribute to the bot (owned pets are included); damage dealt by a totem, a ward or a ground effect may be missing.
- The reference median for a (role, dungeon) cell comes from the specs that happened to visit it (about 12 for damage, 3-4 for healers and tanks), so tank and healer percentages are rougher.
- WotLK-level content was measured only in a first batch of eight 15-minute runs and a scouting pass. Bosses were not killed, so boss mechanics, boss interrupts and cooldown use on boss fights are
  not exercised here.

## Per-spec and per-dungeon tables (generated)

210 runs of 70 specializations from `campaign6` (ten-minute auto-dungeon runs of a group of five in mid-level dungeons, level-60 bots in live gear; the spec under test fills its role slot, the other four are the fixed team). Group figures are for the whole group; "own" figures are the bot of the specialization. "vs role" = own damage (or healing) per combat second divided by the median of the same role in the same dungeon (median over the runs of the spec). Damage and healing are per active combat second (seconds in which the group dealt damage).

| Class | Spec | Name | Role | Runs | Outcomes | Group kills /10 min | Elite kills | Deaths (group) | Wipes | Own dps | vs role | Own hps | Own deaths | Lowest power % | Dungeons |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 12 | 1 | Headhunting | Dps | 3 | TIMEOUT 3 | 27.4 | 19 | 1 | 0 | 241 | 78% | 11 | 0 | 11 | sp, strat, ub |
| 12 | 2 | Brutality | Dps | 3 | TIMEOUT 3 | 17.6 | 14 | 0 | 0 | 294 | 98% | 14 | 0 | 11 | bf, sp, ub |
| 13 | 4 | Shadowhunting | Dps | 3 | TIMEOUT 3 | 14.7 | 11 | 0 | 0 | 216 | 97% | 27 | 0 | 36 | bf, uldaman, zf |
| 13 | 5 | Voodoo | Dps | 3 | TIMEOUT 3 | 23.4 | 18 | 0 | 0 | 413 | 146% | 31 | 0 | 36 | strat, uldaman, zf |
| 14 | 7 | Infernal | Dps | 3 | TIMEOUT 3 | 17.6 | 16 | 0 | 0 | 294 | 89% | 4 | 0 | 68 | sp, strat, ub |
| 14 | 8 | Slayer | Dps | 3 | TIMEOUT 3 | 13.7 | 9 | 0 | 0 | 274 | 89% | 20 | 0 | 34 | bf, sp, ub |
| 15 | 10 | Boltslinger | Dps | 3 | TIMEOUT 3 | 7.8 | 6 | 1 | 0 | 16 | 7% | 3 | 0 | 37 | bf, uldaman, zf |
| 15 | 11 | Houndmaster | Dps | 3 | TIMEOUT 3 | 11.7 | 10 | 1 | 0 | 212 | 63% | 16 | 1 | 19 | strat, uldaman, zf |
| 15 | 12 | Inquisition | Dps | 3 | TIMEOUT 3 | 14.7 | 14 | 0 | 0 | 221 | 68% | 26 | 1 | 36 | strat, ub, zf |
| 16 | 14 | Maelstrom | Dps | 3 | TIMEOUT 3 | 16.6 | 15 | 0 | 0 | 491 | 164% | 4 | 0 | 15 | bf, sp, uldaman |
| 16 | 15 | Lightning | Dps | 3 | TIMEOUT 3 | 22.5 | 22 | 0 | 0 | 516 | 232% | 5 | 0 | 41 | bf, uldaman, zf |
| 17 | 16 | Hellfire | Dps | 3 | TIMEOUT 3 | 21.5 | 18 | 0 | 0 | 319 | 106% | 8 | 0 | 0 | strat, uldaman, zf |
| 17 | 18 | War | Dps | 3 | TIMEOUT 3 | 24.4 | 13 | 0 | 0 | 128 | 40% | 15 | 0 | 0 | sp, strat, ub |
| 18 | 19 | Gladiator | Dps | 3 | TIMEOUT 3 | 13.7 | 12 | 0 | 0 | 165 | 54% | 17 | 0 | 30 | bf, sp, ub |
| 19 | 23 | Zealot | Dps | 3 | TIMEOUT 3 | 17.6 | 17 | 0 | 0 | 319 | 97% | 34 | 0 | 54 | strat, ub, zf |
| 19 | 24 | Crusader | Dps | 3 | TIMEOUT 3 | 22.5 | 13 | 0 | 0 | 137 | 44% | 24 | 0 | 57 | sp, strat, ub |
| 20 | 26 | Sanguine | Dps | 3 | STUCK 1, TIMEOUT 2 | 21.5 | 11 | 0 | 0 | 282 | 93% | 76 | 0 | 0 | bf, sp, uldaman |
| 20 | 27 | Accursed | Dps | 3 | TIMEOUT 3 | 14.7 | 13 | 0 | 0 | 299 | 110% | 31 | 1 | 0 | bf, uldaman, zf |
| 21 | 28 | Archery | Dps | 3 | STUCK 1, TIMEOUT 2 | 8.8 | 9 | 0 | 0 | 141 | 44% | 0 | 1 | 0 | strat, ub, zf |
| 21 | 30 | Brigand | Dps | 3 | TIMEOUT 3 | 19.6 | 13 | 0 | 0 | 386 | 125% | 10 | 0 | 4 | bf, sp, ub |
| 22 | 32 | Infinite | Dps | 3 | TIMEOUT 3 | 15.6 | 15 | 1 | 0 | 252 | 100% | 33 | 1 | 2 | bf, uldaman, zf |
| 22 | 33 | Artificer | Dps | 3 | TIMEOUT 3 | 15.6 | 14 | 1 | 0 | 98 | 45% | 13 | 0 | 0 | strat, uldaman, zf |
| 23 | 34 | Death | Dps | 3 | TIMEOUT 3 | 15.6 | 16 | 1 | 0 | 322 | 101% | 4 | 2 | 0 | strat, ub, zf |
| 23 | 35 | Animation | Dps | 3 | TIMEOUT 3 | 17.6 | 9 | 1 | 0 | 784 | 242% | 8 | 1 | 0 | sp, strat, ub |
| 23 | 36 | Rime | Dps | 3 | TIMEOUT 3 | 24.4 | 16 | 0 | 0 | 478 | 155% | 2 | 0 | 0 | bf, sp, ub |
| 24 | 38 | Incineration | Dps | 3 | TIMEOUT 3 | 19.5 | 18 | 0 | 0 | 297 | 118% | 0 | 0 | 0 | bf, uldaman, zf |
| 24 | 39 | Draconic | Dps | 3 | TIMEOUT 3 | 23.5 | 21 | 1 | 0 | 281 | 128% | 25 | 1 | 0 | strat, uldaman, zf |
| 25 | 41 | Corruption | Dps | 3 | TIMEOUT 3 | 28.3 | 13 | 0 | 0 | 519 | 168% | 27 | 0 | 38 | sp, strat, ub |
| 25 | 42 | Godblade | Dps | 3 | TIMEOUT 3 | 18.6 | 11 | 0 | 0 | 268 | 87% | 45 | 0 | 21 | bf, sp, ub |
| 26 | 44 | Sentinel | Dps | 3 | TIMEOUT 3 | 16.6 | 13 | 0 | 0 | 126 | 57% | 5 | 0 | 44 | strat, uldaman, zf |
| 26 | 45 | Warden | Dps | 3 | TIMEOUT 3 | 13.7 | 14 | 0 | 0 | 348 | 105% | 9 | 1 | 14 | strat, ub, zf |
| 27 | 46 | Piety | Dps | 3 | STUCK 1, TIMEOUT 2 | 12.4 | 11 | 0 | 0 | 207 | 69% | 6 | 0 | 50 | bf, sp, ub |
| 27 | 47 | Valkyrie | Dps | 3 | TIMEOUT 3 | 13.7 | 13 | 0 | 0 | 192 | 64% | 17 | 0 | 50 | bf, sp, uldaman |
| 28 | 49 | Demolition | Dps | 3 | TIMEOUT 3 | 7.8 | 8 | 1 | 0 | 138 | 43% | 2 | 1 | 48 | strat, ub, zf |
| 28 | 50 | Mechanics | Dps | 3 | TIMEOUT 3 | 19.5 | 9 | 0 | 0 | 195 | 63% | 9 | 0 | 13 | sp, strat, ub |
| 29 | 53 | Stalking | Dps | 3 | TIMEOUT 3 | 20.5 | 19 | 0 | 0 | 334 | 150% | 2 | 0 | 14 | bf, uldaman, zf |
| 29 | 54 | Rot | Dps | 3 | TIMEOUT 3 | 27.3 | 18 | 0 | 0 | 297 | 130% | 19 | 0 | 22 | strat, uldaman, zf |
| 30 | 55 | Soul | Dps | 3 | TIMEOUT 3 | 26.4 | 19 | 0 | 0 | 568 | 175% | 112 | 0 | 0 | sp, strat, ub |
| 30 | 56 | Harvest | Dps | 3 | TIMEOUT 3 | 14.7 | 14 | 0 | 0 | 446 | 138% | 89 | 0 | 0 | bf, sp, ub |
| 31 | 59 | Wildwalker | Dps | 3 | TIMEOUT 3 | 21.5 | 20 | 0 | 0 | 286 | 151% | 18 | 1 | 25 | strat, uldaman, zf |
| 32 | 61 | Engravement | Dps | 3 | TIMEOUT 3 | 13.7 | 12 | 1 | 0 | 277 | 85% | 9 | 1 | 47 | bf, sp, ub |
| 32 | 62 | Glyphic | Dps | 3 | TIMEOUT 3 | 22.5 | 13 | 1 | 0 | 390 | 147% | 3 | 0 | 45 | bf, sp, uldaman |
| 32 | 63 | Riftblade | Dps | 3 | TIMEOUT 3 | 14.6 | 12 | 0 | 0 | 130 | 75% | 3 | 0 | 0 | bf, uldaman, zf |
| 31 | 95 | Geomancy | Dps | 3 | TIMEOUT 3 | 27.4 | 15 | 0 | 0 | 461 | 138% | 26 | 0 | 32 | sp, strat, ub |
| 13 | 6 | Brewing | Healer | 3 | TIMEOUT 3 | 17.6 | 15 | 0 | 0 | 26 | 106% | 336 | 1 | 0 | strat, ub, zf |
| 22 | 31 | Time | Healer | 3 | TIMEOUT 3 | 14.7 | 11 | 0 | 0 | 31 | 100% | 293 | 0 | 6 | bf, sp, uldaman |
| 24 | 37 | Flameweaving | Healer | 3 | TIMEOUT 3 | 17.6 | 13 | 0 | 0 | 39 | 78% | 228 | 1 | 0 | bf, sp, uldaman |
| 25 | 40 | Heretic | Healer | 3 | TIMEOUT 3 | 14.7 | 14 | 0 | 0 | 56 | 78% | 264 | 1 | 0 | strat, ub, zf |
| 26 | 43 | Moon Priest | Healer | 3 | TIMEOUT 3 | 13.7 | 11 | 0 | 0 | 12 | 98% | 213 | 0 | 0 | bf, uldaman, zf |
| 28 | 51 | Invention | Healer | 3 | TIMEOUT 3 | 13.7 | 12 | 0 | 0 | 65 | 122% | 372 | 0 | 2 | bf, sp, ub |
| 27 | 98 | Blessings | Healer | 3 | TIMEOUT 3 | 15.6 | 14 | 0 | 0 | 35 | 141% | 361 | 1 | 49 | strat, uldaman, zf |
| 29 | 101 | Vizier | Healer | 3 | TIMEOUT 3 | 14.7 | 14 | 1 | 0 | 2 | 100% | 300 | 1 | 0 | strat, ub, zf |
| 12 | 3 | Ancestry | Support | 3 | TIMEOUT 3 | 19.5 | 14 | 0 | 0 | 231 | 119% | 13 | 1 | 5 | bf, sp, uldaman |
| 16 | 13 | Wind | Support | 3 | TIMEOUT 3 | 15.6 | 13 | 0 | 0 | 298 | 153% | 27 | 0 | 20 | bf, sp, ub |
| 18 | 20 | Inspiration | Support | 3 | TIMEOUT 3 | 18.6 | 13 | 1 | 0 | 194 | 100% | 6 | 0 | 1 | bf, sp, uldaman |
| 20 | 25 | Fleshweaver | Support | 3 | TIMEOUT 3 | 13.7 | 11 | 0 | 0 | 57 | 36% | 169 | 0 | 0 | bf, sp, ub |
| 21 | 29 | Farstrider | Support | 3 | TIMEOUT 3 | 8.8 | 7 | 0 | 0 | 134 | 100% | 12 | 0 | 14 | sp, strat, ub |
| 31 | 58 | Grovekeeper | Support | 3 | TIMEOUT 3 | 13.7 | 11 | 0 | 0 | 140 | 100% | 70 | 0 | 25 | bf, uldaman, zf |
| 14 | 9 | Tyrant | Tank | 3 | TIMEOUT 3 | 10.7 | 8 | 0 | 0 | 95 | 69% | 41 | 0 | 49 | bf, sp, uldaman |
| 17 | 17 | Defiance | Tank | 3 | TIMEOUT 3 | 13.7 | 13 | 0 | 0 | 691 | 188% | 20 | 1 | 0 | strat, ub, zf |
| 18 | 21 | Vanguard | Tank | 3 | TIMEOUT 3 | 13.7 | 13 | 0 | 0 | 209 | 107% | 10 | 0 | 0 | bf, uldaman, zf |
| 19 | 22 | Oathkeeper | Tank | 3 | TIMEOUT 3 | 20.5 | 17 | 0 | 0 | 182 | 93% | 90 | 0 | 3 | strat, uldaman, zf |
| 27 | 48 | Seraphim | Tank | 3 | TIMEOUT 3 | 11.7 | 9 | 0 | 0 | 58 | 34% | 50 | 0 | 40 | bf, uldaman, zf |
| 29 | 52 | Fortitude | Tank | 3 | TIMEOUT 3 | 17.6 | 13 | 2 | 0 | 186 | 136% | 41 | 3 | 0 | bf, sp, uldaman |
| 30 | 57 | Domination | Tank | 3 | TIMEOUT 3 | 9.8 | 7 | 0 | 0 | 63 | 46% | 26 | 1 | 0 | bf, sp, uldaman |
| 31 | 60 | Mountain King | Tank | 3 | TIMEOUT 3 | 12.7 | 12 | 0 | 0 | 388 | 126% | 45 | 1 | 9 | strat, ub, zf |
| 25 | 96 | Dreadnought | Tank | 3 | TIMEOUT 3 | 19.5 | 13 | 0 | 0 | 179 | 154% | 59 | 1 | 28 | bf, sp, uldaman |
| 15 | 97 | Black Knight | Tank | 3 | STUCK 2, TIMEOUT 1 | 13.7 | 9 | 0 | 0 | 195 | 67% | 181 | 0 | 35 | sp, strat, ub |
| 20 | 99 | Eternal | Tank | 3 | TIMEOUT 3 | 21.5 | 20 | 0 | 0 | 358 | 194% | 54 | 0 | 0 | strat, uldaman, zf |
| 26 | 100 | Moon Guard | Tank | 3 | TIMEOUT 3 | 20.5 | 10 | 0 | 0 | 112 | 30% | 10 | 1 | 24 | sp, strat, ub |

## By dungeon

| Dungeon | Runs | Mob level | Group kills /10 min (median) | Deaths (median) | Wipes | Bosses down | Outcomes |
|---|---|---|---|---|---|---|---|
| bf | 36 | 48 | 13.7 | 0 | 0 | 0 | STUCK 1, TIMEOUT 35 |
| sp | 36 | 60 | 24.9 | 0 | 0 | 0 | STUCK 2, TIMEOUT 34 |
| strat | 34 | 44 | 60.6 | 0 | 0 | 0 | STUCK 1, TIMEOUT 33 |
| ub | 35 | 49 | 13.7 | 0 | 0 | 0 | STUCK 1, TIMEOUT 34 |
| uldaman | 35 | 30 | 18.6 | 0 | 0 | 1 | TIMEOUT 35 |
| zf | 34 | 35 | 12.7 | 0 | 0 | 0 | TIMEOUT 34 |

## Runs that ended early or with an error

- `spec026-Sanguine-a2-bf` STUCK: AutoDungeon: tank 'Shani Darkwater': waiting: Glark Aguilar is low onSTUCK-DETECTOR: bot ' / AutoDungeon:STUCK-DETECTOR: bot 'Flora Gallina' made no progress against 'Shadowmoon Adept
- `spec028-Archery-a3-ub` STUCK: AutoDungeon: tank 'Luma Hamby': pull not ready: MemberOutOfRange. / AutoDungeon: tank 'Luma Hamby': pull not ready: MemberOutOfRange.
- `spec046-Piety-a2-sp` STUCK: AutoDungeon: movement of 'Misty Granger' refused: the navigation mesh has no path to this  / AutoDungeon: movement of 'Misty Granger' refused: the navigation mesh has no path to this 
- `spec097-BlackKnight-a1-strat` STUCK: AutoDungeon: tank 'Jerry Atwater': pull not ready: SpecStrategyReadinessFailed. / AutoDungeon: tank 'Jerry Atwater': pull not ready: SpecStrategyReadinessFailed.
- `spec097-BlackKnight-a3-sp` STUCK: AutoDungeon: tank 'Jerry Atwater': pull not ready: SpecStrategyReadinessFailed. / AutoDungeon: tank 'Jerry Atwater': pull not ready: SpecStrategyReadinessFailed.
