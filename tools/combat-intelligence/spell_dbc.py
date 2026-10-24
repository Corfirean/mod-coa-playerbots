"""Read-only Spell.dbc lookup for combat audits.

python -I spell_dbc.py [--dbc PATH] <spell id or name fragment> [...]

Prints id, name, rank, cast time index, cooldown, category cooldown, power type/cost, range index, equipped item class/subclass mask and
the first three effects, so a profile descriptor can be checked against the data the server actually loads (never against a guide).
Layout: 3.3.5 Spell.dbc, 234 fields; field 136 is the English name, 153 the rank.
"""
import struct, sys

DEFAULT = 'C:/games/CoA Server 2/Data/dbc/Spell.dbc'
F = dict(id=0, attr=5, cast_time=28, cooldown=29, cat_cooldown=30, power_type=41, mana_cost=42, range=46, equip_class=68, equip_mask=69,
         effect=71, aura=95, bp=80, period=98, duration=40, name=136, rank=153)


class Dbc:
    def __init__(self, path=DEFAULT):
        self.b = open(path, 'rb').read()
        magic, self.rc, self.fc, self.rs, ss = struct.unpack_from('<4sIIII', self.b, 0)
        assert magic == b'WDBC' and self.fc == 234, 'unexpected Spell.dbc layout'
        self.sb = 20 + self.rc * self.rs

    def field(self, row, i, signed=False):
        return struct.unpack_from('<i' if signed else '<I', self.b, 20 + row * self.rs + i * 4)[0]

    def string(self, off):
        a = self.sb + off
        return self.b[a:self.b.index(b'\0', a)].decode('utf-8', 'replace')

    def rows(self):
        for r in range(self.rc):
            yield r, self.field(r, 0)

    def describe(self, row):
        g = lambda i, s=False: self.field(row, i, s)
        eff = [g(F['effect'] + k) for k in range(3)]
        aura = [g(F['aura'] + k) for k in range(3)]
        return (f"{g(0)} {self.string(g(F['name']))!r} [{self.string(g(F['rank']))}] cast_idx={g(F['cast_time'])} cd={g(F['cooldown'])} "
                f"catcd={g(F['cat_cooldown'])} power={g(F['power_type'], True)}:{g(F['mana_cost'])} range_idx={g(F['range'])} "
                f"equip={g(F['equip_class'], True)}/{hex(g(F['equip_mask'], True) & 0xffffffff)} effects={eff} auras={aura} "
                f"bp={[g(F['bp'] + k, True) for k in range(3)]} period={[g(F['period'] + k) for k in range(3)]} duration_idx={g(F['duration'])}")


def main(argv):
    path = DEFAULT
    if argv[:1] == ['--dbc']:
        path, argv = argv[1], argv[2:]
    dbc = Dbc(path)
    for q in argv:
        if q.isdigit():
            for r, sid in dbc.rows():
                if sid == int(q):
                    print(dbc.describe(r))
        else:
            for r, sid in dbc.rows():
                if q.lower() in dbc.string(dbc.field(r, F['name'])).lower():
                    print(dbc.describe(r))


if __name__ == '__main__':
    main(sys.argv[1:])
