---
name: coa-playerbots
description: Orient to the mod-coa-playerbots project (AI companion bots for CoA's custom Ascension classes) before doing any real work in this repo — architecture decisions, core-patch diff analysis, and Ascension class completeness status.
---

# mod-coa-playerbots project orientation

Use this at the start of any session touching this repository, or when the
user references "the bot project" / "playerbots for CoA" / companion bots
for Ascension classes.

## Do this first

1. Read `AGENTS.md` in the repo root — it is the authoritative, current
   summary of what's decided, what's not, and where the supporting research
   lives. Do not skip straight to writing code or a plan without it.
2. If the task touches **core patching** (what needs to change in
   `azerothcore-wotlk-coa` to support bots): read `docs/architecture.md` and
   `docs/research/module-hook-boundary.md` before proposing anything. The
   boundary between "free via module hooks" and "needs a core patch" has
   already been determined empirically (file:line citations exist) — don't
   re-guess it from a bot project's README.
3. If the task touches **reconciling CoA's core against mod-playerbots'
   patch**: read `docs/research/core-diff-analysis.md`. Every conflicting
   file has already been categorized (mechanical resolution vs. genuine
   design decision needed). Extend this analysis if upstream has moved
   significantly since it was written; don't restart it from scratch.
4. If the task touches **writing bot AI for a specific Ascension class**:
   read `docs/research/ascension-class-status.md` first and check whether
   that class is in the "confirmed working by actual play-testing" list.
   **Never** infer a class's completeness from source-file presence alone —
   this project has direct evidence that method gives wrong answers (see
   that doc's warning section). If a class isn't confirmed, say so and
   suggest testing it in-game before investing rotation-AI effort in it.

## Standing facts worth remembering without re-deriving

- The bot's identity model is "real `Player` object with a null-socket
  `WorldSession`" (Playerbots' approach), not "extended `Creature`"
  (NPCBots' approach) — decided in `architecture.md`, because the former
  gets real group/loot/gear support almost for free (type system already
  supports it) while the latter needs deep `Group`/`Unit`/`Player` type
  changes.
- CoA has **21** custom classes (IDs 12-32), not fewer — get the exact list
  and names from `ascension-class-status.md`, not from memory or from
  grepping only for files matching an `AscensionX*.cpp` naming pattern
  (several real classes have no such file).
- The ground-truth server for "does X actually work" questions is the
  running `CoA-Repack` instance, not source reading alone — see AGENTS.md
  for how to reach it.

## When new research changes something in these docs

Update the relevant `docs/research/*.md` file directly rather than leaving
the correction only in a chat reply — the whole point of this documentation
is that a future session (or a different person, once this is public) can
pick up context without the previous conversation being replayed to them.
