"""List, per spec, the highest-priority descriptors that never got cast in a run and why (the rejection reason that dominated).

python -I blocked_abilities.py <result-dir> [--top 6]

Reads abilities.json of every episode folder plus the descriptors' baseScore from module/src/profiles. A descriptor with a high baseScore that is
never cast while it is *known* is the signature of a profile bug: a gate that cannot be satisfied (missing aura, resource, form, target
condition). Descriptors dominated by 'not known/resolvable' are reported separately (talent not in the build, wrong id).
"""
import json
import re
import sys
from pathlib import Path

PROFILES = Path('module/src/profiles')
BLOCK = re.compile(r'd\.name = "(?P<name>[^"]*)";(?P<body>.*?)p\.abilities\.push_back\(d\);', re.S)
HEADER = re.compile(r'p\.classId = (?P<cls>\d+)[^\n]*\n\s*p\.specId = (?P<spec>\d+)[^\n]*\n\s*p\.role = BotRole::(?P<role>\w+);\s*\n\s*p\.profileName = "(?P<pname>[^"]+)"')


def scores():
    out = {}
    for path in sorted(PROFILES.glob('Profile*.cpp')):
        text = path.read_text(encoding='utf-8', errors='replace')
        heads = list(HEADER.finditer(text))
        for n, h in enumerate(heads):
            end = heads[n + 1].start() if n + 1 < len(heads) else len(text)
            for b in BLOCK.finditer(text[h.end():end]):
                body = b.group('body')
                m = re.search(r'd\.rootSpellId = (\d+);', body)
                if not m:
                    continue
                base = re.search(r'd\.baseScore = ([0-9.]+)f?;', body)
                out.setdefault((int(h.group('spec'))), {})[m.group(1)] = (b.group('name'), float(base.group(1)) if base else 100.0)
    return out


def main(argv):
    results = Path(argv[0])
    top = int(argv[argv.index('--top') + 1]) if '--top' in argv else 6
    sc = scores()
    for folder in sorted(results.glob('spec*')):
        spec = int(re.match(r'spec(\d+)-', folder.name).group(1))
        abilities = json.loads((folder / 'abilities.json').read_text(encoding='utf-8'))
        rows = []
        for sid, a in abilities.items():
            name, base = sc.get(spec, {}).get(sid, ('?', 0.0))
            reasons = a.get('reasons', {})
            dominant = max(reasons.items(), key=lambda kv: kv[1])[0] if reasons else '-'
            ranked = {k: v for k, v in reasons.items() if k not in ('global cooldown', 'internal throttle', 'spell cooldown')}
            blocker = max(ranked.items(), key=lambda kv: kv[1])[0] if ranked else dominant
            rows.append((base, name, sid, a.get('castsAccepted', 0), a.get('castsFailed', 0), blocker))
        rows.sort(reverse=True)
        bad = [r for r in rows if r[3] == 0 and r[5] != 'not known/resolvable' and r[5] != 'passive ability'][:top]
        print(f"== {folder.name}  casts {sum(r[3] for r in rows)}")
        for r in bad:
            print(f"   {r[0]:6.0f} {r[1][:34]:34} {r[2]:>8} cast {r[3]} failed {r[4]}  blocked by: {r[5]}")


if __name__ == '__main__':
    main(sys.argv[1:])
