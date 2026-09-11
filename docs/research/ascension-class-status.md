# Ascension class completeness — what's actually playable

Recorded 2026-09-11. This determines priority order for writing bot rotation
AI: write it for classes that actually work first.

## The 21 custom classes (IDs 12-32)

Names pulled directly from the Ascension client's own `ChrClasses.dbc`
(`CoA-Repack/Data/dbc/ChrClasses.dbc`, format `ChrClassesEntryfmt` from
`DBCfmt.h`), not guessed:

| ID | Class | ID | Class | ID | Class |
|---|---|---|---|---|---|
| 12 | Barbarian | 19 | Templar | 26 | Starcaller |
| 13 | Witch Doctor | 20 | Bloodmage | 27 | Sun Cleric |
| 14 | Felsworn | 21 | Ranger | 28 | Tinker |
| 15 | Witch Hunter | 22 | Chronomancer | 29 | Venomancer |
| 16 | Stormbringer | 23 | Necromancer | 30 | Reaper |
| 17 | Knight of Xoroth | 24 | Pyromancer | 31 | Primalist |
| 18 | Guardian | 25 | Cultist | 32 | Runemaster |

(10 = "Hero", a reserved classless-creator placeholder — see
`AscensionCompat.MapClass10ToWarrior` in `mod_ascension_compat.conf`; not a
real playable class.)

`playercreateinfo` in the live `acore_world` DB has starting rows for **all
21** class IDs — that only means "a character of this class can technically
be created," not "the class works."

## ⚠️ Do not use "has a dedicated `AscensionX.cpp` + completion doc" as a
## completeness signal — it is wrong, confirmed by direct testing

First pass at this (source-file inventory only) concluded 14 classes were
"complete," 4 "partial," 3 "zero implementation." **The 3-classes-at-zero
conclusion was wrong**, caught only because the user had actually created and
played characters on two of them.

Why the file-based method fails: a large amount of class mechanics is
implemented through **generic, `ClassId`-tagged data tables**
(`AscensionCustomResourceData.h` — resource bars, aura-stack mutation rules,
consumption rules), not per-class C++ files. A class can be substantially
functional with **zero dedicated source files** if its abilities are (a)
driven entirely by these generic tables, or (b) built on spells/mechanics the
base game engine already implements (e.g. a class thematically close to an
existing Mage/Hunter spell kit reusing those spell IDs directly, no new C++
needed at all).

Concretely, grepping `AscensionCustomResourceData.h` for class-tagged rows
(`grep '{ *16,\|{ *20,\|{ *22,'`) — the three classes the file-search said
had "zero implementation" — found:

- **Stormbringer (16)**: 30+ data rows (resource "Static", aura-stack rules,
  consumption rules across many spell ids). Substantially data-driven, not
  "0% done."
- **Bloodmage (20)**: exactly 2 resource entries — `"Bloodmage resource"` and
  `"Blood thirst"`. Matches the user's direct testing: *"2 спека вроде
  работают"* (2 specs seem to work) — very likely one resource per working
  spec.
- **Chronomancer (22)**: exactly 1 entry (`"Echo Fragment"`) — genuinely the
  thinnest of the three, consistent with being the least-done.

## What's confirmed by actually playing (the only trustworthy signal)

User-created and tested, live characters exist in `acore_characters.characters`:

| guid | name | class | level | status |
|---|---|---|---|---|
| 1 | Test | 29 (Venomancer) | 80 | works |
| 2 | Shaniel | 21 (Ranger) | 80 | "выглядит вполне рабочим классом" |
| 3 | Mesha | 20 (Bloodmage) | 80 | 2 specs work, not fully checked |

**Also confirmed: character creation for custom classes (12-32) works
through the normal client flow** — the `MapClass10ToWarrior` config note
describes a narrower fallback case, not a universal block. (This contradicts
an earlier inference drawn from reading that config comment in isolation —
another instance of "don't infer behavior from a comment, test it.")

## Source-file inventory (still useful as a secondary signal, not primary)

14 classes have a dedicated `Ascension<Name>.cpp` base file **and** a
completion doc under `modules/mod-ascension-compat/docs/`: Barbarian, Witch
Doctor, Felsworn, Witch Hunter, Knight of Xoroth, Guardian, Templar,
Necromancer, Pyromancer, Cultist, Starcaller, Sun Cleric, Tinker, Venomancer.

4 classes have only support-system files (scaling/damage helpers, no base
class file, no completion doc): Ranger, Reaper, Primalist, Runemaster. Ranger
is confirmed playable by direct testing despite this — the completion-doc
pattern tracks "this class had a dedicated reconstruction project," not
"this class works."

3 classes have zero `AscensionX*.cpp` files at all: Stormbringer, Bloodmage,
Chronomancer. Per the resource-data findings above, at least Stormbringer and
Bloodmage are meaningfully implemented anyway; Chronomancer is the one
genuinely thin case.

## How to actually assess a class before writing bot rotation AI for it

Do not repeat the file-inventory mistake. Per class:

1. Check `AscensionCustomResourceData.h` for `ClassId`-tagged rows (resource
   definitions, aura-stack rules, consumption rules) — gives a rough
   "how much data-driven mechanic exists" signal.
2. Check for a dedicated `Ascension<Name>*.cpp` file set + completion doc —
   gives a "was this specifically reconstructed/audited" signal, useful but
   not sufficient on its own (see Ranger).
3. **Create a level 80 test character and actually play it** — the only
   signal that has been right so far. Do this before committing real time to
   writing rotation AI for a given class.

## Open — not yet checked

Full grep of `AscensionCustomResourceData.h` for every one of the 21 class
IDs (only 16/20/22 were checked, because those were the file-inventory
"zero" cases). Doing this for all 21 would give a genuine data-driven
completeness ranking to prioritize rotation-writing order — worth doing
before starting bot AI work in earnest.
