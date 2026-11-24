"""Smoke check of a running Warcraft Reborn server's bots through RA and the character database. Read-only.

    python check.py --root "C:\\games\\CoA Server 2" [--per-class 4] [--min-level 20] [--json report.json]

Exit code 0 when every expectation holds, 1 when any fails (the list is printed and written to the JSON report).

It needs this module built (it asks each sampled bot for `.botcmd abilities <guid>`). The RA login is the Manager's own, read from
<root>/Settings/repack.json; the character list comes from a SELECT with the login in Core/configs/worldserver.conf. Neither is printed.

What is expected of a sampled bot (level L, spec S), and why:
  talents      at least 80% of min(51, L - 9) free-pick ranks picked         (Reborn: 1 point at 10, +1 per level)
  abilities    at least half of the combat profile's abilities known          (the rest are talents not picked / higher level)
  gear         at least 12 of 17 slots filled and an average item level of L/2 or more, from level 20
  hunter       a pet, no unspent pet talent points, and ammunition in the bags (the core refuses ranged shots without it)
  warlock      a demon (except the Dark Apotheosis tank)
  rogue/shaman a weapon poison / imbue on the main hand from level 20 / 10
and for the whole population: tanks and healers between 10% and 30%, and every stock spec used when its class has 9 or more bots.
"""
import argparse, collections, json, os, re, socket, subprocess, sys

CLASSES = {1: "Warrior", 2: "Paladin", 3: "Hunter", 4: "Rogue", 5: "Priest", 7: "Shaman", 8: "Mage", 9: "Warlock", 11: "Druid"}
HERE = os.path.dirname(os.path.abspath(__file__))
ROLES = os.path.normpath(os.path.join(HERE, "..", "..", "module", "src", "ClassSpecRoles.cpp"))


def ra_session(root):
    cfg = json.load(open(os.path.join(root, "Settings", "repack.json"), encoding="utf-8-sig"))
    s = socket.create_connection(("127.0.0.1", int(cfg.get("raPort", 3443))), timeout=5)

    def read_until(marker):
        s.settimeout(60)
        data = b""
        while marker not in data:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
        return data.decode("utf-8", "replace")

    read_until(b"Username: ")
    s.sendall((cfg["raUsername"] + "\r\n").encode())
    read_until(b"Password: ")
    s.sendall((cfg["raPassword"] + "\r\n").encode())
    if "authentication failed" in read_until(b"AC>").lower():
        sys.exit("RA login rejected")

    def run(cmd):
        s.sendall((cmd + "\r\n").encode())
        return read_until(b"AC>").rsplit("AC>", 1)[0].strip()

    return run


def sql(root, query):
    conf = open(os.path.join(root, "Core", "configs", "worldserver.conf"), encoding="utf-8-sig").read()
    host, port, user, pw, db = re.search(r'^CharacterDatabaseInfo\s*=\s*"([^"]+)"', conf, re.M).group(1).split(";")
    exe = os.path.join(root, "mysql", "bin", "mysql.exe")
    r = subprocess.run([exe, "-h", host, "-P", port, "-u", user, "-D", db, "--batch", "-e", query],
                       env=dict(os.environ, MYSQL_PWD=pw), capture_output=True, text=True, encoding="utf-8")
    if r.returncode:
        sys.exit(r.stderr.replace(pw, "***"))
    return [line.split("\t") for line in r.stdout.splitlines()[1:]]


def stock_roles():
    roles = {}
    for m in re.finditer(r"\{\s*(\d+),\s*(\d+),\s*BotRole::(\w+),", open(ROLES, encoding="utf-8").read()):
        if int(m.group(1)) in CLASSES:
            roles[(int(m.group(1)), int(m.group(2)))] = m.group(3)
    return roles


def parse(out):
    def grab(pattern, cast=int, default=None):
        m = re.search(pattern, out)
        return cast(m.group(1)) if m else default
    pet = re.search(r"pet: ([^.(]+?)(?: \(level (\d+), (\d+) talent points unspent\))?\.", out)
    gear = re.search(r"gear: (\d+)/17 slots filled, average item level (\d+)(.*?)\.\s*$", out, re.M)
    ammo = re.search(r"ammo (\d+) x(\d+)", out)
    imbues = dict(re.findall(r"(main|off) imbue (\w+)", out))
    return {
        "spec": grab(r"spec (\d+), role"), "role": grab(r"role (\w+)", str),
        "resolved": grab(r"=> (\d+)/\d+ profile abilities resolved"), "total": grab(r"=> \d+/(\d+) profile abilities resolved"),
        "talents": grab(r"talent ranks picked: (\d+)"),
        "pet": None if not pet or pet.group(1).strip() == "none" else pet.group(1).strip(),
        "pet_unspent": int(pet.group(3)) if pet and pet.group(3) else 0,
        "slots": int(gear.group(1)) if gear else None, "ilvl": int(gear.group(2)) if gear else None,
        "ammo": int(ammo.group(2)) if ammo else None,
        "imbue_main": imbues.get("main"), "ghost": "Ghost" in out, "no_profile": "NO combat profile" in out,
    }


def judge(cls, level, info):
    problems = []
    spec = info["spec"]
    if level >= 10 and info["talents"] is not None:
        expected = min(51, level - 9)
        if info["talents"] < 0.8 * expected:
            problems.append(f"{info['talents']} talent ranks, expected about {expected}")
    if info["no_profile"]:
        problems.append("no combat profile (spec 0 or unknown)")
    elif info["total"] and info["resolved"] is not None and info["resolved"] < 0.5 * info["total"]:
        problems.append(f"only {info['resolved']}/{info['total']} profile abilities known")
    if level >= 20 and info["slots"] is not None:
        if info["slots"] < 12:
            problems.append(f"{info['slots']}/17 gear slots filled")
        if info["ilvl"] is not None and info["ilvl"] < level / 2:
            problems.append(f"average item level {info['ilvl']}")
    if not info["ghost"]:
        if cls == 3 and level >= 10:
            if not info["pet"]:
                problems.append("hunter without a pet")
            elif info["pet_unspent"]:
                problems.append(f"pet has {info['pet_unspent']} unspent talent points")
            if not info["ammo"]:
                problems.append("hunter without ammunition")
        if cls == 9 and level >= 10 and spec != 4 and not info["pet"]:
            problems.append("warlock without a demon")
        if cls == 4 and level >= 20 and info["imbue_main"] in (None, "none"):
            problems.append("rogue without a poison on the main hand")
        if cls == 7 and level >= 10 and info["imbue_main"] in (None, "none"):
            problems.append("shaman without a weapon imbue on the main hand")
    return problems


def population_checks(root, roles):
    rows = sql(root, "SELECT c.class, s.data FROM characters c JOIN character_settings s ON s.guid = c.guid AND s.source = 'core.ascension_active_spec' WHERE c.online = 1")
    problems, kinds, per_class = [], collections.Counter(), collections.Counter()
    spec_use = collections.Counter()
    total = 0
    for cls, spec in rows:
        cls, spec = int(cls), int(spec.strip() or 0)
        if cls not in CLASSES:
            continue
        total += 1
        per_class[cls] += 1
        spec_use[(cls, spec)] += 1
        kinds[roles.get((cls, spec), "none")] += 1
    if total:
        for role, label in (("Tank", "tanks"), ("Healer", "healers")):
            pct = 100.0 * kinds[role] / total
            if not 10 <= pct <= 30:
                problems.append(f"{label} are {pct:.1f}% of {total} stock-class bots (expected 10-30%)")
        for (cls, spec) in sorted(roles):
            if per_class[cls] >= 9 and not spec_use[(cls, spec)]:
                problems.append(f"{CLASSES[cls]} spec {spec} is used by none of {per_class[cls]} bots")
    return problems, {"bots": total, "roles": dict(kinds), "specs": {f"{c}:{s}": n for (c, s), n in sorted(spec_use.items())}}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=r"C:\games\CoA Server 2")
    ap.add_argument("--per-class", type=int, default=4)
    ap.add_argument("--min-level", type=int, default=20)
    ap.add_argument("--json", default="reborn_check_report.json")
    args = ap.parse_args()

    bots = sql(args.root, f"SELECT guid, class, level, name FROM characters WHERE online = 1 AND level >= {args.min_level} ORDER BY class, level DESC, guid")
    chosen = collections.defaultdict(list)
    for guid, cls, level, name in bots:
        if int(cls) in CLASSES and len(chosen[int(cls)]) < args.per_class:
            chosen[int(cls)].append((guid, int(level), name))

    run = ra_session(args.root)
    report, failures = {"bots": [], "population": {}}, []
    for cls, group in sorted(chosen.items()):
        print(f"\n== {CLASSES[cls]}")
        missing = collections.Counter()
        for guid, level, name in group:
            out = run(f".botcmd abilities {guid}")
            info = parse(out)
            for m in re.finditer(r"\[NOT KNOWN\] (.+?) root", out):
                missing[m.group(1)] += 1
            problems = judge(cls, level, info)
            status = "ok " if not problems else "BAD"
            print(f"  {status} {name:22} lvl {level:2} spec {info['spec']} {info['role']:6} abilities {info['resolved']}/{info['total']}  "
                  f"talents {info['talents']}  pet {info['pet'] or '-'}  gear {info['slots']}/17 ilvl {info['ilvl']}"
                  + (f"  ammo {info['ammo']}" if info["ammo"] is not None else "") + (f"  imbue {info['imbue_main']}" if info["imbue_main"] else ""))
            for p in problems:
                print("        -", p)
                failures.append(f"{CLASSES[cls]} {name} (lvl {level}): {p}")
            report["bots"].append({"guid": guid, "name": name, "class": cls, "level": level, **info, "problems": problems})
        if missing:
            print("  most often not known:", ", ".join(f"{n} x{c}" for n, c in missing.most_common(6)))

    population, summary = population_checks(args.root, stock_roles())
    report["population"] = summary
    print("\n== population:", summary["bots"], "stock-class bots; roles", summary["roles"])
    for p in population:
        print("   -", p)
        failures.append("population: " + p)

    report["failures"] = failures
    with open(args.json, "w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=1, ensure_ascii=False)
    print(f"\n{len(failures)} failure(s); report in {args.json}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
