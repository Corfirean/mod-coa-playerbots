"""Export measured run comparisons; repeat ranges are not pooled histogram quantiles."""
import argparse
import json
from pathlib import Path
import statistics

from benchmark import read_json

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    args = parser.parse_args()
    report = read_json((args.root / 'report.json'))
    groups = {}
    for row in report['runs']:
        if row['scenario'] != 'distributed' or row['run_status'] == 'failed' or row['diagnostic_profile'] or row['instrumentation'] == 'reference' or not row['processing']['count']:
            continue
        options = row['options']
        profile = 'budget' if options.get('CoaBots.Budgets.Enable') else 'adaptive' if options.get('CoaBots.Scheduling.Adaptive') else 'schedule' if options.get('CoaBots.Scheduling.Enable') else 'reuse' if options.get('CoaBots.Navigation.ReuseValidatedPath') else 'baseline'
        source = (row.get('source') or {}).get('sha', 'unknown')[:8]
        key = source + ' / ' + row['binary_sha256'][:8] + ' / ' + profile
        samples = [json.loads(line) for line in (args.root / 'runs' / row['label'] / 'samples.jsonl').read_text(encoding='utf-8-sig').splitlines()]
        private = statistics.median(sample['world']['private_bytes'] for sample in samples) / 1024 ** 3 if samples else 0
        oldest = 0
        epoch = read_json((args.root / 'runs' / row['label'] / 'measurement-final.json'))['epoch']
        for file in (args.root / 'runs' / row['label'] / 'metrics').glob('metrics-*.json'):
            snapshot = read_json(file)
            if snapshot['epoch'] == epoch:
                oldest = max(oldest, *(snapshot['gauges'].get(name + '_oldest_ms', 0) / 1000 for name in ['planner', 'scan', 'navigation']))
        values = [row['processing']['p95_us'] / 1000, row['processing']['p99_us'] / 1000,
                  row['cpu_cores'], private, oldest]
        groups.setdefault(key, {}).setdefault(row['population'], []).append(values)
    figure, axes = plt.subplots(3, 2, figsize=(12, 11), layout='constrained')
    labels = ['Tick processing p95 (ms)', 'Tick processing p99 (ms)', 'World CPU (equivalent cores)',
              'Median world private memory (GiB)', 'Maximum observed work queue age (s)']
    for index, axis in enumerate(axes.flat):
        if index >= len(labels):
            axis.axis('off')
            axis.text(0, .9, 'Each point: median of independent run results.\nBars: minimum–maximum across repeats.\n\nRun quantiles use cumulative histograms.\nRepeat medians are not pooled quantiles.\n\nDiagnostic probes are included.\nSee manifests for duration and configuration.\nCapacity qualification remains incomplete.', va='top', wrap=True)
            continue
        for name, populations in groups.items():
            x = sorted(populations)
            values = [[row[index] for row in populations[n] if row[index] is not None] for n in x]
            y = [statistics.median(value) if value else 0 for value in values]
            errors = [[y[i] - min(value, default=0) for i, value in enumerate(values)],
                      [max(value, default=0) - y[i] for i, value in enumerate(values)]]
            axis.errorbar(x, y, yerr=errors, marker='o', capsize=3, label=name)
        if index in [0, 1]:
            axis.plot([2000, 3000], [50, 75] if index == 0 else [100, 150], ':k', label='Acceptance threshold')
        axis.set(title=labels[index], xlabel='Bots')
        axis.grid(alpha=.25)
        axis.legend(fontsize=7)
    destination = args.root / 'plots'
    destination.mkdir(exist_ok=True)
    figure.suptitle('Measured bot population experiments — qualification incomplete')
    for extension in ['png', 'svg']:
        figure.savefig(destination / ('population-comparison.' + extension), dpi=150)


if __name__ == '__main__':
    main()
