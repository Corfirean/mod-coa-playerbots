# Bot routes, closed expansion maps, starter gear and quest logs (2026-10-27)

Four changes made after the death/repair work in [bot-death-and-repair.md](bot-death-and-repair.md).

## Closed expansion maps

`CoaBots.RestrictExpansionZones` (module config, default 2):

| Value | Meaning |
|---|---|
| 2 | automatic: Outland (map 530) is closed when `MaxPlayerLevel` is 60 or lower and the scaling that brings it down is off (`CoAContentScaling.Enable` together with `CoATBC.Enable`); Northrend (map 571) likewise with `CoAWotLK.Enable` |
| 1 | always closed |
| 0 | always open |

A closed map means for bots: no spawn hubs (the last open bracket serves levels 59-60), no quest givers, enders or kill
targets (the spawns are left out of the quest index at startup), no flight nodes, and a bot found there (a saved
position, a level-up) is relocated at once, whatever quest work it has. The starting areas of the Blood Elves (Eversong,
Ghostlands, Silvermoon) and the Draenei (Azuremyst, Bloodmyst, the Exodar) are on map 530 and stay open: two
rectangles (`BotZoneProgression.cpp`, `STARTING_RACE_BOXES`), a pure position test with no terrain lookup.

Sandbox check (scaling off, cap 60): of 160 logged-in bots, 40 were saved in Outland proper; after login none was on a
closed map, and the 8 left on map 530 were in the two open corners.

## Routes around danger (`world/DangerMap.*`)

- Static data: every creature spawn on a continent that attacks players of a team on sight (faction hostile mask, no
  service flags, not a critter/totem/trigger), as the highest effective level per 40-yard cell and team; elites count 3
  levels higher, world bosses 10.
- Dynamic data: every bot death adds weight to the cells around it for 30 minutes (`NoteDeath`, from the
  player-script hook). It catches what the creature data cannot know: scaled levels, packs, patrols, guards that attack
  for reputation reasons.
- Risk for a bot of level L: creature level minus L minus 3, 0..12, plus the deaths around.
- `WorldExecutor::TravelTo` asks `DangerMap::Plan` for waypoints when the way is 150+ yards and the straight line
  really crosses danger (cost beyond its length at least 100); the route has to remove 60% of that and not be 1.7x longer.
  A* on a 50-yard grid, smoothed to at most 6 waypoints. A waypoint the movement layer refuses for 4 s drops the route
  and the bot goes straight. A death resets the route.
- Relocation hubs with risk 6 or more are left out as long as another one is left.
- `.botcmd dangertest <guid> <x> <y>` prints risk and route, `.botcmd dangernote <map> <x> <y> <n>` fakes deaths.

A bug found while testing: the search added the heuristic to the stored cost, so every node but the first was discarded
and no route was ever found.

## Starter gear

`BotSpawn::NeedsGearRefit` (empty armor slot, a piece above the bot's level, or the weapon subclass its learned spells
need not equipped: `BotAI::KitGearMismatch`) is checked once per login and on level-up; a bot that needs it goes into
the existing throttled gear queue (`BotGear::Queue`, one bot per 2 s), where `RepairGear` runs the level-bracketed
gear-up over it, replacing a piece only when the new one scores higher. Every refit is logged (`GearRefit:`).
Fresh bots already got this through `ApplyFreshBotSetup`.

## Quest logs

Logger `module.coa-playerbots.quests` (INFO): `QuestAccepted`, `QuestTurnIn` (bot, quest, reward choice, money) and
`QuestTurnInFailed` with the reason: quest not complete (and its status), cannot be rewarded (reward choice, free bag
slots), or the npc does not take the quest. The sandbox appender rolls the file at 64 MB.

## Stress run

`.agents/stress/` (not in git): `run_world_quiet.py` (console to NUL), `spawn_stress.py`, `monitor.py` (per-minute
counts, memory, disk; shuts the world down when free RAM falls under 2.5 GB or the disk under 12 GB), `where.py`.
Findings are in the commit message of the run and in the memory notes.
