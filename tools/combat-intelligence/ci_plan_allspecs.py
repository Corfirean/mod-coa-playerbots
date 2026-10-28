"""Build a smoke plan with one ST_STATIC episode for every specialization in ClassSpecRoles.cpp.

python -I ci_plan_allspecs.py <label> <actors.json> <out-dir> <credentials> <binary> <revision> [seconds] [scenario]

actors.json maps a class id to the guid of an online dedicated actor of that class: {"12": 106040, "13": 106041, ...}. Before each episode the
actor switches to the spec and is re-equipped by the live gear-up of its level (`train livegear <guid> new`), so every spec runs in the gear it
would have in the game at the training level. The plan only lists what to measure; ci_plan_run.py executes it.
"""
import json
import re
import sys
from pathlib import Path

ROLES = Path(__file__).resolve().parents[2] / 'module/src/ClassSpecRoles.cpp'


def specs():
    text = ROLES.read_text(encoding='utf-8')
    for m in re.finditer(r'\{\s*(\d+),\s*(\d+),\s*BotRole::(\w+),\s*"([^"]+)"\s*\}', text):
        cls, spec = int(m.group(1)), int(m.group(2))
        if cls >= 12:
            yield cls, spec, m.group(3), m.group(4)


def main(argv):
    label, actors_file, out, credentials, binary, revision = argv[:6]
    seconds = int(argv[6]) if len(argv) > 6 else 30
    scenario = argv[7] if len(argv) > 7 else 'ST_STATIC'
    actors = json.load(open(actors_file, encoding='utf-8'))
    episodes = []
    for cls, spec, role, name in specs():
        guid = actors.get(str(cls))
        if guid is None:
            continue
        episodes.append({'scenario': scenario, 'candidate': guid, 'seconds': seconds, 'seeds': [1001],
                         'name': f'spec{spec:03d}-{name.replace(" ", "")}-{role}-class{cls}',
                         'before': [f'botcmd learnspec {guid} {spec}', f'botcmd train livegear {guid} new']})
    plan = {'label': label, 'out': out, 'credentials': credentials, 'ra_port': 3469, 'binary': binary, 'git_revision': revision,
            'keep_traces': False, 'spawn': sorted(set(actors.values())), 'episodes': episodes}
    Path(f'ci-plan-{label}.json').write_text(json.dumps(plan, indent=1), encoding='utf-8')
    print(f'{len(episodes)} episodes -> ci-plan-{label}.json')


if __name__ == '__main__':
    main(sys.argv[1:])
