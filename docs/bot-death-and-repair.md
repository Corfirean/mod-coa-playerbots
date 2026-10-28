# Open-world bot deaths, corpse runs and repairs (2026-10-27)

The owner reported that of ten level-20 bots in a group only two were alive, and that the bots were not doing
quests. The live server log (`world-console.log`, 16:12-16:51) and the database showed three connected faults.

## What was wrong

| Fault | Evidence | Effect |
|---|---|---|
| A released ghost could not walk. `BotMovement` refused every move of a bot that is not alive, so the corpse run never started. | 171 spirit releases and 168 returns in one run; the time from release to return clustered at 3.5 minutes, the length of the failsafe timer. | Each death cost three and a half minutes, far from the body. In a group most bots were dead at any time. |
| The failsafe revived with `ResurrectPlayer(0.5, true)`, which applies resurrection sickness (-75% stats, ten minutes at level 20). | The median time from coming back to the next death was 59 s; 106 of 125 bots died again within two minutes. | Death loop: revive, die at once, ghost, failsafe, revive. |
| Paid repair did nothing: `DurabilityRepairAll(true, ...)` returned 0 copper and mended nothing, even for bots holding 130 gold. | Sandbox: 806 "repaired ... for 0 copper" lines and 510 repair trips in ten minutes for 40 bots (29 trips per bot). Live database: 256 of 908 equipped items of the level 15-25 bots at 0 durability, 65 of 106 bots with a piece below 25%. | Broken gear gives nothing (a level-20 bot had 235 maximum health), the bots died more, and the repair errand paused their quest task again and again. |

## What changed

- `BotMovement::MoveToInternal` and `Navigate` accept a ghost for `MoveOwner::Corpse`. The corpse run uses `Navigate`
  (legs): one path request fails with "no path" beyond roughly 250 yards.
- The ghost drops whatever movement claim it held, is not pulled to the group leader (`TryFollowLeaderAcrossMaps`
  skips dead bots; `TryReturnGhostToCorpseMap` still covers a corpse on another map), stops 80% of the reclaim radius
  from the body, waits up to 45 s while awake hostile creatures stand within 24 yd of it, and then comes back without
  the reclaim delay (30/60/120 s) and without resurrection sickness.
- A ghost whose run is refused for 20 s comes back where it stands; the overall failsafe is 240 s.
- `TryMaintainEquipment`: what the paid repair leaves undone is repaired free.
- Rest thresholds are higher (start 60% health / 35% mana, stop at 88% / 75%; tanks and healers more), and a bot that
  just came back rests to 92%+ before it goes on (`recoverAfterDeath`).
- `BotDeath:` log lines (killer, level, rank, maximum health, position) and `CorpseRun:` refusal lines.

## Still open (measured, not fixed)

- Low-level combat is thin: 60-100% of the evaluator ticks of many level 19-22 bots find "no usable action" (most
  profile abilities are not known at that level), so they fight with white hits and die to single level 9-17 mobs.
  Level 20 bots have 235-1281 maximum health depending on class.
- Bots walk through zones above their level while travelling (level 26-27 ogres, a level 45 mob) and die on the way.
- Of the quest turn-ins, about a third fail with "interaction failed" (reason not logged).
