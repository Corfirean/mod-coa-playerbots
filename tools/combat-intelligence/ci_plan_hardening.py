"""Plans for the post-review hardening experiments (paired runs, several seeds each).

python -I ci_plan_hardening.py <experiment> <label> <out-dir> <credentials> <binary> <revision> [--seeds 1001 1002 ...] [--out-plan path] [--setup]

--setup writes the one-time plan that gives the actors their specs and live gear; without it the plan has no setup commands (run the setup first).

experiment:
  interrupt   INTERRUPT_MULTI (two enemies cast together every 10 s): five dps from classes that own a real interrupt spell
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


def strip_setup(eps):
    """The experiment itself runs with the specs and gear a setup run has put on the bots: `train livegear` rolls a new item set every time, so
    repeating it before every seed (or in the second arm) would make the arms different experiments (ci_compare rejects such pairs)."""
    return [{k: v for k, v in e.items() if k != 'before'} for e in eps]


def setup_episodes(eps):
    """One 5 s ST_STATIC episode per (actor, spec) that runs the setup commands once."""
    done, out = set(), []
    for e in eps:
        for cmd in e.get('before', []):
            if cmd not in done:
                done.add(cmd)
        guids = {int(c.split()[2]) for c in e.get('before', []) if c.startswith('botcmd learnspec')}
        for g in sorted(guids):
            cmds = [c for c in e['before'] if f' {g} ' in c + ' ']
            if not any(o['candidate'] == g and o['before'] == cmds for o in out):
                out.append({'scenario': 'ST_STATIC', 'candidate': g, 'seconds': 5, 'seeds': [1001], 'name': f'setup-{g}', 'before': cmds})
    return out


def episodes(experiment, seeds):
    t, h, d1, d2, w = (TEAM[k] for k in ('tank', 'healer', 'dps1', 'dps2', 'support'))
    if experiment == 'interrupt':
        # Five dps from classes whose spellbook has a real interrupt effect (Spell.dbc effect 68): Gust of Wind, Shield of Denial, Spellburn,
        # Nullifying Toxin, Ley Lock. Two enemies cast together every 10 s, so the group has to share its kicks.
        roster = [(106044, 14), (106046, 19), (106052, 38), (106057, 53), (106060, 61)]
        candidate, party = roster[0][0], [g for g, _ in roster[1:]]
        prep = [cmd for g, spec in roster for cmd in before(g, spec)]
        return [{'scenario': 'INTERRUPT_MULTI', 'candidate': candidate, 'party': party, 'seconds': 90, 'seeds': seeds,
                 'name': 'INTERRUPT_MULTI', 'before': prep}]
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
    eps = setup_episodes(eps) if '--setup' in argv else strip_setup(eps)
    plan = {'label': label, 'out': out, 'credentials': credentials, 'ra_port': 3469, 'binary': binary, 'git_revision': revision,
            'keep_traces': True, 'episodes': eps,
            'spawn': sorted(set(TEAM.values()) | {g for e in eps for g in [e['candidate']] + e.get('party', [])})}
    path = Path(argv[argv.index('--out-plan') + 1]) if '--out-plan' in argv else Path(f'ci-plan-{label}.json')
    path.write_text(json.dumps(plan, indent=1), encoding='utf-8')
    print(len(eps), 'episode groups x', len(seeds), 'seeds ->', path)


if __name__ == '__main__':
    main(sys.argv[1:])
