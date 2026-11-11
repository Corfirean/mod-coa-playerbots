import argparse
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import sys
import time

from benchmark import digest, write_json

POPULATIONS = [0, 500, 1000, 1500, 2000, 2500, 3000]
PROFILES = {
    'baseline': {'CoaBots.Scheduling.Enable': 0, 'CoaBots.Budgets.Enable': 0, 'CoaBots.Spells.OptimizeRankLookup': 0,
                 'CoaBots.Spells.LazyCleanseScan': 0, 'World.Performance.DirectRankLookup': 0,
                 'CoaBots.Spells.CacheLearnedRanks': 0,
                 'World.Performance.InterleaveWorldObjects': 0,
                 'CoaBots.Scheduling.FarIdleIntervalMs': 0,
                 'CoaBots.Navigation.ReuseValidatedPath': 0},
    'reuse': {'CoaBots.Scheduling.Enable': 0, 'CoaBots.Navigation.ReuseValidatedPath': 1},
    'schedule': {'CoaBots.Scheduling.Enable': 1, 'CoaBots.Scheduling.Adaptive': 0, 'CoaBots.Budgets.Enable': 0,
                 'CoaBots.Navigation.ReuseValidatedPath': 1},
    'adaptive': {'CoaBots.Scheduling.Enable': 1, 'CoaBots.Scheduling.Adaptive': 1, 'CoaBots.Budgets.Enable': 0,
                 'CoaBots.Navigation.ReuseValidatedPath': 1},
    'budget': {'CoaBots.Scheduling.Enable': 1, 'CoaBots.Scheduling.Adaptive': 1, 'CoaBots.Budgets.Enable': 1,
                'CoaBots.Navigation.ReuseValidatedPath': 1},
}


def cases(suite):
    if suite == 'ranklookup':
        return [dict(profile='budget', population=2000, repeat=repeat, warmup=600, seconds=900,
                     ranklookup=enabled, budgets=[3000, 2000, 8000]) for repeat in [1, 2, 3]
                for enabled in ([0, 1] if repeat % 2 else [1, 0])]
    if suite == 'adaptive':
        return [dict(profile='budget', population=2000, repeat=repeat, warmup=600, seconds=900,
                     adaptive=adaptive, budgets=[3000, 2000, 8000]) for repeat in [1, 2, 3]
                for adaptive in ([0, 1] if repeat % 2 else [1, 0])]
    if suite == 'smoke':
        return [dict(profile=profile, population=n, repeat=1, warmup=30, seconds=60)
                for n in [0, 500, 2000, 3000] for profile in ['baseline', 'budget']]
    if suite in ('baseline', 'final'):
        profile = 'baseline' if suite == 'baseline' else 'budget'
        return [dict(profile=profile, population=n, repeat=repeat, warmup=600, seconds=900)
                for n in POPULATIONS for repeat in [1, 2, 3]]
    if suite == 'stages':
        result = []
        previous = 'baseline'
        for profile in ['reuse', 'schedule', 'adaptive', 'budget']:
            for n in [1000, 2000, 3000]:
                for repeat in ([1] if n == 3000 else [1, 2, 3]):
                    pair = [previous, profile] if repeat % 2 else [profile, previous]
                    result.extend(dict(profile=p, population=n, repeat=repeat, warmup=600, seconds=900) for p in pair)
            previous = profile
        return result
    if suite == 'overhead':
        return [dict(profile='baseline', population=2000, repeat=repeat, warmup=600, seconds=900,
                     instrumentation=mode) for repeat in [1, 2, 3]
                for mode in (['reference', 'full'] if repeat % 2 else ['full', 'reference'])]
    if suite == 'soak':
        return [dict(profile='budget', population=n, repeat=1, warmup=900, seconds=21600) for n in [2000, 3000]]
    if suite == 'stress':
        return [dict(profile='budget', population=n, repeat=repeat, warmup=600, seconds=1200,
                     scenario=scenario) for n in [2000, 3000] for repeat in [1, 2, 3]
                for scenario in ['observer', 'combat', 'dense-combat', 'cluster', 'group-content', 'navigation']]
    if suite == 'budgets':
        return [dict(profile='budget', population=3000, repeat=1, warmup=600, seconds=900,
                     budgets=list(budgets)) for budgets in itertools.product([2000, 4000, 8000], repeat=3)]
    raise ValueError(suite)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--core-source', type=Path, required=True)
    parser.add_argument('--suite', choices=['smoke', 'baseline', 'stages', 'final', 'overhead', 'soak', 'budgets', 'stress', 'adaptive', 'ranklookup'], required=True)
    parser.add_argument('--execute', action='store_true')
    args = parser.parse_args()
    selected = cases(args.suite)
    print(json.dumps(dict(suite=args.suite, cases=selected,
                          measurement_hours=sum(x['warmup'] + x['seconds'] for x in selected) / 3600), indent=2))
    if not args.execute:
        return
    binary_sha = digest(args.binary)
    scripts = {name: digest(Path(__file__).with_name(name)) for name in ['matrix.py', 'benchmark.py']}
    plan_sha = hashlib.sha256(json.dumps(dict(cases=selected, profiles=PROFILES, scripts=scripts), sort_keys=True).encode()).hexdigest()
    series = args.suite + '-' + binary_sha[:8] + '-' + plan_sha[:6]
    suite_dir = args.root / 'suites' / series
    suite_dir.mkdir(parents=True, exist_ok=True)
    write_json(suite_dir / 'plan.json', dict(binary=str(args.binary.resolve()), binary_sha256=binary_sha,
               plan_sha256=plan_sha, scripts=scripts, cases=selected, profiles=PROFILES))
    started = time.monotonic()
    result = []
    for index, case in enumerate(selected):
        label = f"{series}-{index + 1:03}-{case['profile']}-{case['population']}-r{case['repeat']}"
        folder = args.root / 'runs' / label
        if (folder / 'summary.json').is_file() and not (folder / 'failure.json').exists():
            result.append(dict(label=label, state='already_measured'))
            continue
        if folder.exists():
            result.append(dict(label=label, state='needs_review'))
            write_json(suite_dir / 'status.json', dict(state='stopped', cases=result))
            raise RuntimeError('An incomplete run needs review; its evidence is preserved')
        options = {**PROFILES['baseline'], **PROFILES[case['profile']], 'CoaBots.Perf.Detailed': case.get('detailed', 1)}
        if 'adaptive' in case:
            options['CoaBots.Scheduling.Adaptive'] = case['adaptive']
        if 'ranklookup' in case:
            options['CoaBots.Spells.OptimizeRankLookup'] = case['ranklookup']
        if 'budgets' in case:
            options.update(zip(['CoaBots.Budgets.PlannerUs', 'CoaBots.Budgets.ScanUs', 'CoaBots.Budgets.NavigationUs'], case['budgets']))
        options_file = suite_dir / (label + '.json')
        write_json(options_file, options)
        write_json(suite_dir / 'status.json', dict(state='running', current=label, cases=result))
        command = [sys.executable, '-B', str(Path(__file__).with_name('benchmark.py')), 'run',
                   '--root', str(args.root), '--binary', str(args.binary), '--core-source', str(args.core_source),
                   '--label', label, '--population', str(case['population']), '--warmup', str(case['warmup']),
                   '--seconds', str(case['seconds']), '--options', str(options_file), '--restore']
        if 'scenario' in case:
            command.extend(['--scenario', case['scenario']])
        if 'instrumentation' in case:
            command.extend(['--instrumentation', case['instrumentation']])
        if args.suite in ['final', 'soak', 'adaptive', 'ranklookup'] and case['population']:
            command.append('--verify-persistence')
        process = subprocess.run(command)
        result.append(dict(label=label, exit_code=process.returncode, state='measured' if process.returncode == 0 else 'failed'))
        if process.returncode:
            write_json(suite_dir / 'status.json', dict(state='stopped', cases=result))
            raise RuntimeError('A run failed; stop before increasing load')
    write_json(suite_dir / 'status.json', dict(state='completed', seconds=time.monotonic() - started, cases=result))


if __name__ == '__main__':
    main()
