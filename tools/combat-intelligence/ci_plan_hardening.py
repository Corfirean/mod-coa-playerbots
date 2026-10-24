"""Plans for the post-review hardening experiments (paired runs, several seeds each).

python -I ci_plan_hardening.py <experiment> <label> <out-dir> <credentials> <binary> <revision> [--seeds 1001 1002 ...] [--out-plan path]

experiment:
  interrupt   INTERRUPT_MULTI (two enemies cast together every 10 s): a dps actor with a kick, party tank/healer/dps/support
  healing     HEAL_RAMP, HEAL_POOL and GROUP_STRESS for two healer specs (Time 31, Flameweaving 37)
  buster      TANKBUSTER_PCT for the Fortitude tank (the hit size is set by CoaBots.Training.TankbusterPercent in the module config)
  threat      THREAT_STRESS for the three tanks that lose aggro most often (Domination 57, Black Knight 97, Moon Guard 100)

The bots are the sandbox's dedicated actors (see ci_plan_roles.py): 106035 tank, 106036 healer, 106037/106038 dps, 106039 support for the
fixed team; one actor per class for the spec under test. Specs are learned with `botcmd learnspec` and live-gear is given before every seed.
"""
import json
import sys
from pathlib import Path

TEAM = {'tank': 106035, 'healer': 106036, 'dps1': 106037, 'dps2': 106038, 'support': 106039}
ACTOR = {31: 106050, 37: 106052, 52: 106057, 57: 106058, 97: 106043, 100: 106054}


def before(guid, spec):
    return [f'botcmd learnspec {guid} {spec}', f'botcmd train livegear {guid} new']


def episodes(experiment, seeds):
    t, h, d1, d2, w = (TEAM[k] for k in ('tank', 'healer', 'dps1', 'dps2', 'support'))
    if experiment == 'interrupt':
        return [{'scenario': 'INTERRUPT_MULTI', 'candidate': d1, 'party': [t, h, d2, w], 'seconds': 90, 'seeds': seeds, 'name': 'INTERRUPT_MULTI'}]
    if experiment == 'healing':
        eps = []
        for spec in (31, 37):
            guid = ACTOR[spec]
            for scenario in ('HEAL_RAMP', 'HEAL_POOL', 'GROUP_STRESS'):
                eps.append({'scenario': scenario, 'candidate': guid, 'party': [t, d1, d2, w], 'seconds': 60, 'seeds': seeds,
                            'name': f'{scenario}-spec{spec:03d}', 'before': before(guid, spec)})
        return eps
    if experiment == 'buster':
        guid = ACTOR[52]
        return [{'scenario': 'TANKBUSTER_PCT', 'candidate': guid, 'party': [h, d1, d2, w], 'seconds': 60, 'seeds': seeds,
                 'name': 'TANKBUSTER_PCT-spec052', 'before': before(guid, 52)}]
    if experiment == 'threat':
        eps = []
        for spec in (57, 97, 100):
            guid = ACTOR[spec]
            eps.append({'scenario': 'THREAT_STRESS', 'candidate': guid, 'party': [h, d1, d2, w], 'seconds': 60, 'seeds': seeds,
                        'name': f'THREAT_STRESS-spec{spec:03d}', 'before': before(guid, spec)})
        return eps
    raise SystemExit(f'unknown experiment {experiment}')


def main(argv):
    experiment, label, out, credentials, binary, revision = argv[:6]
    seeds = [1001, 1002, 1003, 1004, 1005, 1006]
    if '--seeds' in argv:
        i = argv.index('--seeds')
        seeds = [int(x) for x in argv[i + 1:] if x.isdigit()]
    eps = episodes(experiment, seeds)
    plan = {'label': label, 'out': out, 'credentials': credentials, 'ra_port': 3469, 'binary': binary, 'git_revision': revision,
            'keep_traces': True, 'spawn': sorted(set(TEAM.values()) | {e['candidate'] for e in eps}), 'episodes': eps}
    path = Path(argv[argv.index('--out-plan') + 1]) if '--out-plan' in argv else Path(f'ci-plan-{label}.json')
    path.write_text(json.dumps(plan, indent=1), encoding='utf-8')
    print(len(eps), 'episode groups x', len(seeds), 'seeds ->', path)


if __name__ == '__main__':
    main(sys.argv[1:])
