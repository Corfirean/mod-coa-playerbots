"""Port of the core's free-pick Build rules (Reborn realm) used to design and verify bot talent builds."""
import os, struct, collections, json

# Client DBC folder of a Reborn server; set REBORN_DBC to use another one.
D = os.environ.get("REBORN_DBC", r"C:\games\CoA Server 2\Data\dbc") + os.sep
EXPANSIONS = ["EXPANSION_CLASSIC", "EXPANSION_THE_BURNING_CRUSADE", "EXPANSION_WRATH_OF_THE_LICH_KING"]
ROW_DEPRECATED, ROW_RETAINED, ROW_DISABLED, ROW_MASTERY = 0x1, 0x2, 0x8, 0x1000
ROW_DISPLAY, ROW_AUTOMATIC, ROW_CONN_MAX, ROW_MODE = 0x20000, 0x100000, 0x200000, 0x1800000


class Dbc:
    def __init__(self, path):
        b = open(path, "rb").read()
        self.n, self.fields, self.size, self.ssize = struct.unpack_from("<4I", b, 4)
        self.data = b[20:20 + self.n * self.size]
        self.strings = b[20 + self.n * self.size:]

    def rec(self, i):
        return self.data[i * self.size:(i + 1) * self.size]

    def s(self, r, off):
        o = struct.unpack_from("<I", r, off)[0]
        return self.strings[o:self.strings.index(b"\0", o)].decode("utf-8", "replace")


def u32(r, off):
    return struct.unpack_from("<I", r, off)[0]


def load(max_level=60):
    ct = Dbc(D + "CharacterAdvancementClassTypes.dbc")
    types = {}
    for i in range(ct.n):
        r = ct.rec(i)
        types[u32(r, 0)] = dict(cls=u32(r, 8), hero=u32(r, 12), coa=u32(r, 16), stock=u32(r, 20))
    adv = Dbc(D + "CharacterAdvancement.dbc")
    rows = {}
    for i in range(adv.n):
        r = adv.rec(i)
        row = dict(
            id=u32(r, 0), type=adv.s(r, 4), required=[u32(r, 8 + 4 * k) for k in range(3)],
            spells=[u32(r, 20 + 4 * k) for k in range(9)], ae=u32(r, 56), te=u32(r, 60), level=u32(r, 104), group=u32(r, 116),
            ctype=u32(r, 128), tab=u32(r, 132), gte=u32(r, 140), cte=u32(r, 148), tte=u32(r, 156), gae=u32(r, 136), cae=u32(r, 144),
            tae=u32(r, 152), cpv=u32(r, 160), cpi=u32(r, 164), name=adv.s(r, 188), start=bool(r[414]),
            conn=[u32(r, 415 + 4 * k) for k in range(4)], exp=adv.s(r, 475), flags=u32(r, 479), realms=list(r[483:488]))
        row["expansion"] = EXPANSIONS.index(row["exp"]) if row["exp"] in EXPANSIONS else 0
        rows[row["id"]] = row
    es = Dbc(D + "CharacterAdvancementEssence.dbc")
    budget = collections.defaultdict(dict)
    for i in range(es.n):
        r = struct.unpack_from("<%dI" % (es.size // 4), es.rec(i))
        if not any(r[3:7]):
            budget[r[2]][r[1]] = (r[7], r[8])
    return types, rows, budget


class Sim:
    def __init__(self, types, rows, budget, cls, level, max_level=60):
        self.types, self.rows, self.budget, self.cls, self.level = types, rows, budget, cls, level
        self.ruleset = 0 if max_level <= 60 else 1 if max_level <= 70 else 2
        self.entries = collections.OrderedDict()

    def rank(self, eid):
        return self.entries.get(eid, 0)

    def maxrank(self, row):
        n = 0
        while n < 9 and row["spells"][n]:
            n += 1
        return n

    def admits(self, row):
        t = self.types.get(row["ctype"])
        if not t:
            return False
        if t["cls"]:
            return self.cls == t["cls"]
        if t["hero"]:
            return False
        if t["coa"]:
            return False
        return bool(t["stock"]) and (1 <= self.cls <= 9 or self.cls == 11)

    def visible(self, row):
        if row["flags"] & (ROW_DISABLED | ROW_DEPRECATED):
            return False
        t = self.types.get(row["ctype"])
        if not t:
            return False
        # Reborn realm, live
        if t["hero"] != False and t["coa"] != False and t["stock"] != True:
            return False
        if not (t["stock"]):
            if not (t["hero"] == 0 or True):
                pass
        # replicate: if (hero != heroRealm && coa != realmCoA && stock != realmWCR) reject
        hero_realm = False
        if bool(t["hero"]) != hero_realm and bool(t["coa"]) != False and bool(t["stock"]) != True:
            return False
        return bool(row["realms"][0])

    def spent(self, key_cost, key_thr, minimum, ctype=None, tab=None):
        total = 0
        for eid, rk in self.entries.items():
            row = self.rows[eid]
            if ctype is not None and row["ctype"] != ctype:
                continue
            if tab is not None and row["tab"] != tab:
                continue
            thr = row[key_thr]
            if (not minimum) or thr < minimum:
                total += row[key_cost] * rk
        return total

    def te_budget(self):
        return self.budget.get(self.cls, {}).get(self.level, (0, 0))[1]

    def validate(self, eid):
        row = self.rows.get(eid)
        if not row:
            return "BAD_ABILITY"
        if row["flags"] & ROW_DISPLAY:
            return "DISPLAY"
        if row["level"] > self.level:
            return "LOW_LEVEL"
        if self.rank(eid) >= self.maxrank(row):
            return "ALREADY_KNOWN"
        if row["expansion"] > self.ruleset:
            return "WRONG_EXPANSION"
        if row["flags"] & ROW_DISABLED:
            return "DISABLED"
        if not self.admits(row):
            return "WRONG_CLASS"
        if row["start"] and any(self.rows[e]["start"] for e in self.entries):
            return "STARTING_NODE"
        if any(row["conn"]):
            ok = False
            for c in row["conn"]:
                if not c or c not in self.entries:
                    continue
                if not (row["flags"] & ROW_CONN_MAX):
                    ok = True
                    break
                o = self.rows.get(c)
                if o and self.rank(c) == self.maxrank(o):
                    ok = True
                    break
            if not ok:
                return "MISSING_CONNECTED"
        if not all((not x) or x in self.entries for x in row["required"]):
            return "MISSING_REQUIRED"
        if not row["realms"][0]:
            return "WRONG_REALM"
        if row["flags"] & ROW_DEPRECATED:
            return "DEPRECATED"
        if row["group"] not in (0, 1) and any(self.rows[e]["group"] == row["group"] for e in self.entries):
            return "GROUP"
        need = row["gte"]
        if need and self.spent("te", "gte", need) < need:
            return "INVESTED_TE_GLOBAL"
        need = row["cte"]
        if need and self.spent("te", "cte", need, ctype=row["ctype"]) < need:
            return "INVESTED_TE_CLASS"
        need = row["tte"]
        if need and not (need <= self.spent("te", "tte", need, ctype=row["ctype"], tab=row["tab"])):
            return "INVESTED_TE_TAB"
        if row["cpi"]:
            pts = sum(self.rows[e]["cpv"] * rk for e, rk in self.entries.items()
                      if self.rows[e]["ctype"] == row["ctype"] and self.rows[e]["cpi"] < row["cpi"])
            if row["cpi"] > pts:
                return "INVESTED_POINTS"
        if row["te"]:
            rem = max(0, self.te_budget() - self.spent("te", "gte", 0))
            if row["te"] > rem:
                return "MISSING_TE"
        if row["flags"] & ROW_MODE:
            return "MODE"
        return None

    def add(self, eid):
        self.entries[eid] = self.entries.get(eid, 0) + 1


def fit_guide(types, rows, budget, cls, level, guide):
    """guide: ordered list of (entryId, rank). Mirrors FitGuide starting from an empty kept set."""
    s = Sim(types, rows, budget, cls, level)
    progress = True
    while progress:
        progress = False
        for eid, rk in guide:
            while s.rank(eid) < rk and s.validate(eid) is None:
                s.add(eid)
                progress = True
    return s
