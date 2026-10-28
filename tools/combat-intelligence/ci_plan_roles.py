"""Build a role-wide plan: every tank, healer and support spec on its own role scenario.

python -I ci_plan_roles.py <label> <actors.json> <out-dir> <credentials> <binary> <revision> <parties.json>

Tanks: THREAT_STRESS and TANKBUSTER_TELEGRAPH (60 s). Healers: HEAL_RAMP and HEAL_POOL (60 s). Supports: SUPPORT_GROUP (60 s). parties.json maps a role to the four
dedicated bots that fill the group around the candidate: {"Tank": [healer, dps, dps, support], "Healer": [tank, dps, dps, support], "Support": [tank, healer, dps, dps]}. One seed each: a first look
at every spec of the role; repeat suspicious specs with more seeds before changing anything.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ci_plan_allspecs import specs  # noqa: E402

SCENARIOS = {'Tank': ['THREAT_STRESS', 'TANKBUSTER_TELEGRAPH'], 'Healer': ['HEAL_RAMP', 'HEAL_POOL'], 'Support': ['SUPPORT_GROUP']}


def main(argv):
    label, actors_file, out, credentials, binary, revision = argv[:6]
    parties = json.load(open(argv[6], encoding='utf-8'))
    actors = json.load(open(actors_file, encoding='utf-8'))
    episodes = []
    for cls, spec, role, name in specs():
        guid = actors.get(str(cls))
        party = parties.get(role)
        if guid is None or role not in SCENARIOS or not party or guid in party:
            continue
        for scenario in SCENARIOS[role]:
            episodes.append({'scenario': scenario, 'candidate': guid, 'party': party, 'seconds': 60, 'seeds': [1001],
                             'name': f'{scenario}-spec{spec:03d}-{name.replace(" ", "")}',
                             'before': [f'botcmd learnspec {guid} {spec}', f'botcmd train livegear {guid} new']})
    plan = {'label': label, 'out': out, 'credentials': credentials, 'ra_port': 3469, 'binary': binary, 'git_revision': revision,
            'keep_traces': True, 'spawn': sorted({g for p in parties.values() for g in p} | {e['candidate'] for e in episodes}), 'episodes': episodes}
    Path(f'ci-plan-{label}.json').write_text(json.dumps(plan, indent=1), encoding='utf-8')
    print(len(episodes), 'episodes')


if __name__ == '__main__':
    main(sys.argv[1:])
