"""Build an arena plan with one episode per Warcraft Reborn specialization (the stock classes) and scenario.

python -I ci_plan_reborn.py <label> <actors.json> <out-dir> <credentials> <binary> <revision> [seconds] [scenarios]

actors.json maps a stock class id to the guid of an online dedicated bot of that class: {"1": 101, "2": 102, "3": 103, ...} (nine bots,
level 60 or any: the arena raises them to CoaBots.Training.DefaultLevel). Before each episode `botcmd learnspec` sets the spec (for a Reborn
bot: the spec number, the Reborn setup and the spec's talent build, with the gold the respec costs) and `botcmd train livegear <guid> new`
equips it the way the live gear-up would. `scenarios` is a comma list, default ST_STATIC,AOE,SURVIVAL,RESOURCE_ENDURANCE; tanks also get
THREAT_STRESS and healers SUPPORT_GROUP. The plan only lists what to measure; ci_plan_run.py executes it, and the numbers to read in
summary.json are in docs/reborn-bot-testing.md.
"""
import json
import re
import sys
from pathlib import Path

ROLES = Path(__file__).resolve().parents[2] / 'module/src/ClassSpecRoles.cpp'


def stock_specs():
    text = ROLES.read_text(encoding='utf-8')
    for m in re.finditer(r'\{\s*(\d+),\s*(\d+),\s*BotRole::(\w+),\s*"([^"]+)"\s*\}', text):
        cls, spec = int(m.group(1)), int(m.group(2))
        if cls in (1, 2, 3, 4, 5, 7, 8, 9, 11):
            yield cls, spec, m.group(3), m.group(4)


def main(argv):
    label, actors_file, out, credentials, binary, revision = argv[:6]
    seconds = int(argv[6]) if len(argv) > 6 else 60
    scenarios = (argv[7] if len(argv) > 7 else 'ST_STATIC,AOE,SURVIVAL,RESOURCE_ENDURANCE').split(',')
    actors = json.load(open(actors_file, encoding='utf-8'))
    episodes = []
    for cls, spec, role, name in stock_specs():
        guid = actors.get(str(cls))
        if guid is None:
            continue
        wanted = list(scenarios)
        if role == 'Tank' and 'THREAT_STRESS' not in wanted:
            wanted.append('THREAT_STRESS')
        if role == 'Healer' and 'SUPPORT_GROUP' not in wanted:
            wanted.append('SUPPORT_GROUP')
        for scenario in wanted:
            episodes.append({'scenario': scenario, 'candidate': guid, 'seconds': seconds, 'seeds': [1001],
                             'name': f'class{cls:02d}-spec{spec}-{name.replace(" ", "")}-{role}-{scenario}',
                             'before': [f'botcmd learnspec {guid} {spec}', f'botcmd train livegear {guid} new']})
    plan = {'label': label, 'out': out, 'credentials': credentials, 'ra_port': 3469, 'binary': binary, 'git_revision': revision,
            'keep_traces': True, 'spawn': sorted(set(actors.values())), 'episodes': episodes}
    Path(f'ci-plan-{label}.json').write_text(json.dumps(plan, indent=1), encoding='utf-8')
    print(f'{len(episodes)} episodes -> ci-plan-{label}.json')


if __name__ == '__main__':
    main(sys.argv[1:])
