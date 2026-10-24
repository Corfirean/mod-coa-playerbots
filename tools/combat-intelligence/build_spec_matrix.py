"""Build docs/combat-intelligence/SPEC_VALIDATION_MATRIX.md from an all-spec smoke run and the profile audit.

python -I build_spec_matrix.py <smoke-result-dir> <profile_audit_raw.json> <out.md> [--profiles module/src/profiles]

Per spec: role, whether a profile exists, how many descriptors the run evaluated, which ones are dead at the training level (every
decision said "not known/resolvable" or "passive ability"), accepted and failed casts in the 30 s run, the run's DPS, and the data findings
of profile_audit.py. A single 30 s run proves that the profile loads and casts; it does not prove the rotation is good (see TRAINING_RESULTS.md).
"""
import json
import re
import sys
from pathlib import Path

ROLES = Path(__file__).resolve().parents[2] / 'module/src/ClassSpecRoles.cpp'
NAME_BLOCK = re.compile(r'd\.name = "(?P<name>[^"]*)";\s*d\.rootSpellId = (?P<id>\d+);')


def spec_table():
    text = ROLES.read_text(encoding='utf-8')
    for m in re.finditer(r'\{\s*(\d+),\s*(\d+),\s*BotRole::(\w+),\s*"([^"]+)"\s*\}', text):
        if int(m.group(1)) >= 12:
            yield int(m.group(1)), int(m.group(2)), m.group(3), m.group(4)


def spell_names(profile_dir):
    names = {}
    for p in profile_dir.glob('Profile*.cpp'):
        for m in NAME_BLOCK.finditer(p.read_text(encoding='utf-8', errors='replace')):
            names.setdefault(m.group('id'), m.group('name'))
    return names


def main(argv):
    results, audit_file, out = Path(argv[0]), Path(argv[1]), Path(argv[2])
    profile_dir = Path(argv[argv.index('--profiles') + 1]) if '--profiles' in argv else Path('module/src/profiles')
    names = spell_names(profile_dir)
    audit = json.loads(audit_file.read_text(encoding='utf-8'))['findings']
    by_spec = {}
    for f in audit:
        by_spec.setdefault((f['class'], f['spec']), []).append(f)

    lines = ['# Spec validation matrix', '',
             f'Generated from `{results.name}` (ST_STATIC, 30 s, one seed, level-60 live gear of the spec) and `{audit_file.name}`.',
             'DPS of a healer or support spec on a static dummy is not a measure of its role. "Dead" = every decision rejected the descriptor as '
             'not known / passive. "Data findings" come from `profile_audit.py`.', '',
             '| Class | Spec | Name | Role | Profile | Descriptors seen | Dead descriptors | Casts ok/failed | Dummy DPS | Data findings |',
             '|---|---|---|---|---|---|---|---|---|---|']
    totals = {'specs': 0, 'run': 0, 'dead': 0}
    for cls, spec, role, name in spec_table():
        totals['specs'] += 1
        folder = next(iter(results.glob(f'spec{spec:03d}-*')), None)
        findings = by_spec.get((cls, spec), [])
        finding_text = '; '.join(f"{f['kind']} {f['ability']} ({f['spell']})" for f in findings) or '-'
        if folder is None or not (folder / 'summary.json').exists():
            lines.append(f'| {cls} | {spec} | {name} | {role} | ? | not run | | | | {finding_text} |')
            continue
        totals['run'] += 1
        summary = json.loads((folder / 'summary.json').read_text(encoding='utf-8'))
        abilities = json.loads((folder / 'abilities.json').read_text(encoding='utf-8')) if (folder / 'abilities.json').exists() else {}
        dead = []
        for sid, a in abilities.items():
            reasons = a.get('reasons', {})
            if reasons and set(reasons) <= {'not known/resolvable', 'passive ability'}:
                dead.append(f"{names.get(sid, '?')} ({sid})")
        totals['dead'] += len(dead)
        ok = sum(a.get('castsAccepted', 0) for a in abilities.values())
        failed = sum(a.get('castsFailed', 0) for a in abilities.values())
        lines.append(f"| {cls} | {spec} | {name} | {role} | {'yes' if abilities else 'no abilities'} | {len(abilities)} | "
                     f"{', '.join(dead) or '-'} | {ok}/{failed} | {summary['combat']['dps']:.0f} | {finding_text} |")
    lines += ['', f"Specs: {totals['specs']}, run: {totals['run']}, dead descriptors found: {totals['dead']}."]
    out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(out, totals)


if __name__ == '__main__':
    main(sys.argv[1:])
