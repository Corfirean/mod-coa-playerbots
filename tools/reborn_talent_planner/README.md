# Reborn talent planner

Plans the 51-point talent build of every stock class and spec for Warcraft Reborn bots, and checks each build against a
port of the core's free-pick rules (`rsim.py`, from `AscensionFreepickRules.cpp`) for every level from 10 to 60. It then
writes `module/src/RebornTalentBuilds.inc`, which `BotTalentBuilds::ApplyRebornBuild` reads.

    python plan.py            # plan, verify, rewrite the .inc
    python plan.py --check    # plan and verify only

It needs the client DBCs of a Reborn server (`CharacterAdvancement*.dbc`, `SpellRank.dbc`); set `REBORN_DBC` to their
folder if they are not in `C:\games\CoA Server 2\Data\dbc`. Run it again when the catalog or the combat profiles change.
`SPEC_TAB` in `plan.py` maps each internal spec number to its Reborn tab and must match `ClassSpecRoles.cpp`.

How a build is chosen: the talents the spec's combat profile uses (Mortal Strike, Bloodthirst, Shadowform, ...) and the
deepest talent of the tab are targets. Each step takes a target, or the talent it still needs, as soon as the core would
accept it, and otherwise the lowest-tier talent of the tab (talents of little use, such as weapon specialisations of
other weapons, last).
