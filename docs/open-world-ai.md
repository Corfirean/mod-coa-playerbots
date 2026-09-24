# Open-world AI: WorldBrain

How an ungrouped bot lives in the open world: takes quests, plans a route through them, goes to
the right place, finds live targets, fights (through the existing combat engine), loots, checks
its progress, hands quests in — and spreads out relative to other bots instead of converging on
the same spawn point. Code: `module/src/world/`. The pre-rework state and
the problems this replaces are in `docs/research/open-world-ai-audit.md`.

The organising principle: **a bot always knows where it is going, why, what it will do on arrival,
how it will check success, and what it does if the plan fails.**

## Layers

```text
BotAI::UpdateOffensive (idle, ungrouped, no fight/loot/rest)
  └─ UpdateSoloWorld
       ├─ guild gather order (explicit player instruction)
       ├─ BotWorldBehavior::UpdateBeforeSolo   needs (repair/sell) and errands/flights in flight
       └─ WorldBrain::Update  ──────────────►  WorldDirective
            ├─ WorldPlanner   choose the next WorldTask by utility
            ├─ WorldExecutor  advance the task one step
            │    ├─ QuestExecutor ─► objective handler (objectives/)
            │    └─ QuestInteraction (accept / turn in at quest givers)
            └─ fallback activity when there is no task:
                 Gather / Fish / Grind / Ambient  ── run by BotAI / BotWorldBehavior
```

Exactly one of these moves the bot per tick. Gathering, fishing, grinding and ambient errands
still live where they were, but only run when the brain names them in its `WorldDirective`.
Combat is not the brain's business: a task engages with `Attack()` and the combat engine fights;
the brain picks the task up again once the bot is out of combat and has looted.

Suspension: a group, a manual Stay, a battleground or a dungeon suspends the brain
(`WorldBrain::Suspend`) — every claim is released, the task is kept for up to five minutes and
re-evaluated (`Recover`) on return. Death keeps the task, drops the target, and re-evaluates the
area after the corpse run (twice killed in one area writes the area off).

## The task model (`WorldTask.h`)

A `WorldTask` persists across ticks: type (`QuestObjective`, `QuestAccept`, `QuestTurnIn`), phase,
the area (map, anchor, radius, `ObjectiveArea` id), the quest/objective with its type, target
entry, item, required/current count, the claimed live target guid, retry/dry-attempt/bad-area
counters, deaths, last failure reason, deadline.

Phases for a creature objective:

```text
TravelToArea → Search → Approach → Execute → Combat → Loot → Verify → Search …
                  ↑ no targets: wander area points, wait if corpses (respawn), then next area
```

Every phase has a budget; nothing can run forever (see "Anti-loop" below).

## Knowledge (`QuestKnowledgeBase`, `ObjectiveAreas`)

Built once at startup from the data the world was loaded from (plus one read of
`areatrigger_involvedrelation`), so runtime decisions are lookups:

- quest → givers/enders (creatures and objects), objectives classified as Kill, UseObject,
  CollectItem, LootObject, UseObjectForItem, Explore, UseItemOnCreature/Object, TalkTo,
  Escort/Event — with a supported flag and reason;
- kill-credit proxies: entry → creatures whose `KillCredit` names it;
- quest giver spawns in a spatial grid (also clustered into quest hubs with their level range,
  which nothing uses yet — hub travel is Phase 7).

Quests a bot cannot reliably finish are never accepted: escorts/scripted events, talk-to-NPC
credit, PvP, reputation, timed, daily/seasonal, no giver/ender, elite (unless
`AcceptEliteQuests`) — and any quest with an objective kind that has no handler yet. In this
phase that means **kill quests and plain delivery quests** (no objectives, just a turn-in);
collect quests arrive in Phase 4 together with the reverse loot index they need.

**Objective areas**: an entry's static spawns are clustered into places (single-link 45 yd, 14 yd
vertical gap so mine levels stay apart, split above 120 yd radius) lazily per entry and cached
forever. Static spawns are world knowledge only; everything a bot acts on is a live object found
by scanning around it inside the area.

## Planning (`WorldPlanner`)

Candidates, scored with named weights (`WorldUtility.h`, overridable in config):

- each open objective in the log, placed in its best area (spawn count, drop chance, distance,
  bots already assigned, heatmap crowd, recent failures, per-bot jitter);
- turn-ins, batched per ender spawn;
- quest givers within `GiverSearchRadius` with quests the bot would accept;
- when none of that exists, the brain hands the bot a fallback activity (gather, fish, grind,
  ambient errands) weighted by its persona; zone progression still relocates it as before.

The best candidate becomes the task; the planner runs again when it finishes, because the world
has moved on. (Bundling nearby objectives into one trip and a route plan are Phase 5.)

The planner runs on a staggered 2–8 s window while idle and immediately (after a short human
pause) when a task ends; a pass that finds nothing backs off exponentially up to 3 minutes.

## Anti-crowding

- `WorldReservations`: exclusive TTL claims on live creatures/objects (two bots never go for the
  same mob), shared occupancy counts on objective areas. Released on task end, death, suspension,
  logout; expire on their own regardless.
- `PopulationHeatmap`: 100-yard cells counting bots present (by activity) and heading there;
  updated only on cell change / new destination.
- Area scoring subtracts occupancy and crowd, target scoring skips claimed/tagged/engaged mobs.

## Movement (`BotMovement`)

- `MoveTo` is idempotent: asking for the walk already running is a no-op.
- `Navigate` keeps a persistent request per bot (goal id, destination, best distance, last
  progress), walks long trips in 120 yd legs, and runs a recovery ladder when progress stops for
  9 s: repath → detour left → detour right → `Stuck` (the caller escalates: next target, next
  area, blacklist, fail the task). Time spent fighting/looting/resting never counts as stuck.
- Mounting per bot beyond its own 60–95 yd threshold, never in combat, never within 10 s of a
  dismount.

## Failure memory and anti-loop

Per-bot TTL memory (`FailureMemory.h`) of targets (60 s), objects (120 s), areas and quest NPC
spawns (5 min), and suspended quests (10 min); repeat failures stretch the ttl. Budgets:
search 35 s per area (×patience, ×1.5 with corpses around), approach 25 s, travel 6 min, whole task
30 min, 4+ dry attempts (scaled by drop chance), 3 bad areas per objective → quest suspended; three
suspensions → abandoned. Quests that can never be finished (unsupported, lost quest item) are
abandoned by a periodic log cleanup instead of clogging the log.

## Debugging

- `.botcmd brain <guid>` — goal, task, phase and time in phase, quest + knowledge-base verdict,
  objective + handler + progress, area (spawns, distance, crowd, assigned bots), reserved target,
  movement request (owner, goal, distance, last progress, recoveries), live failure memory,
  next planner pass, last event, per-bot counters.
- `.botcmd worldstats` — knowledge base size, brains by task/phase, quest/objective/task counters,
  movement stats (MovePoints issued vs redundant skipped, stalls, recoveries), reservations, heatmap.
- Logs (DEBUG): `module.coa-playerbots.world` (TaskSelected, PhaseChanged, TaskCompleted/Failed,
  Replan), `.quest` (QuestAccepted, TargetSelected, ObjectiveProgress, ObjectiveCompleted,
  QuestCompleted, QuestTurnedIn), `.navigation` (stalls, recoveries).

## Performance notes (3000+ bots)

No per-tick world scans: startup indices, lazily cached areas, 50 yd live scans only while
searching (0.5–1.5 s), planner rarely and staggered with backoff, heatmap updated on change,
reservations swept every 30 s, failure memory pruned on write. Bot AI runs on the world thread
(BotMgr::Update), so none of these structures need locks.

## Verification status

Compiled against the upstream CoA core (`jealous-sound/azerothcore-wotlk-coa`) with this module's
documented core patches stubbed in; the pure logic (failure memory, reservations, heatmap,
clustering, utility scoring) is unit-tested standalone (`module/tests/`, also under ASan/UBSan).
**Not yet run on a live server** — see `AGENTS.md` for the live test checklist.

## Delivery phases

| Phase | Contents | Status |
| --- | --- | --- |
| 1 | WorldTask/WorldBrain/WorldPlanner/WorldExecutor skeleton, idempotent movement + stuck ladder, failure memory, `.botcmd brain`/`worldstats`, config, removal of the old quest walkers | in code |
| 2 | Kill-quest vertical slice: knowledge base, live target search, accept/turn-in, verify | in code |
| 3 | Anti-crowding: live-target reservations, spawn clustering into objective areas, population heatmap | in code |
| 4 | Collect quests: reverse loot index, quest-drop looting, chests | planned |
| 5 | Multi-quest routing: overlap, bundling, route plan | planned |
| 6 | Use-object / explore / use-item-on / talk handlers | planned |
| 7 | Quest-driven hub travel, flights for quest trips, zone-progression guard | planned |
| 8 | Humanisation: opportunity detours, session breaks | planned |
