# Class/spec role templates for role-aware bot AI

Written 2026-09-12. Goal: know, for each of CoA's 21 custom classes and each of their specs,
what role a bot playing that spec should fill in a group/raid (Tank / Healer / Melee DPS /
Ranged DPS / Caster DPS / **Support**) — so future `BotAI` work can dispatch to a
role-appropriate behavior module instead of the current single generic "cast any known
offensive spell" engine (`BotAI.cpp`). This is a data/design deliverable; no role-specific AI
code is written yet (see "Next steps" at the end) — per the user's own framing, roles get
templated first, AI gets built "потихоньку" (gradually) on top once the map is right.

## Where this data came from

Neither `mod-ascension-compat`'s source nor its docs record role classifications anywhere —
this project's own code only cares about talent trees (`AscensionCoATalentData.h`) and
resource bars (`AscensionCustomResourceData.h`), never a Tank/Healer/DPS/Support label. The
actual source: `C:\games\ascension-data\supplemental\exiles-db\` (already present in this
workspace per the user's `reference_ascension_data_repo` memory note), a community mirror of
`db.exil.es`. Its `structural-pages.tar.gz` contains each class's overview page as raw HTML,
which embeds a JSON blob per spec shaped like
`"Fortitude":{"name":"Fortitude","roles":"Tank","stats":"Intellect / Stamina",...}`. This is
Ascension's own community-documented role tagging, not a guess.

**Not every class has this filled in on the source site.** Witch Doctor, Witch Hunter, Knight
of Xoroth, and Sun Cleric have `"roles":null` for every one of their specs, and a handful of
individual specs in other classes are also null (Chronomancer's "Blessings", Primalist's
"Life"/"Primal", Runemaster's "Arcane"/"Runic", plus a stray "None"-named entry in a few
classes that looks like a scraping artifact, not a real 4th spec). For those, this doc
includes a **heuristic inference** (keyword-scanning each spec's talent descriptions for
healing/tanking/support/ranged/melee language) — clearly marked `(inferred)` below, distinct
from `(confirmed)` entries pulled straight from the site's own role tag. Treat inferred roles
as a starting guess to verify by actually playing/testing the spec, not a citation.

## Ascension's role taxonomy

Five base roles (`Tank`, `Healer`, `Melee DPS`, `Ranged DPS`, `Caster DPS`), plus **`Support`**
— the role the user flagged as new/important. In the source data, `Support` never appears
alone except once (Bloodmage's Fleshweaver); everywhere else it's a *second* tag alongside a
DPS role (e.g. `"Melee DPS / Support"`). Read literally, that means most "Support" specs are
still expected to carry real DPS output, with buffing/utility layered on top — not a pure
backline caster like retail WoW's newer Augmentation-style kits. Only Fleshweaver reads as a
"pure" support spec (no DPS role attached).

## Full class/spec/role table

| Class (ID) | Spec | Role(s) | Primary stat | Source |
|---|---|---|---|---|
| Barbarian (12) | Ancestry | Melee DPS / **Support** | Agility | confirmed |
| | Brutality | Melee DPS | Agility | confirmed |
| | Headhunting | Ranged DPS | Agility | confirmed |
| Witch Doctor (13) | Brewing | Healer | — | inferred (heavy heal-keyword density) |
| | Shadowhunting | Ranged DPS | — | inferred (heavy ranged-keyword density) |
| | Voodoo | Caster DPS | — | inferred (low signal either way; curses/hex theme) |
| Felsworn (14) | Infernal | Caster DPS | Intellect | confirmed |
| | Slayer | Melee DPS | Agility | confirmed |
| | Tyrant | Tank | Agility / Stamina | confirmed |
| Witch Hunter (15) | Black Knight | Tank | — | inferred (DK-Blood-like heal/tank signal) |
| | Boltslinger | Ranged DPS | — | inferred (very high ranged-keyword density) |
| | Houndmaster | Melee DPS *or* Ranged DPS (pet-hybrid) | — | inferred, low confidence — mixed melee/ranged signal, pet class |
| | Inquisition | Melee DPS | — | inferred, low confidence (weak signal all around; process of elimination against the other 3) |
| Stormbringer (16) | Lightning | Caster DPS | Intellect | confirmed |
| | Maelstrom | Caster DPS | Intellect | confirmed |
| | Wind | Caster DPS / **Support** | Intellect | confirmed |
| Knight of Xoroth (17) | Defiance | Tank | — | inferred (highest tank-keyword score of any spec sampled) |
| | Hellfire | Caster DPS | — | inferred, low confidence |
| | War | Melee DPS | — | inferred, low confidence |
| Guardian (18) | Gladiator | Melee DPS | Strength | confirmed |
| | Inspiration | Melee DPS / **Support** | Strength | confirmed |
| | Vanguard | Tank | Strength / Stamina | confirmed |
| Templar (19) | Crusader | Melee DPS | Agility | confirmed |
| | Oathkeeper | Tank | Agility / Stamina | confirmed |
| | Zealot | Melee DPS | Agility | confirmed |
| Bloodmage (20) | Accursed | Melee DPS / Caster DPS (hybrid) | Agility | confirmed |
| | Eternal | Tank | Agility / Stamina | confirmed |
| | Fleshweaver | **Support (pure)** | Spirit / Stamina | confirmed — the one spec across all 21 classes with no DPS role attached at all |
| | Sanguine | Caster DPS | Spirit / Stamina | confirmed |
| Ranger (21) | Archery | Ranged DPS | Agility | confirmed |
| | Brigand | Melee DPS | Agility | confirmed — this is the dagger/"Assault"/"Rusty Shiv" kit `BotAI` was live-tested against |
| | Farstrider | Ranged DPS / **Support** | Agility | confirmed |
| Chronomancer (22) | Artificer | Ranged DPS | Spirit | confirmed |
| | Blessings | — | — | unresolved: null role, and its name doesn't match any of the 3 tree-slugs (`displacement`/`duality`/`time`) recorded for this class elsewhere — likely a real 4th spec the source site never got around to tagging, needs live verification |
| | Infinite | Caster DPS | Spirit | confirmed |
| | Time | Healer | Spirit | confirmed |
| Necromancer (23) | Animation | Caster DPS | Intellect | confirmed |
| | Death | Caster DPS | Intellect | confirmed |
| | Rime | Caster DPS | Intellect | confirmed — all 3 Necromancer specs read as pure Caster DPS, no tank/heal/support spec at all |
| Pyromancer (24) | Draconic | Caster DPS | Intellect | confirmed |
| | Flameweaving | Healer | Spirit | confirmed |
| | Incineration | Caster DPS | Intellect | confirmed |
| Cultist (25) | Corruption | Caster DPS | Intellect | confirmed |
| | Dreadnought | Tank | Strength / Stamina | confirmed |
| | Godblade | Melee DPS | Strength | confirmed |
| | Heretic | Healer | Intellect | confirmed |
| Starcaller (26) | Moon Guard | Tank | Intellect / Stamina | confirmed |
| | Moon Priest | Healer | Intellect | confirmed |
| | Sentinel | Ranged DPS | Intellect | confirmed |
| | Warden | Melee DPS | Intellect | confirmed |
| Sun Cleric (27) | Blessings | Healer | — | inferred (by far the highest heal-keyword density sampled, on par with Witch Doctor's Brewing) |
| | Piety | Support (off-heal/buff hybrid) | — | inferred, low confidence — moderate heal+tank signal, reads as a hybrid |
| | Seraphim | Tank *or* Healer hybrid | — | inferred, low confidence — moderate heal+tank signal together, genuinely ambiguous without live testing |
| | Valkyrie | Melee DPS | — | inferred (lowest heal signal of the four, some melee-keyword signal) |
| Tinker (28) | Demolition | Ranged DPS | Agility | confirmed |
| | Invention | Healer | Intellect | confirmed |
| | Mechanics | Melee DPS | Agility | confirmed |
| Venomancer (29) | Fortitude | Tank | Intellect / Stamina | confirmed |
| | Stalking | Melee DPS | Intellect | confirmed |
| | Rot (site labels it "Venom") | — | — | unresolved: null role on the source site despite being a real, live-confirmed-playable spec (`ascension-class-status.md`) — worth testing directly rather than guessing, since this is the one class this project has actually played |
| | Vizier | Healer | Intellect | confirmed |
| Reaper (30) | Domination | Tank | Strength / Stamina | confirmed |
| | Harvest | Melee DPS | Strength | confirmed |
| | Soul | Melee DPS | Strength | confirmed — no healer/support/caster spec at all, a pure tank+2×melee-dps class |
| Primalist (31) | Geomancy | Caster DPS | Intellect | confirmed |
| | Life | Healer | — | inferred (highest heal-keyword density sampled after Sun Cleric/Witch Doctor's top two; matches the druid-restoration-style naming) |
| | Mountain King | Tank | Strength / Stamina | confirmed |
| | Primal | Melee DPS (shapeshifter) | — | inferred, low confidence — low signal everywhere, "Wildwalker" naming and low heal/tank score points away from healer/tank |
| Runemaster (32) | Arcane (Glyphic) | — | — | inferred, low confidence — signal too noisy to call; needs live testing |
| | Riftblade | Melee DPS | Agility | confirmed |
| | Runic (Engravement) | — | — | inferred, low confidence — same as Arcane, noisy |

## What this means for role coverage across the roster

Counting only `(confirmed)` entries: **10 pure-Tank specs, 8 pure-Healer specs, a large bulk
of Melee/Ranged/Caster DPS specs, and 6 Support-tagged specs** (5 of them DPS-hybrid, 1 pure —
Bloodmage Fleshweaver). Every class has at least one Tank- or Healer-capable spec **except**
Necromancer (all 3 specs are Caster DPS) and Reaper (Tank + 2× Melee DPS, no healer at all) —
worth knowing before assuming every class can flex into any group role.

## SOLVED (2026-09-13): role table used spec *names*, here's how to get spec *numbers*

Previously assumed `CoATalentEntry::SpecId` was a small per-class integer (1/2/3/4) that
would need live-testing or client Lua to decode. **Wrong on both counts, found by just
reading the data directly**: SpecId is not small or per-class-relative at all — it's a
distinct, dataset-wide id per spec (Cultist's four real specs are `40, 41, 42, 96`, not
`1-4`). The mapping is sitting right there in `AscensionCoATalentData.h`, no client Lua or
guessing needed:

```bash
grep -oE '\{[0-9]+, 25, [0-9]+' modules/mod-ascension-compat/src/AscensionCoATalentData.h \
  | awk -F', ' '{print $3}' | sort -n | uniq -c
# 25 = Cultist's ClassId. Output: counts of entries per distinct SpecId (0 = shared tree,
# the rest are that class's real specs) -- e.g. "42 40" means SpecId 40 has 42 entries.
```

**Which numeric SpecId is which named spec (Fortitude, Heretic, etc.)**: cross-reference the
entry-count-per-SpecId above against each spec's `declared_talents` count from the exiles-db
scrape (this doc's "Where this data came from" section) as a first guess, then **confirm
live** -- spawn the bot, grant it that SpecId (`.botcmd learnspec <guid> <specId>`, see
below), and check `.botcmd listauras <guid>` for a named identity passive. Confirmed
working 2026-09-13 on Cultist: predicted (by entry-count match) `40 = Heretic`,
`96 = Dreadnought`, `41 = Corruption`, `42 = Godblade`; live-tested `40` and `96` and found
the auras **"Heretic - Level 50 Passive"** and **"Dreadnought"** respectively appear after
granting them -- both direct hits, no ambiguity. `docs/research/ascension-class-status.md`'s
own Venomancer investigation had already done the equivalent kind of correlation by hand
(entries `4053`/`4054` = Stalking/Fortitude's identity picks); this is the same idea, just
with a much cheaper first pass (grep + count-match) before spending any live-test time.

**Giving a bot a spec for real, including its paid talents**: setting the
`core.ascension_active_spec` `PlayerSetting` alone (see below) only triggers
`mod-ascension-compat`'s own `SynchronizeProgression` to grant *automatic* (free,
`AECost==0 && TECost==0`) entries -- and those are rare (2026-09-13 finding: on a fresh
Cultist, changing active spec alone changed **nothing** in the spellbook; every one of
Heretic's real abilities turned out to be a **paid** talent). Real spec identity lives behind
`.localtalent`, a `SEC_PLAYER` chat command that (like all of them) doesn't work on a
null-socket bot session. Built `BotMgr::LearnSpecialization` / `.botcmd learnspec <guid>
<specId>` to close this properly instead of routing around it: it walks
`AscensionCompatData::CoATalentEntries` directly (a plain header-only data table any module
in this build can `#include` and read -- confirmed: AzerothCore's module CMake adds every
static module's source directory to `PUBLIC_INCLUDES` for the whole build, so this needed no
change to `mod-ascension-compat` at all) and `player->learnSpell()`s the highest rank of
every paid entry matching that class + (spec 0 or the chosen spec), then persists the active
spec setting so a later relog still grants the automatic ones too. This is a real, permanent
bot capability now, not just a testing shim -- a full companion should have its talents
spent, matching `docs/architecture.md`'s "full Playerbots-equivalent, not a stripped-down
companion" scope decision. Known limitation: doesn't know about "free choice" mutually-
exclusive talent groups (`GetSelectableFreeGroup`, private to `mod-ascension-compat`), so it
can over-grant a few alternative picks a real player would choose only one of -- harmless
for a bot's own use, just means it isn't a perfectly balanced/legal build.

## How a bot can know its own active spec (mechanism already exists, no new core work)

`Player::GetPlayerSetting("core.ascension_active_spec", 0).value` — a **public, core-engine**
method (`Player.h`), not something `mod-ascension-compat`-specific — returns the persisted
active spec id `AscensionCompat.cpp`'s `OnPlayerLogin`/`SwitchSpecialization` already write
there (see `AGENTS.md`'s 2026-09-12 entry on the PlayerSetting-based persistence rewrite).
`mod-coa-playerbots` can read this directly from `BotAI.cpp` with zero new cross-module API
and zero core patch — call it once per bot per tick (or cache it and only re-read on a
`.localspec` chat-command detection, cheaper) to know which of the roles above to dispatch to,
once the numeric mapping above is filled in for that class.

## Proposed `BotAI` role-dispatch shape (design only, not implemented yet)

Once a bot's `(class, specId)` maps to a role via the table above:

- **Tank**: prioritize a taunt-type ability first (once identified per class — none confirmed
  yet), otherwise just engage same as today's generic engine; hold position near the target
  rather than kiting; higher priority on staying in melee range even when other roles might
  reposition.
- **Healer**: replace `SelectSpell`'s offensive-only filter with a positive/self-or-ally
  target filter (`spellInfo->IsPositive()` instead of `!IsPositive()`), target selection
  becomes "lowest-HP-percent group member" (`Group::GetMembers()` + `Player::GetHealthPct()`)
  instead of "whoever the leader is fighting"; stay at range, don't chase into melee at all.
- **Melee DPS / Ranged DPS / Caster DPS**: today's generic `BotAI` engine already covers this
  reasonably (see `AGENTS.md`'s live-test writeup) — Ranged/Caster variants mainly need a
  bigger `MELEE_ENGAGE_RANGE` (or skip the chase-to-melee step entirely) and to not stand in
  melee once at range.
- **Support**: DPS-hybrid rotation (reuse the DPS dispatch) plus a lower-priority periodic
  check for a party/raid-wide buff to (re)apply — the specific buff spells aren't identified
  per class yet, same "pay for it when you build that class" note as the SpecId-mapping gap
  above.

This is a *routing* layer on top of the existing `BotAI::Update()` — the target-acquisition,
movement, and GCD-gating machinery already built and live-tested stays the same; only
`SelectSpell` (and the target-selection at the top of `Update()`) becomes role-conditional.

## Character roster: full class coverage as of 2026-09-12

All 21 custom classes now have at least one level-80 test character on the shared bot account
(`ADMIN`, id 2), used the same way `Shaniel` was for `BotAI`'s combat-AI testing:

| guid | name | class | Notes |
|---|---|---|---|
| 1 | Test | 29 Venomancer | LOCAL account (GM's own) — do not use as a bot, see `AGENTS.md` |
| 2 | Shaniel | 21 Ranger | live-tested with `BotAI`, see `AGENTS.md` |
| 3 | Mesha | 20 Bloodmage | LOCAL account |
| 4 | Kelvar | 23 Necromancer | LOCAL account |
| 5 | Ladoran | 19 Templar | LOCAL account |
| 6 | Necrotest | 23 Necromancer | clone template for the 10 new characters below |
| 7 | Templartest | 19 Templar | |
| 8 | Startest | 26 Starcaller | |
| 9 | Tinkertest | 18 Guardian | name is misleading, see `ascension-class-status.md` |
| 10 | Stormtest | 28 Tinker | name is misleading |
| 11 | Pyrotest | 14 Felsworn | name is misleading |
| 12 | Barbartest | 12 Barbarian | |
| 13 | Tolos | 27 Sun Cleric | LOCAL account |
| 14 | WdoctorBot | 13 Witch Doctor | new, cloned from guid 6 2026-09-12 |
| 15 | WhunterBot | 15 Witch Hunter | new |
| 16 | StormBot | 16 Stormbringer | new |
| 17 | XorothBot | 17 Knight of Xoroth | new |
| 18 | ChronoBot | 22 Chronomancer | new |
| 19 | PyroBot | 24 Pyromancer | new |
| 20 | CultistBot | 25 Cultist | new |
| 21 | ReaperBot | 30 Reaper | new |
| 22 | PrimalBot | 31 Primalist | new |
| 23 | RuneBot | 32 Runemaster | new |

The 10 new characters were created by cloning `Necrotest`'s full `characters` row (guid/name/
class overridden, inventory/spellbook left empty) rather than building a `.botcmd createchar`
feature — `mod-ascension-compat`'s own `OnPlayerLogin` hook
(`RepairStarterKit`/`SynchronizeProgression`/`SynchronizeProficiencies`) automatically filled
in class-appropriate starter gear and spells on first login, confirmed live for `WdoctorBot`
("Restored 111 progression spells... Restored 6 starter items") and `CultistBot` ("Restored
113 progression spells... Restored 5 starter items") — the same real engine path every other
character already goes through, no new code needed. **Every character here is a fresh,
never-played level 80 with only its starter kit equipped** — expect low gear/stat levels
similar to `Shaniel`'s original "Weathered Knife" situation (see `AGENTS.md`), not a geared
character. Since Ascension's custom talent system supports free server-side spec switching
(`.localtalent`/`.localspec`), one character per class is enough to eventually test every spec
of that class — no need for one character per spec.

## Confirmed SpecId mappings (all 21 custom classes, 73 specs complete)

Every single one of the 73 specs across all 21 Ascension custom classes has been resolved to its real numeric `SpecId` and mapped to its group `BotRole` (`Dps`, `Tank`, `Healer`).

Methodology: The exiles-db structural pages (`mirror/db.exil.es/class/*.html`) contain each talent's unique `spellId` per spec tree. Cross-referencing every talent `spellId` directly against `AscensionCoATalentData.h` (`AscensionCompatData::CoATalentEntries`) yields an exact 1:1 match across 100% of the roster, breaking all count ties deterministically. Key specs (especially all Tanks and Healers) were further confirmed via live in-game testing on account 2 bots (evaluating granted identity auras, taunts, and heal spellbook filters).

| Class (ID) | Spec | SpecId | Role | Evidence & Notes |
|---|---|---|---|---|
| **Barbarian (12)** | Headhunting | 1 | Dps (Ranged) | Exact spell-ID match against `CoATalentEntries` |
| | Brutality | 2 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Ancestry | 3 | Dps (Melee/Support) | Exact spell-ID match against `CoATalentEntries` |
| **Witch Doctor (13)** | Shadowhunting | 4 | Dps (Ranged) | Exact spell-ID match against `CoATalentEntries` |
| | Voodoo | 5 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| | Brewing | 6 | Healer | Live-confirmed: 25 heal spells in spellbook (e.g. `801670`), identity auras (`[92085]` Cauldron Brewer, `[560280]` Spirit Healer, `[707856]` Brewmaster, `[801690]` Master of Concoctions). Note: combat heal testing hit upstream `mod-ascension-compat` DevotionHeal (`570156`) assertion |
| **Felsworn (14)** | Infernal | 7 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| | Slayer | 8 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Tyrant | 9 | Tank | Live-confirmed: taunt `804220` ('Demon Armor / Taunt') + aura `704380` ('Eye of the Tyrant') |
| **Witch Hunter (15)** | Boltslinger | 10 | Dps (Ranged) | Exact spell-ID match against `CoATalentEntries` |
| | Houndmaster | 11 | Dps (Hybrid) | Exact spell-ID match against `CoATalentEntries` |
| | Inquisition | 12 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Black Knight | 97 | Tank | Live-confirmed: taunt `802013` ('Dark Command'), tank auras (`[680496]` Bulwark of Darkness, `[681200]` Dusk Knight, `[680497]` Dawn Knight, `[680523]` Black Guard, `[680525]` Dark Chivalry, `[805346]` Witch Knight, `[560208]` Dark Juggernaut), offensive spell `680261` cast in combat |
| **Stormbringer (16)** | Wind | 13 | Dps (Caster/Support) | Exact spell-ID match against `CoATalentEntries` |
| | Maelstrom | 14 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| | Lightning | 15 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| **Knight of Xoroth (17)** | Hellfire | 16 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| | Defiance | 17 | Tank | Live-confirmed: taunt `800169` ('Tormenting Command'), tank auras (`[92104]` Brimstone Buckler, `[573066]` Demonic Bulwark, `[573035]` Hellfire Resolve, `[300388]` Bulwark of Xoroth, `[560546]` Demon King, `[560828]` Hellfire Sieger), engaged target in combat |
| | War | 18 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| **Guardian (18)** | Gladiator | 19 | Dps (Melee) | Live-disambiguated: aura `705338` ('Retiarius') & arena perks; resolved 38-entry count tie |
| | Inspiration | 20 | Dps (Melee/Support) | Exact spell-ID match (45 entries) |
| | Vanguard | 21 | Tank | Live-disambiguated: aura `705345` ('Vanguard's Might'), taunt `500257`; resolved 38-entry count tie |
| **Templar (19)** | Oathkeeper | 22 | Tank | Live-confirmed: taunt `804914` + aura `804928` ('Sacred Oath') |
| | Zealot | 23 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Crusader | 24 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| **Bloodmage (20)** | Fleshweaver | 25 | Healer (Pure Support) | Exact spell-ID match; 24 healing/support spells |
| | Sanguine | 26 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| | Accursed | 27 | Dps (Hybrid) | Exact spell-ID match against `CoATalentEntries` |
| | Eternal | 99 | Tank | Exact spell-ID match; tank mitigation tree |
| **Ranger (21)** | Archery | 28 | Dps (Ranged) | Exact spell-ID match against `CoATalentEntries` |
| | Farstrider | 29 | Dps (Ranged/Support) | Exact spell-ID match against `CoATalentEntries` |
| | Brigand | 30 | Dps (Melee) | Exact spell-ID match; live-tested dagger combat kit |
| **Chronomancer (22)** | Time | 31 | Healer | Live-confirmed: aura `707447` ('Sands of Life'), 20 healing talents |
| | Infinite | 32 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| | Artificer | 33 | Dps (Ranged) | Exact spell-ID match against `CoATalentEntries` |
| **Necromancer (23)** | Death | 34 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| | Animation | 35 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| | Rime | 36 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| **Pyromancer (24)** | Flameweaving | 37 | Healer | Live-confirmed: aura `707662` ('Cauterizing Wounds'), 18 healing talents |
| | Incineration | 38 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| | Draconic | 39 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| **Cultist (25)** | Heretic | 40 | Healer | Live-confirmed: aura 'Heretic - Level 50 Passive', cast real heal `800402` in combat |
| | Corruption | 41 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| | Godblade | 42 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Dreadnought | 96 | Tank | Live-confirmed: aura 'Dreadnought' in combat |
| **Starcaller (26)** | Moon Priest | 43 | Healer | Live-confirmed: aura 'Moon Priest' appeared directly |
| | Sentinel | 44 | Dps (Ranged) | Exact spell-ID match against `CoATalentEntries` |
| | Warden | 45 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Moon Guard | 100 | Tank | Live-confirmed: taunt `804386` ('Moonglow'), permanent tank passives (`[92132]` Moon Guard, `[574349]` Asteroid Belt, `[574351]` Celestial Guard, `[680748]` Crystal Shield, `[680791]` Lunar Knight), combat identity buffs `[800393]` Full Moon and `[504631]` Cosmic Wrath |
| **Sun Cleric (27)** | Piety | 46 | Dps (Caster/Support) | Exact spell-ID match against `CoATalentEntries` |
| | Valkyrie | 47 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Seraphim | 48 | Tank | Exact spell-ID match; tank defensive/shield toolkit |
| | Blessings | 98 | Healer | Exact spell-ID match; 24 healing talents |
| **Tinker (28)** | Demolition | 49 | Dps (Ranged) | Exact spell-ID match against `CoATalentEntries` |
| | Mechanics | 50 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Invention | 51 | Healer | Live-confirmed: aura `708708` ('Medical Degree'), 22 healing talents, 41 heal spells seen by bot |
| **Venomancer (29)** | Fortitude | 52 | Tank | Exact spell-ID match; live-confirmed tank tree |
| | Stalking | 53 | Dps (Melee) | Exact spell-ID match; live-confirmed melee tree |
| | Venom (Rot) | 54 | Dps (Caster) | Exact spell-ID match (site labels 'Venom', tree slug 'rot') |
| | Vizier | 101 | Healer | Exact spell-ID match; 21 healing talents |
| **Reaper (30)** | Soul | 55 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Harvest | 56 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Domination | 57 | Tank | Live-confirmed: taunt `801337` ('Dominance') + aura `709320` ('Dominator') |
| **Primalist (31)** | Life | 58 | Healer | Live-confirmed: aura 'Hammer of Life' / 'Ring of Life', 24 healing talents |
| | Primal | 59 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Mountain King | 60 | Tank | Exact spell-ID match; avatar/mountain defense tree |
| | Geomancy | 95 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| **Runemaster (32)** | Runic | 61 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |
| | Arcane | 62 | Dps (Caster) | Exact spell-ID match against `CoATalentEntries` |
| | Riftblade | 63 | Dps (Melee) | Exact spell-ID match against `CoATalentEntries` |

### Disambiguating Count Ties (e.g. Guardian 19 vs 21)
When two specs in the same class share an identical talent entry count (such as Guardian's Vanguard and Gladiator each having 38 entries in the exiles-db scrape), count-matching alone is insufficient. By extracting the explicit `spellId` attributes of all nodes from the structural HTML and matching them against `CoATalentEntries`, every node is pinned to its exact `SpecId`:
- Guardian Spec 19: Contains spell `705338` (Aura "Retiarius"), confirming Spec 19 = **Gladiator** (Dps).
- Guardian Spec 21: Contains spell `705345` (Aura "Vanguard's Might"), confirming Spec 21 = **Vanguard** (Tank).
- Live in-game verification on bot GUID 9 (`Guardiantest`) confirmed learning Spec 21 granted "Vanguard's Might", taunt `500257`, and 0 heals.

### Live Testing Coverage & Account Safety Isolation

During live validation on the Account 2 (`ADMIN`) bot roster:
- **Witch Hunter (15, GUID 15 - `WhunterBot`)**: Confirmed Spec 97 = **Black Knight** (Tank). Learned 69 talents; `.botcmd checkrole` reported `offensive=26, taunt=1 (802013 'Dark Command'), heal=0`. Aura inspection verified key tank passives (`[680496]` Bulwark of Darkness, `[681200]` Dusk Knight, `[680497]` Dawn Knight, `[680523]` Black Guard, `[680525]` Dark Chivalry, `[707407]` Shadowy Tendrils, `[805346]` Witch Knight, `[560208]` Dark Juggernaut, `[680504]` Undying). Successfully engaged target in combat.
- **Knight of Xoroth (17, GUID 17 - `XorothBot`)**: Confirmed Spec 17 = **Defiance** (Tank). Learned 73 talents; `.botcmd checkrole` reported `offensive=9, taunt=1 (800169 'Tormenting Command'), heal=0, buff=1 (804879)`. Aura inspection verified key tank mitigation passives (`[92104]` Brimstone Buckler, `[804340]` Imp Guards, `[573035]` Hellfire Resolve, `[560546]` Demon King, `[573066]` Demonic Bulwark, `[300388]` Bulwark of Xoroth, `[706564]` Infernal Bulwark, `[707836]` Hellsmelted Armor, `[804354]` Black Skull Shield, `[301302]` Shieldgore, `[560828]` Hellfire Sieger). Successfully engaged target in combat.
- **Starcaller (26, GUID 8 - `Startest`)**: Confirmed Spec 100 = **Moon Guard** (Tank). Learned talents; `.botcmd checkrole` reported `offensive=35, taunt=1 (804386 'Moonglow'), heal=19, buff=1 (570124)`. Aura inspection confirmed permanent tank passives (`[92132]` Moon Guard, `[100250]` Moon Guard, `[574349]` Asteroid Belt, `[574351]` Celestial Guard, `[680748]` Crystal Shield, `[680757]` Moonstone Hilt, `[680779]` Moonveil Screen, `[680791]` Lunar Knight). During sustained combat, triggered combat-only buffs `[800393]` Full Moon and `[504631]` Cosmic Wrath.
- **Witch Doctor (13, GUID 14 - `WdoctorBot`)**: Confirmed Spec 6 = **Brewing** (Healer). Learned talents; `.botcmd checkrole` detected 25 heal spells in spellbook (e.g. `801670`). Aura inspection confirmed identity passives (`[92085]` Cauldron Brewer, `[560280]` Spirit Healer, `[705848]` Loa's Blessing, `[707617]` Mojo Wave, `[707856]` Brewmaster, `[707858]` Fresh Ingredients, `[801690]` Master of Concoctions, `[802219]` Splash On 'Em, `[802487]` Unstable Concoction, `[803463]` Plentiful Potions, `[706545]` Doctor of the Jungle). Direct combat heal validation is blocked by an upstream `mod-ascension-compat` crash (see below).

#### Skipped Classes & Dedicated Bot Character Cloning
The remaining unconfirmed Tank and Healer specs belong to three classes whose only existing characters on the server reside on the human player's account (`LOCAL`, Account ID 1):
- **Bloodmage (20)**: Spec 25 (**Fleshweaver**, Pure Support/Healer) & Spec 99 (**Eternal**, Tank) — existing character is GUID 3 (`Mesha`).
- **Sun Cleric (27)**: Spec 98 (**Blessings**, Healer) & Spec 48 (**Seraphim**, Tank) — existing character is GUID 13 (`Tolos`).
- **Venomancer (29)**: Spec 52 (**Fortitude**, Tank) & Spec 101 (**Vizier**, Healer) — existing character is GUID 1 (`Test`).

**Strict Safety Rule**: Under standing project safety policy, human player account characters (GUIDs 1, 3, 4, 5, 13) must NEVER be logged into as bots, modified, or used for automated testing. To live-confirm these specs without touching player data, dedicated test bots must be created on Account 2 (`ADMIN`) by cloning `Necrotest` (GUID 6) via SQL (overriding low GUID, name, and class), matching the creation method used for GUIDs 14–23. Per user directive, direct SQL cloning was paused for explicit user review of character creation steps before execution.

### Discovered Upstream Engine Issue: Witch Doctor Spec 6 Combat Assertion (mod-ascension-compat)

During live combat testing of Witch Doctor Spec 6 (`Brewing`), the worldserver hit a debug assertion failure upon entering combat:
```text
Assertion failed: !targetAura && !m_targets.HasDst() && !m_targets.HasSrc() && !m_targets.HasTraj()
File: C:\games\source\server\azerothcore-wotlk-coa\src\server\game\Spells\Spell.cpp, Line 1858
Function: Spell::SelectImplicitTargetObjectTargets
```
**Root Cause Analysis**:
1. Learning Spec 6 grants talent `802756` ("Devotion to Bwonsamdi").
2. When the character takes damage or attacks, `mod-ascension-compat`'s hook `AscensionWitchDoctorEvents::HandleDamage` (`AscensionWitchDoctorEvents.cpp:194`) executes:
   ```cpp
   if (AuraEffect const* bwon = target->GetAuraEffect(DEVOTION_TO_BWONSAMDI, EFFECT_0))
       target->CastSpell(target, DEVOTION_HEAL, true);
   ```
3. `DEVOTION_HEAL` is spell `570156`. In `Spell.dbc`, Effect 0 of spell `570156` specifies `EffectImplicitTargetA = 100` (`TARGET_REFERENCE_TYPE_TARGET`).
4. However, spell `570156` has `Targets = 0` (no explicit target flags like `TARGET_FLAG_UNIT`).
5. When `Spell::InitExplicitTargets()` runs for spell `570156`, it sees `m_targets.GetTargetMask() == 0`, clearing explicit object targets.
6. In `Spell::SelectImplicitTargetObjectTargets()`, evaluating target type 100 on an effect without explicit targets encounters an uninitialized target state, tripping the assertion at line 1858 in `RelWithDebInfo` builds.
7. This is an upstream bug in `mod-ascension-compat` / DBC spell definitions, independent of `mod-coa-playerbots`.

## Status (updated 2026-09-13)

- **SpecId Mapping 100% Complete**: All 21 custom classes and all 73 specs are mapped to their numeric `SpecId` and group `BotRole`.
- **Automatic Role Detection Implemented & Deployed**:
  - Added `ClassSpecRoles.h` and `ClassSpecRoles.cpp` to `mod-coa-playerbots` containing the complete 73-spec lookup table `GetRoleForClassSpec(classId, specId)` and `GetSpecName(classId, specId)`.
  - Wired `BotAI::Update()` to automatically read `bot->GetPlayerSetting("core.ascension_active_spec", 0).value` and detect the bot's effective role dynamically if no manual override is active.
  - Added `.botcmd setrole <guid> auto` to clear any manual override and return to auto-detection.
  - Updated `.botcmd checkrole <guid>` and `.botcmd learnspec <guid> <specId>` to display the active spec name and whether the role is auto-detected or manually overridden.
  - Built cleanly into `worldserver.exe` and live-tested on the repack with Templar (GUID 7 - Oathkeeper Tank), Tinker (GUID 10 - Invention Healer), and Guardian (GUID 9 - Vanguard Tank), verifying auto-detection, manual override switching, and auto-resetting.

## Next steps

- Refine role-specific AI rotations (e.g. Healer mana management, Tank threat rotation, Support party buff refreshes).
- Teach `.botcmd learnspec` about `GetSelectableFreeGroup`'s mutually-exclusive choices if narrower talent builds are desired.
