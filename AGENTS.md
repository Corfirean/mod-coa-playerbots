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

## Decided

- **Scope (decided 2026-09-11): full Playerbots-equivalent** — real group
  member, loot-roll participation, guild-capable, fully geared/talented.
  Not a scoped-down companion. See `architecture.md`'s Scoping section. This
  also means the project isn't obligated to just replicate Playerbots'
  stock-class bot behavior once the chassis works — custom AI ideas for
  Ascension classes are explicitly in scope, write them down as they come up
  (`architecture.md`'s "Beyond parity" section, or a new `docs/ai-ideas.md`
  once there's enough to warrant its own file).
- **Category D files (the 7 "real overlapping changes" from
  `core-diff-analysis.md`) are resolved**: all 7 turned out to need CoA's
  side taken (no bot-specific logic was actually in any of them), plus one
  unrelated one-line bugfix worth carrying over from playerbots-fork in
  `Creature.cpp`. Full per-file reasoning is in that doc — nothing left to
  re-derive here. **Only 3 files in the entire 77-file patch
  (`Item.cpp`, `Group.cpp`, `PointMovementGenerator.h` — category C) contain
  logic that must be preserved verbatim** because it exists specifically for
  a session-less bot `Player`.

- **Pilot succeeded (2026-09-11): the fake-session chassis works on real CoA
  core.** A real `Player`, driven by a `WorldSession` with a `nullptr`
  socket, logged in (via an existing character), went through genuine
  `Player::LoadFromDB` validation, fired real `ScriptMgr` login hooks
  (including `mod-ascension-compat`'s own), stayed in world for several
  minutes of live ticking, and the server shut down cleanly with it still
  present — all verified against the actual running `CoA-Repack` instance,
  not simulated. Full writeup, the module source, and the exact core patch:
  `pilot/README.md`. **This is no longer an open architectural risk** — the
  remaining work is building the real module on top of this proven chassis,
  not re-litigating whether the approach works.
- The pilot surfaced one core-patch item that wasn't anticipated: a
  detached bot session's login-callback needs `sWorld->AddQueryHolderCallback`
  (a small addition to `World`/`IWorld`), not the per-session one, because
  `WorldSession::ProcessQueryCallbacks()` is private/friend-only to `World`
  and a never-registered session has no other path to get it called. See
  `pilot/README.md` for the full reasoning — worth knowing before assuming
  the full-parity patch plan in `core-diff-analysis.md` is complete as
  written; it should get this same addition when written for real.

- **Module milestone 1 succeeded (2026-09-11): a bot can be a real group member.**
  `.botcmd spawnbot <guid>` + GM `/invite <name>` (real client) + `.botcmd acceptinvite
  <guid>` (calls `WorldSession::HandleGroupAcceptOpcode` directly with a padding
  packet) → confirmed via `.group list` on both sides AND visually in the GM's real
  party frame. **No `Group.cpp`/`.h` core patch was needed** — grouping worked on
  CoA's stock, unpatched `Group.cpp`; the Category-C double-invite fix stays
  unapplied until (if ever) it's actually triggered. Full writeup: `module/README.md`.
- **The real blocker wasn't core code at all — it was test-account hygiene.** The
  first attempt used a bot character on the *same account* as the GM's own login
  (`Test`, guid 1, account `LOCAL`/1 — same account the GM plays from). Two
  simultaneous sessions on one account is a state normal login never produces;
  the bot came up visibly wrong (`.pinfo` showed `GM Mode active, Phase: -1`) and
  `/invite` couldn't find it by name. **Bots must run on a different account than
  whichever account the human tester is using that session** — this is exactly why
  real Playerbots always separates `masterAccountId` from the bot's own account.
  Current state on this repack: `LOCAL` (account 1) has `Test` (guid 1) and `Mesha`
  (guid 3); `Shaniel` (guid 2) was moved to `ADMIN` (account 2) specifically to be a
  collision-free bot test character — reuse `Shaniel`/account 2 for bot testing
  going forward rather than `Test`/`Mesha`, or set up a dedicated bot account before
  testing with more than one bot at once.
- Two red herrings hit during that same test, worth knowing so they don't cost time
  again: the WotLK client's Friends List "Invite" button has its own cooldown
  (shows greyed out "in N minutes") unrelated to the server — use `/invite <name>`
  typed in chat instead, no cooldown there. And GM-issued invites skip the
  same-faction check entirely (`HandleGroupInviteOpcode`'s guard is
  `!invitingPlayer->IsGameMaster() && ...`), so cross-faction GM/bot pairings (which
  happened here) were never actually a problem.
- **Standing decision (2026-09-11): stop restoring the original `worldserver.exe`
  after every test.** The pilot and this milestone both defaulted to backing up and
  restoring the binary per session; the user says not to bother going forward —
  the fork's own devs keep a Discord backup, so rollback is cheap if ever needed.
  Keep working directly on the patched binary across sessions unless told otherwise.

## Build environment notes (learned the hard way during the pilot)

`azerothcore-wotlk-coa/build` may exist but be configured for something
*other* than a real server build (it was, for an earlier unrelated tool) —
check `APPS_BUILD`/`SCRIPTS`/`MODULES` in `build/CMakeCache.txt` before
assuming a worldserver build is one `ninja worldserver` away. For a real
build on this machine:

- Toolchain: MSVC via Visual Studio 18 Insiders + vcpkg
  (`C:\vcpkg`, toolchain file already wired into the cache,
  triplet `x64-windows-static-md`). `vswhere.exe` isn't on `PATH` by
  default — add `C:\Program Files (x86)\Microsoft Visual Studio\Installer`
  to `PATH` before calling `vcvarsall.bat`, or `cmake`/`ninja` invocations
  fail confusingly.
- `libmysql` was never installed under vcpkg on this machine — install with
  `vcpkg install libmysql:x64-windows-static-md`, then also
  `vcpkg install boost:x64-windows-static-md` (AzerothCore's own
  `find_package(Boost COMPONENTS ...)` only requests 4 components, but the
  source uses several header-only boost libraries — like `boost/heap/` in
  `ThreatManager.cpp` — that need their own vcpkg sub-port installed even
  though nothing links them).
- Even after that, CMake's `find_library(MYSQL_LIBRARY NAMES libmysql ...)`
  in `src/cmake/macros/FindMySQL.cmake` won't find it — this vcpkg port
  builds `mysqlclient.lib`, not `libmysql.lib`. Pass
  `-DMYSQL_LIBRARY=C:/vcpkg/installed/x64-windows-static-md/lib/mysqlclient.lib`
  explicitly at configure time rather than editing `FindMySQL.cmake`.
- That same vcpkg `libmysql` build exports its own `localtime_r`, which
  collides with CoA's own Windows shim in `common/Utilities/Timer.cpp` at
  link time (`LNK2005`/`LNK1169`). Fix with
  `-DCMAKE_EXE_LINKER_FLAGS=/FORCE:MULTIPLE` at configure time — do not
  "fix" this by editing `Timer.cpp`, it's an artifact of this specific
  vcpkg build.
- Deploying to `CoA-Repack`: stop with
  `Runtime/python/python.exe -B Scripts/manage.py stop-all`, copy the new
  `worldserver.exe` into `Core/`, restart with `... start-world` (does not
  restart `authserver` — use `... start-auth` too if it was running before).
  RA (remote console) is enabled on this repack
  (`Settings/repack.json`: port 3443, user/pass `local`/`local`) — far
  easier for scripted testing than driving an actual game client; see
  `pilot/` for a minimal Python RA client.

## Not yet decided

- Scope, the core-patch categorization, and the chassis are all settled and proven.
  Module milestone 1 (real group membership) is done too. What's actually open now:
  which of loot-roll participation, guild support, auto-accept-on-invite, a proper
  dedicated bot account, or custom AI-behavior ideas to tackle next — ask the user
  rather than assuming.

## Publishing

Intended to eventually go to GitHub, publicly. Nothing has been pushed yet —
confirm with the user before any push (per this session's own standing
rule about explicit confirmation before publishing/pushing).
