import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile


SCRIPT = Path(__file__).resolve().parents[2] / 'tools/performance/stack_report.py'
SPEC = importlib.util.spec_from_file_location('coa_stack_report', SCRIPT)
report = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(report)


class StackReportTests(unittest.TestCase):
    def test_waiting_threads_excluded_and_recursion_counted_once(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'profile.sleepy'
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('Stats.txt', 'Duration: 3\n')
                archive.writestr('Symbols.txt',
                                 '0x1 "ntdll" "NtWaitForSingleObject" "[unknown]" 0\n'
                                 '0x2 "world" "Player::Update" "C:\\source\\Player.cpp" 5\n'
                                 '0x3 "world" "World::Update" "C:\\source\\World.cpp" 8\n')
                archive.writestr('Callstacks.txt', '7 0x1 0x3\n2 0x2 0x2 0x3\n1 0x9 0x3\n')
            result = report.summarize(path)
            self.assertEqual(result['total_weight'], 10)
            self.assertEqual(result['excluded_wait_weight'], 7)
            self.assertEqual(result['retained_weight'], 3)
            rows = {row['function']: row for row in result['functions']}
            self.assertEqual(rows['Player::Update']['inclusive_weight'], 2)
            self.assertEqual(rows['World::Update']['inclusive_weight'], 3)
            self.assertEqual(result['unresolved_source_leaf_weight'], 1)


if __name__ == '__main__':
    unittest.main()
