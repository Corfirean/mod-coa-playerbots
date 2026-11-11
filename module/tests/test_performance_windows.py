import importlib.util
from pathlib import Path
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'tools/performance/windows.py'
SPEC = importlib.util.spec_from_file_location('coa_performance_windows', SCRIPT)
windows = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(windows)


class WindowTests(unittest.TestCase):
    def test_cumulative_difference_excludes_earlier_slow_samples(self):
        before = dict(bins=[0, 0, 0, 2], count=2, sum_us=1800)
        after = dict(bins=[0, 100, 0, 2], count=102, sum_us=31800)
        result = windows.delta_histogram(before, after)
        self.assertEqual(result['count'], 100)
        self.assertEqual(result['sum_us'], 30000)
        self.assertEqual(result['p99_upper_us'], 500)

    def test_empty_window_is_not_reported_as_zero_latency(self):
        snapshot = dict(bins=[0, 0], count=0, sum_us=0)
        self.assertIsNone(windows.delta_histogram(snapshot, snapshot)['p95_upper_us'])

    def test_reset_and_inconsistent_snapshots_are_rejected(self):
        with self.assertRaises(ValueError):
            windows.delta_histogram(dict(bins=[2], count=2, sum_us=0), dict(bins=[1], count=1, sum_us=0))
        with self.assertRaises(ValueError):
            windows.delta_histogram(dict(bins=[0], count=0, sum_us=0), dict(bins=[1], count=2, sum_us=0))


if __name__ == '__main__':
    unittest.main()
