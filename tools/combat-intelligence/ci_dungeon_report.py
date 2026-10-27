"""Summarise a folder of dungeon runs written by ci_dungeon_run.py.

python -I ci_dungeon_report.py <results-dir> <out.md> [--scout <scout-dir>]

Per specialization (all its runs): the group's kills per ten minutes, elite kills, deaths and wipes, bosses, outcome counts, and what the bot of that specialization
did itself - damage and healing per second of combat, damage taken, deaths, lowest power - with the figure of the median spec of the same role in the same dungeons next
to it, so a spec is judged against specs that had the same job and the same places, not against a different role. Every run's dungeon, level of the mobs and any
problem the run met (stuck, no route, errors) is listed at the end. Nothing here decides about a rotation: it only counts.
"""
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path

SLOT = {'Tank': 0, 'Healer': 1}


def load(folder):
    runs = []
    for f in sorted(Path(folder).glob('*.json')):
        r = json.load(open(f, encoding='utf-8'))
        if r.get('meta', {}).get('spec') is None or 'members_result' not in r:
            r['members_result'] = r.get('members_result', {})
        runs.append(r)
    return runs


def subject(run):
    """The member that carried the specialization under test."""
    items = list(run.get('members_result', {}).items())
    idx = SLOT.get(run['meta'].get('role'), 2)
    return items[idx] if len(items) > idx else (None, None)


def per_run(run):
    secs = max(run.get('activeSeconds') if run.get('activeSeconds') is not None else run.get('combatSeconds', 0), 1)
    members = run['members_result'].values()
    name, me = subject(run)
    out = {
        'label': run['label'], 'dungeon': run['dungeon'], 'outcome': run['outcome'], 'seconds': run['seconds'],
        'kills': sum(m['kills'] for m in members), 'elite': sum(m['eliteKills'] for m in members),
        'deaths': sum(m['deaths'] for m in members), 'wipes': run.get('wipes', 0),
        'boss': run.get('bossDone'), 'bossTotal': run.get('bossTotal'), 'combat': secs,
        'kills_per_10min': sum(m['kills'] for m in members) * 600.0 / max(run['seconds'], 1), 'stalemate': run.get('stalemateSeconds', 0),
    }
    if me:
        out.update({'dps': me['damage'] / secs, 'hps': me['healing'] / secs, 'taken': me['taken'] / secs, 'myDeaths': me['deaths'],
                    'minPw': me['minPw'], 'minHp': me['minHp'], 'myKills': me['kills']})
    mobs = run.get('mobs', {})
    out['mobLevel'] = max([m['lvl'] for m in mobs.values() if m.get('lvl')] or [0])
    return out


def med(values):
    values = [v for v in values if v is not None]
    return statistics.median(values) if values else None


def fmt(v, digits=0):
    return '-' if v is None else f'{v:.{digits}f}'


def main(argv):
    results, out = Path(argv[0]), Path(argv[1])
    runs = [r for r in load(results) if r.get('meta', {}).get('spec') is not None]
    errors = [r for r in load(results) if r['outcome'] == 'ERROR']
    rows = [(r, per_run(r)) for r in runs if r['outcome'] != 'ERROR' and r.get('members_result')]
    by_spec = defaultdict(list)
    for r, p in rows:
        by_spec[(r['meta']['cls'], r['meta']['spec'], r['meta']['role'], r['meta']['name'])].append(p)
    # reference = median of the same role in the same dungeon, so a spec is never judged on the dungeon set it happened to draw
    ref = defaultdict(list)
    for r, p in rows:
        role = r['meta']['role']
        ref[(role, r['dungeon'])].append(p.get('hps') if role == 'Healer' else p.get('dps'))
    for r, p in rows:
        role = r['meta']['role']
        base = med(ref[(role, r['dungeon'])])
        own = p.get('hps') if role == 'Healer' else p.get('dps')
        p['rel'] = own / base if own is not None and base else None
    lines = ['# Dungeon runs', '',
             f'{len(rows)} runs of {len(by_spec)} specializations from `{results.name}` (ten-minute auto-dungeon runs of a group of five in mid-level dungeons, level-60 bots in live gear; '
             'the spec under test fills its role slot, the other four are the fixed team). Group figures are for the whole group; "own" figures are the bot of the specialization. '
             '"vs role" = own damage (or healing) per combat second divided by the median of the same role in the same dungeon (median over the runs of the spec). Damage and healing are per active combat second (seconds in which the group dealt damage).', '',
             '| Class | Spec | Name | Role | Runs | Outcomes | Group kills /10 min | Elite kills | Deaths (group) | Wipes | Own dps | vs role | Own hps | Own deaths | Lowest power % | Dungeons |',
             '|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|']
    for (cls, spec, role, name), ps in sorted(by_spec.items(), key=lambda kv: (kv[0][2], kv[0][1])):
        outcomes = defaultdict(int)
        for p in ps:
            outcomes[p['outcome']] += 1
        rel = med([p.get('rel') for p in ps])
        lines.append(f'| {cls} | {spec} | {name} | {role} | {len(ps)} | {", ".join(f"{k} {v}" for k, v in sorted(outcomes.items()))} | '
                     f'{fmt(med([p["kills_per_10min"] for p in ps]), 1)} | {fmt(med([p["elite"] for p in ps]), 0)} | {fmt(med([p["deaths"] for p in ps]), 0)} | '
                     f'{sum(p["wipes"] for p in ps)} | {fmt(med([p.get("dps") for p in ps]), 0)} | {fmt(rel * 100 if rel is not None else None, 0)}% | '
                     f'{fmt(med([p.get("hps") for p in ps]), 0)} | {sum(p.get("myDeaths", 0) for p in ps)} | {fmt(med([p.get("minPw") for p in ps]), 0)} | '
                     f'{", ".join(sorted({p["dungeon"] for p in ps}))} |')
    lines += ['', '## By dungeon', '', '| Dungeon | Runs | Mob level | Group kills /10 min (median) | Deaths (median) | Wipes | Bosses down | Outcomes |', '|---|---|---|---|---|---|---|---|']
    by_dungeon = defaultdict(list)
    for r, p in rows:
        by_dungeon[r['dungeon']].append(p)
    for d, ps in sorted(by_dungeon.items()):
        outcomes = defaultdict(int)
        for p in ps:
            outcomes[p['outcome']] += 1
        lines.append(f'| {d} | {len(ps)} | {max(p["mobLevel"] for p in ps)} | {fmt(med([p["kills_per_10min"] for p in ps]), 1)} | {fmt(med([p["deaths"] for p in ps]), 0)} | '
                     f'{sum(p["wipes"] for p in ps)} | {sum(p["boss"] or 0 for p in ps)} | {", ".join(f"{k} {v}" for k, v in sorted(outcomes.items()))} |')
    problems = [(r['label'], r['outcome'], r.get('error') or ' / '.join(l[l.find('AutoDungeon'):][:90] for l in r.get('autoDungeonLog', [])[-2:]))
                for r in runs if r['outcome'] in ('STUCK', 'ERROR', 'WIPES', 'AUTO_OFF')]
    lines += ['', '## Runs that ended early or with an error', '']
    lines += [f'- `{label}` {outcome}: {text}' for label, outcome, text in problems] or ['- none']
    out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(out, len(rows), 'runs', len(by_spec), 'specs')


if __name__ == '__main__':
    main(sys.argv[1:])
