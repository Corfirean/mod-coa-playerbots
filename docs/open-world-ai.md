# Open-world AI: WorldBrain

How an ungrouped bot lives in the open world: takes quests, plans a route through them, goes to
the right place, finds live targets, fights (through the existing combat engine), loots, checks
its progress, hands quests in, moves on to the next hub — and spreads out relative to other bots
instead of converging on the same spawn point. Code: `module/src/world/`. The pre-rework state and
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
            │    ├─ QuestInteraction (accept / turn in at quest givers)
            │    └─ hub travel
            └─ fallback activity when there is no task:
                 Gather / Fish / Grind / Ambient  ── run by BotAI / BotWorldBehavior
```

Exactly one of these moves the bot per tick. Gathering, fishing, grinding and ambient errands
still live where they were, but only run when the brain names them in its `WorldDirective`.
Combat is not the brain's business: a task engages with `Attack()` and the combat engine fights;
the brain picks the task up again once the bot is out of combat and has looted.

### Time: what counts toward a task's budgets

Every phase has a budget and every task a deadline, so the question "whose time was that?" has one
answer per kind of interruption:

| Interruption | Task | Clocks |
| --- | --- | --- |
| Ambient errand (repair, vendor, flight) | paused, target let go | **stopped** (phase, deadline, scan/wait timers move on by the pause) |
| Group, manual Stay, `.botcmd` AI suspend, guild gather order, battleground, dungeon | suspended, every claim released; kept ≤ 5 min, then re-planned | **stopped**; on return the task re-enters through `Recover` |
| Its own business away from the brain: a fight with an add, resting, looting, a corpse run | kept | the **phase** clock does not count it; the task **deadline** does (anti-loop) |
| Death | target dropped, `Recover`; twice killed in one area writes the area off | as above |
| Map change, or a jump of 400+ yd between two brain ticks (a teleport, a flight the task did not ask for) | re-planned | — |
| A flight the task asked for itself (`taxiRequested`) | kept, carries on from the landing | like its own business: the **phase** clock does not count the flight, the **deadline** does |

`WorldTask::Pause/Resume/ShiftClocks/ShiftPhaseClock/ClockNow` implement it; a phase entered while
paused starts at the frozen clock so a resume can never put it in the future.

## The task model (`WorldTask.h`)

A `WorldTask` persists across ticks: type (`QuestObjective`, `QuestAccept`, `QuestTurnIn`, `Travel`), phase,
the area (map, anchor, radius, `ObjectiveArea` id), the quest/objective with its type, target
entry, item, required/current count, the claimed live target guid, retry/dry-attempt/bad-area
counters, deaths, last failure reason, deadline, pause state, and the objectives bundled into the
same trip.

Phases for a creature objective:

```text
TravelToArea → Search → Approach → Execute → Combat → Loot → Verify → Search …
                  ↑ no targets: wander area points, wait if corpses (respawn), then next area
```

Every phase has a budget; nothing can run forever (see "Anti-loop" below).

## Knowledge (`QuestKnowledgeBase`, `ObjectiveAreas`)

Built once at startup from the data the world was loaded from (plus one read of the loot tables and
`areatrigger_involvedrelation`), so runtime decisions are lookups:

- quest → givers/enders (creatures and objects), objectives classified as Kill, UseObject,
  CollectItem, LootObject, UseObjectForItem, Explore, UseItemOnCreature/Object, TalkTo,
  Escort/Event — with a supported flag and reason;
- **reverse loot**: item → creatures/objects that drop it (reference loot one level deep), with
  drop chance; objects whose use spell creates the item;
- kill-credit proxies: entry → creatures whose `KillCredit` names it;
- quest giver spawns in a spatial grid, clustered into **quest hubs** (90 yd, split above 220 yd,
  at least 3 supported quests) with their level range;
- generic "Opening" spells per lock type (chests are opened by spell, not `GameObject::Use()`).

Three questions, kept apart and each answered in exactly one place:

- **Would a bot take it?** (`QuestKnowledge::supported`) — acceptance policy plus what the data
  says: no escorts/scripted events, talk-to-NPC credit, PvP, reputation, timed, daily/seasonal,
  item-started, no loot source, elite (unless `AcceptEliteQuests`).
- **Can a bot ever complete it?** (`QuestKnowledge::completable`) — false only for player kills,
  reputation targets, no ender. A daily or item-started quest already in the log is completable.
- **Can this build execute this objective?** (`ObjectiveHandlers::CanExecute`) — there is
  something to do (a quest-provided *source* item is not an action) and a handler takes it.

`QuestInteraction::Workable` combines the last two for a quest in the log (completable, and every
open objective executable). Acceptance requires all three for every objective; the planner,
`HasQuestWork` and the log cleanup all ask `Workable`/`CanExecute`, so no part of the brain can
think a quest is doable while another can never create a task for it. Handlers (`objectives/`, one
per kind, stateless, picked through a registry):

- **kill** (including kill-credit proxies) and **collect** (kill the creatures that drop the
  item, loot it) — `Attack()` and the combat engine;
- **use object** (levers, crates, notes — goober objects) through `HandleGameObjectUseOpcode`,
  whose goober case hands out the credit or casts the item-creating spell;
- **open and loot** (chest-type objects) with the lock's Opening spell;
- **use the quest item on a creature / an object** through the item's own spell. AzerothCore
  gives *every* creature/object objective the internal KILL|CAST|SPEAKTO flags, so the flag
  can't tell these apart from kills; the knowledge base recognises them by a quest item whose
  spell has a kill-credit effect for that entry, and the kill handler falls back to the item
  when kills give no credit;
- **explore**: walk into the area trigger's volume (as the core's `IsInAreaTriggerRadius` judges
  it), then send `CMSG_AREATRIGGER` through the real handler — the server never notices a player
  entering a trigger by itself, the client reports it;
- **talk**: talk-to credit comes from gossip scripts and is never accepted; the handler only
  tries a gossip hello for such quests already in a log, and drops the quest if that gives no
  credit;
- plain delivery quests (no objectives) are just an accept and a turn-in.

**Quest drops.** Quest-only drops live in `Loot::quest_items`, in loot slots numbered after the
regular items, and only for players who need them. The old loot code only ever took
`Loot::items`, so a bot never picked up a single quest drop. `BotAI::TakeAllLoot` takes both and is
now used by the post-kill loot queue and by gathering.

**Objective areas**: an entry's static spawns are clustered into places (single-link 45 yd, 14 yd
vertical gap so mine levels stay apart, split above 120 yd radius, never mixing phases — so a bot
that sees an area sees its anchor, every member and every wander point) lazily per entry and
cached forever. Static spawns are world knowledge only; everything a bot acts on is a live object found
by scanning around it inside the area.

## Planning (`WorldPlanner`)

Candidates, scored with named weights (`WorldUtility.h`, overridable in config):

- each open objective in the log, placed in its best area (spawn count, drop chance, distance,
  bots already assigned, heatmap crowd, recent failures, per-bot jitter);
- turn-ins, batched per ender spawn;
- quest givers within `GiverSearchRadius` with quests the bot would accept;
- when none of that exists: the best **quest hub** on the map for the bot's level (quests left for
  it, distance, crowd, recent failures) as a `Travel` task; when no hub either, zone progression
  is asked to move the bot (flight, teleport only as its fallback), at most every 10 minutes.
  Zone progression no longer relocates a bot that still has quest work on its map.
- meanwhile the brain hands the bot a fallback activity (gather, fish, grind, ambient errands)
  weighted by its persona; a bot in a grinding mood sometimes finishes its grind before leaving
  for a new hub.

Objectives score higher when they share targets with another open objective (overlap) or have an
area within 150 yd of one (route synergy); a turn-in scores higher with work nearby. The chosen
objective **bundles** every other objective with the same action that shares its targets or lies
within 70 yd (up to 4; their targets join the live search, their progress counts toward the
trip). The remaining candidates are laid out nearest-next as a short route, shown by `.botcmd
brain`. Only the first step executes; the planner runs again when it finishes, because the world
has moved on.

The planner runs on a staggered 2–8 s window while idle and immediately (after a short human
pause) when a task ends; a pass that finds nothing backs off exponentially up to 3 minutes.

## Anti-crowding

- `WorldReservations`: exclusive TTL claims on live creatures/objects (two bots never go for the
  same mob), shared occupancy counts on objective areas. Released on task end, death, suspension,
  logout; expire on their own regardless.
- `PopulationHeatmap`: 100-yard cells counting bots present (by activity) and heading there.
  *Incoming* means exactly "travelling to the task's destination" (`WorldTask::CountsAsIncoming`:
  phase `TravelToArea`, not paused): it is set and cleared from the task's phase after every
  executor step (`WorldExecutor::SyncIncoming`), so a bot already working in an area is counted
  once, by its presence — never also as incoming, and never for a 15-yard walk to a mob.
- Area scoring subtracts occupancy and crowd, target scoring skips claimed/tagged/engaged mobs.

## Movement (`BotMovement`)

- `MoveTo` is idempotent: asking for the walk already running is a no-op.
- `Navigate` keeps a persistent request per bot (goal id, destination, progress state), walks long
  trips in 120 yd legs, and runs a recovery ladder when progress stops for 9 s: repath → detour
  left → detour right → `Stuck` (the caller escalates: next target, next area, blacklist, fail the
  task). Progress counts on the ground (2.5 yd) **or in height** (1.5 yd) — stairs, ramps, towers
  and mine shafts are progress — and small accidental progress restarts the stall timer without
  resetting the ladder: it only resets once the goal is 20 yd (or 8 yd of height) closer than at
  the first stall (`BotNavProgress.h`). Time spent fighting/looting/resting never counts as stuck.
- Mounting per bot beyond its own 60–95 yd threshold, never in combat, never within 10 s of a
  dismount; trips beyond 900 yd (`TaxiMinDistance`) take a flight path when the bot knows a route.
  A quest trip whose route turns out not to work at the flight master walks on — it is never
  teleported (zone progression's own trips still fall back to the teleport).

## Failure memory and anti-loop

Per-bot TTL memory (`FailureMemory.h`) of targets (60 s), objects (120 s), areas and quest NPC
spawns (5 min), hubs (10 min), set-aside quests, and blocked turn-ins (kept apart: a finished quest
is handed in even while its objective work was set aside); repeat failures stretch the ttl.
Budgets: search 35 s per area (×patience, ×1.5 with corpses around), approach 25 s, travel 6 min,
whole task 30 min, 4+ dry attempts (scaled by drop chance), 3 bad areas per objective → task fails.

What a failed task does to its quest (`QuestPolicy.h`):

- **Transient** (no targets, no path, no progress, a death, a failed cast or interaction): the quest
  is set aside for `QuestSuspendMs` (10 min), doubling each time in a row up to
  `QuestSuspendMaxMs` (2 h), and retried. **Never abandoned** — a pathing defect or a busy camp
  must not cost a quest chain. A completed objective resets the back-off.
- **Dead end** (the quest has failed, can never be completed, or has an open objective this build
  cannot execute): the planner never works on it; the log cleanup notes it and abandons one per
  pass **only when the log is full** (`MaxActiveQuests`), preferring failed quests.

A kill by someone else is not a failed attempt (no credit was possible); a claimed target is
re-confirmed on every approach step, since the claim can lapse while the bot fights an add.

## Humanisation

Variety comes from bias, never from deliberately bad play:

- each bot's persona (from BotAI's personality: patience, gathering, current lean) biases
  activity choice, session length and detours; scores get deterministic per-bot jitter, and every
  timer is staggered per bot (no shared RNG, so thousands of bots never act in lock-step);
- short reaction delays after a kill or a finished task, and reading pauses at quest NPCs;
- **opportunity detours**: while travelling to or searching an area, a bot with Herbalism or Mining
  that passes a node it can pick within `DetourRadius` (15 yd) — always if gathering is in its
  nature, sometimes otherwise — pauses the task, gathers, and resumes it where it was. The detour
  gives up after 30 s, or after 4.5 s if gathering never starts (node unreachable, taken);
- **session rhythm**: after 20–45 minutes of questing (scaled by patience) a bot takes a 3–8
  minute break to ambient life (errands, repairs, wandering in town), then plans afresh.

Both switch off in config (`GatherDetours`, `SessionBreaks`).

## Debugging

- `.botcmd brain <guid>` — goal (and suspension reason), task, phase and time in phase (PAUSED and
  for how long), remaining phase budget and task deadline, heatmap state (present / incoming),
  quest + knowledge-base verdict + workable, objective + handler + progress + dry attempts of the
  limit, area (spawns, distance, crowd, assigned bots), reserved target, movement request (ground
  and height left, best so far, last progress, recovery stage), route, live failure memory,
  detour/session/break state, next planner pass, last event, per-bot counters.
- `.botcmd worldstats` — knowledge base size, brains by task/phase, quest/objective/task counters,
  movement stats (MovePoints issued vs redundant skipped, stalls, recoveries), reservations, heatmap.
- Logs (DEBUG): `module.coa-playerbots.world` (TaskSelected, PhaseChanged, TaskCompleted/Failed,
  Replan), `.quest` (QuestAccepted, TargetSelected, ObjectiveProgress, ObjectiveCompleted,
  QuestCompleted, QuestTurnedIn, set-aside and dead-end decisions), `.navigation` (stalls,
  recoveries, flights), plus task pause/resume lines under `.world`.

## Performance notes (3000+ bots)

No per-tick world scans: startup indices, lazily cached areas, 50 yd live scans only while
searching (0.5–1.5 s), planner rarely and staggered with backoff, heatmap updated on change,
reservations swept every 30 s, failure memory pruned on write. Bot AI runs on the world thread
(BotMgr::Update), so none of these structures need locks.

## Verification status

Compiled against the upstream CoA core (`jealous-sound/azerothcore-wotlk-coa`) with this module's
documented core patches stubbed in, and the full `worldserver` binary links. The pure logic
(failure memory, reservations, heatmap, clustering, utility scoring, task clocks, navigation
progress, quest policy) is unit-tested standalone (`module/tests/`, also under ASan/UBSan).
**Not yet run on a live server** — see `AGENTS.md` for the live test checklist (tests A–E).

## Delivery phases

| Phase | Contents | Status |
| --- | --- | --- |
| 1 | WorldTask/WorldBrain/WorldPlanner/WorldExecutor skeleton, idempotent movement + stuck ladder, failure memory, `.botcmd brain`/`worldstats`, config, removal of the old quest walkers | in code |
| 2 | Kill-quest vertical slice: knowledge base, live target search, accept/turn-in, verify | in code |
| 3 | Anti-crowding: live-target reservations, spawn clustering into objective areas, population heatmap | in code |
| 4 | Collect quests: reverse loot index, quest-drop looting, chests | in code |
| 5 | Multi-quest routing: overlap, bundling, route plan | in code |
| 6 | Use-object / explore / use-item-on / talk handlers | in code |
| 7 | Quest-driven hub travel, flights for quest trips, zone-progression guard | in code |
| 8 | Humanisation: opportunity detours, session breaks | in code |
