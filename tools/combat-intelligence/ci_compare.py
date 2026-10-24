"""Paired comparison of two result folders written by ci_plan_run.py.

python -I ci_compare.py <baseline-dir> <candidate-dir> [--only PREFIX ...] [--metric dotted.path ...]

--only keeps episodes whose folder name starts with one of the prefixes (e.g. HEAL_RAMP-106036), so each role is compared on its own.

Episodes are paired by (scenario, candidate, seed). For each metric: mean, median, stdev of both sides, mean paired difference, relative change,
a normal-approximation 95% interval of the paired difference and the number of pairs that got worse. It computes statistics only; whether a change
is accepted is decided by the analyst, and a metric that an episode did not record is reported as `unknown`, never as 0.
"""
import json
import math
import statistics
import sys
from pathlib import Path

DEFAULT_METRICS = [
    'combat.dps', 'combat.hps', 'combat.damage', 'combat.damageTaken', 'combat.deaths', 'combat.effectiveHealing',
    'combat.castingMs', 'combat.noTargetIdleMs', 'combat.movingMs',
    'healing.oomMs', 'healing.criticalPersonSeconds',
    'threat.aggroLosses', 'threat.aggroMs',
    'mechanics.avoidableDamage', 'mechanics.avoidableHits',
]
# For these, a larger number is worse.
LOWER_IS_BETTER = {'combat.damageTaken', 'combat.deaths', 'combat.noTargetIdleMs', 'combat.movingMs', 'healing.oomMs',
                   'healing.criticalPersonSeconds', 'threat.aggroLosses', 'mechanics.avoidableDamage', 'mechanics.avoidableHits'}


def load(folder):
    runs = {}
    for d in Path(folder).iterdir():
        if d.is_dir() and (d / 'summary.json').exists():
            runs[d.name] = json.load(open(d / 'summary.json', encoding='utf-8'))
    return runs


def get(summary, dotted):
    node = summary
    for part in dotted.split('.'):
        if not isinstance(node, dict) or part not in node:
            return None
        node = node[part]
    return node if isinstance(node, (int, float)) and not isinstance(node, bool) else None


def describe(values):
    if not values:
        return 'unknown'
    sd = statistics.stdev(values) if len(values) > 1 else 0.0
    return f'mean {statistics.mean(values):.3f} median {statistics.median(values):.3f} sd {sd:.3f} n={len(values)}'


def main(argv):
    metrics = DEFAULT_METRICS
    if '--metric' in argv:
        i = argv.index('--metric')
        metrics = argv[i + 1:]
        argv = argv[:i]
    only = []
    if '--only' in argv:
        i = argv.index('--only')
        only = argv[i + 1:]
        argv = argv[:i]
    base, cand = load(argv[0]), load(argv[1])
    keys = sorted(k for k in set(base) & set(cand) if not only or any(k.startswith(p) for p in only))
    print(f'paired episodes: {len(keys)} (baseline only: {len(set(base) - set(cand))}, candidate only: {len(set(cand) - set(base))})')
    for metric in metrics:
        pairs = [(get(base[k], metric), get(cand[k], metric)) for k in keys]
        pairs = [(a, b) for a, b in pairs if a is not None and b is not None]
        if not pairs:
            print(f'{metric}: unknown (not recorded in these runs)')
            continue
        a_vals, b_vals = [p[0] for p in pairs], [p[1] for p in pairs]
        diffs = [b - a for a, b in pairs]
        mean_diff = statistics.mean(diffs)
        sd = statistics.stdev(diffs) if len(diffs) > 1 else 0.0
        half = 1.96 * sd / math.sqrt(len(diffs)) if len(diffs) > 1 else float('nan')
        base_mean = statistics.mean(a_vals)
        rel = (mean_diff / base_mean * 100.0) if base_mean else float('nan')
        worse = sum(1 for d in diffs if (d > 0 if metric in LOWER_IS_BETTER else d < 0))
        print(f'{metric}:\n  baseline  {describe(a_vals)}\n  candidate {describe(b_vals)}\n'
              f'  paired difference {mean_diff:+.3f} (95% interval +-{half:.3f}), relative {rel:+.1f}%, worse in {worse}/{len(diffs)} pairs')


if __name__ == '__main__':
    main(sys.argv[1:])
