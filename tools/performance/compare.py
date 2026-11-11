import argparse
import json
from pathlib import Path
import statistics

from benchmark import digest, read_json, write_json
from report import evaluate
from windows import delta_histogram


def summarize(root, labels):
    runs, manifests = [], []
    merged = None
    for label in labels:
        folder = root / 'runs' / label
        if not (folder / 'summary.json').exists() or (folder / 'failure.json').exists():
            raise ValueError('Comparison requires a completed, nonfailed run: ' + label)
        runs.append(evaluate(folder))
        if runs[-1]['diagnostic_profile']:
            raise ValueError('Diagnostic CPU profiles cannot be used as comparison runs')
        manifest = read_json((folder / 'manifest.json'))
        manifests.append(manifest)
        initial = read_json((folder / 'measurement-initial.json'))
        final = read_json((folder / 'measurement-final.json'))
        if initial['epoch'] != final['epoch']:
            raise ValueError('Metric epoch differs within run: ' + label)
        before, after = initial['histograms']['world_processing'], final['histograms']['world_processing']
        delta_histogram(before, after)
        if merged is None:
            merged = dict(bins=[0] * len(after['bins']), count=0, sum_us=0)
        merged['bins'] = [total + new - old for total, old, new in zip(merged['bins'], before['bins'], after['bins'], strict=True)]
        merged['count'] += after['count'] - before['count']
        merged['sum_us'] += after['sum_us'] - before['sum_us']
        binaries = folder / 'runtime-binaries.json'
        if not binaries.exists():
            write_json(binaries, {file.name: digest(file) for file in (folder / 'runtime').iterdir()
                                 if file.suffix.lower() in ['.exe', '.dll']})
    exposure = sum(run['population'] * run['seconds'] / 3600 for run in runs)
    rates = {name: sum(run['per_bot_hour'][name] * run['population'] * run['seconds'] / 3600 for run in runs) / exposure
             if exposure and all(run['per_bot_hour'][name] is not None for run in runs) else None
             for name in runs[0]['per_bot_hour']}
    zero = dict(bins=[0] * len(merged['bins']), count=0, sum_us=0)
    return dict(labels=labels, manifests=manifests, population=runs[0]['population'],
                processing=delta_histogram(zero, merged), per_bot_hour=rates,
                run_p95_us=[run['processing']['p95_us'] for run in runs],
                run_p99_us=[run['processing']['p99_us'] for run in runs],
                each_run_timing_passed=all(run['timing_checks_passed'] for run in runs),
                cpu_cores_median=statistics.median(run['cpu_cores'] for run in runs))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--baseline', nargs='+', required=True)
    parser.add_argument('--candidate', nargs='+', required=True)
    parser.add_argument('--name', default='comparison')
    args = parser.parse_args()
    if not args.name or Path(args.name).name != args.name or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in args.name):
        parser.error('Name must contain only letters, numbers, hyphens and underscores')
    baseline, candidate = summarize(args.root, args.baseline), summarize(args.root, args.candidate)
    manifests = baseline['manifests'] + candidate['manifests']
    seed_ids = {manifest.get('seed_manifest_sha256') for manifest in manifests}
    checks = dict(population=len({manifest['population'] for manifest in manifests}) == 1,
                  seed=len(seed_ids) == 1 and None not in seed_ids,
                  scenario=all(manifest.get('scenario') == 'distributed' for manifest in manifests),
                  duration=len({(manifest['warmup'], manifest['seconds']) for manifest in manifests}) == 1,
                  three_repeats=len(args.baseline) >= 3 and len(args.candidate) >= 3)
    behavior = {}
    for name in baseline['per_bot_hour']:
        before, after = baseline['per_bot_hour'][name], candidate['per_bot_hour'][name]
        ratio = after / before if before and after is not None else None
        behavior[name] = dict(baseline=before, candidate=after, ratio=ratio)
    for name in ['bot_kills', 'quests_turned_in', 'objective_progress', 'tasks_completed', 'gathered_items']:
        ratio = behavior[name]['ratio']
        behavior[name]['no_decline_over_ten_percent'] = ratio >= .9 if ratio is not None else None
    ratio = behavior['bot_deaths']['ratio']
    behavior['bot_deaths']['no_increase_over_ten_percent'] = ratio <= 1.1 if ratio is not None else None
    output = dict(baseline=baseline, candidate=candidate, checks=checks, behavior=behavior,
                  acceptance='incomplete', limitations=[
                      'Gathered items count item units; this is an activity proxy, not gather action count.',
                      'Native randomness is uncontrolled; one pair is diagnostic and requires independent repeats.',
                      'Behavior proxies must be reviewed alongside command latency, content coverage and long soaks.',
                      'Pooled quantiles use merged histogram bin counts; no averaging of run p99.'])
    destination = args.root / 'comparisons'
    write_json(destination / (args.name + '.json'), output)
    lines = ['# ' + args.name, '', 'Acceptance remains incomplete.', '',
             '| Metric per bot-hour | Baseline | Candidate | Ratio |', '|---|---:|---:|---:|']
    for name, values in behavior.items():
        ratio_text = f"{values['ratio']:.3f}" if values['ratio'] is not None else 'unmeasured'
        before_text = f"{values['baseline']:.3f}" if values['baseline'] is not None else 'unmeasured'
        after_text = f"{values['candidate']:.3f}" if values['candidate'] is not None else 'unmeasured'
        lines.append(f"| {name} | {before_text} | {after_text} | {ratio_text} |")
    (destination / (args.name + '.md')).write_text('\n'.join(lines) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
