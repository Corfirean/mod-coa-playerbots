# Architecture: why a patch is needed, and how small we can make it

## The goal

Bot companions that can play any of CoA's ~21 custom Ascension classes
(`mod-ascension-compat`, IDs 12-32), packaged so that upgrading the CoA core
doesn't mean re-deriving a huge merge every time. Solo-play focus, not a
production multi-thousand-bot realm.

## Why a pure module (zero core changes) can't reach full player parity

Verified by reading the actual AzerothCore source (`azerothcore-wotlk-coa`),
not by trusting a README. See `docs/research/module-hook-boundary.md` for the
full file:line citations. Summary:

- **Autonomous combat AI for a `Creature` needs zero core changes.**
  `CreatureAI`/`SmartAI`/`UnitAI` already expose everything (cast, move,
  target-select, react) through the sanctioned `CreatureScript::GetAI()` hook.
  This is not the hard part.
- **The wall is specifically: real 19-slot player equipment with stats,
  talent trees, and genuine `Group` membership with loot-roll participation.**
  These are concrete, non-virtual, `Player`-typed data structures with no
  seam on `Unit`/`Creature`:
  - `Group::AddMember(Player* player, ...)` takes a `Player*` **by type**,
    not `Unit*`. `GroupReference : Reference<Group, Player>` is
    template-locked to `Player`. Loot-roll code hard-requires
    `member->GetSession()`.
  - Item storage (`Player::m_items[PLAYER_SLOTS_COUNT]`), talents
    (`Player::m_talents`) are private `Player` members with no analog
    declared on `Unit`.
  - A `Creature`'s only "equipment" is 3 cosmetic virtual-item-display slots
    (`Unit::SetVirtualItem`) — no real `Item` objects, no stats, no bag.

## The two ways past the wall, and which one to use

1. **NPCBots' way**: patch `Unit.h`/`Player.h`/`Group.cpp` directly to add
   the missing fields/overloads onto `Creature`. Heavier, more invasive —
   you're generalizing types that were never meant to be generalized.
2. **Playerbots' way (chosen)**: the bot **is a real `Player` object**,
   attached to a `WorldSession` whose socket is `nullptr`. This works because
   `WorldSession` already null-checks its socket almost everywhere
   (`SendPacket` no-ops on a null socket, `Update`/`IsSocketClosed` guard
   every socket-touching branch) — incidental defensive coding, not a
   designed bot API, but real and exploitable. Because the bot genuinely is a
   `Player`, it automatically has real equipment, talents, and satisfies
   `Group::AddMember(Player*)` with **no changes to `Group`'s type system at
   all** — confirmed empirically: playerbots-fork's actual diff to
   `Group.cpp` is 17 lines, `Group.h` is 3 lines, `GroupReference.h` is
   untouched. My first-pass assessment (before measuring) assumed this would
   need a type-system rewrite; it doesn't, because the fake-session trick
   sidesteps the problem entirely rather than solving it.

**Decision: follow Playerbots' architecture** (real `Player` + dead
`WorldSession`), not NPCBots'. The measured patch footprint (~2700 lines / 77
files, see `core-diff-analysis.md`) confirms this is a moderate, bounded
patch — not the "rewrite the type system" outcome the theoretical analysis
alone would have predicted.

## What the patch is actually for (from the measured diff, not guessing)

Reading what playerbots-fork's core patch *does* file-by-file, the real
categories of core changes are:

1. **Let a `Player` exist and act without a live socket** — `WorldSession`
   already tolerates this; the patch is mostly small guard additions at the
   edges (mail, item ownership checks — see category C in
   `core-diff-analysis.md`) where code assumed "a `Player*` implies a live
   client" in ways the null-check convention didn't already cover.
2. **Character creation/login flow for bots** (`CharacterHandler.cpp`, ~500
   lines) — bots need character rows without going through the real login
   packet sequence.
3. **Guild support** (`Guild.cpp/h`, ~830 lines — the single largest chunk)
   — **only needed if bots should be able to join/interact with guilds.**
   Candidate for dropping entirely if out of scope (see Scoping below).
4. **New `ScriptMgr` hooks** so the external bot-AI module can observe/react
   to things the base hook set doesn't cover.
5. **A handful of genuine bug-compatibility fixes** (double-invite, item
   owner-null checks) that only manifest because a bot drives `Player`
   methods directly/synchronously instead of through the normal
   packet-round-trip path.

## Scoping question — decide before writing any code

Full Playerbots-equivalent (real group member, loot rolls, guild-capable,
can be geared by the player) requires accepting something close to the full
measured patch. A scoped-down "AI companion" (follows you, fights with real
Ascension-class abilities, doesn't need to show up in the party frame as a
distinguishable geared character) could plausibly stay much smaller — worth
re-measuring the *minimum* required subset once this is decided, rather than
assuming the full 2700-line patch is the target. **Not decided yet.**

## Zero bot AI exists for any of the 21 Ascension classes, in either base project

Neither NPCBots nor Playerbots has any awareness of non-standard class IDs.
Their bot combat logic is hardcoded per one of the 11 stock classes
(rotation = "which existing spell ID to cast in what situation"). Whichever
scoping we land on, **100% of the actual class-rotation AI is new work**,
written against the mechanics `mod-ascension-compat` already implements
(auras/procs/resources — see that module's own docs). The core patch buys us
the *chassis* (a controllable bot-Player); it buys us zero rotation logic.
