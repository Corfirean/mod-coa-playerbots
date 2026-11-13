import argparse
import csv
import json
from pathlib import Path
import statistics

from benchmark import read_json, write_json


def evaluate(folder):
    manifest = read_json((folder / 'manifest.json'))
    final = read_json((folder / 'measurement-final.json'))
    initial = read_json((folder / 'measurement-initial.json'))
    shutdown = read_json((folder / 'shutdown.json'))
    population = manifest['population']
    histogram = final['histograms']['world_processing']
    elapsed = (final['elapsed_us'] - initial['elapsed_us']) / 1e6
    with (folder / 'subsystems.csv').open('w', newline='', encoding='utf-8') as output:
        writer = csv.writer(output)
        writer.writerow(['metric', 'calls', 'calls_per_second', 'inclusive_wall_ms', 'mean_us', 'p95_us', 'p99_us', 'max_us'])
        for name, value in final['histograms'].items():
            previous = initial['histograms'].get(name, {})
            count = value['count'] - previous.get('count', 0)
            total = value['sum_us'] - previous.get('sum_us', 0)
            writer.writerow([name, count, count / elapsed if elapsed else 0, total / 1000,
                             total / count if count else 0, value['p95_us'], value['p99_us'], value['max_us']])
    exposure = elapsed * population / 3600
    checks = dict(processing_samples=histogram['count'] > 0,
                  population=final['gauges'].get('online_in_world') == population,
                  clean_shutdown=shutdown['exit_code'] == 0 and not shutdown['forced'])
    if population in [2000, 3000]:
        checks.update(p95=histogram['p95_us'] <= (50000 if population == 2000 else 75000),
                      p99=histogram['p99_us'] <= (100000 if population == 2000 else 150000),
                      shutdown_time=shutdown['seconds'] <= (180 if population == 2000 else 300),
                      no_one_second_stalls=histogram['over'][3] == 0)
    rates = {}
    reference = manifest.get('instrumentation') == 'reference'
    for key in ['bot_kills', 'bot_deaths', 'gathered_items', 'looted_items']:
        rates[key] = (final['counters'].get(key, 0) - initial['counters'].get(key, 0)) / exposure if exposure and not reference else None
    for key in ['quests_accepted', 'quests_turned_in', 'objective_progress', 'tasks_completed', 'tasks_failed', 'movement_stuck']:
        rates[key] = (final['gauges'].get(key, 0) - initial['gauges'].get(key, 0)) / exposure if exposure else None
    samples = [json.loads(line) for line in (folder / 'samples.jsonl').read_text(encoding='utf-8-sig').splitlines()]
    if samples:
        resource_elapsed = max(.001, samples[-1]['t'] - samples[0]['t'])
        cpu = (samples[-1]['world']['cpu_seconds'] - samples[0]['world']['cpu_seconds']) / resource_elapsed
        mysql_cpu = (samples[-1]['mysql']['cpu_seconds'] - samples[0]['mysql']['cpu_seconds']) / resource_elapsed
    else:
        cpu = mysql_cpu = None
    thread_cpu = []
    if samples:
        first_threads = {(thread['id'], thread['creation']): thread for thread in samples[0].get('world_threads', [])}
        for thread in samples[-1].get('world_threads', []):
            before = first_threads.get((thread['id'], thread['creation']))
            if before:
                thread_cpu.append(dict(id=thread['id'], creation=thread['creation'],
                                       cpu_cores=(thread['cpu_seconds'] - before['cpu_seconds']) / resource_elapsed))
    main_thread = min(thread_cpu, key=lambda thread: thread['creation']) if thread_cpu else None
    private_growth = None
    if elapsed >= 6 * 3600 and samples:
        last_four = [sample for sample in samples if sample['t'] >= samples[-1]['t'] - 4 * 3600]
        first = statistics.median(sample['world']['private_bytes'] for sample in last_four if sample['t'] <= last_four[0]['t'] + 300)
        last = statistics.median(sample['world']['private_bytes'] for sample in last_four if sample['t'] >= last_four[-1]['t'] - 300)
        private_growth = (last - first) / first
    persistence = read_json((folder / 'persistence.json')) if (folder / 'persistence.json').exists() else None
    coverage = read_json((folder / 'scenario-coverage.json')) if (folder / 'scenario-coverage.json').exists() else None
    dispatch = final['histograms'].get('command_dispatch', {})
    unverified = ['behavior_vs_baseline', 'real_client_command_action_latency', 'stress_scenario_coverage',
                  'instrumentation_overhead_vs_disabled']
    if not persistence or persistence['status'] != 'passed':
        unverified.append('persistence_after_restart')
    if private_growth is None:
        unverified.append('six_hour_memory_stability')
    else:
        checks['private_memory_growth'] = private_growth <= .05
        unverified.append('stable_loaded_area_coverage_during_soak')
    return dict(label=manifest['label'], population=population, seconds=elapsed,
                run_status='failed' if (folder / 'failure.json').exists() else 'measured',
                binary_sha256=manifest['binary_sha256'],
                instrumentation=manifest.get('instrumentation', 'full'),
                diagnostic_profile=bool(manifest.get('external_cpu_profile') or manifest.get('options', {}).get('CoaBots.Perf.ProfileCombat') or
                                        manifest.get('options', {}).get('World.Performance.EntitySampleRate')),
                source=manifest.get('module_source'), scenario=manifest.get('scenario', 'distributed'),
                options=manifest.get('options', {}), persistence=persistence, scenario_coverage=coverage,
                final_four_hour_private_growth=private_growth,
                command_dispatch={k: v for k, v in dispatch.items() if k != 'bins'},
                processing={k: v for k, v in histogram.items() if k != 'bins'},
                period={k: v for k, v in final['histograms']['world_period'].items() if k != 'bins'},
                checks=checks, timing_checks_passed=all(checks.values()), cpu_cores=cpu, per_bot_hour=rates,
                mysql_cpu_cores=mysql_cpu, main_thread=main_thread,
                thread_cpu=sorted(thread_cpu, key=lambda thread: thread['cpu_cores'], reverse=True),
                acceptance='incomplete', unverified=unverified)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    args = parser.parse_args()
    rows, failures = [], []
    for folder in sorted((args.root / 'runs').iterdir()):
        if (folder / 'measurement-final.json').is_file():
            rows.append(evaluate(folder))
        if (folder / 'failure.json').is_file():
            failure = dict(label=folder.name, failure=read_json((folder / 'failure.json')))
            if (folder / 'abort-diagnostics.json').exists():
                failure['diagnostic_reason'] = read_json((folder / 'abort-diagnostics.json'))['reason']
            failures.append(failure)
    write_json(args.root / 'report.json', dict(acceptance='incomplete', runs=rows, failures=failures))
    with (args.root / 'runs.csv').open('w', newline='', encoding='utf-8') as output:
        writer = csv.writer(output)
        writer.writerow(['label', 'population', 'seconds', 'scenario', 'status', 'binary_sha256', 'processing_p95_us',
                         'processing_p99_us', 'cpu_cores', 'final_four_hour_private_growth'])
        for row in rows:
            writer.writerow([row['label'], row['population'], row['seconds'], row['scenario'], row['run_status'],
                             row['binary_sha256'], row['processing']['p95_us'], row['processing']['p99_us'],
                             row['cpu_cores'], row['final_four_hour_private_growth']])
    lines = ['# Companion performance measurements', '',
             'Timing checks alone do not qualify a population. Pending acceptance checks remain explicit.', '',
             '| Run | Bots | p95 processing ms | p99 processing ms | CPU cores | Timing checks | Run status |',
             '|---|---:|---:|---:|---:|---|---|']
    for row in rows:
        p95 = f"{row['processing']['p95_us'] / 1000:.2f}" if row['processing']['count'] else 'unmeasured'
        p99 = f"{row['processing']['p99_us'] / 1000:.2f}" if row['processing']['count'] else 'unmeasured'
        status = row['run_status'] + (' / profile' if row['diagnostic_profile'] else ' / reference' if row['instrumentation'] == 'reference' else '')
        lines.append(f"| {row['label']} | {row['population']} | {p95} | "
                     f"{p99} | {row['cpu_cores']:.2f} | {row['timing_checks_passed']} | {status} |")
    if failures:
        lines.extend(['', '## Failed preparations and runs', ''])
        for failure in failures:
            lines.append('- ' + failure['label'] + ': ' + failure.get('diagnostic_reason', failure['failure']['message']))
    (args.root / 'report.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
