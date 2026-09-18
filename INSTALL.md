# Install & setup

## Quick install (precompiled, for a CoA-Repack user — no building required)

1. Download the latest `dist/` release package (zip) from this repo's
   Releases page.
2. Stop the server (`worldserver.exe` must not be running).
3. Extract the zip anywhere.
4. Run `Install-CoaBots.bat` (double-click, or `Install-CoaBots.ps1
   -RepackPath "<path to your CoA-Repack folder>"` if it's not at the
   default `C:\games\CoA-Repack`).
5. It checks your repack's `RELEASE.json` against the release the patch was
   built for, backs up your original `worldserver.exe` to
   `worldserver.exe.orig`, installs the patched `worldserver.exe`, the
   module config, the talent-build data, the client addon (if a `Client\`
   folder exists next to `Core\`), and any pending core DB migration the
   binary needs.
6. Start the server.
7. From GM chat or RA: `.botcmd spawnleveled 500` to create a bot
   population. Set `CoaBots.AutoLoginOnStartup = 1` in
   `Core\configs\modules\mod_coa_playerbots.conf` so they log back in
   automatically on future restarts.

To uninstall: stop the server, run `Uninstall-CoaBots.bat` (restores
`worldserver.exe.orig`), delete the extracted files.

If step 5 refuses with a repack-version mismatch, your repack doesn't match
this patch release — wait for a matching one, or re-run with `-Force` at
your own risk (bots may not work correctly).

## Building from source (for development)

This is a module for a private `azerothcore-wotlk-coa` core checkout, not a
standalone distributable — these steps assume you already have that core
built and a working CoA repack (auth DB, characters DB, world DB, a
`worldserver.conf`) up and running without bots first.

### 1. Register the module with the core

The module source lives at `mod-coa-playerbots/module/src` in this repo. The
core build expects it under `azerothcore-wotlk-coa/modules/mod-coa-playerbots/src`
— either clone this repo there directly, or (the workflow this project
actually uses day to day, since the two live in separate git repos) mirror
the files across after every edit:

```bash
cp mod-coa-playerbots/module/src/*.h  azerothcore-wotlk-coa/modules/mod-coa-playerbots/src/
cp mod-coa-playerbots/module/src/*.cpp azerothcore-wotlk-coa/modules/mod-coa-playerbots/src/
```

CMake picks up any file already present under `modules/mod-coa-playerbots/src`
automatically on the next configure — no `CMakeLists.txt` changes needed for
a new `.cpp`/`.h` pair using the existing naming pattern.

### 2. Build

From the core checkout, run whatever CMake+Ninja build you normally use for
`azerothcore-wotlk-coa` (this project's own dev loop uses a `build2.bat` that
calls `vcvarsall.bat x64` then `cmake --build` against the existing
`build/` directory — reuse that if you already have one). A clean rebuild of
just this module after a source edit takes well under a minute; only the
first full-core build is slow.

The build produces `build/bin/worldserver.exe`. **This module does need a
handful of small core patches** (a bot is a real `Player`/`WorldSession`
with a null socket, which needs a few private-member/module-visibility
holes opened in core that a normal module can't make on its own) — see
[`docs/core-patches.md`](docs/core-patches.md) for the exact files, why each
one is needed, and — importantly — which ones are still sitting as
**uncommitted** working-tree changes in the core checkout rather than real
commits. Apply/verify those before assuming a fresh core checkout will
support bots at all. See [`docs/architecture.md`](docs/architecture.md) for
why this small-patch approach was chosen over a pure-module-only design.

### 3. Deploy the binary and config

```powershell
Copy-Item -Path "<core-checkout>\build\bin\worldserver.exe" `
          -Destination "<repack>\Core\worldserver.exe" -Force
```

Copy the module's config template into the repack's modules config
directory on first install, then edit the real file (never the `.dist`) to
set your own values:

```powershell
Copy-Item "mod-coa-playerbots\module\conf\mod_coa_playerbots.conf.dist" `
          "<repack>\Core\configs\modules\mod_coa_playerbots.conf.dist"
# first time only -- creates the real config from the template:
Copy-Item "<repack>\Core\configs\modules\mod_coa_playerbots.conf.dist" `
          "<repack>\Core\configs\modules\mod_coa_playerbots.conf"
```

On every later update, re-copy the `.dist` file (it's always safe to
overwrite — it's just documentation of defaults) but **never overwrite the
real `.conf`** — instead diff the two and manually add any new keys the
`.dist` gained. See [Configuration](#configuration) below for what every key
does.

The module also needs `reference/ascensionsidekick-level-builds.json` (this
repo's own community talent-build data, keyed by classId:specId) to be
reachable at runtime for talent allocation to work. `CoaBots.TalentBuildsPath`
in the config can point at it explicitly; left blank, the module searches a
few relative paths and a hardcoded absolute dev-machine path as a last
resort — set the config value explicitly if you're not running from the
same machine/path this project was developed on.

### 4. Database

The module adds no new database *server* (no separate `acore_playerbots` DB
the way upstream mod-playerbots uses) — bots are real characters in the
existing `acore_characters` database, on their own dedicated accounts (never
the human operator's own account — see `CoaBots.RandomSpawn.AccountPrefix`).
No bot-specific SQL migrations are needed to start.

If `worldserver.exe` fails to start with `Could not prepare statements of
the Character/World database, see log for details`, check the *core's* own
pending SQL migrations under `modules/*/data/sql/db-*/` — automatic DB
updates are typically disabled in this project's config
(`AUTOUPDATER: Automatic database updates are disabled`), so anything not
manually applied stays missing and only surfaces as a startup crash the
first time a code path that needs it actually runs. Run the specific `.sql`
file directly against the named database with the `mysql` client if this
happens; it's not usually a bot-module problem even when a bot-adjacent
feature (like talent allocation) is what triggers it.

### 5. First start

Start `worldserver.exe` as usual. With `CoaBots.AutoLoginOnStartup = 0`
(the template default), zero bots come online automatically — useful for a
clean dev boot. To get a population going:

```
.botcmd spawnleveled 500
```

from the in-game GM chat or the RA console (`.botcmd` commands work from
either). This creates 500 new bot characters spanning level 1-80 (weighted
mostly low-level — see `RollWeightedLevel` in `BotSpawnRandom.cpp`), each
with profession skills, starting bags/food/water, level-appropriate gear,
and (once it reaches level 10) a real talent spec from the community build
data. Creation and login are both throttled (`CoaBots.RandomSpawn.BatchSize`
etc. — see [Configuration](#configuration)) specifically because a
synchronous bulk spawn has crashed this server before; don't lower those
intervals aggressively without re-testing at scale.

Once you have bots, set `CoaBots.AutoLoginOnStartup = 1` so they come back
online automatically on every future restart without needing this step
again.

### 6. Client addon (optional but recommended)

Copy `mod-coa-playerbots/addon/CoABotUI` into your WotLK client's
`Interface/AddOns/` folder. See
[`docs/addon-client.md`](docs/addon-client.md) for what it does and
[`docs/addon-protocol.md`](docs/addon-protocol.md) for the wire protocol, if
you're extending it.

## Configuration

Every setting lives in `mod_coa_playerbots.conf` under `[worldserver]`. The
`.conf.dist` in this repo is the authoritative reference — every key has a
comment there explaining what it does, its default, and (for the throttle
and threshold values in particular) *why* that default was chosen, often
citing a specific incident from this project's development history. Skim
that file rather than this one for the full, current list; the categories
are:

- **Bulk creation** (`CoaBots.RandomSpawn.*`) — how many bots
  `.botcmd spawnrandom`/`spawnleveled` create, the safety cap, and the
  creation throttle pace.
- **Auto-login** (`CoaBots.AutoLoginOnStartup`, `CoaBots.AutoLogin.*`) —
  whether existing bots log themselves back in on every restart, and how
  fast.
- **Talent builds** (`CoaBots.TalentBuildsPath`) — where to find the
  community build data.
- **Role balance** (`CoaBots.SpecBalance.*`) — target Tank/Healer
  percentages the auto-spec-assignment logic aims for.
- **Combat/AI tuning** (`CoaBots.Healer.CriticalHealPct`,
  `CoaBots.Grind.*`) — when a solo healer breaks formation to heal, and how
  solo/idle bots pick a grinding target.
- **Group fill** (`CoaBots.BGFill.*`, `CoaBots.LfgFill.*`) — automatic
  Battleground/Dungeon Finder population.

See [`docs/capabilities.md`](docs/capabilities.md) for what all of this adds
up to in terms of actual bot behavior.
