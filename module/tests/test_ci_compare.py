import copy
import importlib.util
import sys
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[2] / 'tools/combat-intelligence/ci_compare.py'
spec = importlib.util.spec_from_file_location('ci_compare', TOOL)
ci = importlib.util.module_from_spec(spec)
sys.modules['ci_compare'] = ci
spec.loader.exec_module(ci)


def member(cls=13, spec_id=4, role='Dps', level=60, gear_entry=1000):
    return {'classId': cls, 'specId': spec_id, 'role': role, 'level': level,
            'gear': [{'slot': 0, 'entry': gear_entry, 'randomProperty': 0, 'enchants': [0, 0]}]}


def summary(dps=100.0, seed=1001, duration=30000, cls=13, spec_id=4, role='Dps', gear_entry=1000, deaths=0, extra=None, scenario='ST_STATIC'):
    s = {'scenario': scenario, 'seed': seed, 'requestedDurationMs': duration, 'classId': cls, 'specId': spec_id, 'role': role,
         'schemaVersion': 1, 'combat': {'dps': dps, 'deaths': deaths},
         'normalization': {'roster': [member(cls, spec_id, role, 60, gear_entry)]}}
    if extra:
        s.update(extra)
    return s


def runs(values, **kw):
    return {f'ST_STATIC-spec{kw.get("spec_id", 4):03d}-{1001 + i}': summary(dps=v, seed=1001 + i, **kw) for i, v in enumerate(values)}


def state(result, group_prefix, metric):
    for name, group in result['groups'].items():
        if name.startswith(group_prefix):
            for row in group['metrics']:
                if row['metric'] == metric:
                    return row['state']
    return None


class StatisticsTests(unittest.TestCase):
    def test_t_critical_values(self):
        self.assertAlmostEqual(ci.t_critical(1), 12.706)
        self.assertAlmostEqual(ci.t_critical(4), 2.776)
        self.assertAlmostEqual(ci.t_critical(35), 2.042)   # between table points: the next lower df, wider interval
        self.assertAlmostEqual(ci.t_critical(500), 1.980)

    def test_interval_is_wider_than_the_normal_one_for_small_samples(self):
        diffs = [1.0, 2.0, 3.0, 2.0, 1.5]
        row = ci.analyse('combat.dps', [(0.0, d) for d in diffs], 3)
        normal_half = 1.96 * row['ci95Half'] / ci.t_critical(4)
        self.assertGreater(row['ci95Half'], normal_half)

    def test_exact_permutation(self):
        self.assertAlmostEqual(ci.permutation_p([1, 1, 1]), 0.25)      # three pairs can never reach 5%
        self.assertAlmostEqual(ci.permutation_p([1] * 6), 2 / 64)
        self.assertAlmostEqual(ci.permutation_p([1, -1]), 1.0)
        self.assertIsNone(ci.permutation_p([1] * 17))


class VerdictTests(unittest.TestCase):
    def compare(self, base, cand, **kw):
        return ci.compare(base, cand, ['combat.dps', 'combat.deaths'], **kw)

    def test_single_run_gives_no_verdict(self):
        r = self.compare(runs([100]), runs([150]))
        self.assertEqual(state(r, '13/4', 'combat.dps'), 'MISSING_METRIC')

    def test_three_and_five_runs_are_insufficient(self):
        for n in (3, 5):
            r = self.compare(runs([100] * n), runs([150 + i for i in range(n)]))
            self.assertEqual(state(r, '13/4', 'combat.dps'), 'INSUFFICIENT_EVIDENCE', n)

    def test_six_consistent_pairs_are_an_improvement(self):
        r = self.compare(runs([100, 101, 99, 100, 102, 98]), runs([110, 112, 108, 111, 113, 109]))
        self.assertEqual(state(r, '13/4', 'combat.dps'), 'IMPROVEMENT')

    def test_regression_direction_follows_the_metric(self):
        base = runs([100] * 6)
        for i, k in enumerate(base):
            base[k]['combat']['deaths'] = 0
        cand = runs([100] * 6)
        for i, k in enumerate(cand):
            cand[k]['combat']['deaths'] = 1 + i % 2     # more deaths: worse
            cand[k]['combat']['dps'] = 100 + i
        r = self.compare(base, cand)
        self.assertEqual(state(r, '13/4', 'combat.deaths'), 'REGRESSION')

    def test_equal_results_are_inconclusive(self):
        r = self.compare(runs([100] * 6), runs([100] * 6))
        self.assertEqual(state(r, '13/4', 'combat.dps'), 'INCONCLUSIVE')

    def test_noisy_small_difference_is_inconclusive(self):
        r = self.compare(runs([100, 120, 80, 110, 90, 100]), runs([101, 118, 83, 112, 89, 99]))
        self.assertEqual(state(r, '13/4', 'combat.dps'), 'INCONCLUSIVE')

    def test_outlier_driven_difference_is_not_an_improvement(self):
        # one huge gain, the rest slightly worse: the mean says up, the median says down
        base = runs([100, 100, 100, 100, 100, 100, 100])
        cand = runs([99, 99, 99, 99, 99, 99, 400])
        r = self.compare(base, cand)
        self.assertNotEqual(state(r, '13/4', 'combat.dps'), 'IMPROVEMENT')

    def test_zero_baseline_has_no_relative_change(self):
        base = runs([0] * 6)
        cand = runs([5, 6, 5, 7, 6, 5])
        r = self.compare(base, cand)
        row = r['groups']['13/4/Dps/ST_STATIC']['metrics'][0]
        self.assertIsNone(row['relativePct'])
        self.assertEqual(row['state'], 'IMPROVEMENT')

    def test_missing_metric(self):
        base, cand = runs([100] * 6), runs([110] * 6)
        r = ci.compare(base, cand, ['combat.hps'])
        self.assertEqual(state(r, '13/4', 'combat.hps'), 'MISSING_METRIC')

    def test_independent_specs_are_not_pooled(self):
        base = {**runs([100, 101, 99, 100, 102, 98], spec_id=4), **runs([50, 51, 49, 50, 52, 48], spec_id=5)}
        cand = {**runs([110, 112, 108, 111, 113, 109], spec_id=4), **runs([50, 51, 49, 50, 52, 48], spec_id=5)}
        r = self.compare(base, cand)
        self.assertEqual(state(r, '13/4', 'combat.dps'), 'IMPROVEMENT')
        self.assertEqual(state(r, '13/5', 'combat.dps'), 'INCONCLUSIVE')
        self.assertEqual(len(r['groups']), 2)


class PairValidityTests(unittest.TestCase):
    def test_different_seed_is_invalid(self):
        base, cand = runs([100] * 6), runs([110] * 6)
        key = next(iter(cand))
        cand[key]['seed'] = 9999
        r = ci.compare(base, cand, ['combat.dps'])
        self.assertEqual(len(r['invalidPairs']), 1)
        self.assertEqual(r['validPairs'], 5)

    def test_different_duration_is_invalid(self):
        base, cand = runs([100] * 6), runs([110] * 6, duration=60000)
        r = ci.compare(base, cand, ['combat.dps'])
        self.assertEqual(r['validPairs'], 0)
        self.assertTrue(all('requestedDurationMs' in ' '.join(p['reasons']) for p in r['invalidPairs']))

    def test_changed_gear_is_invalid(self):
        base, cand = runs([100] * 6), runs([110] * 6, gear_entry=2000)
        r = ci.compare(base, cand, ['combat.dps'])
        self.assertEqual(r['validPairs'], 0)
        self.assertIn('gear/loadout differs', r['invalidPairs'][0]['reasons'])

    def test_changed_party_is_invalid(self):
        base, cand = runs([100] * 6), runs([110] * 6)
        for s in cand.values():
            s['normalization']['roster'].append(member(cls=14, spec_id=7, role='Healer'))
        r = ci.compare(base, cand, ['combat.dps'])
        self.assertEqual(r['validPairs'], 0)
        self.assertIn('party composition differs', r['invalidPairs'][0]['reasons'])

    def test_older_results_without_roster_are_valid_but_unknown(self):
        base, cand = runs([100] * 6), runs([110] * 6)
        for s in list(base.values()) + list(cand.values()):
            del s['normalization']
        r = ci.compare(base, cand, ['combat.dps'])
        self.assertEqual(r['validPairs'], 6)
        self.assertIn('gear', r['unknownFields'])

    def test_module_revision_may_differ_but_core_and_config_may_not(self):
        base, cand = runs([100] * 6), runs([110] * 6)
        same = {'core_revision': 'abc', 'config': {'CoaBots.A': '1'}, 'git_revision': 'old'}
        other = {'core_revision': 'abc', 'config': {'CoaBots.A': '1'}, 'git_revision': 'new', 'binary_sha256': 'x'}
        self.assertEqual(ci.compare(base, cand, ['combat.dps'], base_manifest=same, cand_manifest=other)['validPairs'], 6)
        core = dict(other, core_revision='def')
        self.assertEqual(ci.compare(base, cand, ['combat.dps'], base_manifest=same, cand_manifest=core)['validPairs'], 0)
        self.assertEqual(ci.compare(base, cand, ['combat.dps'], base_manifest=same, cand_manifest=core, allow_core=True)['validPairs'], 6)
        config = dict(other, config={'CoaBots.A': '2'})
        self.assertEqual(ci.compare(base, cand, ['combat.dps'], base_manifest=same, cand_manifest=config)['validPairs'], 0)
        self.assertEqual(ci.compare(base, cand, ['combat.dps'], base_manifest=same, cand_manifest=config,
                                    allow_config=['CoaBots.A'])['validPairs'], 6)


if __name__ == '__main__':
    unittest.main()
