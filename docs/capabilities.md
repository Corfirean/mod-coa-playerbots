# What bots can do today

A snapshot of actual, confirmed-working bot behavior, not a roadmap. Every
item here has been verified live on a running server, most against a
population in the hundreds-to-low-thousands. See `AGENTS.md` and
`module/README.md` for the development history behind each feature if you
need the "why," not just the "what."

## Population & leveling

- **Bulk population**: `.botcmd spawnleveled <count>` creates new bot
  characters spanning level 1-80, weighted toward low levels (60% level
  1-10, 20% 11-30, 15% 31-60, 5% 61-80 by default), each on its own
  dedicated account (never the operator's own account). Creation and login
  are throttled to a configurable pace specifically because an
  unthrottled bulk spawn has crashed the server before (see
  `CoaBots.RandomSpawn.*` in the config).
- **`.botcmd spawnrandom <count>`**: clones an existing level-80
  hand-verified test character per class instead of building one from
  scratch — faster, used for quick population testing rather than a
  "real" leveling population.
- **Race/faction consistency**: a randomly-assigned bot's race always
  matches the faction of the class template it was cloned from — an
  earlier version could put a Horde-race bot standing in an Alliance city.
- **Auto-login on startup**: with `CoaBots.AutoLoginOnStartup = 1`, every
  existing bot character logs itself back in automatically on every
  worldserver restart, gradually, with no manual respawn step needed.
- **Real leveling**: bots gain levels through the same grinding/questing
  paths a real player would (see Combat AI below for how they find
  targets solo), not an instant level-set.

## Gear & professions

- **Level-appropriate gear on spawn**: every fresh bot gets a full
  15-slot equipment loadout scored for its actual level and role (tank
  gear favors Stamina/armor/avoidance stats; healers/casters favor
  Intellect/Spirit; melee DPS favors Strength/Agility/AP), tries all
  armor-proficiency types per slot rather than assuming a class can wear
  plate, and gives tanks a shield in the offhand where appropriate.
- **All professions at once**: every bot learns all 11 primary
  professions, both secondary skills (Cooking, First Aid, Fishing), and
  this realm's 2 custom professions (Woodcutting, Woodworking), skilled up
  to a level-appropriate cap — CoA allows a character to hold every
  profession simultaneously, unlike stock WoW's 2-primary limit.
- **Starting kit**: bags, food, and water are granted alongside gear so a
  fresh bot doesn't need a vendor run to be immediately useful.
- **`.botcmd geartrainer [charLowGuid]`**: re-runs the level-80 gearing
  pass against an existing bot (or the whole online population) — useful
  for backfilling gear on any bot that missed its initial setup (e.g. one
  created during a mid-batch crash).

## Talents & specialization

- **Automatic spec assignment**: a bot without a specialization picks one
  at level 10, weighted toward whichever role (Tank/Healer) the current
  online bot population is short of relative to a configurable target
  percentage, falling back to DPS.
- **Real talent spending from community build data**: as a bot levels
  past 10, it spends its talent points following curated per-spec builds
  (`reference/ascensionsidekick-level-builds.json`, sourced from
  ascensionsidekick.com, covering all 70 specs across the 21 custom
  classes) — not random points, not "learn everything," an actual
  community-vetted build followed level by level, including correctly
  replacing a lower talent rank with the next one as it becomes
  available.
- **`.botcmd learnspec <charLowGuid> <specId>`**: manually assign/change a
  bot's specialization; re-applies the community talent build for the new
  spec up to the bot's current level.
- **`.botcmd setrole` / `checkrole`**: manually override or inspect a
  bot's combat role (Tank/Healer/Dps/Support) independent of its detected
  spec, for group-composition testing.

## Combat AI

- **Class-aware rotations**: dedicated spell-priority combat logic (real
  spell IDs, resource checks, cooldown-aware sequencing — verified
  against this realm's own `ascension_custom_class_spell` table, not
  assumed from retail WoW ability names) exists for representative specs
  across all four roles (tank, healer, melee DPS, caster DPS), with a
  generic spellbook-driven fallback (casts whatever offensive/heal/taunt
  spell the bot has actually learned and can currently use) covering every
  other class so nothing is left doing nothing.
- **Group-aware target acquisition**: a DPS or healer bot reacts to
  combat started by *any* group member, not just the nominal party
  leader — checks each member's actual current target/victim, falling
  back to nearby-attacker detection if that's unreliable for a given
  class.
- **Tank proactive peel**: a tank bot scans for group-mates being
  attacked by a hostile that isn't already its own target and moves to
  help, instead of only reacting once its own current target changes.
- **Role-aware follow formation**: tanks hold the front, healers the
  back, DPS/support fan out to the sides — positioning is computed from
  the bot's actual role, not just its position in the group's member
  list.
- **Formation-respecting healing**: a healer only breaks formation to
  chase a heal target when there's a real fight happening or the target
  has dropped below a critical health threshold (configurable) — it
  doesn't abandon the group's movement for routine chip-damage top-offs.
- **Solo/idle grinding**: an ungrouped bot with nothing else to do finds
  and fights a nearby hostile on its own (configurable search radius and
  max level-above-self), explicitly excluding unkillable training
  dummies/critters/this realm's own custom test-dummy NPCs so it doesn't
  waste cycles attacking something it can never finish off.
- **Group auto-disband**: if the real player leaves a group and no other
  real player remains, the remaining bots leave the group too instead of
  continuing to "follow" nothing — they resume acting like independent
  players rather than pets.
- **Auto-accept invites, loot-roll (auto-Greed), auto-mount-matching with
  the group leader.**

## Grouping & social

- **Quick Fill**: `.botcmd quickfill <charLowGuid>` (or the addon's
  equivalent button) pulls free bots into a group for whichever
  Tank/Healer/DPS roles are missing, giving real feedback if the bot pool
  is empty rather than failing silently.
- **Dungeon Finder auto-fill** (`CoaBots.LfgFill.Enable`): a solo player
  queuing for a dungeon via the real Dungeon Finder gets bots
  automatically added for whichever roles their own selection doesn't
  cover, so a full 5-man is ready immediately.
- **Battleground auto-fill** (`CoaBots.BGFill.Enable`): both factions'
  queues are topped off with bots the moment anyone queues for a normal
  BG bracket, so matches pop immediately instead of waiting on real
  population.
- **Guild support**: bots can create guilds, accept/send guild invites,
  and deposit/withdraw items or gold from the guild bank
  (`.botcmd guildcreate`/`guildinvite`/`guilddeposit`/`guildwithdraw`/etc.).
- **Craft orders**: `.botcmd craftorder` for profession-based crafting
  requests between a bot and the operator.

## Operator tooling (GM/RA commands)

`.botcmd spawnbot`, `spawnrandom`, `spawnleveled`, `despawn`, `acceptinvite`,
`invite`, `follow`, `stay`, `attack`, `stopattack`, `kill`, `suspend`,
`resume`, `setrole`, `checkrole`, `learnspec`, `listauras`, `hasspells`,
`runchat`, `quickfill`, `geartrainer`, `joinbg`, `joinlfg`,
`guildcreate`/`guildinvite`/`acceptguildinvite`/`guildgather`/`guilddeposit`/
`guildwithdraw`/`guilddepositgold`/`guildwithdrawgold`, `guildroster`,
`craftorder` — all console-usable (RA or in-game GM chat), see
`BotCommand.cpp` for exact syntax per command.

## Client addon

`addon/CoABotUI` gives a real player UI for the commands above (grouping,
role assignment, quick-fill, etc.) instead of needing raw GM commands from
the client. See `docs/addon-client.md` (what it does) and
`docs/addon-protocol.md` (the wire protocol, if extending it).

## Known gaps / not yet done

- Tank threat/aggro generation is not implemented per-class — most of the
  21 custom classes have no taunt/threat-transfer mechanic in the engine
  at all yet (each would need its own bespoke research, like the handful
  that already have one via `mod-ascension-compat` spell scripts).
- Combat rotations exist for one representative spec per role so far, not
  all 70 specs — the rest fall back to the generic spellbook-driven
  logic.
- Mount-matching with the group leader has open reports of not always
  working correctly; not yet root-caused with fresh log evidence.
- Complex pathing over multi-level geometry (e.g. a boss platform's
  stairs) can occasionally clip through terrain — believed to be an MMAP
  navmesh data limitation, not bot AI logic.
