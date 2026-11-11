import argparse
import csv
import json
from pathlib import Path


def delta_histogram(before, after):
    bins = [new - old for old, new in zip(before['bins'], after['bins'], strict=True)]
    count = after['count'] - before['count']
    if count < 0 or any(value < 0 for value in bins) or sum(bins) != count:
        raise ValueError('Histogram reset or inconsistent cumulative snapshot')
    result = dict(count=count, sum_us=after['sum_us'] - before['sum_us'])
    for percent in [50, 95, 99, 100]:
        rank, seen, upper = (count * percent + 99) // 100, 0, None
        for index, value in enumerate(bins):
            seen += value
            if count and seen >= rank:
                upper = (index + 1) * 250 if index < 4096 else 1024000 << (index - 4096 + 1)
                break
        result['p' + str(percent) + '_upper_us'] = upper
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--run', type=Path, required=True)
    args = parser.parse_args()
    paths = sorted((file for file in (args.run / 'metrics').glob('metrics-*.json')
                    if file.stem.split('-')[-1].isdigit()), key=lambda file: int(file.stem.split('-')[-1]))
    previous = None
    with (args.run / 'windows.csv').open('w', newline='', encoding='utf-8') as output:
        writer = csv.writer(output)
        writer.writerow(['epoch', 'elapsed_seconds', 'window_seconds', 'processing_samples', 'processing_p95_upper_ms',
                         'processing_p99_upper_ms', 'processing_mean_ms', 'online', 'combat', 'dead',
                         'planner_waiting', 'scan_waiting', 'navigation_waiting', 'navigation_oldest_ms'])
        for path in paths:
            snapshot = json.loads(path.read_text(encoding='utf-8-sig'))
            if previous and snapshot['epoch'] == previous['epoch']:
                elapsed = (snapshot['elapsed_us'] - previous['elapsed_us']) / 1e6
                if elapsed < 1:
                    continue
                histogram = delta_histogram(previous['histograms']['world_processing'], snapshot['histograms']['world_processing'])
                gauges = snapshot['gauges']
                writer.writerow([snapshot['epoch'], snapshot['elapsed_us'] / 1e6, elapsed, histogram['count'],
                                 histogram['p95_upper_us'] / 1000 if histogram['count'] else None,
                                 histogram['p99_upper_us'] / 1000 if histogram['count'] else None,
                                 histogram['sum_us'] / histogram['count'] / 1000 if histogram['count'] else None,
                                 *(gauges.get(name) for name in ['online_in_world', 'combat', 'dead', 'planner_waiting',
                                                               'scan_waiting', 'navigation_waiting', 'navigation_oldest_ms'])])
            previous = snapshot


if __name__ == '__main__':
    main()
