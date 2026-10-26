# Dungeon runs: every specialization in auto-dungeon, three runs each

210 runs (70 specializations x 3) on the isolated training server, core `12f459cff4d1`, module build `cand18` (module commits up to `9e39e06` plus the `dgroup locked` option, which the runs
do not use). Harness: `tools/combat-intelligence/ci_dungeon_run.py`; report: `ci_dungeon_report.py`; raw per-run JSON stays local in `.agents/training-live/dungeon-results/campaign5/`
(not committed: it names characters).

## What was run

- **Group:** five bots, the tank as leader. The specialization under test fills its role slot (tank, healer, or a damage/support slot); the other four are a fixed team in the same
  specs for every run (tank Fortitude, healer Vizier, damage Templar-Zealot-class and Mechanics, support Wind). Eight such fixed teams ran in parallel, one dungeon instance each.
- **Level and gear:** every bot is level 60 and wears what the live gear-up gives a level-60 bot of its spec (`train livegear <guid> new`). The content scaling module maps dungeons to
  effective levels at the 60 cap (Classic 1-45, TBC 45-55, WotLK 55-60). Level-60 bots in this gear **cannot** make progress in the WotLK dungeons (Utgarde Keep, Azjol-Nerub, Ahn'kahet,
  Drak'Tharon, Halls of Stone: 0 bosses and 5-9 deaths per 15 minutes for the fixed team in the first batch, healer and mana users out of mana), so the counted runs use six mid-level
  dungeons the team can play without wiping: Stratholme (mobs level 44), Underbog (49), Slave Pens (49-50), Blood Furnace (48), Uldaman (30), Zul'Farrak (35). Every spec visited three of the six, in a rotation.
  All of them are far below the 60 requirement, so no entry requirement was violated; the choice is about how hard the content is for these bots.
- **Run:** normal difficulty, auto-dungeon on (`botcmd train dauto`), ten minutes, ended early only when the dungeon is cleared, the group wipes three times, or nothing moves for four minutes.
  The group is brought to full health before it enters; nothing heals, buffs or resurrects anybody during a run.
- **Warm-up:** an actor that was just re-specced and re-equipped (`train livegear` removes every aura and resets the bot's AI state) died in its first fight in 29 of 33 runs, against 14 of 196
  for the fixed team (the same bot, same slot, without that preparation: 1 of 6 against 4 of 5 with it, in a control run). Each specialization therefore plays an uncounted four-minute warm-up
  run first. With it the specialization under test died in 21 of 132 damage-dealer runs (16%), 8 of 36 tank runs, 5 of 24 healer runs and 1 of 18 support runs, against 41 of 840 for the fixed team (5%).

## Results at a glance

- **No wipes in 210 runs, 4 runs ended STUCK, 0 errors, 1 boss down in total.** Ten minutes is not long enough for these groups to reach a boss: they spend it on trash (5-63 kills per ten
  minutes depending on the dungeon) and on resting between packs. Progress is therefore measured by kills, elite kills, deaths and what each bot did, not by bosses.
- **Dungeon difficulty for the fixed team** (group kills per ten minutes, median): Stratholme 63, Slave Pens 25, Uldaman 18, Blood Furnace 15, Underbog 15, Zul'Farrak 10. The spread is mostly
  how much trash there is and how far apart the packs are, not how hard the mobs hit.
- **Mana is the limit.** The healer and the mana-using damage dealers reach 0% power in most runs; only Blessings (healer) never ran dry. The tank waits for 80% mana before every pull, so a
  group that runs dry spends most of its time resting. This matches the earlier finding that mana is not paced (F-007).
- **Damage output per bot varies by a factor of 20 between damage specs in the same slot** (median own damage per combat second: Lightning 865, Animation 777, Soul 699, Rime 689, Corruption 681,
  Wildwalker 678, Glyphic 622 ... Crusader 139, Inquisition 129, Archery 67, Sentinel 58, **Boltslinger 42**). The very low ones are not plausible damage for a level-60 spec in 140-ilvl gear and
  are worth a look before anything else: Boltslinger, Archery, Sentinel, Inquisition, Crusader, Gladiator (168), Infernal (189), Harvest (186), Artificer (199). Some may do their damage through a
  pet, a totem or a ground effect that the damage hook does not attribute to the bot; that has not been checked.
- **Tanks:** Eternal 547, Dreadnought 496 and Oathkeeper 430 damage per second are the highest; Seraphim (77), Domination (121) and Moon Guard (151) the lowest. Domination and Moon Guard are
  also two of the three tanks that lose aggro most in the threat-stress scenario (the third, Black Knight, is the spec that gets stuck below).
- **Healers:** healing per second 339-518 across the seven healers (Brewing 518, Blessings 477, Invention 436, Moon Priest 425, Heretic 362, Vizier 349, Time 348, Flameweaving 339). All but
  Blessings hit zero mana in the median run.

## Problems the runs found

1. **Dungeon bots waited 150 seconds after every fight (fixed in this round, `9205661`).** The tank holds the pull until everybody is above 80% mana (60% to start waiting) and 90% health; a bot
   only sat down to drink below 25-50% mana. A damage dealer at 55% mana was never asked to rest, so the group stood still for the whole wait after each pack. Rest thresholds in an auto-dungeon are
   now at least the tank's.
2. **The Black Knight tank (class 15, spec 97) gets stuck.** Two of its three runs ended STUCK with "pull not ready: SpecStrategyReadinessFailed": the spec's readiness check requires Blade Stance
   (aura 802002) and the bot does not put the stance back on after it died or was re-equipped. Needs a look at `ExecutePrePullStrategy` for `requiredState`.
3. **Navigation mesh refusals:** Zul'Farrak (Lightning, Rime runs) and, in the scouting runs, Hellfire Ramparts, The Nexus, Mana-Tombs, Shattered Halls, Botanica, Uldaman, Razorfen Downs and
   Maraudon: "the navigation mesh has no path to this point". The tank stops walking toward the next boss. Scholomance's entrance leaves the group stuck at the spawn. These are map-data or
   route problems, not spec problems; Ramparts never moved at all in an 8-minute run.
4. **A freshly equipped bot is much more fragile than a settled one** (see "Warm-up"): after `train livegear` the bot has no buffs and a blank AI state. Real players do not hit this, but it
   means a test that swaps gear right before a run measures the swap.
5. **`dgroup locked` (gear kept as it is) halves the group's damage** (the bots do about a quarter of their usual damage while training-locked), so the runs use unlocked gear. Unlocked bots loot and
   may upgrade their gear during a long campaign; the fixed team's average item level stayed in the 110-145 band.

## Limits of these numbers

- Ten minutes, one group, three dungeons per spec: the per-spec figures are the median of three runs and vary a lot between dungeons (a Stratholme run has ten times the kills of a Zul'Farrak run).
  Compare a spec with the other specs of its role in the same dungeons (the "vs role" column), not across roles.
- "Own dps" and "own hps" count what the damage and heal hooks attribute to the bot (owned pets are included); damage dealt by a totem, a ward or a ground effect may be missing.
- Level-60 bots in WotLK-level content were not measured beyond a first batch of eight 15-minute runs and a scouting pass; Classic dungeons far below the level were scouted only.
- Bosses were not killed, so boss mechanics, interrupts on bosses and cooldown use on boss fights are not exercised here.

## Per-spec and per-dungeon tables (generated)

| Class | Spec | Name | Role | Runs | Outcomes | Group kills /10 min | Elite kills | Deaths (group) | Wipes | Own dps | vs role | Own hps | Own deaths | Lowest power % | Dungeons |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 12 | 1 | Headhunting | Dps | 3 | TIMEOUT 3 | 26.4 | 14 | 0 | 0 | 305 | 93% | 8 | 0 | 17 | sp, strat, uldaman |
| 12 | 2 | Brutality | Dps | 3 | TIMEOUT 3 | 16.6 | 16 | 0 | 0 | 371 | 113% | 16 | 0 | 5 | bf, ub, zf |
| 13 | 4 | Shadowhunting | Dps | 3 | TIMEOUT 3 | 12.7 | 10 | 0 | 0 | 256 | 78% | 35 | 1 | 40 | bf, ub, zf |
| 13 | 5 | Voodoo | Dps | 3 | TIMEOUT 3 | 30.3 | 21 | 0 | 0 | 463 | 141% | 43 | 0 | 36 | sp, strat, uldaman |
| 14 | 7 | Infernal | Dps | 3 | TIMEOUT 3 | 23.5 | 13 | 0 | 0 | 189 | 58% | 10 | 1 | 53 | sp, strat, uldaman |
| 14 | 8 | Slayer | Dps | 3 | TIMEOUT 3 | 15.6 | 13 | 0 | 0 | 265 | 81% | 13 | 0 | 47 | bf, ub, zf |
| 15 | 10 | Boltslinger | Dps | 3 | TIMEOUT 3 | 6.8 | 6 | 0 | 0 | 42 | 13% | 40 | 0 | 38 | bf, ub, zf |
| 15 | 11 | Houndmaster | Dps | 3 | TIMEOUT 3 | 16.6 | 12 | 0 | 0 | 258 | 79% | 9 | 1 | 35 | sp, strat, uldaman |
| 15 | 12 | Inquisition | Dps | 3 | TIMEOUT 3 | 13.7 | 11 | 1 | 0 | 129 | 39% | 60 | 1 | 29 | bf, ub, zf |
| 16 | 14 | Maelstrom | Dps | 3 | TIMEOUT 3 | 26.4 | 17 | 1 | 0 | 517 | 158% | 9 | 2 | 16 | sp, strat, uldaman |
| 16 | 15 | Lightning | Dps | 3 | STUCK 1, TIMEOUT 2 | 17.6 | 15 | 0 | 0 | 865 | 264% | 11 | 0 | 42 | bf, ub, zf |
| 17 | 16 | Hellfire | Dps | 3 | TIMEOUT 3 | 30.3 | 15 | 0 | 0 | 459 | 140% | 14 | 0 | 0 | sp, strat, uldaman |
| 17 | 18 | War | Dps | 3 | TIMEOUT 3 | 29.3 | 13 | 1 | 0 | 456 | 139% | 17 | 0 | 0 | sp, strat, uldaman |
| 18 | 19 | Gladiator | Dps | 3 | TIMEOUT 3 | 14.7 | 12 | 0 | 0 | 168 | 51% | 4 | 0 | 40 | bf, ub, zf |
| 19 | 23 | Zealot | Dps | 3 | TIMEOUT 3 | 13.7 | 13 | 0 | 0 | 208 | 64% | 19 | 1 | 55 | bf, ub, zf |
| 19 | 24 | Crusader | Dps | 3 | TIMEOUT 3 | 23.5 | 12 | 0 | 0 | 139 | 42% | 19 | 1 | 54 | sp, strat, uldaman |
| 20 | 26 | Sanguine | Dps | 3 | TIMEOUT 3 | 26.4 | 16 | 0 | 0 | 436 | 133% | 100 | 1 | 0 | sp, strat, uldaman |
| 20 | 27 | Accursed | Dps | 3 | TIMEOUT 3 | 14.7 | 13 | 0 | 0 | 439 | 134% | 54 | 0 | 0 | bf, ub, zf |
| 21 | 28 | Archery | Dps | 3 | TIMEOUT 3 | 7.8 | 7 | 1 | 0 | 67 | 20% | 4 | 2 | 3 | bf, ub, zf |
| 21 | 30 | Brigand | Dps | 3 | TIMEOUT 3 | 13.7 | 11 | 1 | 0 | 293 | 90% | 3 | 1 | 0 | bf, ub, zf |
| 22 | 32 | Infinite | Dps | 3 | TIMEOUT 3 | 17.6 | 16 | 0 | 0 | 419 | 128% | 35 | 0 | 28 | bf, ub, zf |
| 22 | 33 | Artificer | Dps | 3 | TIMEOUT 3 | 25.4 | 14 | 0 | 0 | 199 | 61% | 30 | 0 | 1 | sp, strat, uldaman |
| 23 | 34 | Death | Dps | 3 | TIMEOUT 3 | 12.7 | 12 | 1 | 0 | 387 | 118% | 7 | 1 | 0 | bf, ub, zf |
| 23 | 35 | Animation | Dps | 3 | TIMEOUT 3 | 17.6 | 7 | 0 | 0 | 777 | 237% | 4 | 1 | 0 | sp, strat, uldaman |
| 23 | 36 | Rime | Dps | 3 | STUCK 1, TIMEOUT 2 | 12.7 | 12 | 0 | 0 | 689 | 210% | 12 | 0 | 0 | bf, ub, zf |
| 24 | 38 | Incineration | Dps | 3 | TIMEOUT 3 | 16.6 | 16 | 1 | 0 | 432 | 132% | 6 | 1 | 9 | bf, ub, zf |
| 24 | 39 | Draconic | Dps | 3 | TIMEOUT 3 | 25.4 | 17 | 0 | 0 | 544 | 166% | 11 | 0 | 4 | sp, strat, uldaman |
| 25 | 41 | Corruption | Dps | 3 | TIMEOUT 3 | 27.4 | 18 | 0 | 0 | 681 | 208% | 34 | 0 | 45 | sp, strat, uldaman |
| 25 | 42 | Godblade | Dps | 3 | TIMEOUT 3 | 16.6 | 14 | 0 | 0 | 298 | 91% | 45 | 0 | 26 | bf, ub, zf |
| 26 | 44 | Sentinel | Dps | 3 | TIMEOUT 3 | 14.7 | 10 | 1 | 0 | 58 | 18% | 0 | 1 | 53 | sp, strat, uldaman |
| 26 | 45 | Warden | Dps | 3 | TIMEOUT 3 | 19.5 | 19 | 2 | 0 | 560 | 171% | 19 | 2 | 12 | bf, ub, zf |
| 27 | 46 | Piety | Dps | 3 | TIMEOUT 3 | 17.6 | 17 | 0 | 0 | 354 | 108% | 6 | 0 | 47 | bf, ub, zf |
| 27 | 47 | Valkyrie | Dps | 3 | TIMEOUT 3 | 25.4 | 14 | 0 | 0 | 277 | 85% | 18 | 0 | 50 | sp, strat, uldaman |
| 28 | 49 | Demolition | Dps | 3 | TIMEOUT 3 | 5.9 | 6 | 1 | 0 | 212 | 65% | 9 | 4 | 47 | bf, ub, zf |
| 28 | 50 | Mechanics | Dps | 3 | TIMEOUT 3 | 18.6 | 11 | 0 | 0 | 231 | 70% | 4 | 0 | 8 | sp, strat, uldaman |
| 29 | 53 | Stalking | Dps | 3 | TIMEOUT 3 | 21.5 | 21 | 0 | 0 | 561 | 171% | 4 | 0 | 18 | bf, ub, zf |
| 29 | 54 | Rot | Dps | 3 | TIMEOUT 3 | 30.3 | 19 | 0 | 0 | 455 | 139% | 20 | 0 | 0 | sp, strat, uldaman |
| 30 | 55 | Soul | Dps | 3 | TIMEOUT 3 | 30.3 | 18 | 0 | 0 | 699 | 213% | 59 | 0 | 0 | sp, strat, uldaman |
| 30 | 56 | Harvest | Dps | 3 | TIMEOUT 3 | 11.7 | 9 | 0 | 0 | 186 | 57% | 33 | 0 | 0 | bf, ub, zf |
| 31 | 59 | Wildwalker | Dps | 3 | TIMEOUT 3 | 23.5 | 17 | 0 | 0 | 678 | 207% | 28 | 0 | 24 | sp, strat, uldaman |
| 32 | 61 | Engravement | Dps | 3 | TIMEOUT 3 | 15.6 | 13 | 0 | 0 | 333 | 102% | 7 | 0 | 48 | bf, ub, zf |
| 32 | 62 | Glyphic | Dps | 3 | TIMEOUT 3 | 26.4 | 19 | 0 | 0 | 622 | 190% | 3 | 0 | 47 | sp, strat, uldaman |
| 32 | 63 | Riftblade | Dps | 3 | TIMEOUT 3 | 14.7 | 12 | 1 | 0 | 450 | 137% | 6 | 2 | 13 | bf, ub, zf |
| 31 | 95 | Geomancy | Dps | 3 | TIMEOUT 3 | 25.4 | 14 | 0 | 0 | 583 | 178% | 36 | 0 | 46 | sp, strat, uldaman |
| 13 | 6 | Brewing | Healer | 3 | TIMEOUT 3 | 15.6 | 13 | 1 | 0 | 27 | 140% | 518 | 1 | 0 | bf, ub, zf |
| 22 | 31 | Time | Healer | 3 | TIMEOUT 3 | 25.4 | 12 | 0 | 0 | 53 | 94% | 348 | 1 | 0 | sp, strat, uldaman |
| 24 | 37 | Flameweaving | Healer | 3 | TIMEOUT 3 | 26.4 | 18 | 0 | 0 | 85 | 92% | 339 | 0 | 0 | sp, strat, uldaman |
| 25 | 40 | Heretic | Healer | 3 | TIMEOUT 3 | 13.7 | 12 | 0 | 0 | 54 | 98% | 362 | 1 | 0 | bf, ub, zf |
| 26 | 43 | Moon Priest | Healer | 3 | TIMEOUT 3 | 18.6 | 18 | 0 | 0 | 19 | 115% | 425 | 0 | 0 | bf, ub, zf |
| 28 | 51 | Invention | Healer | 3 | TIMEOUT 3 | 12.7 | 12 | 0 | 0 | 41 | 118% | 436 | 0 | 0 | bf, ub, zf |
| 27 | 98 | Blessings | Healer | 3 | TIMEOUT 3 | 25.4 | 19 | 0 | 0 | 64 | 129% | 477 | 1 | 88 | sp, strat, uldaman |
| 29 | 101 | Vizier | Healer | 3 | TIMEOUT 3 | 12.7 | 13 | 1 | 0 | 4 | 94% | 349 | 1 | 0 | bf, ub, zf |
| 12 | 3 | Ancestry | Support | 3 | TIMEOUT 3 | 28.3 | 19 | 1 | 0 | 384 | 193% | 2 | 1 | 2 | sp, strat, uldaman |
| 16 | 13 | Wind | Support | 3 | TIMEOUT 3 | 13.7 | 12 | 0 | 0 | 254 | 128% | 23 | 0 | 20 | bf, ub, zf |
| 18 | 20 | Inspiration | Support | 3 | TIMEOUT 3 | 23.5 | 16 | 0 | 0 | 252 | 127% | 5 | 0 | 3 | sp, strat, uldaman |
| 20 | 25 | Fleshweaver | Support | 3 | TIMEOUT 3 | 8.8 | 9 | 0 | 0 | 65 | 33% | 235 | 0 | 0 | bf, ub, zf |
| 21 | 29 | Farstrider | Support | 3 | TIMEOUT 3 | 10.8 | 9 | 0 | 0 | 173 | 87% | 13 | 0 | 35 | sp, strat, uldaman |
| 31 | 58 | Grovekeeper | Support | 3 | TIMEOUT 3 | 14.7 | 14 | 0 | 0 | 218 | 110% | 89 | 0 | 30 | bf, ub, zf |
| 14 | 9 | Tyrant | Tank | 3 | TIMEOUT 3 | 10.7 | 7 | 0 | 0 | 189 | 70% | 54 | 1 | 50 | sp, strat, uldaman |
| 17 | 17 | Defiance | Tank | 3 | TIMEOUT 3 | 13.7 | 11 | 1 | 0 | 298 | 111% | 31 | 1 | 0 | bf, ub, zf |
| 18 | 21 | Vanguard | Tank | 3 | TIMEOUT 3 | 11.7 | 11 | 1 | 0 | 276 | 103% | 25 | 1 | 1 | bf, ub, zf |
| 19 | 22 | Oathkeeper | Tank | 3 | TIMEOUT 3 | 28.3 | 19 | 0 | 0 | 430 | 160% | 122 | 1 | 9 | sp, strat, uldaman |
| 27 | 48 | Seraphim | Tank | 3 | TIMEOUT 3 | 10.8 | 9 | 0 | 0 | 77 | 29% | 78 | 0 | 27 | bf, ub, zf |
| 29 | 52 | Fortitude | Tank | 3 | TIMEOUT 3 | 28.3 | 15 | 0 | 0 | 293 | 109% | 50 | 0 | 0 | sp, strat, uldaman |
| 30 | 57 | Domination | Tank | 3 | TIMEOUT 3 | 23.5 | 12 | 0 | 0 | 121 | 45% | 39 | 0 | 0 | sp, strat, uldaman |
| 31 | 60 | Mountain King | Tank | 3 | TIMEOUT 3 | 16.6 | 15 | 1 | 0 | 373 | 139% | 54 | 2 | 18 | bf, ub, zf |
| 25 | 96 | Dreadnought | Tank | 3 | TIMEOUT 3 | 27.4 | 19 | 0 | 0 | 496 | 184% | 39 | 1 | 42 | sp, strat, uldaman |
| 15 | 97 | Black Knight | Tank | 3 | STUCK 2, TIMEOUT 1 | 17.6 | 8 | 0 | 0 | 256 | 95% | 182 | 1 | 32 | sp, strat, uldaman |
| 20 | 99 | Eternal | Tank | 3 | TIMEOUT 3 | 30.3 | 19 | 0 | 0 | 547 | 203% | 65 | 0 | 0 | sp, strat, uldaman |
| 26 | 100 | Moon Guard | Tank | 3 | TIMEOUT 3 | 15.6 | 9 | 1 | 0 | 151 | 56% | 12 | 0 | 23 | sp, strat, uldaman |

## By dungeon

| Dungeon | Runs | Mob level | Group kills /10 min (median) | Deaths (median) | Wipes | Bosses down | Outcomes |
|---|---|---|---|---|---|---|---|
| bf | 35 | 48 | 14.7 | 0 | 0 | 0 | TIMEOUT 35 |
| sp | 35 | 60 | 25.4 | 0 | 0 | 0 | STUCK 1, TIMEOUT 34 |
| strat | 35 | 44 | 63.5 | 0 | 0 | 0 | STUCK 1, TIMEOUT 34 |
| ub | 35 | 49 | 14.7 | 0 | 0 | 0 | TIMEOUT 35 |
| uldaman | 35 | 30 | 17.6 | 0 | 0 | 1 | TIMEOUT 35 |
| zf | 35 | 35 | 9.8 | 0 | 0 | 0 | STUCK 2, TIMEOUT 33 |

## Runs that ended early or with an error

- `spec015-Lightning-a2-zf` STUCK: AutoDungeon: movement of 'Flora Gallina' refused: the navigation mesh has no path to this  / AutoDungeon: movement of 'Flora Gallina' refused: the navigation mesh has no path to this 
- `spec036-Rime-a3-zf` STUCK: AutoDungeon: movement of 'Veira Dumas' refused: the navigation mesh has no path to this po / AutoDungeon: movement of 'Veira Dumas' refused: the navigation mesh has no path to this po
- `spec097-BlackKnight-a1-strat` STUCK: AutoDungeon: tank 'Jerry Atwater': pull not ready: SpecStrategyReadinessFailed. / AutoDungeon: tank 'Jerry Atwater': pull not ready: SpecStrategyReadinessFailed.
- `spec097-BlackKnight-a2-sp` STUCK: AutoDungeon: tank 'Jerry Atwater': pull not ready: SpecStrategyReadinessFailed. / AutoDungeon: tank 'Jerry Atwater': pull not ready: SpecStrategyReadinessFailed.
