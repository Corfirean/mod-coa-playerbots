import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SCRIPT = Path(__file__).resolve().parents[2] / 'tools/performance/benchmark.py'
SPEC = importlib.util.spec_from_file_location('coa_benchmark', SCRIPT)
benchmark = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(benchmark)


class BenchmarkTests(unittest.TestCase):
    def test_wait_state_query_is_limited_to_the_selected_process(self):
        with patch.object(benchmark, 'powershell', return_value='{"id":4,"state":"Wait","wait_reason":"Suspended"}') as query:
            self.assertEqual(benchmark.thread_wait_states(123),
                             [{'id': 4, 'state': 'Wait', 'wait_reason': 'Suspended'}])
            self.assertIn('Get-Process -Id 123 -ErrorAction Stop', query.call_args[0][0])
        with self.assertRaises(ValueError):
            benchmark.thread_wait_states(0)

    def test_reset_ignores_an_old_status_arriving_after_output_is_cleared(self):
        class World:
            lock = benchmark.threading.Lock()
            text = ''

            def dump_metrics(self):
                return {'epoch': 7}

            def command(self, value):
                self.command_sent = value
                self.text = 'CoaBotPerf enabled=1 epoch=7 samples=400\n'

        world = World()

        def acknowledge(_):
            world.text += 'CoaBotPerf enabled=1 epoch=8 samples=0\n'

        with patch.object(benchmark.time, 'sleep', side_effect=acknowledge):
            self.assertEqual(benchmark.World.reset_metrics(world), 8)
        self.assertEqual(world.command_sent, 'botcmd perf reset')

    def test_controller_archive_keeps_the_loaded_script_and_lazy_imports(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            identity, source = benchmark.archive_controller(folder)
            self.assertEqual(identity['sha256'], benchmark.digest(folder / 'controller-source.zip'))
            self.assertEqual(identity['files']['benchmark.py'], benchmark.SCRIPT_SHA256)
            with zipfile.ZipFile(folder / 'controller-source.zip') as archive:
                self.assertEqual(archive.read('benchmark.py'), benchmark.SCRIPT_SOURCE)
                self.assertEqual(archive.read('wait_chains.py'), (source / 'wait_chains.py').read_bytes())
                self.assertEqual(archive.read('stack_profile.py'), (source / 'stack_profile.py').read_bytes())

    def test_delayed_stack_capture_stays_within_the_selected_window(self):
        self.assertTrue(benchmark.valid_stack_window(900, 120, 480))
        self.assertTrue(benchmark.valid_stack_window(60, 60, 0))
        for capture, after in [(0, 0), (60, -1), (61, 0), (30, 31)]:
            self.assertFalse(benchmark.valid_stack_window(60, capture, after))

    def test_direct_field_reads_require_native_activation(self):
        options = {'World.Performance.DirectFieldReads': 1}
        for gauges in [{}, {'direct_object_field_reads_enabled': 0}]:
            with self.assertRaises(RuntimeError):
                benchmark.verify_native_schedule({'gauges': gauges}, options)
        benchmark.verify_native_schedule({'gauges': {'direct_object_field_reads_enabled': 1}}, options)

    def test_aura_fast_path_requires_native_activation(self):
        options = {'World.Performance.AuraSelfTargetFastPath': 1}
        for gauges in [{}, {'aura_self_target_fast_path_enabled': 0}]:
            with self.assertRaises(RuntimeError):
                benchmark.verify_native_schedule({'gauges': gauges}, options)
        benchmark.verify_native_schedule({'gauges': {'aura_self_target_fast_path_enabled': 1}}, options)

    def test_candidate_filter_requires_native_activation(self):
        options = {'World.Performance.FilterSpellCandidates': 1}
        for gauges in [{}, {'spell_candidate_filter_enabled': 0}]:
            with self.assertRaises(RuntimeError):
                benchmark.verify_native_schedule({'gauges': gauges}, options)
        benchmark.verify_native_schedule({'gauges': {'spell_candidate_filter_enabled': 1}}, options)

    @unittest.skipUnless(os.name == 'nt', 'Windows process snapshot')
    def test_runtime_inventory_detects_process_and_excludes_owned_pid(self):
        with patch.object(benchmark, 'contaminating_process', return_value=True):
            self.assertIn(os.getpid(), [row['pid'] for row in benchmark.live_competitors(0)])
            self.assertNotIn(os.getpid(), [row['pid'] for row in benchmark.live_competitors(os.getpid())])

    def test_interleaving_requires_active_world_maps(self):
        options = {'World.Performance.InterleaveWorldObjects': 1}
        for gauges in [{}, {'interleaved_world_maps': 0}]:
            with self.assertRaises(RuntimeError):
                benchmark.verify_native_schedule({'gauges': gauges}, options)
        benchmark.verify_native_schedule({'gauges': {'interleaved_world_maps': 2}}, options)

    def test_new_scheduling_features_cannot_be_silently_ignored(self):
        options = {'CoaBots.Spells.CacheLearnedRanks': 1, 'CoaBots.Scheduling.Enable': 1,
                   'CoaBots.Scheduling.Adaptive': 1, 'CoaBots.Scheduling.FarIdleIntervalMs': 1000}
        with self.assertRaises(RuntimeError):
            benchmark.verify_native_schedule({'gauges': {}}, options)
        with self.assertRaises(RuntimeError):
            benchmark.verify_native_schedule({'gauges': {'learned_rank_cache_enabled': 1}}, options)
        benchmark.verify_native_schedule({'gauges': {'learned_rank_cache_enabled': 1, 'far_idle_interval_ms': 1000}}, options)

    @unittest.skipUnless(os.name == 'nt', 'Windows process liveness check')
    def test_current_process_is_live_and_invalid_pid_is_not(self):
        self.assertTrue(benchmark.process_running(os.getpid()))
        self.assertFalse(benchmark.process_running(0))

    def test_native_feature_requires_runtime_activation_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            benchmark.verify_native_features(folder, {})
            console = folder / 'console.log'
            console.write_bytes(b'Direct spell rank lookup disabled: inconsistent chain 7\n')
            with self.assertRaises(RuntimeError):
                benchmark.verify_native_features(folder, {'World.Performance.DirectRankLookup': 1})
            console.write_bytes(b'Direct spell rank lookup enabled after validating 14374 chain entries\n')
            benchmark.verify_native_features(folder, {'World.Performance.DirectRankLookup': 1})

    def test_named_world_variants_and_builds_contaminate_measurements(self):
        for name in ['worldserver.exe', 'worldserver-scaling1.exe', 'WorldServer_test.EXE', 'cl.exe', 'MSBuild.exe']:
            self.assertTrue(benchmark.contaminating_process(name), name)
        for name in ['mysqld.exe', 'chrome.exe', 'worldserver-notes.txt']:
            self.assertFalse(benchmark.contaminating_process(name), name)

    def test_utf8_and_bom_reports_do_not_depend_on_windows_locale(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'manifest.json'
            expected = {'name': '\u0418\u0437\u043c\u0435\u0440\u0435\u043d\u0438\u0435', 'units': '\u03bcs'}
            benchmark.write_json(path, expected)
            self.assertEqual(benchmark.read_json(path), expected)
            path.write_bytes(b'\xef\xbb\xbf' + path.read_bytes())
            self.assertEqual(benchmark.read_json(path), expected)

    def test_options_cannot_reduce_measured_population_or_disable_behavior(self):
        benchmark.validate_options({'CoaBots.Scheduling.Enable': 1, 'CoaBots.Perf.Detailed': 0})
        for key in ['CoaBots.AutoLogin.MaxCount', 'CoaBots.Benchmark.SuspendOnLogin', 'CoaBots.Economy.Enable']:
            with self.assertRaises(ValueError):
                benchmark.validate_options({key: 0})

    def test_coverage_requires_sustained_native_content(self):
        rows = [{'gauges': {'benchmark_dungeons': 5, 'benchmark_battlegrounds': 2}} for _ in range(9)]
        rows.append({'gauges': {'benchmark_dungeons': 0, 'benchmark_battlegrounds': 0}})
        self.assertTrue(benchmark.scenario_coverage('group-content', rows)['passed'])
        rows.append({'gauges': {}})
        self.assertFalse(benchmark.scenario_coverage('group-content', rows)['passed'])
        self.assertFalse(benchmark.scenario_coverage('combat', [])['passed'])
        self.assertTrue(benchmark.scenario_coverage('distributed', [])['passed'])

    def test_literal_config_replacement(self):
        original = '[worldserver]\nDataDir = "old"\n'
        replacement = '"C:\\games\\new\\data"'
        updated = benchmark.set_value(original, 'DataDir', replacement)
        self.assertEqual(updated, '[worldserver]\nDataDir = ' + replacement + '\n')
        self.assertEqual(updated.count('DataDir'), 1)

    def test_sanitizes_database_and_password(self):
        text = 'WorldDatabaseInfo = "localhost;3306;user;secret;world"\nRa.Password = "hidden"\nThreadPool = 2\n'
        clean = benchmark.sanitized_config(text)
        self.assertNotIn('secret', clean)
        self.assertNotIn('hidden', clean)
        self.assertIn('ThreadPool = 2', clean)

    def test_cleanup_requires_marker_and_descendant(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            inside = root / 'data'
            inside.mkdir()
            with self.assertRaises(RuntimeError):
                benchmark.safe_remove(root, inside)
            (root / '.disposable-benchmark').write_text('test')
            with self.assertRaises(RuntimeError):
                benchmark.safe_remove(root, root)
            with self.assertRaises(RuntimeError):
                benchmark.safe_remove(root, root.parent)
            benchmark.safe_remove(root, inside)
            self.assertFalse(inside.exists())

    def test_atomic_json_replaces_existing_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'report.json'
            benchmark.write_json(path, {'count': 1})
            benchmark.write_json(path, {'count': 2})
            self.assertEqual(json.loads(path.read_text()), {'count': 2})
            self.assertFalse(path.with_suffix('.json.tmp').exists())

    def test_checkpoint_compares_persistent_state_without_slot_order(self):
        row = dict(guid=1, level=20, **{'class': 12}, race=1, spec=90, xp=123, money=456,
                   map=0, x=1.25, y=-2.5, z=3, quests=[[4, 1], [2, 3]], items=[[9, 10, 2], [8, 11, 1]])
        after = {**row, 'quests': list(reversed(row['quests'])), 'items': list(reversed(row['items'])), 'x': 1.251}
        self.assertEqual(benchmark.compare_checkpoints({'players': [row]}, {'players': [after]}), [])
        changed = {**after, 'xp': 0, 'items': [[9, 10, 1]]}
        fields = {error['field'] for error in benchmark.compare_checkpoints({'players': [row]}, {'players': [changed]})}
        self.assertEqual(fields, {'xp', 'items'})
        self.assertEqual(benchmark.compare_checkpoints({'players': [row]}, {'players': []})[0]['field'], 'missing_player')


if __name__ == '__main__':
    unittest.main()
