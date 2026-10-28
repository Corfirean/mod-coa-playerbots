"""Check every combat-profile descriptor against Spell.dbc.

python -I profile_audit.py [--src module/src/profiles] [--dbc PATH] [--json out.json]

For each `d.name / d.rootSpellId / d.tags` block in module/src/profiles/Profile*.cpp it reports data problems that make a descriptor dead or
mis-classified (it never decides a rotation):
  PASSIVE        the spell is passive in Spell.dbc, so the engine rejects it on every decision
  MISSING        the spell id is not in Spell.dbc at all
  TAUNT_TAG      tagged Taunt, but the spell has neither ATTACK_ME (114) nor MOD_TAUNT (11)
  INTERRUPT_TAG  tagged Interrupt, but no INTERRUPT_CAST (68) effect or MOD_SILENCE (27) aura
  HEAL_TAG       tagged DirectHeal/EmergencyHeal, but no heal effect (10, 67, 136)
  HOT_TAG        tagged PeriodicHeal, but no PERIODIC_HEAL (8) aura
  SHIELD_TAG     tagged Shield, but no absorb aura (69, 301)
  NAME           descriptor name shares no word with the Spell.dbc name (possible wrong id)
Findings are hypotheses for the analyst: a spell may be a trigger/dummy whose real effect lives in a script.
"""
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from spell_dbc import Dbc, F  # noqa: E402

PASSIVE = 0x40
BLOCK = re.compile(r'd\.name = "(?P<name>[^"]*)";(?P<body>.*?)p\.abilities\.push_back\(d\);', re.S)
HEADER = re.compile(r'p\.classId = (?P<cls>\d+)[^\n]*\n\s*p\.specId = (?P<spec>\d+)[^\n]*\n\s*p\.role = BotRole::(?P<role>\w+);\s*\n\s*p\.profileName = "(?P<pname>[^"]+)"')


def words(s):
    return {w for w in re.findall(r'[a-z]+', s.lower()) if len(w) > 2}


def main(argv):
    src = Path('module/src/profiles')
    dbc_path = None
    out_json = None
    i = 0
    while i < len(argv):
        if argv[i] == '--src':
            src = Path(argv[i + 1]); i += 2
        elif argv[i] == '--dbc':
            dbc_path = argv[i + 1]; i += 2
        elif argv[i] == '--json':
            out_json = argv[i + 1]; i += 2
        else:
            i += 1
    dbc = Dbc(dbc_path) if dbc_path else Dbc()
    rows = {sid: r for r, sid in dbc.rows()}
    findings = []
    profiles = 0
    abilities = 0
    for path in sorted(src.glob('Profile*.cpp')):
        text = path.read_text(encoding='utf-8', errors='replace')
        # split into profile sections by the header that precedes each abilities list
        heads = list(HEADER.finditer(text))
        for n, h in enumerate(heads):
            end = heads[n + 1].start() if n + 1 < len(heads) else len(text)
            section = text[h.end():end]
            profiles += 1
            for b in BLOCK.finditer(section):
                body = b.group('body')
                m = re.search(r'd\.rootSpellId = (\d+);', body)
                if not m:
                    continue
                abilities += 1
                sid = int(m.group(1))
                name = b.group('name')
                tags = set(re.findall(r'AbilityTag::(\w+)', re.search(r'd\.tags = ([^;]*);', body).group(1))) if 'd.tags' in body else set()
                where = f"{h.group('pname')} (class {h.group('cls')} spec {h.group('spec')} {h.group('role')})"

                def add(kind, detail=''):
                    findings.append({'profile': h.group('pname'), 'class': int(h.group('cls')), 'spec': int(h.group('spec')),
                                     'role': h.group('role'), 'file': path.name, 'ability': name, 'spell': sid, 'kind': kind, 'detail': detail})

                row = rows.get(sid)
                if row is None:
                    add('MISSING', 'spell id not in Spell.dbc')
                    continue
                g = lambda k, s=False: dbc.field(row, k, s)
                effects = [g(F['effect'] + k) for k in range(3)]
                auras = [g(F['aura'] + k) for k in range(3)]
                dbc_name = dbc.string(g(F['name']))
                if g(F['attr']) & PASSIVE:
                    add('PASSIVE', f"Spell.dbc '{dbc_name}' is passive")
                if 'Taunt' in tags and 114 not in effects and 11 not in auras:
                    add('TAUNT_TAG', f"'{dbc_name}' effects={effects} auras={auras}")
                if 'Interrupt' in tags and 68 not in effects and 27 not in auras:
                    add('INTERRUPT_TAG', f"'{dbc_name}' effects={effects} auras={auras}")
                if tags & {'DirectHeal', 'EmergencyHeal'} and not ({10, 67, 136} & set(effects)):
                    add('HEAL_TAG', f"'{dbc_name}' effects={effects} auras={auras}")
                if 'PeriodicHeal' in tags and 8 not in auras:
                    add('HOT_TAG', f"'{dbc_name}' effects={effects} auras={auras}")
                if 'Shield' in tags and not ({69, 301} & set(auras)):
                    add('SHIELD_TAG', f"'{dbc_name}' effects={effects} auras={auras}")
                if sid >= 300000 and name and not (words(name) & words(dbc_name)):
                    add('NAME', f"descriptor '{name}' vs Spell.dbc '{dbc_name}'")
    summary = {}
    for f in findings:
        summary[f['kind']] = summary.get(f['kind'], 0) + 1
    print(f'profiles {profiles}, descriptors {abilities}, findings {len(findings)}: {summary}')
    for f in findings:
        print(f"{f['kind']:14} {f['profile']:38} {f['ability'][:34]:34} {f['spell']:>8}  {f['detail'][:110]}")
    if out_json:
        Path(out_json).write_text(json.dumps({'summary': summary, 'findings': findings}, indent=1), encoding='utf-8')


if __name__ == '__main__':
    main(sys.argv[1:])
