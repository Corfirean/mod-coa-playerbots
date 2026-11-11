import argparse
from collections import defaultdict
import csv
import json
from pathlib import Path
import re
import zipfile


WAIT = re.compile(r'^(?:Nt|Zw)(?:Wait|DelayExecution|ReadFile|RemoveIoCompletion)|'
                  r'^(?:WaitFor|Sleep(?:Ex)?$|GetQueuedCompletionStatus|MsgWait|GetMessage)')


def summarize(path):
    with zipfile.ZipFile(path) as archive:
        symbols = {}
        for line in archive.read('Symbols.txt').decode('utf-8-sig').splitlines():
            match = re.fullmatch(r'(0x[0-9a-fA-F]+) "(.*?)" "(.*?)" "(.*)" (\d+)', line)
            if match:
                address, module, function, source, number = match.groups()
                symbols[address.lower()] = (module, function, source, int(number))
        totals = defaultdict(lambda: [0., 0.])
        all_weight = waiting_weight = unresolved_weight = 0.
        for line in archive.read('Callstacks.txt').decode('utf-8-sig').splitlines():
            parts = line.split()
            if len(parts) < 2:
                continue
            weight = float(parts[0])
            stack = [symbols.get(address.lower(), ('?', address, '?', 0)) for address in parts[1:]]
            all_weight += weight
            leaf = stack[0]
            if WAIT.search(leaf[1].split('!')[-1]):
                waiting_weight += weight
                continue
            if leaf[2] in {'?', '[unknown]'}:
                unresolved_weight += weight
            totals[(leaf[0], leaf[1])][0] += weight
            for key in {(frame[0], frame[1]) for frame in stack}:
                totals[key][1] += weight
        rows = [dict(module=key[0], function=key[1], exclusive_weight=value[0], inclusive_weight=value[1])
                for key, value in totals.items()]
        rows.sort(key=lambda row: row['exclusive_weight'], reverse=True)
        return dict(stats=archive.read('Stats.txt').decode('utf-8-sig'),
                    limitations='Sampled thread wall weights, not CPU seconds. Known waiting leaves excluded; '
                    'unrecognized waits and unresolved symbols may remain. Inclusive weights overlap.',
                    total_weight=all_weight, excluded_wait_weight=waiting_weight,
                    retained_weight=all_weight-waiting_weight, unresolved_source_leaf_weight=unresolved_weight,
                    functions=rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = summarize(args.profile)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    with args.output.with_suffix('.csv').open('w', newline='', encoding='utf-8') as out:
        writer = csv.DictWriter(out, fieldnames=['module', 'function', 'exclusive_weight', 'inclusive_weight'])
        writer.writeheader()
        writer.writerows(result['functions'])


if __name__ == '__main__':
    main()
