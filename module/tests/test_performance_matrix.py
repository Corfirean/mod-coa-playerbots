import importlib.util
from pathlib import Path
import sys
import unittest


DIRECTORY = Path(__file__).resolve().parents[2] / 'tools/performance'
sys.path.insert(0, str(DIRECTORY))
SPEC = importlib.util.spec_from_file_location('coa_perf_matrix', DIRECTORY / 'matrix.py')
matrix = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(matrix)
sys.path.pop(0)


class MatrixTests(unittest.TestCase):
    def test_isolated_comparisons_alternate_three_pairs(self):
        for suite, key in [('adaptive', 'adaptive'), ('ranklookup', 'ranklookup')]:
            cases = matrix.cases(suite)
            self.assertEqual(len(cases), 6)
            for index, order in enumerate([[0, 1], [1, 0], [0, 1]]):
                pair = cases[index * 2:index * 2 + 2]
                self.assertEqual([case[key] for case in pair], order)
                self.assertEqual({case['repeat'] for case in pair}, {index + 1})
                self.assertEqual({case['population'] for case in pair}, {2000})
                self.assertEqual(pair[0]['budgets'], [3000, 2000, 8000])
                self.assertEqual({k: v for k, v in pair[0].items() if k != key},
                                 {k: v for k, v in pair[1].items() if k != key})


if __name__ == '__main__':
    unittest.main()
