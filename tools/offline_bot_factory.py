#!/usr/bin/env python3
"""
mod-coa-playerbots -- offline bot factory.

Creates and fully equips CoA bot characters directly in the character/auth
databases while the worldserver is OFFLINE. No RA console, no gradual-batch
throttling, no live async save queue to race against -- the exact things that
made bulk live-server spawning slow and crash-prone (see this module's session
notes: RASession::Send crashing on a dropped RA socket, Player::DeleteFromDB's
async transaction not draining reliably, the multi-minute character-save
backlog after a big spawnleveled batch). Character creation and gear both go
straight into the database via a single `mysql` client invocation.

Requirements: Python 3.8+ stdlib only (no pip install needed) and the `mysql`
CLI client -- every CoA-Repack ships one at Core's sibling `mysql/bin/mysql.exe`
(Windows) or plain `mysql` on PATH elsewhere. Run this with the worldserver
(and ideally authserver) stopped; MySQL itself must be running.

Usage examples (run from anywhere; point --mysql-exe/--defaults-file at your
repack if auto-detection doesn't find them):
    python offline_bot_factory.py --count 600 --leveled
    python offline_bot_factory.py --count 200 --level 80
    python offline_bot_factory.py --count 50 --level 80 --dry-run

What it does NOT do (still handled by the existing live-server code path, on
each bot's first real login -- see BotSpawnRandom.cpp's ApplyFreshBotSetup and
mod-ascension-compat's OnPlayerLogin repair hook, both unaffected by this
tool): granting spells/talents/recipes, assigning a spec, professions, mounts,
taxi nodes, or relocating to a level-appropriate zone. Those systems already
run automatically the first time a character logs in, regardless of how it
was created -- this tool only needs to solve character creation and gear,
since nothing else backfills gear for a character that never went through
ApplyFreshBotSetup.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import random
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# --------------------------------------------------------------------------
# SRP6 account registration -- verified byte-for-byte against a real account
# created by AzerothCore's own AccountMgr::CreateAccount (same salt in,
# identical verifier out). See Acore::Crypto::SRP6 in
# src/common/Cryptography/Authentication/SRP6.cpp for the source of truth;
# N/g and the v = g^H(s|H(u:p)) mod N formula, salt/verifier stored
# little-endian, are exactly what's replicated here.
# --------------------------------------------------------------------------
SRP6_N = int("894B645E89E1535BBDAD5B8B290650530801B18EBFBF5E8FAB3C82872A3E9BB7", 16)
SRP6_G = 7


def srp6_salt_verifier(username: str, password: str) -> tuple[bytes, bytes]:
    salt = os.urandom(32)
    h1 = hashlib.sha1(f"{username}:{password}".encode()).digest()
    h2 = hashlib.sha1(salt + h1).digest()
    x = int.from_bytes(h2, "little")
    v = pow(SRP6_G, x, SRP6_N)
    return salt, v.to_bytes(32, "little")


# --------------------------------------------------------------------------
# Armor/weapon proficiency per Ascension custom class (classId 12-32).
# Derived empirically from a real population this same session already had
# the live engine gear correctly (CanEquipNewItem-verified, not guessed) --
# see the module's session notes for the extraction queries. Armor subclass
# numbers: 1=cloth, 2=leather, 3=mail, 4=plate (class=4 item_template rows).
# Neck/ring/trinket/cloak are proficiency-agnostic in WotLK (any class can
# wear them) so they're not in this table at all -- handled uniformly below.
# Weapon entries are {inventoryType: [subclass,...]}; inventoryType 13=1H,
# 17=2H, 21=mainhand-only, 22=offhand-only, 14=shield, 15/25/26=ranged.
# --------------------------------------------------------------------------
ARMOR_SUBCLASSES: dict[int, list[int]] = {
    12: [1, 2], 13: [1, 2, 3], 14: [1, 2], 15: [1, 2, 3], 16: [1],
    17: [1, 2, 3, 4], 18: [1, 2, 3, 4], 19: [1, 2, 3], 20: [1, 2],
    21: [1, 2], 22: [1], 23: [1], 24: [1], 25: [1, 2, 3, 4],
    26: [1, 2, 3, 4], 27: [1, 2, 3, 4], 28: [1, 2, 3], 29: [1, 2, 3],
    30: [1, 2, 3, 4], 31: [1, 2, 3, 4], 32: [1, 2],
}

WEAPON_TYPES: dict[int, dict[int, list[int]]] = {
    12: {13: [4, 7, 13, 15], 17: [1, 5, 8], 21: [7], 25: [16]},
    13: {13: [4, 13, 15], 15: [2], 17: [6, 10], 21: [0], 26: [18, 19]},
    14: {13: [0, 7, 15], 15: [2], 17: [1, 5, 6, 8, 10], 21: [0, 7], 22: [13, 15], 26: [19]},
    15: {13: [0, 7, 15], 17: [1, 5, 8], 21: [0, 7], 22: [15], 26: [3, 18]},
    16: {13: [4, 15], 17: [10], 21: [15], 26: [19]},
    17: {13: [0, 4, 7], 15: [2], 17: [1, 5, 6, 8], 21: [0, 4, 7], 26: [18]},
    18: {13: [0, 4, 7, 13], 15: [2], 21: [0, 4, 7], 25: [16], 26: [3, 18, 19]},
    19: {13: [4], 15: [2], 17: [1, 5, 8], 21: [7], 26: [18]},
    20: {13: [7, 15], 15: [2], 17: [1, 5, 6, 10, 20], 21: [7], 26: [18, 19]},
    21: {13: [0, 7, 13, 15], 15: [2], 17: [6], 21: [15], 22: [13], 26: [18]},
    22: {13: [15], 17: [10, 20], 21: [15], 26: [19]},
    23: {13: [7], 17: [10], 21: [7, 15], 26: [19]},
    24: {13: [7], 17: [10], 21: [7, 15], 26: [19]},
    25: {13: [0, 7, 13], 15: [2], 17: [5, 8, 10], 21: [0], 26: [3, 18, 19]},
    26: {13: [7, 15], 15: [2], 17: [10, 20], 21: [0, 4], 26: [18, 19]},
    27: {13: [0, 4, 7], 15: [2], 17: [5, 6, 8, 10, 20], 21: [0, 4], 26: [18, 19]},
    28: {13: [0, 4], 17: [1, 5, 6, 10, 20], 21: [0, 4], 26: [3]},
    29: {13: [4, 15], 15: [2], 17: [5, 6, 10], 21: [4, 15], 22: [13], 26: [18, 19]},
    30: {13: [0, 7], 15: [2], 17: [1, 5, 6], 21: [0], 22: [15], 26: [18]},
    31: {13: [0, 4, 13], 15: [2], 17: [1, 5, 6], 21: [0], 26: [19]},
    32: {13: [0, 7, 15], 17: [1, 6, 8, 10], 21: [0, 7], 26: [19]},
}

ARMOR_SLOTS = [  # (EquipmentSlot, InventoryType)
    (0, 1), (2, 3), (4, 5), (5, 6), (6, 7), (7, 8), (8, 9), (9, 10),
]
EQUIPMENT_SLOT_NECK = 1
EQUIPMENT_SLOT_FINGER1, EQUIPMENT_SLOT_FINGER2 = 10, 11
EQUIPMENT_SLOT_TRINKET1, EQUIPMENT_SLOT_TRINKET2 = 12, 13
EQUIPMENT_SLOT_BACK = 14
EQUIPMENT_SLOT_MAINHAND, EQUIPMENT_SLOT_OFFHAND, EQUIPMENT_SLOT_RANGED = 15, 16, 17

VALID_RACES = [1, 2, 3, 4, 5, 6, 7, 8, 10, 11]
ALLIANCE_RACES = {1, 3, 4, 7, 11}

ONSETS = ["Th", "Br", "Cr", "Dr", "Gr", "Kr", "Fr", "Sh", "Sk", "Sn",
          "St", "Tr", "Vr", "Wr", "Zar", "Mor", "Kel", "Val", "Ral", "Bel"]
VOWELS = ["a", "e", "i", "o", "u", "ae", "io", "ou", "ei", "ya"]
CODAS = ["n", "r", "s", "th", "x", "l", "m", "d", "k", "rin", "dor", "las", "mir", "gar", "noth", "wyn"]


def gen_name(rng: random.Random) -> str:
    parts = [rng.choice(ONSETS), rng.choice(VOWELS)]
    for _ in range(rng.randint(1, 2)):
        parts += [rng.choice(CODAS), rng.choice(VOWELS)]
    parts.append(rng.choice(CODAS))
    name = "".join(parts)[:12]
    return name[0].upper() + name[1:].lower()


def sql_quote(s: str) -> str:
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


@dataclass
class MysqlClient:
    exe: Path
    defaults_file: Path

    def run(self, sql: str, db: str | None = None) -> list[list[str]]:
        args = [str(self.exe), "--defaults-file=" + str(self.defaults_file), "-N", "-B"]
        if db:
            args.append(db)
        result = subprocess.run(args, input=sql, capture_output=True, text=True, encoding="utf-8")
        if result.returncode != 0:
            raise RuntimeError(f"mysql failed: {result.stderr.strip()}\nSQL was:\n{sql[:2000]}")
        rows = [line.split("\t") for line in result.stdout.splitlines() if line]
        return rows

    def execute_file(self, path: Path) -> None:
        args = [str(self.exe), "--defaults-file=" + str(self.defaults_file)]
        with path.open("r", encoding="utf-8") as f:
            result = subprocess.run(args, stdin=f, capture_output=True, text=True, encoding="utf-8")
        if result.returncode != 0:
            raise RuntimeError(f"mysql failed executing {path}: {result.stderr.strip()}")


def find_repack_mysql(explicit_exe: str | None, explicit_defaults: str | None) -> MysqlClient:
    if explicit_exe and explicit_defaults:
        return MysqlClient(Path(explicit_exe), Path(explicit_defaults))
    here = Path(__file__).resolve()
    for base in [Path.cwd(), *here.parents]:
        exe = base / "mysql" / "bin" / "mysql.exe"
        defaults = base / "mysql" / "admin-client.ini"
        if exe.exists() and defaults.exists():
            return MysqlClient(exe, defaults)
    raise SystemExit(
        "Could not auto-detect the repack's mysql client. Run this from inside a "
        "CoA-Repack directory, or pass --mysql-exe and --defaults-file explicitly."
    )


@dataclass
class ClassTemplate:
    guid: int
    race: int


@dataclass
class BotAccount:
    id: int
    capacity_left: int


@dataclass
class GearPoolCache:
    mysql: MysqlClient
    cache: dict = field(default_factory=dict)

    def query(self, item_class: int, subclass: int, inv_type: int) -> list[tuple[int, int, int, int]]:
        key = (item_class, subclass, inv_type)
        if key in self.cache:
            return self.cache[key]
        rows = self.mysql.run(
            f"SELECT entry, ItemLevel, RequiredLevel, Quality FROM item_template "
            f"WHERE class={item_class} AND subclass={subclass} AND InventoryType={inv_type} "
            f"AND Quality BETWEEN 1 AND 4 AND AllowableClass=-1 "
            f"AND name NOT LIKE '%RPGITEM%' AND name NOT LIKE '%[PH]%' AND name NOT LIKE '% PH %' "
            f"AND name NOT LIKE '%Test%' AND name NOT LIKE 'Monster - %' AND name NOT LIKE '%Deprecated%' "
            f"AND name NOT LIKE '%[DND]%' AND name NOT LIKE 'NPC %' "
            f"AND RequiredSkill=0 AND requiredspell=0 AND requiredhonorrank=0 AND RequiredCityRank=0 "
            f"AND RequiredReputationFaction=0 AND Map=0 AND area=0 AND HolidayId=0 "
            f"AND (AllowableRace=-1 OR (AllowableRace & 1791)=1791)",
            db="acore_world",
        )
        parsed = [(int(e), int(il), int(rl), int(q)) for e, il, rl, q in rows]
        self.cache[key] = parsed
        return parsed

    def pick(self, item_class: int, subclasses: list[int], inv_type: int, level: int,
              quality_cap: int, rng: random.Random) -> int | None:
        matches = []
        for subclass in subclasses:
            for entry, ilvl, reqlvl, quality in self.query(item_class, subclass, inv_type):
                if quality > quality_cap:
                    continue
                for window in (5, 255):
                    if reqlvl <= level and reqlvl + window >= level:
                        matches.append((quality, reqlvl, ilvl, entry))
                        break
        if not matches:
            return None
        matches.sort(reverse=True)
        top = matches[: min(6, len(matches))]
        return rng.choice(top)[3]


def roll_quality_cap(level: int, rng: random.Random) -> int:
    roll = rng.randint(1, 100)
    if level <= 20:
        return 2 if roll <= 25 else 3 if roll <= 90 else 4
    if level <= 60:
        return 2 if roll <= 5 else 3 if roll <= 75 else 4
    if level < 80:
        return 3 if roll <= 55 else 4
    return 3 if roll <= 20 else 4 if roll <= 90 else 5


def roll_leveled_level(rng: random.Random, max_level: int = 80) -> int:
    # Mirrors RollWeightedLevel: mostly 1-10, tapering toward the cap.
    roll = rng.random()
    if roll < 0.45:
        return rng.randint(1, 10)
    if roll < 0.70:
        return rng.randint(11, 30)
    if roll < 0.88:
        return rng.randint(31, 60)
    return rng.randint(61, max_level)


class GuidAllocator:
    def __init__(self, start: int):
        self.next = start

    def take(self) -> int:
        v = self.next
        self.next += 1
        return v


def build_template_roster(mysql: MysqlClient, bot_account_ids: set[int]) -> dict[int, dict[int, ClassTemplate]]:
    rows = mysql.run(
        "SELECT class, guid, race, account FROM characters WHERE account <> 1 AND level >= 80 "
        "AND class BETWEEN 12 AND 32 ORDER BY guid",
        db="acore_characters",
    )
    roster: dict[int, dict[int, ClassTemplate]] = {}
    for class_s, guid_s, race_s, account_s in rows:
        if int(account_s) in bot_account_ids:
            continue
        classId, guid, race = int(class_s), int(guid_s), int(race_s)
        team = 0 if race in ALLIANCE_RACES else 1
        slot = roster.setdefault(classId, {})
        if team not in slot:
            slot[team] = ClassTemplate(guid, race)
    return roster


def get_or_create_bot_accounts(mysql: MysqlClient, prefix: str, capacity: int,
                                 needed_slots: int, rng: random.Random, dry_run: bool) -> list[BotAccount]:
    """Returns a pool of accounts with real, already-resolved ids and enough
    combined capacity_left to cover needed_slots. Any new accounts this needs
    are created (and their real ids fetched back) immediately, synchronously,
    via the mysql CLI -- no placeholder ids to patch up later."""
    existing = mysql.run(
        f"SELECT id, username FROM account WHERE username LIKE '{prefix}%'", db="acore_auth"
    )
    accounts: dict[str, int] = {name: int(aid) for aid, name in existing}
    counts_rows = mysql.run(
        "SELECT account, COUNT(*) FROM characters WHERE account IN ({}) GROUP BY account".format(
            ",".join(str(v) for v in accounts.values()) or "0"
        ),
        db="acore_characters",
    ) if accounts else []
    used = {int(a): int(c) for a, c in counts_rows}

    pool: list[BotAccount] = []
    for name, aid in accounts.items():
        left = capacity - used.get(aid, 0)
        if left > 0:
            pool.append(BotAccount(aid, left))

    total_capacity = sum(a.capacity_left for a in pool)
    if total_capacity >= needed_slots:
        return pool

    new_usernames: list[str] = []
    create_sql: list[str] = []
    n = 1
    while total_capacity < needed_slots:
        while f"{prefix}{n}" in accounts:
            n += 1
        username = f"{prefix}{n}"
        accounts[username] = -1  # reserve the name locally so we don't reuse it
        new_usernames.append(username)
        if not dry_run:
            password = gen_name(rng) + str(rng.randint(1000, 9999))
            salt, verifier = srp6_salt_verifier(username.upper(), password.upper())
            create_sql.append(
                f"INSERT INTO acore_auth.account (username, salt, verifier, expansion, email, reg_mail) "
                f"VALUES ({sql_quote(username)}, 0x{salt.hex()}, 0x{verifier.hex()}, 2, '', '');"
            )
        total_capacity += capacity
        n += 1

    if dry_run:
        print(f"  (dry run) would create {len(new_usernames)} new bot-hosting account(s): "
              f"{new_usernames[0]}..{new_usernames[-1]}" if new_usernames else "")
        for name in new_usernames:
            pool.append(BotAccount(-1, capacity))
        return pool

    print(f"Creating {len(new_usernames)} new bot-hosting account(s) ...")
    mysql.run("\n".join(create_sql), db="acore_auth")
    rows = mysql.run(
        "SELECT id, username FROM account WHERE username IN ({})".format(
            ",".join(sql_quote(u) for u in new_usernames)
        ),
        db="acore_auth",
    )
    for aid, name in rows:
        pool.append(BotAccount(int(aid), capacity))
    print(f"  done -- {len(pool)} bot-hosting account(s) available in total.")
    return pool


def is_worldserver_running() -> bool:
    if os.name != "nt":
        return False
    try:
        out = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq worldserver.exe"],
            capture_output=True, text=True, encoding="utf-8", errors="ignore",
        ).stdout
        return "worldserver.exe" in out.lower()
    except Exception:
        return False  # best-effort only -- never block a run over a failed check


class InteractiveArgs:
    """Plain container matching argparse's Namespace shape, built from input()
    prompts -- used when the script is double-clicked / run with no arguments,
    so someone who's never touched a command line can still use it."""

    def __init__(self):
        print("=== mod-coa-playerbots offline bot factory ===")
        print("Creates and fully equips bot characters directly in the database.")
        print("The worldserver should be STOPPED before continuing.\n")

        if is_worldserver_running():
            print("worldserver.exe looks like it's still running.")
            if input("Continue anyway? Not recommended. [y/N]: ").strip().lower() != "y":
                raise SystemExit("Stop the worldserver (Stop_All_Server.bat) first, then run this again.")

        while True:
            raw = input("How many bots to create? [600]: ").strip()
            if not raw:
                self.count = 600
                break
            try:
                self.count = int(raw)
                if self.count > 0:
                    break
            except ValueError:
                pass
            print("Enter a positive whole number.")

        print("\nLevel mode:")
        print("  1) Mixed levels 1-80 (a population, like .botcmd spawnleveled)")
        print("  2) All at one fixed level (like .botcmd spawnrandom)")
        choice = input("Choose 1 or 2 [1]: ").strip() or "1"
        if choice == "2":
            self.level = None
            while self.level is None:
                raw = input("Level for every bot [80]: ").strip() or "80"
                try:
                    lvl = int(raw)
                    if 1 <= lvl <= 80:
                        self.level = lvl
                except ValueError:
                    pass
                if self.level is None:
                    print("Enter a number from 1 to 80.")
            self.leveled = False
        else:
            self.level = None
            self.leveled = True

        self.account_prefix = "CoaBotHost"
        self.characters_per_account = 50
        self.mysql_exe = None
        self.defaults_file = None
        self.out = None
        self.dry_run = False
        self.seed = None
        print()


def main() -> None:
    if len(sys.argv) == 1:
        args = InteractiveArgs()
    else:
        ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
        ap.add_argument("--count", type=int, required=True, help="Number of bots to create.")
        level_group = ap.add_mutually_exclusive_group(required=True)
        level_group.add_argument("--level", type=int, help="Fixed level for every bot (e.g. 80, like spawnrandom).")
        level_group.add_argument("--leveled", action="store_true", help="Weighted random levels 1-80 (like spawnleveled).")
        ap.add_argument("--account-prefix", default="CoaBotHost")
        ap.add_argument("--characters-per-account", type=int, default=50)
        ap.add_argument("--mysql-exe")
        ap.add_argument("--defaults-file")
        ap.add_argument("--out", default=None, help="Write the generated SQL here instead of a temp file (kept for inspection).")
        ap.add_argument("--dry-run", action="store_true", help="Generate and print a summary, write no SQL, touch no DB.")
        ap.add_argument("--seed", type=int, default=None)
        args = ap.parse_args()

    rng = random.Random(args.seed)
    mysql = find_repack_mysql(args.mysql_exe, args.defaults_file)

    print(f"Reading current DB state via {mysql.exe} ...")
    existing_bot_accounts = mysql.run(
        f"SELECT id FROM account WHERE username LIKE '{args.account_prefix}%'", db="acore_auth"
    )
    bot_account_ids = {int(r[0]) for r in existing_bot_accounts}

    roster = build_template_roster(mysql, bot_account_ids)
    missing = [c for c in range(12, 33) if c not in roster]
    if missing:
        print(f"WARNING: no level-80+ template character found for class id(s) {missing} "
              f"(need at least one per class, not on a bot account, not account 1) -- "
              f"those classes will be skipped.")
    if not roster:
        raise SystemExit("No usable template characters found at all. Aborting.")

    columns_rows = mysql.run("SHOW COLUMNS FROM characters", db="acore_characters")
    columns = [r[0] for r in columns_rows]

    max_char_guid = int(mysql.run("SELECT COALESCE(MAX(guid),0) FROM characters", db="acore_characters")[0][0])
    max_item_guid = int(mysql.run("SELECT COALESCE(MAX(guid),0) FROM item_instance", db="acore_characters")[0][0])
    char_guids = GuidAllocator(max_char_guid + 1)
    item_guids = GuidAllocator(max_item_guid + 1)

    pool = get_or_create_bot_accounts(
        mysql, args.account_prefix, args.characters_per_account, args.count, rng, args.dry_run
    )

    gear = GearPoolCache(mysql)

    print(f"Generating {args.count} bot(s) ({'level ' + str(args.level) if args.level else 'leveled 1-80'}) ...")

    lines: list[str] = ["USE acore_characters;"]
    account_pool_iter = iter(pool)
    current_account = next(account_pool_iter, None)

    class_ids = sorted(roster.keys())
    created = 0
    class_counts: dict[int, int] = {c: 0 for c in class_ids}
    level_counts: dict[int, int] = {}

    for _ in range(args.count):
        if current_account is None or current_account.capacity_left <= 0:
            current_account = next(account_pool_iter, None)
        if current_account is None:
            print("Ran out of account capacity mid-batch -- this shouldn't happen "
                  "(get_or_create_bot_accounts under-provisioned). Stopping early.")
            break

        classId = rng.choice(class_ids)
        team_templates = roster[classId]
        team = rng.choice(list(team_templates.keys()))
        template = team_templates[team]
        race = template.race

        level = args.level if args.level else roll_leveled_level(rng)
        level_counts[level] = level_counts.get(level, 0) + 1

        name = gen_name(rng)
        new_guid = char_guids.take()
        money = level * level * 20 * rng.randint(50, 150) // 100

        overrides = {
            "guid": str(new_guid),
            "account": str(current_account.id),
            "name": sql_quote(name),
            "race": str(race),
            "gender": str(rng.randint(0, 1)),
            "online": "0",
            "at_login": "0",
            "extra_flags": "0",
            "creation_date": "NOW()",
            "deleteInfos_Account": "NULL",
            "deleteInfos_Name": "NULL",
            "deleteDate": "NULL",
            "level": str(level),
            "xp": "0",
            "money": str(money),
            "totaltime": "0",
            "leveltime": "0",
        }

        insert_cols = ",".join(f"`{c}`" for c in columns)
        select_vals = ",".join(overrides.get(c, f"`{c}`") for c in columns)
        lines.append(
            f"INSERT INTO characters ({insert_cols}) SELECT {select_vals} "
            f"FROM characters WHERE guid = {template.guid};"
        )
        lines.append(
            f"INSERT INTO character_homebind (guid, mapId, zoneId, posX, posY, posZ) "
            f"SELECT {new_guid}, mapId, zoneId, posX, posY, posZ FROM character_homebind WHERE guid = {template.guid};"
        )
        # core.ascension_active_spec is deliberately left unset (no character_settings
        # row): spec/talent assignment still happens via the live repair hook on this
        # bot's first login, same as any other bot -- see this script's docstring.
        # This tool has no authority to validate a spec choice against ClassSpecRoles
        # offline, so it leaves that entirely to the code that already does it right.

        # Gear: 8 real armor slots (proficiency-gated) + universal neck/rings/
        # trinkets/back (any class can wear these) + weapons.
        cap = roll_quality_cap(level, rng)
        armor_subclasses = ARMOR_SUBCLASSES.get(classId, [1])
        equip: dict[int, int] = {}
        for slot, inv_type in ARMOR_SLOTS:
            item = gear.pick(4, armor_subclasses, inv_type, level, roll_quality_cap(level, rng), rng)
            if item is None and inv_type == 5:
                item = gear.pick(4, armor_subclasses, 20, level, cap, rng)  # cloth robe fallback
            if item:
                equip[slot] = item

        neck = gear.pick(4, [0], 2, level, roll_quality_cap(level, rng), rng)
        if neck:
            equip[EQUIPMENT_SLOT_NECK] = neck
        back = gear.pick(4, [1], 16, level, roll_quality_cap(level, rng), rng)
        if back:
            equip[EQUIPMENT_SLOT_BACK] = back
        ring1 = gear.pick(4, [0], 11, level, roll_quality_cap(level, rng), rng)
        if ring1:
            equip[EQUIPMENT_SLOT_FINGER1] = ring1
        ring2 = gear.pick(4, [0], 11, level, roll_quality_cap(level, rng), rng)
        if ring2 and ring2 != ring1:
            equip[EQUIPMENT_SLOT_FINGER2] = ring2
        trinket1 = gear.pick(4, [0], 12, level, roll_quality_cap(level, rng), rng)
        if trinket1:
            equip[EQUIPMENT_SLOT_TRINKET1] = trinket1
        trinket2 = gear.pick(4, [0], 12, level, roll_quality_cap(level, rng), rng)
        if trinket2 and trinket2 != trinket1:
            equip[EQUIPMENT_SLOT_TRINKET2] = trinket2

        weapon_types = WEAPON_TYPES.get(classId, {})
        mh_priority = [17, 21, 13]
        for inv_type in mh_priority:
            if inv_type in weapon_types:
                item = gear.pick(2, weapon_types[inv_type], inv_type, level, cap, rng)
                if item:
                    equip[EQUIPMENT_SLOT_MAINHAND] = item
                    used_inv = inv_type
                    break
        else:
            used_inv = None
        if used_inv == 13:  # one-hander -- try an offhand (shield, offhand item, or dual-wield)
            for inv_type in (14, 22, 13):
                if inv_type in weapon_types or inv_type == 14:
                    subclasses = weapon_types.get(inv_type, [6] if inv_type == 14 else [])
                    item_class = 4 if inv_type == 14 else 2
                    item = gear.pick(item_class, subclasses or [6], inv_type, level, cap, rng)
                    if item:
                        equip[EQUIPMENT_SLOT_OFFHAND] = item
                        break
        for inv_type in (15, 25, 26):
            if inv_type in weapon_types:
                item = gear.pick(2, weapon_types[inv_type], inv_type, level, cap, rng)
                if item:
                    equip[EQUIPMENT_SLOT_RANGED] = item
                    break

        for slot, item_entry in equip.items():
            item_guid = item_guids.take()
            lines.append(
                f"INSERT INTO item_instance (guid, itemEntry, owner_guid, creatorGuid, giftCreatorGuid, "
                f"count, duration, charges, flags, enchantments, randomPropertyId, durability, playedTime) "
                f"VALUES ({item_guid}, {item_entry}, {new_guid}, 0, 0, 1, 0, '', 0, '', 0, 0, 0);"
            )
            lines.append(
                f"INSERT INTO character_inventory (guid, bag, slot, item) VALUES ({new_guid}, 0, {slot}, {item_guid});"
            )

        current_account.capacity_left -= 1
        class_counts[classId] += 1
        created += 1

    used_classes = sum(1 for c in class_counts.values() if c > 0)
    print(f"Created {created} bot(s) across {used_classes} class(es).")
    if args.level:
        print(f"  all at level {args.level}")
    else:
        buckets = {"1-10": 0, "11-30": 0, "31-60": 0, "61-80": 0}
        for lvl, cnt in level_counts.items():
            key = "1-10" if lvl <= 10 else "11-30" if lvl <= 30 else "31-60" if lvl <= 60 else "61-80"
            buckets[key] += cnt
        print(f"  level spread: {buckets}")

    if args.dry_run:
        print("Dry run -- no SQL written, no DB touched.")
        return

    out_path = Path(args.out) if args.out else Path.cwd() / "offline_bot_factory_generated.sql"
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {len(lines)} statement(s) to {out_path}.")
    print("Executing ...")
    mysql.execute_file(out_path)
    print("Done. Start the worldserver and use .botcmd spawnbot / the normal auto-login "
          "to bring these bots online -- their gear is already in place; spells, spec, "
          "professions, mounts and taxi nodes still fill in on that first login as usual.")


if __name__ == "__main__":
    main()
