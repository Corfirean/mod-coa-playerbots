"""Offline checks for the Warcraft Reborn bot data and profiles. One command, no server:

    python validate.py            # exit code 0 = everything holds

What it proves (against the client DBCs of a Reborn server, REBORN_DBC, and the core's base SQL, CORE_BASE_SQL):
  * every combat profile ability exists, is named what the profile says, is something the class can get by level 60 (the
    free-pick catalog or the stock trainer) and is castable by the engine (no item-target spells such as poisons or imbues,
    which BotAI's own upkeep applies);
  * the 30 generated talent builds are valid step by step for every level 10-60 under a port of the core's rules, reach the
    talents the profiles use, and the committed .inc files are exactly what the generators produce now (not stale);
  * the extra trainer abilities and the hunter pet talents likewise;
  * ClassSpecRoles, the tests' spec table and the stat models agree for every stock spec;
  * the spell ids BotAI.cpp uses for totems, poisons, imbues and pets are the spells they are meant to be.
Run it after any change to profiles, the catalog data or the tables.
"""
import collections, glob, os, re, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, "..", "..", "module", "src"))
TESTS = os.path.normpath(os.path.join(HERE, "..", "..", "module", "tests"))
SQL = os.environ.get("CORE_BASE_SQL", r"C:\games\coa-core-fork\data\sql\base\db_world")
OFF = 1100000
CLS = {1: "Warrior", 2: "Paladin", 3: "Hunter", 4: "Rogue", 5: "Priest", 7: "Shaman", 8: "Mage", 9: "Warlock", 11: "Druid"}

errors, warnings = [], []


def fail(msg):
    errors.append(msg)


def warn(msg):
    warnings.append(msg)


def section(title):
    print("\n== " + title)


def run_generator(script, *args):
    r = subprocess.run([sys.executable, os.path.join(HERE, script), *args], capture_output=True, text=True, encoding="utf-8",
                       env=dict(os.environ, PYTHONIOENCODING="utf-8"))
    if r.returncode:
        fail(f"{script} failed: {r.stderr.strip()[-300:]}")
    return r.stdout


def normalised(path):
    return open(path, encoding="utf-8").read().replace("\r\n", "\n")


def check_generated_tables():
    section("generated tables are current")
    with tempfile.TemporaryDirectory() as tmp:
        for script, target, label in (("plan.py", "RebornTalentBuilds.inc", "talent builds"),
                                      ("extra_spells.py", "RebornExtraSpells.inc", "extra trainer abilities"),
                                      ("pet_talents.py", "RebornPetTalents.inc", "pet talents")):
            out = os.path.join(tmp, target)
            stdout = run_generator(script, "--out", out)
            if script == "plan.py":
                for line in stdout.splitlines():
                    if "verify problems:" in line and not line.rstrip().endswith("verify problems: 0"):
                        fail("talent build does not verify: " + line.strip())
                    if "missed [" in line and "missed []" not in line:
                        fail("talent build misses a target: " + line.strip())
                print(f"  {sum('verify problems: 0' in l for l in stdout.splitlines())} talent builds verified at every level 10-60")
            if not os.path.exists(out):
                fail(f"{script} wrote nothing")
                continue
            if normalised(out) != normalised(os.path.join(SRC, target)):
                fail(f"{target} is stale: run `python {script}` and commit the result ({label})")
            else:
                print(f"  {target}: up to date")


def spell_world():
    import spelldb, rsim
    db = spelldb.SpellDb()
    types, rows, budget = rsim.load()
    chain = collections.defaultdict(set)
    data = open(os.path.join(rsim.D, "SpellRank.dbc"), "rb").read()
    n, f, sz, ss = struct.unpack_from("<4I", data, 4)
    for i in range(n):
        r = struct.unpack_from("<%dI" % (sz // 4), data, 20 + i * sz)
        if r[3]:
            chain[r[1]].add(r[2])
    first_of = {s: fst for fst, v in chain.items() for s in v}
    trainer = {}
    for m in re.finditer(r"\((\d+),(\d+),(\d+),'", open(os.path.join(SQL, "trainer.sql"), encoding="utf-8").read()):
        trainer[int(m.group(1))] = (int(m.group(2)), int(m.group(3)))
    taught = collections.defaultdict(set)
    for m in re.finditer(r"\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(-?\d+)\)",
                         open(os.path.join(SQL, "trainer_spell.sql"), encoding="utf-8").read()):
        t = int(m.group(1))
        if t in trainer and trainer[t][0] == 0:
            taught[trainer[t][1]].add(int(m.group(2)))
    # Abilities ApplyRebornBotSetup hands out by itself (the tank lists), whatever the trainer table says.
    setup = open(os.path.join(SRC, "BotSpawnRandom.cpp"), encoding="utf-8").read()
    start = setup.index("void ApplyRebornBotSetup(Player* bot, uint8 level, uint32 forcedSpec)")
    for sid, _ in re.findall(r"\{\s*(\d+),\s*(\d+)\s*\}", setup[start:setup.index("// 3. Remove out-of-level", start)]):
        for cls in CLS:
            taught[cls].add(int(sid))
    catalog = collections.defaultdict(list)
    for r in rows.values():
        for s in r["spells"]:
            if s:
                catalog[s].append(r)
    return db, types, rows, budget, chain, first_of, taught, catalog, rsim


def parse_profiles():
    profiles = []
    for path in glob.glob(os.path.join(SRC, "profiles", "Profile*.cpp")):
        text = open(path, encoding="utf-8").read()
        parts = re.split(r"CombatProfile p;", text)
        for i, block in enumerate(parts[1:], 1):
            cls = re.search(r"p\.classId = (\d+)", block)
            if not cls or int(cls.group(1)) not in CLS:
                continue
            specs = re.search(r"p\.specId = (\w+)", block).group(1)
            specs = [int(specs)] if specs.isdigit() else [int(x) for x in re.findall(r"\d+", re.findall(r"specId : \{([^}]*)\}", parts[i - 1])[-1])]
            abilities = []
            # keep the enclosing `if (specId == N)` guards: a name listed twice is fine when the copies belong to different specs
            for m in re.finditer(r'd\.name = "([^"]+)";\s*d\.rootSpellId = (\d+);', block):
                head = block[:m.start()]
                guard = re.findall(r"if \(specId == (\d+)u\)", head[-400:])
                abilities.append((m.group(1), int(m.group(2)), bool(guard)))
            profiles.append((int(cls.group(1)), specs, os.path.basename(path), abilities, block))
    return profiles


def check_profiles(world):
    section("combat profiles")
    db, types, rows, budget, chain, first_of, taught, catalog, rsim = world
    ITEM_TARGET = 0x10
    total = 0
    for cls, specs, fname, abilities, block in parse_profiles():
        sim = rsim.Sim(types, rows, budget, cls, 60)
        seen = collections.Counter()
        for name, root, guarded in abilities:
            total += 1
            label = f"{CLS[cls]} spec {specs} '{name}' ({root})"
            stock_name = db.name(root)
            twin_name = db.name(root + OFF)
            actual = stock_name or twin_name
            if not actual:
                fail(f"{label}: no such spell in Spell.dbc")
                continue
            key = name.split(" (")[0].replace("'", "").lower()[:5]
            if key not in actual.replace("'", "").lower() and actual.lower()[:5] not in name.lower():
                fail(f"{label}: the id is '{actual}'")
            sid = root if stock_name else root + OFF
            if (db.u(sid, 16) or 0) & ITEM_TARGET:
                fail(f"{label}: an item-target spell (poison/imbue) cannot be cast by a profile entry; BotAI's weapon upkeep does it")
            ids = chain.get(first_of.get(root, root), {root}) | {root}
            ids |= {x + OFF for x in ids}
            rows_hit = [r for x in ids for r in catalog.get(x, []) if sim.admits(r)]
            if not rows_hit and not (ids & taught[cls]) and root < 1000000:
                fail(f"{label}: neither the Reborn catalog nor the class trainer teaches it")
            elif rows_hit and min(r["level"] for r in rows_hit) > 60:
                fail(f"{label}: needs level {min(r['level'] for r in rows_hit)}")
            seen[(name, root)] += 1
            if seen[(name, root)] > 1 and not guarded:
                # the same spell as an interrupt and as a survival stun is deliberate (two descriptors); anything else is a slip
                if not re.search(r'name = "' + re.escape(name) + r'";[\s\S]{0,300}?Interrupt|name = "' + re.escape(name) + r'";[\s\S]{0,300}?CrowdControl', block):
                    warn(f"{label}: listed twice in {fname}")
        if len(abilities) < 4:
            warn(f"{CLS[cls]} spec {specs}: only {len(abilities)} abilities")
    print(f"  {total} profile abilities checked against Spell.dbc, the catalog and the trainers")


def stock_role_rows(path, pattern):
    text = open(path, encoding="utf-8").read()
    return {(int(m.group(1)), int(m.group(2))): m.group(3) for m in re.finditer(pattern, text)}


def check_spec_tables():
    section("spec tables agree")
    roles = stock_role_rows(os.path.join(SRC, "ClassSpecRoles.cpp"), r"\{\s*(\d+),\s*(\d+),\s*BotRole::(\w+),")
    stock = {k: v for k, v in roles.items() if k[0] in CLS}
    kind = {"Dps": "Damage", "Support": "Damage", "Tank": "Tank", "Healer": "Healer"}
    test = stock_role_rows(os.path.join(TESTS, "reborn_data_tests.cpp"), r"\{\s*(\d+),\s*(\d+),\s*Kind::(\w+)\s*\}")
    if {k: kind[v] for k, v in stock.items()} != test:
        fail("the spec table in tests/reborn_data_tests.cpp differs from ClassSpecRoles.cpp: " +
             str(sorted(set({k: kind[v] for k, v in stock.items()}.items()) ^ set(test.items()))[:6]))
    else:
        print(f"  {len(stock)} stock specs: ClassSpecRoles.cpp and the test table agree")
    stats = set((int(a), int(b)) for a, b in re.findall(r"\{\s*(\d+),\s*(\d+),\s*\d+,\s*\d+,\s*(?:true|false),\s*(?:true|false)\s*\}", open(os.path.join(SRC, "BotSpecStats.h"), encoding="utf-8").read()))
    missing = [k for k in stock if k not in stats]
    if missing:
        fail(f"no gear stat model for stock specs {missing}")
    else:
        print("  every stock spec has a gear stat model")
    import plan
    for cls, specs in plan.SPEC_TAB.items():
        for spec in specs:
            if (cls, spec) not in stock:
                fail(f"planner spec ({cls},{spec}) is not in ClassSpecRoles.cpp")
    for key in stock:
        if key[1] not in plan.SPEC_TAB.get(key[0], {}):
            fail(f"ClassSpecRoles spec {key} has no planner tab")


def check_runtime_ids(world):
    section("spell ids the upkeep code uses")
    db = world[0]
    expected = {
        688: "Summon Imp", 697: "Summon Voidwalker", 883: "Call Pet", 982: "Revive Pet", 1515: "Tame Beast",
        8017: "Rockbiter Weapon", 8024: "Flametongue Weapon", 8232: "Windfury Weapon", 51730: "Earthliving Weapon",
        2060001: "Instant Poison", 2060011: "Deadly Poison", 2060031: "Crippling Poison",
        8075: "Strength of Earth Totem", 8071: "Stoneskin Totem", 8512: "Windfury Totem", 3738: "Wrath of Air Totem",
        8227: "Flametongue Totem", 3599: "Searing Totem", 5675: "Mana Spring Totem", 5394: "Healing Stream Totem",
        25771: "Forbearance", 57724: "Sated", 57723: "Exhaustion",
        1126: "Mark of the Wild", 21849: "Gift of the Wild", 1243: "Power Word: Fortitude", 21562: "Prayer of Fortitude",
        14752: "Divine Spirit", 27681: "Prayer of Spirit", 1459: "Arcane Intellect", 23028: "Arcane Brilliance",
        19740: "Blessing of Might", 19742: "Blessing of Wisdom", 20217: "Blessing of Kings", 20911: "Blessing of Sanctuary",
        25782: "Greater Blessing of Might", 25894: "Greater Blessing of Wisdom", 25898: "Greater Blessing of Kings",
    }
    bad = 0
    for sid, name in expected.items():
        actual = db.name(sid)
        if actual != name:
            fail(f"spell {sid} should be {name}, is {actual}")
            bad += 1
    print(f"  {len(expected) - bad}/{len(expected)} ids are the spells they are meant to be")
    # the poisons rank by consecutive ids from their first rank
    for first, label in ((2060001, "Instant"), (2060011, "Deadly")):
        ranks = [db.s(first + k, 153) for k in range(9)]
        if ranks != [f"Rank {k + 1}" for k in range(9)]:
            fail(f"{label} Poison ranks are not consecutive from {first}: {ranks}")
    # the stock summons need no reagent (the Reborn twins do), which is why the pets cast the stock ids
    for sid in (697, 712, 691):
        if any(db.u(sid, 52 + i) for i in range(8)):
            fail(f"stock summon {sid} wants a reagent")
    for sid in (1100697,):
        if not any(db.u(sid, 52 + i) for i in range(8)):
            warn(f"the Reborn Voidwalker {sid} no longer needs a reagent: the stock-first note in BotAI can go")


def main():
    sys.path.insert(0, HERE)
    world = spell_world()
    check_generated_tables()
    check_profiles(world)
    check_spec_tables()
    check_runtime_ids(world)
    section("result")
    for w in warnings:
        print("  warning:", w)
    for e in errors:
        print("  FAIL:", e)
    print(f"  {len(errors)} failure(s), {len(warnings)} warning(s)")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
