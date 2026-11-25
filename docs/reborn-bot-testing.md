# Warcraft Reborn bots: what they do and how to test it

Bots on a Reborn realm (`CoA.ClassModel = "wcr"`, stock classes 1-9 and 11) are meant to level like a player of that
realm: spend the talent points of a build for their spec, learn what a Reborn trainer teaches, fight with their spec's
profile, keep buffs, pets, poisons and totems up, and wear gear that suits the spec. This file lists what is in place, where it
lives, and the three layers of tests that keep it from breaking again.

## What is in place

| Area | What it does | Where |
|---|---|---|
| Spec choice | Tank and healer shares follow `CoaBots.SpecBalance.*`; the rest spread over the class's damage specs | `BotRebornSpecChoice.h`, `BotTalentBuilds::ChooseRebornSpec` |
| Talents | One 51-point build per class and spec (talents of its tab, the ones its profile uses first), spent from level 10 through the core's free-pick build; retried after combat or death | `RebornTalentBuilds.inc`, `BotTalentBuilds::ApplyRebornBuild`, `BotAI.cpp` (upkeep in `Update`) |
| Spells | The stock trainer table plus 90-odd Reborn abilities it lacks (demons, pet spells, poisons, stances, Growl, ...), learned at their level, also for bots made earlier | `RebornExtraSpells.inc`, `BotSpawn::LearnRebornExtraSpells` |
| Spells | Reborn versions preferred (`SpellResolver::SetPreferRebornTwins`); the warlock summons the stock demon because the Reborn one needs an item | `SpellResolver.cpp`, `BotWorldScript.cpp` |
| Combat | A profile per spec: rotation, cooldowns, interrupts, stuns (stunning what is casting, and a survival stun when hurt), area attacks, damage over time, defensives, seals, aspects, armors | `profiles/Profile*.cpp` |
| Pets | A warlock summons Voidwalker (Imp otherwise); a hunter gets a wolf, calls or revives it; the hunter's pet spends its talent points | `TryMaintainStockPet`, `SpendPetTalentPoints`, `RebornPetTalents.inc` |
| Poisons and imbues | A rogue coats its weapons (Deadly, Instant); a shaman imbues by spec. Both are item-target spells, cast through `Spell::prepare` with an item target | `TryMaintainWeaponImbues` |
| Totems | A shaman keeps one totem per element near it | `TryMaintainTotems` |
| Gear | Spec stat models for every stock spec (Balance druids no longer scored for Agility); a hunter's ammunition | `BotSpecStats.h`, `BotProgression::ProvisionAmmo` |
| Arena | Reborn bots are normalised like CoA ones (level, setup, talents, ammunition); `botcmd learnspec` switches a Reborn bot's spec | `TrainingArenaManager.cpp`, `BotMgr::LearnSpecialization` |

## The three layers of tests

### 1. Offline, instant, no server

```
python tools/reborn_talent_planner/validate.py      # data against the client DBCs and the core's rules
python tools/run_training_tests.py                  # the module's C++ and Python unit tests (needs the MSVC environment)
```

`validate.py` fails when: a profile ability is misnamed, unobtainable by level 60, or an item-target spell the engine cannot cast;
a generated table (`Reborn*.inc`) is stale; a talent build is not valid at some level 10-60 or misses a talent its profile uses;
the spec tables (`ClassSpecRoles.cpp`, the tests, the stat models, the planner) disagree; a spell id the upkeep code uses is no longer
the spell it should be. It needs the client DBCs of a Reborn server (`REBORN_DBC`, default `C:\games\CoA Server 2\Data\dbc`) and the
core's base SQL (`CORE_BASE_SQL`).

`reborn_data_tests.cpp` checks the tables the module compiles in and runs the spec chooser over three class mixes
(tanks and healers must land within a few points of 20%, every damage spec must be used). `test_reborn_bot_check.py` keeps the live
check's parsing honest.

**When you change a profile, a table or a spec:** run both commands. If `validate.py` says a table is stale, run the named generator
(`plan.py`, `extra_spells.py`, `pet_talents.py` in `tools/reborn_talent_planner/`) and commit its output.

### 2. Live smoke, minutes, read-only

After the world is up with the bots logged in (give the auto-login a few minutes, and the first minute of each bot for its upkeep):

```
python tools/reborn_bot_check/check.py --root "C:\games\CoA Server 2"
```

It samples bots of every class and asks each for `.botcmd abilities <guid>`, then checks the expectations listed at the top of
`check.py` (talents, profile abilities known, gear, hunter pet and ammunition, warlock demon, rogue poison, shaman imbue) and the
population's tank/healer shares and spec coverage. Exit code 1 and a JSON report when anything fails. For a single bot:
`.botcmd abilities <guid>` shows ruleset, profile, each ability, the spellbook, talents, pet, gear and consumables.

Turn on the module's debug logger (`module.coa-playerbots`, level Debug) for the reasons behind a refusal: `ActionEvaluator ... can't
cast ... -- <reason>`, `BotTalentBuilds: ... could not apply its build`, `BotAI: '<bot>' puts spell ... on its main hand (result ...)`.

### 3. Live combat, the arena

The training arena measures one bot at a time against an immortal boss and writes damage, resource use, cast refusals and aura uptime
(`docs/training-arena.md`). For Reborn:

```
python -I tools/combat-intelligence/ci_plan_reborn.py reborn-1 actors.json <out-dir> <credentials> <binary> <revision>
python -I tools/combat-intelligence/ci_plan_run.py ci-plan-reborn-1.json
```

`actors.json` maps each stock class id to a disposable online bot ({"1": 101, "2": 102, ...}). Each episode sets the spec
(`botcmd learnspec`, which for a Reborn bot also applies the build), equips the bot by the live gear-up and runs the scenario. Read
`summary.json` and `abilities.json` of each result:

- a damage spec must show damage (`combat.damage` > 0) and a rotation that uses more than its filler (`abilities.json`: accepted casts of
  its cooldowns and DoTs, not only the filler);
- rejected casts by reason: a large share of `native stance/form requirement`, `not known` or `can't afford` points at the profile
  or the spellbook, not at the arena;
- a tank must hold threat in THREAT_STRESS; a healer must keep the party alive in SUPPORT_GROUP;
- run the same plan before and after a change and compare with `tools/combat-intelligence/ci_compare.py`.

## Content the bots can enter

What exists, by reading the code (nothing here was run for the Reborn work):

- **Battlegrounds.** `CoaBots.BGFill.Enable` tops both queues up with bots when a human joins one, within the bracket of that
  player; the bots' own BG behaviour is `BotBattlegroundAI.cpp`. Bots never queue by themselves. With a 60 cap the classic battlegrounds
  (Warsong Gulch, Arathi Basin, Alterac Valley) are the ones in reach.
- **Dungeons.** `CoaBots.LfgFill.Enable` fills a dungeon group with bots by role when a human queues. This is why the spec balance matters:
  a realm with too few tank or healer specs leaves the queue short. `CoaBots.AutoQueueDungeon.Enable` makes bots queue by
  themselves; it stays **off** (it once created bots in bulk, see the incident note), so bots do not enter dungeons on their own.
- **Raids.** Not supported: the LFG fill says raids are out of scope. A raid needs encounter logic per boss; none exists for the
  classic raids.

To check the group content once the server is up: queue a character for a dungeon and for each battleground and see that the group
fills with the right roles (one tank, one healer, three damage), that the tank holds the pull, and that the healer heals; read
`.botcmd strategy <guid>` and `.botcmd brain <guid>` for what a bot is doing and why.

## What nothing here covers yet

- No live run of any of this has been done: the checks above were written against the code and the client data. The first live run
  is the real test; start with `check.py`, then one arena plan.
- Raids, bots queuing for content on their own, and gear from content (dungeon drops, vendors, crafting) are not part of this work.
- Reborn versions of spells: a Reborn realm now resolves a spell to its Reborn version (id + 1100000) when the bot knows it
  (`CoaBots.Spells.PreferRebornTwins`, on by default there), the stock one otherwise. The Reborn versions are what a player casts and
  differ in what matters: warrior abilities need a stance (Fury fights in Berserker Stance from level 30, Arms in Battle Stance, Prot in
  Defensive), several have other cast times and cooldowns, and a stock warrior's Bloodthirst carries a stance mask no warrior can meet.
  Read `native stance/form requirement` rejections in the arena as the first sign that a stance descriptor is missing.
