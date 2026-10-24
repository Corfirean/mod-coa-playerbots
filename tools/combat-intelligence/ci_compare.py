"""Paired comparison of two result folders written by ci_plan_run.py.

python -I ci_compare.py <baseline-dir> <candidate-dir> [--only PREFIX ...] [--metric dotted.path ...] [--min-pairs N]
                        [--allow-config-diff KEY ...] [--allow-core-diff] [--json out.json]

Episodes are paired by folder name (scenario, spec, seed). A pair is used only if both sides really are the same experiment (VALID_PAIR):
same scenario, seed, requested duration, class/spec/role, party composition (class, spec, role, level of every member) and gear of every member.
The module revision and the binary are expected to differ and are not compared. When both manifests record them, the core revision and the
CoaBots.* settings must match too, except settings named with --allow-config-diff (the one deliberately varied in a config A/B run) and
--allow-core-diff. Anything that cannot be checked (older result folders) is reported as unknown, never as a match.

Per group (class, spec, scenario) and metric the tool prints mean, median and sd of both sides, the paired difference with a 95% Student's t
interval, an exact sign-flip permutation p-value (small samples, no normality assumption), and a state:

  MISSING_METRIC        fewer than two valid pairs recorded the metric
  INSUFFICIENT_EVIDENCE fewer than --min-pairs valid pairs (default 6: with fewer, an exact paired test cannot reach 5% at all)
  INCONCLUSIVE          the interval contains 0, or the permutation test disagrees, or the median points the other way (outlier)
  IMPROVEMENT / REGRESSION   interval excludes 0 and the exact test agrees (n <= 16), in the metric's good / bad direction

It only computes statistics. Whether a change is accepted, and what the next rotation edit is, is decided by the analyst. Metrics matter
differently per role (PRIMARY_BY_ROLE); a role verdict is printed for the primary ones only. About 5% of metrics will show a "significant"
difference by chance alone, so a single flagged secondary metric is a lead, not a finding.
"""
import argparse
import itertools
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
# Counts with no good direction (how many kicks were spent says nothing about whether the result was better): a difference is reported but never
# as IMPROVEMENT or REGRESSION.
NEUTRAL_METRICS = {'mechanics.interruptKicksAttempted', 'mechanics.interruptKicksAccepted', 'mechanics.interruptCasts'}
# What decides whether a change helped, per role. The rest are context.
PRIMARY_BY_ROLE = {
    'Tank': ['threat.aggroLosses', 'threat.aggroMs', 'combat.deaths', 'combat.damageTaken', 'mechanics.avoidableDamage'],
    'Healer': ['combat.deaths', 'healing.criticalPersonSeconds', 'combat.effectiveHealing', 'healing.oomMs'],
    'Dps': ['combat.dps', 'combat.deaths', 'mechanics.avoidableHits', 'combat.noTargetIdleMs'],
    'Support': ['combat.deaths', 'mechanics.avoidableHits'],
}

MIN_PAIRS_DEFAULT = 6
EXACT_PERMUTATION_MAX_N = 16

# Two-sided 95% critical values of Student's t by degrees of freedom; between tabulated points the next lower df is used (wider, conservative).
T_975 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228, 11: 2.201, 12: 2.179,
         13: 2.160, 14: 2.145, 15: 2.131, 16: 2.120, 17: 2.110, 18: 2.101, 19: 2.093, 20: 2.086, 21: 2.080, 22: 2.074, 23: 2.069,
         24: 2.064, 25: 2.060, 26: 2.056, 27: 2.052, 28: 2.048, 29: 2.045, 30: 2.042, 40: 2.021, 60: 2.000, 120: 1.980}


def t_critical(df):
    if df < 1:
        return float('nan')
    for key in sorted(T_975, reverse=True):
        if df >= key:
            return T_975[key]
    return T_975[1]


def load(folder):
    runs = {}
    for d in Path(folder).iterdir():
        if d.is_dir() and (d / 'summary.json').exists():
            runs[d.name] = json.load(open(d / 'summary.json', encoding='utf-8'))
    manifest = {}
    if (Path(folder) / 'manifest.json').exists():
        manifest = json.load(open(Path(folder) / 'manifest.json', encoding='utf-8'))
    return runs, manifest


def get(summary, dotted):
    node = summary
    for part in dotted.split('.'):
        if not isinstance(node, dict) or part not in node:
            return None
        node = node[part]
    return node if isinstance(node, (int, float)) and not isinstance(node, bool) else None


def gear_signature(member):
    return tuple(sorted((g.get('slot'), g.get('entry'), g.get('randomProperty'), tuple(g.get('enchants') or ())) for g in member.get('gear', [])))


def roster_of(summary):
    return (summary.get('normalization') or {}).get('roster')


def party_signature(summary):
    roster = roster_of(summary)
    if roster is None:
        return None
    return tuple(sorted((m.get('classId'), m.get('specId'), m.get('role'), m.get('level')) for m in roster))


def gear_signatures(summary):
    roster = roster_of(summary)
    if roster is None:
        return None
    return sorted((m.get('classId'), m.get('specId'), m.get('role'), gear_signature(m)) for m in roster)


def check_pair(base, cand):
    """Returns (state, reasons, unknowns): the two summaries are the same experiment or not."""
    reasons, unknowns = [], []
    for field in ('scenario', 'seed', 'requestedDurationMs', 'classId', 'specId', 'role', 'schemaVersion'):
        a, b = base.get(field), cand.get(field)
        if a is None or b is None:
            if a is not b:
                unknowns.append(field)
        elif a != b:
            reasons.append(f'{field} differs ({a} vs {b})')
    pa, pb = party_signature(base), party_signature(cand)
    if pa is None or pb is None:
        unknowns.append('party composition')
    elif pa != pb:
        reasons.append('party composition differs')
    ga, gb = gear_signatures(base), gear_signatures(cand)
    if ga is None or gb is None:
        unknowns.append('gear')
    elif ga != gb:
        reasons.append('gear/loadout differs')
    return ('INVALID_PAIR' if reasons else 'VALID_PAIR'), reasons, unknowns


def check_environment(base_manifest, cand_manifest, allow_config, allow_core):
    """Run-level checks from the manifests. Returns (reasons, unknowns)."""
    reasons, unknowns = [], []
    for field, allowed in (('core_revision', allow_core),):
        a, b = base_manifest.get(field), cand_manifest.get(field)
        if a is None or b is None:
            unknowns.append(field)
        elif a != b and not allowed:
            reasons.append(f'{field} differs ({a} vs {b})')
    ca, cb = base_manifest.get('config'), cand_manifest.get('config')
    if ca is None or cb is None:
        unknowns.append('config')
    else:
        for key in sorted(set(ca) | set(cb)):
            if ca.get(key) != cb.get(key) and key not in allow_config:
                reasons.append(f'config {key} differs ({ca.get(key)} vs {cb.get(key)})')
    return reasons, unknowns


def permutation_p(diffs):
    """Exact two-sided sign-flip p-value of the mean paired difference (no distribution assumption). None above EXACT_PERMUTATION_MAX_N."""
    n = len(diffs)
    if n == 0 or n > EXACT_PERMUTATION_MAX_N:
        return None
    observed = abs(sum(diffs))
    hits = 0
    for signs in itertools.product((1, -1), repeat=n):
        if abs(sum(s * d for s, d in zip(signs, diffs))) >= observed - 1e-12:
            hits += 1
    return hits / float(2 ** n)


def analyse(metric, pairs, min_pairs):
    """pairs: list of (baseline value, candidate value). Returns a dict with the statistics and the state."""
    out = {'metric': metric, 'n': len(pairs)}
    if len(pairs) < 2:
        out['state'] = 'MISSING_METRIC'
        return out
    a_vals, b_vals = [p[0] for p in pairs], [p[1] for p in pairs]
    diffs = [b - a for a, b in pairs]
    n = len(diffs)
    mean_diff = statistics.mean(diffs)
    sd = statistics.stdev(diffs)
    half = t_critical(n - 1) * sd / math.sqrt(n)
    base_mean = statistics.mean(a_vals)
    lower_better = metric in LOWER_IS_BETTER
    out.update({
        'baseline': {'mean': statistics.mean(a_vals), 'median': statistics.median(a_vals), 'sd': statistics.stdev(a_vals)},
        'candidate': {'mean': statistics.mean(b_vals), 'median': statistics.median(b_vals), 'sd': statistics.stdev(b_vals)},
        'meanDiff': mean_diff, 'medianDiff': statistics.median(diffs), 'ci95Half': half,
        'relativePct': (mean_diff / base_mean * 100.0) if base_mean else None,   # None when the baseline mean is 0
        'worseIn': sum(1 for d in diffs if (d > 0 if lower_better else d < 0)),
        'permutationP': permutation_p(diffs),
        'lowerIsBetter': lower_better,
    })
    if n < min_pairs:
        out['state'] = 'INSUFFICIENT_EVIDENCE'
        return out
    excludes_zero = abs(mean_diff) > half
    perm = out['permutationP']
    if not excludes_zero or (perm is not None and perm >= 0.05):
        out['state'] = 'INCONCLUSIVE'
        return out
    median = out['medianDiff']
    if median != 0 and (median > 0) != (mean_diff > 0):
        out['state'] = 'INCONCLUSIVE'
        out['note'] = 'mean and median point in different directions (outlier-driven)'
        return out
    if metric in NEUTRAL_METRICS:
        out['state'] = 'INCONCLUSIVE'
        out['note'] = 'significant change, but this count has no good direction'
        return out
    better = (mean_diff < 0) if lower_better else (mean_diff > 0)
    out['state'] = 'IMPROVEMENT' if better else 'REGRESSION'
    return out


def compare(base_runs, cand_runs, metrics=None, only=(), min_pairs=MIN_PAIRS_DEFAULT, base_manifest=None, cand_manifest=None,
            allow_config=(), allow_core=False):
    metrics = metrics or DEFAULT_METRICS
    env_reasons, env_unknowns = check_environment(base_manifest or {}, cand_manifest or {}, set(allow_config), allow_core)
    keys = sorted(k for k in set(base_runs) & set(cand_runs) if not only or any(k.startswith(p) for p in only))
    result = {'environment': {'reasons': env_reasons, 'unknown': env_unknowns},
              'baselineOnly': len(set(base_runs) - set(cand_runs)), 'candidateOnly': len(set(cand_runs) - set(base_runs)),
              'invalidPairs': [], 'groups': {}}
    valid = []
    for k in keys:
        state, reasons, unknowns = check_pair(base_runs[k], cand_runs[k])
        if env_reasons:
            state, reasons = 'INVALID_PAIR', reasons + env_reasons
        if state == 'INVALID_PAIR':
            result['invalidPairs'].append({'pair': k, 'reasons': reasons})
        else:
            valid.append((k, unknowns))
    result['validPairs'] = len(valid)
    result['unknownFields'] = sorted({u for _, unk in valid for u in unk} | set(env_unknowns))
    groups = {}
    for k, _ in valid:
        s = cand_runs[k]
        groups.setdefault((s.get('classId'), s.get('specId'), s.get('role'), s.get('scenario')), []).append(k)
    for gkey, gkeys in sorted(groups.items(), key=lambda kv: str(kv[0])):
        role = gkey[2]
        primary = set(PRIMARY_BY_ROLE.get(role, []))
        rows = []
        for metric in metrics:
            pairs = [(get(base_runs[k], metric), get(cand_runs[k], metric)) for k in gkeys]
            pairs = [(a, b) for a, b in pairs if a is not None and b is not None]
            row = analyse(metric, pairs, min_pairs)
            row['primary'] = metric in primary
            rows.append(row)
        prim = [r for r in rows if r['primary']]
        if any(r['state'] == 'REGRESSION' for r in prim):
            verdict = 'REGRESSION'
        elif any(r['state'] == 'IMPROVEMENT' for r in prim):
            verdict = 'IMPROVEMENT'
        elif prim and all(r['state'] in ('MISSING_METRIC', 'INSUFFICIENT_EVIDENCE') for r in prim):
            verdict = 'INSUFFICIENT_EVIDENCE'
        else:
            verdict = 'INCONCLUSIVE'
        result['groups']['/'.join(str(x) for x in gkey)] = {'pairs': len(gkeys), 'roleVerdict': verdict, 'metrics': rows}
    return result


def fmt(x, digits=3):
    return 'n/a' if x is None else f'{x:.{digits}f}'


def print_report(result):
    print(f"valid pairs: {result['validPairs']}, invalid: {len(result['invalidPairs'])}, "
          f"baseline only: {result['baselineOnly']}, candidate only: {result['candidateOnly']}")
    if result['unknownFields']:
        print('could not be verified (older result folders): ' + ', '.join(result['unknownFields']))
    for reason in result['environment']['reasons']:
        print('INVALID_PAIR (run level): ' + reason)
    for item in result['invalidPairs']:
        print(f"INVALID_PAIR {item['pair']}: " + '; '.join(item['reasons']))
    for name, group in result['groups'].items():
        print(f"\n== {name}  pairs={group['pairs']}  role verdict (primary metrics only): {group['roleVerdict']}")
        for r in group['metrics']:
            tag = '*' if r['primary'] else ' '
            if r['state'] == 'MISSING_METRIC':
                print(f" {tag} {r['metric']}: MISSING_METRIC (recorded in {r['n']} valid pair(s))")
                continue
            p = r['permutationP']
            print(f" {tag} {r['metric']}: {r['state']}  n={r['n']}  baseline mean {fmt(r['baseline']['mean'])} (median {fmt(r['baseline']['median'])}, "
                  f"sd {fmt(r['baseline']['sd'])})  candidate mean {fmt(r['candidate']['mean'])} (median {fmt(r['candidate']['median'])}, "
                  f"sd {fmt(r['candidate']['sd'])})\n     paired difference {r['meanDiff']:+.3f} (95% t interval +-{r['ci95Half']:.3f}), "
                  f"relative {'n/a' if r['relativePct'] is None else format(r['relativePct'], '.1f') + '%'}, exact p {fmt(p)}, worse in {r['worseIn']}/{r['n']}"
                  + (f"  [{r['note']}]" if r.get('note') else ''))
    print('\n* = primary metric for the role. Statistics only: the analyst decides.')


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('baseline')
    parser.add_argument('candidate')
    parser.add_argument('--only', nargs='*', default=[])
    parser.add_argument('--metric', nargs='*', default=None)
    parser.add_argument('--min-pairs', type=int, default=MIN_PAIRS_DEFAULT)
    parser.add_argument('--allow-config-diff', nargs='*', default=[])
    parser.add_argument('--allow-core-diff', action='store_true')
    parser.add_argument('--json')
    args = parser.parse_args(argv)
    base_runs, base_manifest = load(args.baseline)
    cand_runs, cand_manifest = load(args.candidate)
    result = compare(base_runs, cand_runs, args.metric, args.only, args.min_pairs, base_manifest, cand_manifest,
                     args.allow_config_diff, args.allow_core_diff)
    print_report(result)
    if args.json:
        Path(args.json).write_text(json.dumps(result, indent=1), encoding='utf-8')


if __name__ == '__main__':
    main(sys.argv[1:])
