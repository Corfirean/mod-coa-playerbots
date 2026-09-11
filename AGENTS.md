# AGENTS.md

## What this project is

AI companion bots that can play CoA's custom Ascension classes (IDs 12-32,
`mod-ascension-compat`), for solo play. Not a production multi-thousand-bot
realm feature — the target is a handful of controllable companions.

Started 2026-09-11 after researching whether this could be a clean
AzerothCore module (no core patch) at all — it can't, for the reasons in
`docs/architecture.md`, but the actual required patch turned out to be much
smaller than assumed once measured against real data instead of guessed from
READMEs. Read that file before touching anything architectural.

## Read before starting any task

- **Planning a core patch, or wondering "does X need a core change"?** →
  `docs/architecture.md` (the wall, why, and the chosen way past it) and
  `docs/research/module-hook-boundary.md` (the file:line evidence).
- **Reconciling CoA's core against mod-playerbots' patch?** →
  `docs/research/core-diff-analysis.md` — every one of the 104 conflict
  markers across the 34 genuinely-conflicting files has already been read
  and categorized (mechanical vs. needs-a-real-decision). Do not re-derive
  this from scratch; extend it if upstream has moved.
- **Writing rotation AI for a specific Ascension class?** →
  `docs/research/ascension-class-status.md` first, to check whether that
  class is actually confirmed working before investing time in it. **Do
  not** infer "is this class implemented" from source-file presence alone —
  that method gave a wrong answer once already (see that doc's warning
  section) and was only caught because of direct in-game testing.

## Ground truth sources this project depends on

- **CoA core**: `C:\games\source\server\azerothcore-wotlk-coa` (jealous-sound's
  fork; the actual target core, includes `modules/mod-ascension-compat`).
- **CoA-Repack**: `C:\games\CoA-Repack` — a built, running instance of the
  above (worldserver/authserver/mysql), account `local`/`local` GM 3. Use
  this for any "does X actually work" question — it's faster and more
  trustworthy than reading source.
- **mod-playerbots core fork**: `mod-playerbots/azerothcore-wotlk` @
  `Playerbot` branch — the patch-plan source of truth. Shallow-cloned for
  research at `C:\games\source\server\playerbots_research\playerbots-fork`
  (scratch, not committed — reclone if needed, see
  `core-diff-analysis.md`'s reproduction steps).
- **mod-playerbots bot-AI module**: `mod-playerbots/mod-playerbots` — this is
  where the actual bot decision-making/strategy code lives (1374 files, a
  real standalone module, no core patch needed for *it specifically*). The
  reference for "how does an existing bot project structure rotation logic,"
  even though none of its per-class code applies directly (it only knows the
  11 stock classes).

## Key facts, so they don't get re-derived every session

- Both established AzerothCore bot projects (NPCBots, Playerbots) require a
  core fork, not a drop-in module — confirmed from each project's own README,
  not assumed.
- The *reason* is narrow and specific: `Group`/loot-roll code and real
  gear/talent storage are type-locked to `Player`, not just missing hooks
  (see `module-hook-boundary.md`). Autonomous combat AI needs no core
  changes at all.
- Playerbots' actual approach: the bot **is** a real `Player` with a
  `WorldSession` whose socket is `nullptr` — `WorldSession` already
  null-checks its socket almost everywhere, so this mostly works without
  patching. The measured core patch is ~2700 lines across 77 files, and more
  than half those files apply to CoA with zero conflict.
- No existing bot project has any AI for non-standard classes. 100% of the
  Ascension-class rotation logic is new work regardless of which base
  project's core-patch approach is used.
- CoA has 21 custom classes, not the 18 an earlier file-only search found —
  see `ascension-class-status.md`. Source-file presence is not a reliable
  signal of whether a class actually works; test in-game.

## Not yet decided

- **Scope**: full Playerbots-equivalent (real group member, loot rolls,
  guild-capable) vs. a lighter "AI companion" that doesn't need full party
  UI parity. This decision changes how much of the measured patch is
  actually required — see `architecture.md`'s Scoping section. Decide this
  before writing the first line of patch or module code.
- The `docs/research/core-diff-analysis.md` category D files (real
  overlapping changes, not mechanical) haven't been individually resolved
  yet — they're categorized, not fixed.

## Publishing

Intended to eventually go to GitHub, publicly. Nothing has been pushed yet —
confirm with the user before any push (per this session's own standing
rule about explicit confirmation before publishing/pushing).
