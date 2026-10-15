# Local combat training arena

This is a measurement harness. It never edits profiles, scores, priorities, talents policies, resource policies,
or Git history. Candidates and party actors use production BotAI, including its normal profile and utility paths.

## Architecture and current scope

`module/src/training/` contains the arena manager, session data, scenario schedule, immortal boss AI, reset lifecycle,
native telemetry scripts, and report writer. WorldScript advances the manager before BotMgr updates bots.
Preparation resets existing combat before taking ownership. Session ownership bypasses ordinary progression, ambient behavior, corpse recovery and cross-map following.
Economy and crafting skip participants. Production combat functions remain responsible for fighting.

The default configuration permits one active arena; MaxParallelArenas permits up to four. Session, batch, participant and enemy IDs are separate;
event routing requires source and target to belong to the same session. Pets are attributed to their owner.
The arenas use reserved phases `0x40000000`, `0x20000000`, `0x10000000`, `0x08000000` on map 0 at `(-13200, 275, 22)`.
Do not assign that phase to other content. This is a local development feature, disabled by default.

## Configuration

```ini
CoaBots.Training.Enable = 0
CoaBots.Training.OutputDir = training/results
CoaBots.Training.DefaultLevel = 60
CoaBots.Training.DefaultDurationSec = 180
CoaBots.Training.Deterministic = 1
CoaBots.Training.LogDecisionTrace = 1
CoaBots.Training.BossEntry = 31144
CoaBots.Training.HazardSpellId = 5740
CoaBots.Training.InterruptCastSpellId = 133
```

Training.Enable is read at startup. When disabled, the telemetry UnitScript is not registered and the dispatch
hooks exit immediately. Restart after enabling training. Output failures stop the session and are logged.
Deterministic schedules are always enabled in this prototype; the Deterministic option is reserved for future modes.

Required Support effects are explicit per specialization, using resolved aura IDs from the effective server data:

```ini
CoaBots.Training.RequiredBuffs.<specId> = <auraId>,<auraId>
CoaBots.Training.RequiredDebuffs.<specId> = <auraId>
```

No requirement list is inferred from personal DPS. Absent required effects have zero coverage. An empty requirement
list is reported as unconfigured. Buff coverage counts each living or dead party slot equally over episode time;
the numerator requires the candidate to be the aura caster. Boss debuffs are tracked separately.

## Commands (GM, console or RA)

```text
botcmd train status
botcmd train prepare <botGuid> <scenario>
botcmd train start <botGuid> <scenario> [seconds] [seed]
botcmd train run <botGuid> <scenario> <runs> [seedBase]
botcmd train stop
botcmd train reset
botcmd train report <sessionId>
botcmd train party <actorGuid1> <actorGuid2> <actorGuid3> <actorGuid4>
```

Use a leading dot in game chat. Console and RA need no client or selected target.
Participants must already be online through BotMgr, belong to configured bot-host accounts, have curated builds,
be unguilded, and be outside battlegrounds, flights and teleports. Account 1 is always rejected.
Set the specialization through the existing `botcmd learnspec` command before preparing the arena.
Group scenarios require four configured actor bots. Ungrouped participants are placed in a temporary real Group;
an existing party is accepted only when exactly the five selected bots are its members.

`prepare` reserves participants and starts pre-pull setup. A matching `start` begins the prepared run. `stop` and
`reset` terminate the run and return participants. At most 1000 episodes are accepted per batch.
Each batch episode uses `seedBase + episodeIndex`; restart two batches with the same base seed to pair episodes.
The endurance default is 600 seconds; other scenarios use DefaultDurationSec. Explicit start duration overrides it.

## Scenarios and mechanics

Supported definitions: ST_STATIC, RESOURCE_ENDURANCE, BURST, ST_MOVEMENT, CLEAVE, AOE, SURVIVAL,
GROUND_HAZARDS, GROUP_STRESS, THREAT_STRESS and SUPPORT_GROUP.
BURST currently shares the static environment. Endurance extends the duration without replenishing resources.
Cleave and AoE spawn two and five enemies. Group stress supplies native melee, scripted tankbusters, targeted
damage and party damage. Threat pressure uses real ThreatManager::AddThreat and samples actual threat references.
Support group supplies a real five-player party with moderate incoming damage.

The boss uses native melee resolution at scheduled swing times. Tankbusters and party/target damage call the
server damage path with fixed scripted amounts; they do not simulate an armor calculation or a specific spell.
Ground hazards use a configured, validated persistent area aura through the real spell engine. BotAvoidance can
observe that ground effect. The warning event records a fixed target location; it does not itself render a warning.
The hazard damage value in the schedule is descriptive; actual damage comes from the configured spell data.
Interrupt tests cast a real configured spell. Events record interrupt availability and early cast termination;
early termination is not automatically declared a successful interrupt.

Boss AI cannot evade and cannot lose its last health point. Loot rewards and loot mode are disabled. A session
despawns its enemies rather than killing them, so the boss has no death reward path. No creature-template SQL is needed.

## Reset lifecycle and normalization

Before an episode, remove casts, combat, controlled entities, movement and non-passive auras; resurrect if needed;
clear native spell cooldowns and known-spell GCD categories; repair durability; restore health, mana and energy;
start rage and runic power at zero. BotAI::Forget clears evaluator throttles, failure backoff, strategy runtime,
reservations, damage/heal tracking, engine retry state, target state and movement/world state.
Production pre-pull setup then restores required forms, summons and auras before the pull.
Teleports must finish before setup/pull, with a 30-second setup timeout.

Level normalization reuses GiveLevel, Book of Ascension acquisition and the curated talent build allocator.
The current gear is captured as a fixed training loadout, including random properties and enchantments;
subsequent episodes reject changes instead of rerolling equipment. Equipment selection is currently an operator
prerequisite. Use a dedicated character already built for level 60: lowering a higher-level character has not been
validated as a complete spellbook/talent downgrade. Known weapon skills are set to the native maximum for the level. The manifest records level, spellbook and weapon
skill values for each participant.

Returning from training restores phase and location, then releases ownership. Character level, learned spells and
talents are not rolled back. Training-created groups are disbanded. Use disposable bot characters for testing.

## Reports and measurement semantics

Each episode writes `<OutputDir>/<specId>/<sessionId>/`:

- `summary.json`: identity, normalization manifest, combat, healing, threat, resources, support and mechanics.
- `abilities.json`: eligible/selected/rejected decision windows and accepted/failed cast attempts with reasons.
- `resources.json`: observed capped and zero-resource time by channel.
- `auras.json`: spell/caster/target uptime, apply/remove/refresh observations.
- `threat.json`, `healing.json`, `support.json`: role-specific sections.
- `mechanics.json`: the complete seeded schedule.
- `decisions.jsonl`: evaluator candidates, rejection reasons, modified scores, selected spell, phase and resources.
- `events.jsonl`: casts, damage, effective heals, auras and executed mechanics with scheduled and observed times.
- `samples.jsonl`: resource values, casting/movement state, party health and real threat samples (about 100 ms).

A batch aggregate alongside episode directories lists completed runs and DPS mean, median, min, max and population
standard deviation. The read-only Python tool aggregates additional numeric measurements:

```text
python tools/training_reports.py aggregate <resultsDirectory>
python tools/training_reports.py compare <baselineDirectory> <experimentDirectory>
```

Comparison rejects mismatched scenario/seed/duration/spec/role/party gear. It reports numeric deltas only.
Provide directories containing exactly the intended paired episodes; do not mix unrelated batches.

Tank reports separate survival, aggro duration/losses/recovery, damage taken and actual threat samples. Healer reports
separate deaths, lowest member HP, criticalPersonSeconds below 25%, effective healing and observed zero-mana time.
Support reports personal damage alongside candidate-owned required buff coverage and boss debuff uptime.
DPS reports damage, movement, casting, resource observations, ability decisions and avoidable periodic damage.
These measurements are not collapsed into a success score.

OnDamage is after mitigation and before final health application; reported totals cap the observed amount to
current health but can differ from final damage if another hook changes damage later. OnHeal reports effective healing.
Cast acceptance is not cast completion. Resource requirement and gain metadata in cast events describe the engine's
rules, not confirmed amounts spent/generated. Sampled capped/zero time is an observation, not proof of overcap waste
or inability to cast. Unknown GCD utilization and unconfirmed interrupt success remain null.

## Determinism and remaining validation

The schedule uses a specified 32-bit LCG and stable timestamp ordering. Identical scenario, duration, roster size and
seed produce identical mechanic sequences. World tick timing, crit/miss/proc rolls and production AI reaction timing
are not globally seeded. Damage/healing totals are not guaranteed identical. Fixed schedules support repeated-run
statistics; they do not establish exact combat replay.

Exact finalized damage attribution per spell/hit/crit, completed-cast observation, shield/overheal
and response-latency analytics, actual resource overcap waste, cooldown-category analytics, exact aura refresh/expiry
reasons, arbitrary core-owned resource cache reset, and crash recovery are not complete. Aura apply/remove hooks do
not necessarily report every in-place refresh. Additional class/spec combinations and hazard damage attribution need live acceptance.
Do not use unvalidated measurements to infer a rotation change.

## Adding scenarios and telemetry

Add data to Scenario::Find and scheduled events to Timeline. Keep seed transitions and ordering explicit; extend the
native schedule tests. Execute mechanics only against session participants/enemies. Add optional hooks through the
training API, with an immediate disabled exit, and require matching source and target sessions. Do not edit profiles
to produce telemetry. New measurements should state their sampling or hook semantics and use null when unavailable.

## Baseline workflow

1. Build the server with this module and run the focused training harness.
2. Enable training on an isolated local server/database.
3. Select disposable level-60 bots with curated specs and fixed gear.
4. Configure required Support aura IDs from effective data, and select actors for group scenarios.
5. Run `botcmd train run <guid> ST_STATIC 20 1000`; save the baseline directories.
6. Inspect reports with the relevant profile and strategy source, then form one hypothesis.
7. Change that hypothesis manually, rebuild, and repeat exactly the scenario/seed/gear/party setup.
8. Use the comparator for factual deltas; keep or revert the source change manually.

The trainer performs none of steps 6â€“8 autonomously.

## Local validation on 2026-10-05

The isolated integration server built successfully through `tools/verify_all.py` (build and focused bot_training harness).
The harness checks schedule determinism, typed JSON, aura interval accounting and five report/comparator cases.
Focused source checks and the training C++ codestyle check also passed; the full gameplay suite was not run.

Live RA runs used disposable CoaBotHost characters in cloned world/characters/auth schemas. DPS completed a
20-episode ST_STATIC batch with seeds 1000–1019. Tank THREAT_STRESS, Healer GROUP_STRESS and Support SUPPORT_GROUP
each completed two same-seed episodes; scheduled mechanics matched per pair. Native accepted cast counters were
checked against SPELL_CAST_OK events. Combat totals varied, as expected with unseeded combat RNG. Required Wind
Support buff coverage was zero in these runs; this is a measured result, not a policy recommendation.

GROUND_HAZARDS produced native spell 5740 aura apply/remove events but recorded zero periodic damage; that run
does not validate avoidable-damage attribution. Private logs, cloned-server configuration and reports remain under
`.agents/training-live/` and are excluded from Git. No original repack database was migrated.

After the multi-target engagement fix, live CLEAVE and AOE decision traces reported two and five engaged enemies respectively; both episodes completed.

## Parallel campaigns

Set `CoaBots.Training.MaxParallelArenas = 4` only on an isolated server where bits 30–27 are reserved.
Each active session reserves one phase until the entire batch finishes, and must have a disjoint roster.
Use `train party` immediately before starting each group batch; it selects actors for the next command only.
`train stop <candidateGuid>` stops one candidate; omit the GUID to stop all sessions.
`train actor <classId>` creates one fresh level-appropriate Human bot through the existing production spawn setup.
Set its curated specialization using `botcmd learnspec` after login.

Output directories remain per spec/session; events, decisions and samples carry session/arena IDs. Summary includes
phaseMask. Historical reports are retained unchanged. Changing actors changes the experiment: treat the parallel
roster as a new baseline, and do not compare its results as improvement over a different roster.

Live parallel validation on 2026-10-05: Tank, Healer and Support completed concurrent 15-second smoke episodes in three disjoint phases. Session IDs were checked in all JSONL streams, and event actor IDs did not reference another arena roster. Reuse of an owned candidate was rejected. Three long batches then started concurrently.

## Fixed item-level baseline

`train equip <guid> <slot> <itemEntry> ...` applies explicit slot/item pairs to an idle dedicated bot, preserving
old items in bags or mail. Entry 0 clears a slot. Native CanEquipNewItem validates each item; a rejected item
stops application and may leave a partially equipped bot, so always verify every slot afterwards.

The local 2026-10-05 ilvl-150 baseline uses separate training-only copies of ordinary item templates in the cloned
world schema (9500100–9500150). Item levels are 146–154. Required level is 60; all item spells, random properties,
sockets and set bonuses are removed. Source item IDs and unchanged static stats are recorded in the private
fixed-gear-plan.json. These templates are not installed into original databases. This is a controlled test loadout,
not a recommended optimal gear build. Previous low/mixed gear results remain separate exploratory data.

## Permanent all-specialization roster

The isolated local server retains one GUID per each of the 70 ClassSpecRoles entries, assigned a curated build.
The private all-spec-bots.json registry records class, spec, role, exact loadout and readiness. Candidates log out
after provisioning. Four reusable five-bot helper pools support disjoint group arenas; the helper matching the
candidate role is omitted. DPS candidates use solo ST_STATIC for initial coverage.

`train loadout` accepts ordered alternatives for each slot and selects the first natively equippable item. It
requires all body/jewelry slots and a main-hand weapon; off-hand/ranged legality remains dependent on proficiency
and two-handed weapon rules. The static training library has ilvl 145–155, no item spells/sets/random properties.
All-spec tests write to a separate output root, initially three 180-second episodes per specialization. The local
queue logs in existing GUIDs on demand and logs them out after each batch. No rotation or profile source is edited.

Reset removes at most one captured non-passive aura instance per spell ID. Core-owned lifecycle helpers may recreate themselves; the bounded reset retains such recreated helpers rather than looping indefinitely. This was observed during Cultist provisioning, and full private core-state reset remains unverified.
