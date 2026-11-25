import struct, mmap, os
"""Reads names, levels, stance masks and costs out of a client Spell.dbc (REBORN_DBC points at its folder)."""
P = os.path.join(os.environ.get("REBORN_DBC", r"C:\games\CoA Server 2\Data\dbc"), "Spell.dbc")
class SpellDb:
    def __init__(self):
        self.f = open(P, "rb")
        self.m = mmap.mmap(self.f.fileno(), 0, access=mmap.ACCESS_READ)
        self.n, self.fields, self.size, self.ssize = struct.unpack_from("<4I", self.m, 4)
        self.base = 20
        self.sbase = 20 + self.n * self.size
        self.index = {}
        for i in range(self.n):
            self.index[struct.unpack_from("<I", self.m, self.base + i * self.size)[0]] = i
    def u(self, sid, field):
        i = self.index.get(sid)
        return None if i is None else struct.unpack_from("<I", self.m, self.base + i * self.size + 4 * field)[0]
    def s(self, sid, field):
        o = self.u(sid, field)
        if o is None: return None
        a = self.sbase + o
        return self.m[a:self.m.find(b"\0", a)].decode("utf-8", "replace")
    def name(self, sid): return self.s(sid, 136)
    def desc(self, sid): return self.s(sid, 170)
