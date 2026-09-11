# Pilot: minimal fake-session Player on CoA's core

**Status: succeeded, 2026-09-11.** This directory holds the actual code and
patch from the pilot described in `AGENTS.md` — proof that a real `Player`
can exist via a `WorldSession` with a `nullptr` socket on CoA's live core,
with zero bot AI. Kept here as a reference and a starting point, not as a
finished module.

## What this proves

A GM command (`.pilot spawnbot <charGuid>`) logs an *existing* character in
through a fake, null-socket session instead of a real client connection.
Verified end-to-end against a live `CoA-Repack` instance:

- The character went through real `Player::LoadFromDB` validation (it even
  caught and removed a genuinely invalid spell on the test character, exactly
  as a normal login would).
- Real `ScriptMgr` `OnPlayerLogin`-family hooks fired, including
  `mod-ascension-compat`'s own wardrobe/collection sync for that character.
- The bot showed as an entity in world (`Characters in world` count) while
  `Connected players` stayed at 0 — i.e., a real world presence with no
  network connection, which is the entire point.
- It survived several minutes of live ticking with no errors, and the
  server shut down cleanly with the bot still present.

## Files

- `core.patch` — the exact core diff against CoA (`azerothcore-wotlk-coa`),
  5 files, +29/-13 lines. Already committed directly to that checkout
  (commit `227a371b2`); this file is a portable copy for reference/reapplying
  elsewhere.
- `src/` — the pilot module's source. Copy (or symlink) this directory to
  `azerothcore-wotlk-coa/modules/mod-coa-playerbots-pilot/` before building
  — `modules/` is gitignored by the core repo itself (except
  `mod-ascension-compat`, which has an explicit carve-out), so it won't show
  up as a core-repo change.

## The core patch, and why each piece was needed

Two additions beyond what was originally planned — both found empirically
by actually running the pilot, not anticipated in advance:

1. **`LoginQueryHolder` moved from private-in-`CharacterHandler.cpp` to
   public in `WorldSession.h`** (planned — this was the known, expected
   part). An external module can't subclass a class it can't see.
2. **`World::AddQueryHolderCallback` + a `_queryHolderProcessor` member,
   mirroring the existing per-session ones** (NOT planned — found during
   testing). `WorldSession::ProcessQueryCallbacks()` — the only thing that
   ever drains a session's async query callbacks — is `private` and
   `friend`-only to `World`. A session that's never registered with
   `WorldSessionMgr` (which a null-socket bot session never is) has *no*
   path to ever get that private method called on it. The fix: queue the
   bot's login-completion callback on `sWorld` instead of on the bot's own
   session — `World::Update()` calls its own `ProcessQueryCallbacks()`
   unconditionally every tick regardless of session registration, so a
   world-level callback still fires. Concretely: `PilotBotMgr::SpawnPilotBot`
   calls `sWorld->AddQueryHolderCallback(...)`, not
   `botSession->AddQueryHolderCallback(...)`.

   This is exactly the reason `mod-playerbots`' own core patch touches
   `World.cpp`/`World.h`/`IWorld.h` — confirmed by diffing their fork
   against vanilla and finding the identical addition there.

## Two non-source gotchas hit while getting this running (not core-patch related)

- **vcpkg's `libmysql` port exports its own `localtime_r`**, which collides
  with CoA's own Windows compat shim in `Timer.cpp` at link time
  (`LNK2005`/`LNK1169`). Fixed with `-DCMAKE_EXE_LINKER_FLAGS=/FORCE:MULTIPLE`
  at CMake configure time — a build-environment flag, not a source change.
  Don't "fix" this by editing `Timer.cpp`; it's an artifact of this specific
  vcpkg mysql build, not a real conflict in AzerothCore's own code.
- **`LOG_INFO` on a custom channel (`"module.pilot"`) may not appear in the
  log** depending on the configured `Logger.module=<level>` threshold in
  `worldserver.conf` — CoA-Repack's default is WARN-and-above for the
  `module` logger category. Not a bug; either log at `LOG_WARN`/`LOG_ERROR`
  or raise that logger's configured level if you need to see `LOG_INFO`
  output during testing. `Characters in world` (via `server info` over RA)
  and the character's own login side effects (spell validation, ascension
  hooks) are more reliable signals than log lines anyway.

## Reproducing the test

1. Apply `core.patch` (or use the commit directly) to `azerothcore-wotlk-coa`.
2. Copy `src/` to `azerothcore-wotlk-coa/modules/mod-coa-playerbots-pilot/src/`.
3. Configure: `-DSCRIPTS=static -DMODULES=static -DAPPS_BUILD=world-only -DCMAKE_EXE_LINKER_FLAGS=/FORCE:MULTIPLE` (plus `-DMYSQL_LIBRARY=<path to mysqlclient.lib>` if CMake can't auto-resolve it — see `AGENTS.md`'s environment notes).
4. Build the `worldserver` target.
5. Deploy the binary, start the server, connect via RA (or in-game GM
   chat), run `.pilot spawnbot <existing character's low guid>`.
6. Check via RA `server info` — `Characters in world` should increment
   with `Connected players` staying unchanged.
