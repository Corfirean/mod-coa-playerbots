# mod-coa-playerbots

AI companion bots that can play [Conquest of AzerothCore](https://github.com/jealous-sound/azerothcore-wotlk-coa)'s
custom Ascension classes, for solo play.

**Status: research/planning. No code yet.** See `docs/` for what's been
figured out so far and why.

## Why this exists

CoA runs [`mod-ascension-compat`](https://github.com/jealous-sound/azerothcore-wotlk-coa/tree/main/modules/mod-ascension-compat),
a reconstruction of ~21 custom classes from the now-shut-down Project
Ascension. Playing solo with a custom class and a couple of AI-controlled
companions is the goal. Neither of the two established AzerothCore bot
projects — [NPCBots](https://github.com/trickerer/AzerothCore-wotlk-with-NPCBots)
or [Playerbots](https://github.com/mod-playerbots/mod-playerbots) — has any
concept of a non-standard class, and both require a patched core rather than
a drop-in module.

This project exists to figure out, with actual measurements rather than
assumptions, how small that core patch can be kept, and to build the bot
rotation AI for Ascension's classes from scratch (nobody has done this
anywhere — regardless of which base bot project's patch approach gets used,
100% of the class-specific AI is new).

## Read this first

[`AGENTS.md`](AGENTS.md) — the actual working-context doc (also the entry
point Claude Code loads automatically). Start there, not here, if you're
about to do real work on this project.

## Documentation map

- [`docs/architecture.md`](docs/architecture.md) — why a pure module can't
  reach full player parity, the two ways past that wall, and which one this
  project uses (and why).
- [`docs/research/module-hook-boundary.md`](docs/research/module-hook-boundary.md)
  — exactly what a normal module can/can't do, with file:line citations
  against the real AzerothCore source.
- [`docs/research/core-diff-analysis.md`](docs/research/core-diff-analysis.md)
  — a real three-way diff between vanilla AzerothCore, mod-playerbots' core
  fork, and CoA's core: which files apply cleanly, which conflict, and what
  every conflict actually is.
- [`docs/research/ascension-class-status.md`](docs/research/ascension-class-status.md)
  — which of the 21 Ascension classes are actually confirmed playable
  (verified in-game, not just "has source files").

## License / attribution

Builds on research into [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots)
(GPL2, per AzerothCore convention) and [azerothcore-wotlk-coa](https://github.com/jealous-sound/azerothcore-wotlk-coa).
No code from either has been copied yet — the diff analysis in this repo is
original research (line counts and categorization), not a redistribution of
either project's source.
