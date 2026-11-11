# Population performance experiments

The new optimizations are experimental and disabled by default. A successful build or login of 3000
characters does not qualify 3000 active bots. Do not change the default population caps based on these tools.

The current comparison base includes `origin/feat/open-world-autonomous-hardening` at
`ef14440e4750be3b27c2c5f951e93d0d4575723d`: route-aware stall detection, 3D arrival, ownership holds,
shared contextual no-path memory and objective hysteresis. Those changes are present in both comparison arms.
Budget deferral holds navigation progress clocks and does not record a no-path failure. The performance work
does not add a competing local path-failure backoff. The branch's algorithmic benchmarks do not prove live load capacity.

## Build and switches

Build the module with its normal source discovery. Adding the performance and scheduling translation units
requires reconfiguring CMake before an incremental build. The optional `performance-core.patch` adds a generic
processing-time observer to the world loop. Without it, subsystem and period metrics work, but full processing
time remains unavailable. An empty processing histogram must never be interpreted as a zero-duration tick.

| Setting | Default | Meaning |
|---|---:|---|
| `CoaBots.Perf.Enable` | 0 | Aggregate metrics and administrative `botcmd perf status/reset/dump` |
| `CoaBots.Perf.Detailed` | 1 | Inclusive subsystem scopes; disable for instrumentation overhead comparisons |
| `CoaBots.Perf.OutputDir` | bot-performance | Asynchronous JSON snapshots; use a unique directory per world run |
| `CoaBots.Scheduling.Enable` | 0 | Bounded deadline queue and event wakeups |
| `CoaBots.Scheduling.Adaptive` | 0 | Distance-based background cadence; requires scheduling |
| `CoaBots.Scheduling.IdleIntervalMs` | 200 | Idle group updates |
| `CoaBots.Scheduling.SoloIntervalMs` | 500 | Solo decisions; native motion continues between decisions |
| `CoaBots.Budgets.Enable` | 0 | Admission queues for background planning, quest scans and Navigate processing |
| `CoaBots.Budgets.PlannerUs/ScanUs/NavigationUs` | 3000/2000/4000 | Per-world-update budgets |
| `CoaBots.Navigation.ReuseValidatedPath` | 0 | Reuse a complete validated ground path; requires the optional core patch |

Combat, active group fights, healers in groups, battlegrounds, dungeons and player-owned groups retain frequent
updates. Death handling also stays frequent. A command wakes the next AI update; level, spell and loot hooks
invalidate maintenance through a bounded mailbox. All bots remain loaded Players with real progression.

Distance classification uses map, instance, phase and a spatial index of real players. Enter radius is 250 yards;
leaving requires more than 350 yards for ten seconds. Distant peaceful bots use a 1.5-second quest scan interval,
5–15-second successful planning intervals and 60-second maintenance. Nearby maintenance uses 30 seconds.
Existing slower gathering, fishing and grinding scans are preserved.

Budget queues retain one waiting entry per bot and work class, replace obsolete generations and promote old
requests after five seconds. A request not observed for two seconds is pruned. Combat and avoidance bypass the
queues; player-owned groups, dungeons and battlegrounds also bypass background admission. Ready AI updates
are ordered by pending work so a queue head cannot block behind unrelated dispatch ordering on a long tick.
Tasks whose next AI call has not arrived are excluded from admission order until their deadline.
Navigate budgets apply to issuing the next leg or a recovery path, while ordinary progress checks continue.
Deferral retains its current request and retries an unissued first leg; it is not a path failure.
Direct MoveTo calls are measured but do not use Navigate's admission queue. Budgets are cooperative: one
non-preemptible operation can exceed its budget. No Player mutation is moved onto a worker thread.

## Metrics

Durations use `steady_clock` and microseconds. Histograms use 250-microsecond bins below 1.024 seconds and
exponential tail bins. Their count, sum, maximum and threshold exceedances are exact; percentile bounds are
rounded upwards. Scopes are nested and inclusive: do not sum AI and its children to infer total CPU time.

Snapshots are cumulative within an epoch. Use the final snapshot for that epoch; do not add consecutive
snapshots together. For distinct runs, merge histogram bins rather than averaging their percentiles. Reset
acknowledgements and initial/final snapshots separate warmup from measurement. World activity gauges are
lifetime counters; subtract their initial values. Death, kill and loot counters reset with the epoch.

`world_period` includes waiting between updates. `world_processing` excludes the loop's waiting. The optional
core observer is required for the latter. With `WorldTickObserver.h`, period timestamps are taken at update
starts (`world_period_update_start=1`). Without it, period timestamps are taken at the bot script phase and
are only a diagnostic fallback. The early r8 probes use this fallback; their processing timers remain valid.
`command_dispatch` measures server acceptance to the next AI call,
not network round-trip time or completion of movement/casting. Zero command samples mean untested latency.

JSON file I/O runs in one background writer, with at most one pending dump. Snapshot creation and copying
happen on the world thread and are included in the overhead comparison. Failed writes are logged.

## Disposable Windows fixture

Use a cleanly stopped source MySQL. Preparation copies the data directory, configuration and credentials into
a new explicitly marked fixture. It never starts or changes the installed source server. Fixture credentials and
runtime configs are private; share only the sanitized effective configs and reports.

```powershell
python -B tools/performance/benchmark.py prepare --source "C:/games/CoA Server 2" --root C:/games/source/bot-performance-20261102
python -B tools/performance/benchmark.py run --root C:/games/source/bot-performance-20261102 --binary C:/path/to/worldserver.exe --label seed --population 0 --warmup 0 --seconds 1 --seed
python -B tools/performance/matrix.py --root C:/games/source/bot-performance-20261102 --binary C:/path/to/worldserver.exe --core-source C:/path/to/core --suite smoke --execute
```

Seeding deliberately logs leveled characters in so the normal gear/spell/talent setup runs. It is not a measured
load test. Its cleanly stopped database becomes the immutable seed. Every independent case restores the entire
seed directory before starting its own MySQL. Populations use the same ascending-GUID prefix subsets.
The runner checks SQL online flags against native `online_in_world` gauges, rather than trusting SQL alone.

The runner uses isolated ports and process IDs. It refuses simultaneous ownership of a fixture and refuses
measurement alongside another world or compiler. Case labels cannot overwrite previous evidence. A case
failure stops its matrix for review. Completed cases are resumable. Never restore a snapshot into a running DB.

`--wait-for-idle 600` waits up to ten minutes for competing worlds and builds to finish; it never stops them.
During a run, a native process snapshot checks for competitors every ten seconds. A detected competitor
invalidates the run, records process names/PIDs and requests a normal shutdown of the owned world.
This sampling cannot prove that no shorter competing process ran between checks.
Preflight rechecks process liveness after inventory collection, so an already exited compiler does not block
startup. Direct rank lookup cases also require native activation evidence from the startup console.

## Qualification sequence

`matrix.py` prints its cases and durations without `--execute`. Available suites:

1. `smoke`: short 0/500/2000/3000 baseline/candidate probes. These validate the tools, not the acceptance targets.
2. `overhead`: three paired reference/full measurements at 2000 bots; full overhead must remain within 3%.
3. `baseline`: 0/500/1000/1500/2000/2500/3000, three repeats, 10-minute warmup and 15-minute measurement.
4. `stages`: paired baseline and scheduling/adaptive/budget cases, alternating order; 1000 and 2000 three times,
   then 3000 once for each stage. Do not run this before initial profiles justify the candidate changes.
5. `budgets`: the 27 combinations of 2/4/8 ms at 3000 bots; reject settings with growing queues or lost throughput.
6. `final`: repeat the full population ladder with the selected candidate configuration.
7. `soak`: 15-minute warmup and six measured hours each at 2000 and 3000 bots.
8. `stress`: six controlled scenarios at 2000/3000, three repeats, 10-minute warmup and 20-minute measurement.
9. `adaptive`: three alternating pairs at 2000 bots with only adaptive frequency toggled. Both use the
   same scheduling, path reuse and 3/2/8 ms budgets, with persistence readback. The `adaptive_far` gauge
   records the actual population eligible for reduced frequency. This isolates adaptive frequency from
   changes to navigation admission; an empty world with no human observer cannot qualify wakeup latency.
10. `ranklookup`: three alternating pairs at 2000 bots with only optimized rank lookup toggled. Other
    scheduling/adaptive/path settings and 3/2/8 ms budgets are identical; persistence is checked after each run.

The complete campaign takes multiple days including restarts. Run only one case at a time. Freeze binary,
source identities and configuration before each series. Stop increasing load after a failed case. Low RAM for
30 seconds or stopped telemetry for 30 seconds aborts the case through a normal world shutdown.

Also qualify controlled player commands/approach, 20% distributed combat, a 200-bot battle, a 300-bot cluster,
five dungeon groups, two battlegrounds, navigation failures/map transfers/death and persistence after restart.
The scenario driver is restricted to explicitly marked disposable fixtures. The runner records observed native
coverage in `scenario-coverage.json`; a queued dungeon or battleground request is insufficient. Combat, density
and group-content thresholds must hold in at least 90% of snapshots or the run fails coverage. The synthetic
observer tests server wakeups and dispatch, but real-client responsiveness still requires a controlled client.
Navigation uses ten suspended diagnostic actors and cannot qualify general bot activity. No stress scenario
is considered passed until its live evidence is recorded and reviewed.

Every run restores the same physical seed and preserves saved login positions. Native login normally relocates
bots randomly; the fixture-only override makes starting positions reproducible. Earlier diagnostic runs
`baseline-500-probe` and `baseline-2000-r1` predate this override and the movement hardening base. They must
not be paired with current candidates to claim a performance improvement.

`--verify-persistence` captures twenty eligible native players immediately before logout, restarts the world,
explicitly logs those GUIDs back in as hidden suspended readback actors and compares level, class, race,
specialization, XP, money, map, position, active quests and permanent items. Dead/in-transit actors and
buyback/conjured/expiring items are excluded explicitly. Background behavior is disabled only during readback.

Shutdown uses the native command, measures time and retains slow-stop evidence after 300 seconds. It does not
kill the world after the old 90-second timeout. Forced termination is restricted to the owned disposable world
after ten minutes without file/log progress and makes the case unsuccessful. MySQL stops only after the world.

```powershell
python -B tools/performance/report.py --root C:/games/source/bot-performance-20261102
python -B tools/performance/windows.py --run C:/games/source/bot-performance-20261102/runs/CASE
python -B tools/performance/plots.py --root C:/games/source/bot-performance-20261102
python -B tools/performance/compare.py --root C:/games/source/bot-performance-20261102 --baseline BASE_R1 BASE_R2 BASE_R3 --candidate NEW_R1 NEW_R2 NEW_R3 --name paired-2000
```

Plot export requires Matplotlib in a separate reporting environment. Reports include per-run subsystem CSV,
the population CSV and raw cumulative histograms. Window quantiles are bucket upper bounds computed by
subtracting cumulative bin counts within the same epoch. Empty windows are unmeasured. Repeat plots show
the median and minimum–maximum of independent run results, not an average or pooled p99.

Timing checks are necessary, not sufficient. At 2000/3000 bots, processing p95 targets are 50/75 ms and p99
100/150 ms, with no unexplained one-second stalls. AI command dispatch targets are 250/350 ms. Shutdown
targets are 180/300 seconds. Successful activity per bot-hour may decline by at most 10%, mortality may increase
by at most 10%, and final four-hour private-memory growth must stay within 5% at stable world coverage.
Reports must preserve missing acceptance evidence explicitly; do not enable experimental defaults until all
required timing, behavior, persistence, stress and soak checks pass.

## Stack profiles and current evidence

The overhead reference disables module subsystem instrumentation while retaining the same world processing
and period clocks. It is a common measurement floor, not a completely uninstrumented executable.

Optional `--stack-profiler PATH --stack-seconds 60` captures an owned diagnostic world with Very Sleepy.
The profiler runs on an inactive desktop, with symbol-server access and minidump capture disabled. Its
matching PDB must be beside the frozen world executable. These cases are excluded from comparative results.
Export function rankings with `python -B tools/performance/stack_report.py --profile FILE.sleepy --output PREFIX`.
Raw weights include waiting thread wall time. The exporter excludes recognized waiting leaf functions;
remaining weights are approximate sampled activity, not measured CPU seconds. Inclusive weights overlap.

The first same-base 2000-bot pair (r9 reference / r10 budget candidate, one repeat each) measured processing
p95 187 / 172.5 ms and p99 215.75 / 199.75 ms. Both passed twenty-character persistence readback and clean
shutdown (54.047 / 53.047 seconds). This does not meet the tick targets. Quest turn-ins per bot-hour fell
from 1.615 to 1.434 (11.3%), exceeding the allowed decline in this single pair. Three repeats and remaining
qualification scenarios are still required; experimental settings remain disabled by default.

The r11 diagnostic entity samples point to core Player/Creature and Unit updates as major costs. They do
not identify a safe nested optimization yet. The subsequent stack-profile run aborted during login when
available RAM stayed below 10% for thirty seconds; shutdown was clean in 29.031 seconds. Another test
world was running. Preflight now recognizes suffixed worldserver executables as competing measurements.
No load runs should resume while that world is needed by its owner.

Population proximity checks use a stable GUID phase within each second, including after map changes, so
simultaneous logins or transfers do not permanently align all background classifications. Map changes
still trigger an immediate classification. This spreads checks over actual world updates; it cannot
split a single long world tick into smaller updates.

Restoring the physical seed does not freeze wall-clock time. Auction expiry, mail and other absolute
timestamps can change startup work between dates. Record this limitation when comparing older runs;
prefer adjacent alternating pairs for timing and activity comparisons.

The 2026-11-07 r11 diagnostic stack capture completed at 2000 bots: 215747 samples over 60.047 seconds,
clean shutdown in 94.094 seconds. It used only two minutes of warmup and is not an acceptance comparison.
Recognized waiting leaves account for 921.725 of 1020.705 sampled thread wall-weight units. Of the
remaining 98.980 units, 3.826 were exclusive in `SpellMgr::GetSpellChainNode`, 3.624 in AI relocation
notification and 2.881 in `Player::HasSpell`. Several spell-chain callers are native class scripts;
module-only optimization cannot remove their cost.

`CoaBots.Spells.OptimizeRankLookup = 0` is an experimental startup-only switch. When enabled, spell
resolution traverses current `SpellInfo` rank links instead of repeating manager hash lookups. Cleanse
selection filters non-dispel effective spells before rank resolution, then retains the highest-active-rank
check. Learned ranks, disabled ranks and temporary replacements are still read live; no per-player rank
cache or retained metadata pointer is introduced. The disabled branch preserves the original resolver.
Tests exercise both branches, live spellbook changes, root/non-root lookups, standalone spells, missing
metadata and temporary replacement. Measured performance and live combat regression qualification of this
switch remain pending.

`World.Performance.DirectRankLookup = 0` optionally replaces core rank-map hash searches with existing
`SpellInfo::ChainEntry` links. Activation validates the complete bidirectional correspondence at rank
load; inconsistencies disable it. Rank-chain and spell-info unload disable the fast path before clearing
metadata. Added native rank chains maintain the same links. No decision cache or retained new pointer
is introduced. The candidate requires the matching core patch and separate live qualification.

`CoaBots.Spells.LazyCleanseScan = 0` delays construction of the current cleanse spell candidates until
the first eligible harmful dispellable aura. Candidate and ally ordering remain unchanged; the list is
local to one decision and is never retained across updates.

`CoaBots.Scheduling.FarIdleIntervalMs = 0` preserves the previous AI cadence. Nonzero values (500–2000 ms)
apply only to distant solo bots outside combat with adaptive scheduling enabled, including distant dead
solo bots. Group members, BG/dungeon bots, nearby bots and combat retain their existing intervals.
Map/combat/lifecycle events, commands and reclassification still wake the scheduler. Native movement and
world updates continue, but navigation leg servicing and task execution follow the new AI interval.
Qualify movement pauses and action throughput before selecting a value; 1000 ms is an experimental candidate.

`CoaBots.Spells.CacheLearnedRanks = 0` is a subsequent candidate requiring the core spellbook revision API.
It caches only IDs present in each native rank chain, including currently inactive/removed entries. Active
and removed flags and temporary replacements are checked live. All four native spell-map key insertion/erase
sites advance a per-player revision; spec/active flag changes need no membership rebuild. Metadata unload,
rank-chain additions and runtime spell registration advance a shared exported epoch. The cache belongs to
the player's existing custom-data lifetime, is capped at 256 queried roots per player, and stores no object
or metadata pointers. Revision tracking is opt-in. Missing core APIs leave the feature disabled, and the
runner rejects a requested cache when the native `learned_rank_cache_enabled` gauge does not acknowledge it.
Runtime qualification of this subsequent candidate remains pending.

`World.Performance.InterleaveWorldObjects = 0` is an experimental core candidate for smoothing world-map
object-update peaks. Objects remain on the owning map thread and retain one update per native map cycle;
three stable GUID phases spread work over that cycle. Each receives its actual elapsed time once, with
no replay of missed updates. Dungeon/BG/private maps and simulated gameplay retain the legacy path.
Unexpected native update cadence disables interleaving for that map. Object removal clears its clock,
and identity checks protect against reused addresses. `interleaved_world_maps` acknowledges activation.
This candidate is not yet runtime-qualified and remains disabled by default.

The r12 500-bot probe with optimized rank lookup enabled completed on 2026-11-07: 30 seconds warmup,
60 seconds measurement, clean exit and shutdown in 22.032 seconds. Persistence readback passed for all
20 sampled characters with no differences. Source/build/unit verification and module regressions passed.
This short probe establishes runtime viability, not a comparative speedup or combat correctness acceptance.

The first full r12 3000-bot distributed case (600 seconds warmup, 900 seconds measurement) completed with
p95 347.5 ms, p99 443.75 ms and maximum 2032.245 ms. Two processing ticks exceeded one second. Manager
time accounted for the long pauses outside individual AI calls. The final navigation queue contained 617
requests with oldest age 2404 ms. Shutdown was clean in 84.093 seconds and all twenty persistence samples
matched. These timings fail acceptance. Subsequent manager/gear subscopes isolate maintenance costs;
they do not justify blaming a particular function until its own timings are observed.

The first full r14 3000-bot case recorded p95 279.75 ms, p99 306.5 ms and maximum 605.437 ms,
with no processing ticks above one second. Shutdown took 80.078 seconds; all twenty persistence
samples matched. Compared with the single r12 case, kills increased 7.1%, quest turn-ins 12.9%
and completed tasks 18.4%, but deaths increased 37.8%. These are single cases, not replicated
acceptance evidence: tick targets and the mortality criterion are not met. Detailed gear maintenance
accounted for about 1.2% of measured wall time and did not explain the remaining steady cost.

On 2026-11-10 the r15 interleaving candidate passed source/build/unit verification, including four
clock tests for phase distribution, lifecycle, irregular elapsed time and incompatible cadence.
A 500-bot viability probe (30 seconds warmup, 60 seconds measurement) acknowledged three active
interleaved maps, exited cleanly in 21.032 seconds and matched all twenty persistence samples.
This probe does not establish a speedup, combat correctness or 3000-bot qualification.

The clean full r15 case `hardening-r15-3000-r3-20261111` completed on 2026-11-11 with 3000
characters confirmed in the world, 600 seconds warmup and 900 seconds measurement. Processing
p50 was 192.25 ms, p95 252.25 ms, p99 276.5 ms and maximum 430.367 ms, with no pauses above
one second. Login took 228.406 seconds, shutdown 78.063 seconds, and twenty persistence samples
matched. Against the single r14 case, p95 improved 9.8% and p99 9.8%; kills changed +0.6%,
quest turn-ins +1.0%, tasks +6.9%, and deaths -10.5%. These unrepeated observations still fail
the 75/150 ms tick targets and do not qualify long-term stability or player command latency.
The two preceding r15 attempts were invalidated by another world's startup, not included in
the comparison; both shut down cleanly. Interleaving remains experimental and off by default.

The next compiler experiment keeps the r15 sources and settings fixed and replaces MSVC
`/Ob1` with `/Ob2 /GL`, linking with `/LTCG /INCREMENTAL:NO`. `/Zi`, `/O2`, `NDEBUG` and
debug symbols are retained. Results must be attributed to this build configuration, separately
from code changes. The initial link failed because project flag normalization stripped `/INCREMENTAL`
from an explicitly supplied `/INCREMENTAL:NO`, leaving `:NO`; configure with `/DEBUG /LTCG` and let
the existing project configuration supply `/INCREMENTAL:NO` in the effective link command.

The first clean r16 LTCG 3000-bot case completed with p50 190 ms, p95 242 ms, p99 267.5 ms,
maximum 364.577 ms and zero ticks above one second. Shutdown took 82.078 seconds; all twenty
persistence samples matched. Relative to the single r15 case, p95 improved 4.1%, p99 3.3%, kills
increased 2.8%, quest turn-ins 13.1% and completed tasks 12.1%; gathering decreased 7.2% and deaths
increased 9.1%. Repeats are absent, so neither statistical reproducibility nor behavioral acceptance
is established. The 3000-bot timing targets remain unmet; a six-hour soak would not repair that failure.

The separate 60-second r16 stack capture retained 104.19 sampled thread-wall weight after filtering
known waiting leaves. `Map::Update` accounted for 76.78 inclusive weight (73.7%), `Player::Update`
31.39 (30.1%), nonplayer map updates 27.95 (26.8%), and `BotAI::Update` 23.51 (22.6%). These
overlapping sampled weights are not CPU seconds; unresolved-source leaves account for 12.47 weight.
This identifies further profiling targets, not a proof that any particular population is impossible.

No population has completed the full acceptance matrix. The current work establishes instrumentation,
opt-in candidates and preliminary functional load results. Three-repeat comparisons, instrumentation
overhead, real observer command completion, stress-content coverage and six-hour stability remain
unqualified. Do not enable these controls by default or advertise stable 3000-bot support from this work.
Further substantial reductions need measured changes to native player/NPC/visibility work and AI costs;
running the existing stateful `BotAI::Update` concurrently remains unsafe. A parallel design requires
explicit ownership and immutable inputs rather than placing existing updates in a thread pool.

`World.Performance.FilterSpellCandidates = 0` is an additional experimental native candidate, requiring
the updated core patch. In eleven loops for Tinker, Xoroth and Cultist it checks rank-chain eligibility
before calling the existing `Player::HasSpell`. Nonmatching candidates avoid that membership lookup;
matching candidates still use the live native method, including spec/removed-state and copied-spell
fallback behavior. No spellbook, metadata or decision is cached. When disabled, the original short-circuit
order remains. Three native tests check all boolean combinations, call order, current membership/copy
changes and query elimination for unrelated candidates. `spell_candidate_filter_enabled` acknowledges
the native configuration; the runner rejects a requested but unsupported feature. Runtime comparisons
for this subsequent candidate require repeats and the same-binary disabled control.

The first r17 enabled 3000-bot case recorded p50 149 ms, p95 209.75 ms, p99 233.25 ms,
maximum 302.652 ms and no processing ticks above one second. Shutdown took 76.063 seconds;
all twenty persistence samples matched. Against the single r16 case, p95 decreased 13.3%, but
deaths per bot-hour increased 17.1%, exceeding the behavioral allowance. Kills decreased 2.3%,
quest turn-ins 6.9% and completed tasks 8.9%. This case fails acceptance and cannot establish
a causal performance benefit. The disabled control uses the identical r17 binary, compiler flags,
fixture snapshot, warmup and measurement durations; its only changed option is the filter switch.

The disabled r17 control completed with p50 172.75 ms, p95 228 ms, p99 250 ms and maximum
314.086 ms. Shutdown took 79.063 seconds and all twenty persistence samples matched. In this
same-binary pair, enabling the filter reduced p95 by 8.0% and p99 by 6.7%; kills changed -2.8%,
quest turn-ins -4.6%, tasks -5.3%, and deaths +4.1%. These behavioral proxies stay inside the
10% allowance for this pair, but three independent repeats and the timing targets remain missing.
The discrepancy against the earlier r16 comparison must not be treated as proof of a causal
mortality regression or ignored when reporting variability. The filter stays disabled by default.

The retired local r18 prototype used `World.Performance.AuraSelfTargetFastPath`. An existing
unit aura may keep its current target map when it has exactly one application on its owner,
all active effects are ordinary `APPLY_AURA`, the complete application mask still matches,
the aura is not an area aura and no aura scripts are loaded. Live applicability is still checked.
Mixed/area effects, other owners, partial masks, application lifecycle changes and scripted auras
retain the original path. Periodic effects and duration updates continue in the normal owner update.
No target, mask or applicability result is retained between updates. This avoids temporary target
containers only for an unchanged ordinary application; it does not reduce aura update frequency.
The archived native activation gauge was `aura_self_target_fast_path_enabled`. Four focused tests and
native source/build/unit verification passed. The 500-bot viability case exited cleanly in 22.015 seconds
and matched twenty persistence samples; this did not establish a speedup.

On 3000 bots, the enabled r18 case produced p50 180.5 ms, p95 226.75 ms, p99 250.5 ms and maximum
309.916 ms. The disabled same-binary control produced p50 163.75 ms, p95 223 ms, p99 250.5 ms and
maximum 545.595 ms. Shutdown took 80.063 and 82.063 seconds respectively, with twenty matching
persistence samples in both cases. Enabled versus disabled, kills changed -0.2%, deaths +2.2%, quest
turn-ins -6.0% and completed tasks +4.3%. This single pair showed no timing benefit, so the candidate,
its configuration entry and native tests were removed from the current source and core patch.
This is not proof of a general regression; it is insufficient evidence to retain this extra path.

Reproduction artifacts remain local in `bot-performance-artifacts-20261102/runtime-hardening-r18-aura-safe`
and the corresponding run directories, including source archives, core patch, symbols, verification,
configuration fingerprints and raw histograms. The runner retains the native activation check for
replaying those archived binaries; requesting that retired feature on an unsupported binary fails.
