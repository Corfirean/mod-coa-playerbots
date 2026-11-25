"""Plan Reborn talent builds for the bots (one per class/spec) and verify them with a port of the core's rules."""
import os, re, glob, struct, collections, json, sys
import rsim

OFF = 1100000
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "module", "src")
PROFILES = os.path.join(SRC, "profiles") + os.sep
types, rows, budget = rsim.load()

CLS = {1: "Warrior", 2: "Paladin", 3: "Hunter", 4: "Rogue", 5: "Priest", 7: "Shaman", 8: "Mage", 9: "Warlock", 11: "Druid"}
# internal spec number -> Reborn tab (see ClassSpecRoles.cpp); tab ids from CharacterAdvancementTabTypes.dbc
SPEC_TAB = {
    1: {1: 6, 2: 5, 3: 7}, 2: {1: 14, 2: 7, 3: 25}, 3: {1: 4, 2: 3, 3: 2}, 4: {1: 10, 2: 8, 3: 9},
    5: {1: 16, 2: 14, 3: 15}, 7: {1: 23, 2: 24, 3: 20, 4: 24}, 8: {1: 11, 2: 13, 3: 12},
    9: {1: 17, 2: 18, 3: 19, 4: 18}, 11: {1: 22, 2: 21, 3: 20, 4: 21},
}

# stock rank chains (client SpellRank.dbc) to map profile spell ids to catalog rows
b = open(os.path.join(rsim.D, "SpellRank.dbc"), "rb").read()
n, f, sz, ss = struct.unpack_from("<4I", b, 4)
chain = collections.defaultdict(set)
for i in range(n):
    r = struct.unpack_from("<%dI" % (sz // 4), b, 20 + i * sz)
    if r[3]:
        chain[r[1]].add(r[2])
first_of = {s: fst for fst, v in chain.items() for s in v}


def ids_of(root):
    base = chain.get(first_of.get(root, root), {root}) | {root}
    return base | {x + OFF for x in base}


profiles = {}
for fn in glob.glob(PROFILES + "Profile*.cpp"):
    t = open(fn, encoding="utf-8").read()
    parts = re.split(r"CombatProfile p;", t)
    for i, blk in enumerate(parts[1:], 1):
        c = re.search(r"p\.classId = (\d+)", blk)
        sp = re.search(r"p\.specId = (\w+)", blk)
        if not (c and sp) or int(c.group(1)) not in CLS:
            continue
        specs = [int(sp.group(1))] if sp.group(1).isdigit() else [int(x) for x in re.findall(r"\d+", re.findall(r"specId : \{([^}]*)\}", parts[i - 1])[-1])]
        ab = [int(x) for x in re.findall(r"d\.rootSpellId = (\d+);", blk)]
        for s in specs:
            profiles[(int(c.group(1)), s)] = ab

spell_rows = collections.defaultdict(list)
for r in rows.values():
    for sp in r["spells"]:
        if sp:
            spell_rows[sp].append(r)


def usable(sim, r):
    return (sim.admits(r) and not (r["flags"] & (rsim.ROW_DISPLAY | rsim.ROW_DISABLED | rsim.ROW_DEPRECATED | rsim.ROW_MODE))
            and r["expansion"] <= 0 and r["realms"][0] and r["level"] <= 60 and r["type"] in ("Talent", "TalentAbility"))


def tab_rows(sim, tab):
    return [r for r in rows.values() if r["tab"] == tab and usable(sim, r)]


def ancestors(sim, tab_set, target):
    """Rows to learn before `target` (one connected chain per row, shortest by tier)."""
    chain_ids = []
    seen = set()
    cur = target
    while cur and any(cur["conn"]):
        cands = [rows[c] for c in cur["conn"] if c and c in tab_set and c not in seen]
        if not cands:
            break
        cands.sort(key=lambda r: (r["tte"], r["id"]))
        cur = cands[0]
        seen.add(cur["id"])
        chain_ids.append(cur["id"])
    return chain_ids[::-1]


def plan(cls, spec):
    tab = SPEC_TAB[cls][spec]
    sim = rsim.Sim(types, rows, budget, cls, 10)
    trows = tab_rows(sim, tab)
    tset = {r["id"] for r in trows}
    # targets: talents the profile uses + the deepest talent of the tab
    targets = []
    for root in profiles.get((cls, spec), []):
        for sid in ids_of(root):
            for r in spell_rows.get(sid, []):
                if r["id"] in tset and r["id"] not in targets:
                    targets.append(r["id"])
    deepest = sorted((r for r in trows if r["te"]), key=lambda r: (-r["tte"], -r["level"], r["id"]))
    for r in deepest[:1]:
        if r["id"] not in targets:
            targets.append(r["id"])
    # also the class special for custom tanks
    special = {(7, 4): 1182045}
    for sid in [special.get((cls, spec))]:
        for r in spell_rows.get(sid, []) if sid else []:
            if r["id"] in tset and r["id"] not in targets:
                targets.append(r["id"])
    targets.sort(key=lambda i: (rows[i]["tte"], rows[i]["level"], i))
    # chain entries per target
    need = collections.OrderedDict()
    for t in targets:
        for a in ancestors(sim, tset, rows[t]):
            need.setdefault(a, 1)
        need.setdefault(t, 0)
    steps = []
    sim = rsim.Sim(types, rows, budget, cls, 10)
    JUNK = ("Mace Specialization", "Sword Specialization", "Poleaxe Specialization", "Axe Specialization", "Dagger Specialization",
            "Fist Weapon Specialization", "Polearm", "Bow Specialization", "Gun Specialization", "Crossbow Specialization", "Thrown",
            "Reach", "Resistance", "Warding", "Improved Ghost Wolf", "Improved Spirit Tap", "Improved Hunter's Mark", "Eagle Eye",
            "Improved Mend Pet", "Pathfinding", "Endurance Training", "Improved Revive Pet", "Savage Strikes", "Improved Fade",
            "Improved Sprint", "Improved Gouge", "Improved Sap", "Improved Kick", "Improved Distract", "Dirty Tricks", "Dirty Deeds",
            "Improved Slice and Dice", "Improved Seal of the Crusader", "Improved Concussive Shot", "Improved Aspect")
    def junk(r):
        return 1 if any(j in r["name"] for j in JUNK) else 0
    order_fill = sorted(trows, key=lambda r: (junk(r), r["tte"], r["level"], r["id"]))

    def remaining(rid):
        return sim.maxrank(rows[rid]) - sim.rank(rid)

    for i in range(51):
        sim.level = 10 + i
        pick = None
        # 1. a target (or its chain) that is valid now, earliest tier first
        for rid in need:
            if remaining(rid) > 0 and sim.validate(rid) is None:
                pick = rid
                break
        # 2. filler: lowest tier valid row of the tab
        if pick is None:
            for r in order_fill:
                if remaining(r["id"]) > 0 and sim.validate(r["id"]) is None:
                    pick = r["id"]
                    break
        if pick is None:
            break
        sim.add(pick)
        steps.append((pick, 10 + i))
    return tab, targets, steps


def verify(cls, steps):
    """Run the core's FitGuide for every level against the guide prefix valid at that level."""
    problems = []
    for level in range(10, 61):
        guide_ids = [e for e, lv in steps if lv <= level]
        agg = collections.OrderedDict()
        for e in guide_ids:
            agg[e] = agg.get(e, 0) + 1
        s = rsim.fit_guide(types, rows, budget, cls, level, list(agg.items()))
        got = dict(s.entries)
        # CheckApply: ValidateAll in entry order, else Reorder (rank units sorted by mastery, required level, id)
        steps = [e for e, rk in s.entries.items() for _ in range(rk)]
        steps.sort(key=lambda e: (not (rows[e]["flags"] & rsim.ROW_MASTERY), rows[e]["level"], e))
        walk = rsim.Sim(types, rows, budget, cls, level)
        while steps:
            moved = False
            for st in list(steps):
                if walk.validate(st) is None:
                    walk.add(st); steps.remove(st); moved = True
            if not moved:
                problems.append((level, "reorder stuck at " + rows[steps[0]]["name"]))
                break
        if got != dict(agg):
            problems.append((level, {k: (agg.get(k), got.get(k)) for k in set(agg) | set(got) if agg.get(k) != got.get(k)}))
    return problems


if __name__ == "__main__":
    out = {}
    for cls in CLS:
        for spec in SPEC_TAB[cls]:
            tab, targets, steps = plan(cls, spec)
            probs = verify(cls, steps)
            got_t = [rows[t]["name"] for t in targets if any(e == t for e, _ in steps)]
            miss_t = [rows[t]["name"] for t in targets if not any(e == t for e, _ in steps)]
            print(f"{CLS[cls]:8} spec {spec} tab {tab:2}: {len(steps)} points, targets got {got_t}, missed {miss_t}, verify problems: {len(probs)}")
            if probs:
                print("    first problem:", probs[0])
            out[f"{cls}:{spec}"] = [(rows[e]["spells"][0], lv, rows[e]["name"]) for e, lv in steps]
    if any(p for p in sys.argv[1:] if p == "--check"):
        sys.exit(0)

    lines = ["// Generated from the Reborn client DBC (CharacterAdvancement*.dbc) by tools/reborn_talent_planner/plan.py; do not edit by hand.",
             "// Each build is the order in which a bot spends its talent points: {first-rank spell of the talent, level that pays for it}.",
             "// A talent with several ranks appears once per rank. The core fits the prefix valid at the bot's level",
             "// (AscensionFreepick::ApplyGuide), so a build never needs the same pick twice in a different order.",
             "static constexpr RebornPick REBORN_PICKS[] =", "{"]
    table, pos = [], 0
    for key in sorted(out, key=lambda k: tuple(map(int, k.split(":")))):
        c_, s_ = map(int, key.split(":"))
        lines.append(f"    // class {c_} spec {s_}")
        line = "    "
        for sp, lv, name in out[key]:
            item = f"{{ {sp}, {lv} }}, "
            if len(line) + len(item) > 118:
                lines.append(line.rstrip())
                line = "    "
            line += item
        lines.append(line.rstrip())
        table.append((c_, s_, pos, len(out[key])))
        pos += len(out[key])
    lines += ["};", "", "static constexpr RebornBuild REBORN_BUILDS[] =", "{"]
    lines += [f"    {{ {c_}, {s_}, {p_}, {n_} }}," for c_, s_, p_, n_ in table] + ["};"]
    target = (sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else os.path.join(SRC, "RebornTalentBuilds.inc"))
    with open(target, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")
    print("wrote", os.path.normpath(target), pos, "picks")
