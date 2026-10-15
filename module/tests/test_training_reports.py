import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[2] / "tools/training_reports.py"
if not path.exists():
    path = Path(__file__).resolve().parents[1] / "tools/training_reports.py"
spec = importlib.util.spec_from_file_location("training_reports", path)
reports = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reports)


def fixture(dps, seed=1001):
    return {"schemaVersion": 1, "classId": 20, "specId": 31, "role": "Support",
            "scenario": "SUPPORT_GROUP", "seed": seed, "requestedDurationMs": 180000,
            "normalization": {"roster": [{"classId": 20, "specId": 31, "role": "Support", "gear": []}]},
            "combat": {"dps": dps, "deaths": 0}, "support": {"buffUptimePct": 99, "unknown": None}}


class ReportTests(unittest.TestCase):
    def test_aggregate(self):
        result = reports.aggregate([fixture(100), fixture(200)])
        self.assertEqual(result["metrics"]["combat.dps"]["mean"], 150)
        self.assertEqual(result["metrics"]["combat.dps"]["stddev"], 50)
        self.assertNotIn("support.unknown", result["metrics"])

    def test_compare_facts(self):
        result = reports.compare([fixture(100)], [fixture(110)])
        self.assertEqual(result["deltas"]["combat.dps"]["percentDelta"], 10)
        self.assertIsNone(result["deltas"]["combat.deaths"]["percentDelta"])

    def test_incompatible_seed_rejected(self):
        with self.assertRaises(ValueError):
            reports.compare([fixture(100)], [fixture(110, 1002)])

    def test_incompatible_gear_rejected(self):
        other = fixture(100)
        other["normalization"]["roster"][0]["gear"] = [{"entry": 1}]
        with self.assertRaises(ValueError):
            reports.compare([fixture(100)], [other])

    def test_incompatible_spellbook_rejected(self):
        other = fixture(100)
        other["normalization"]["roster"][0]["spellbook"] = [123]
        with self.assertRaises(ValueError):
            reports.compare([fixture(100)], [other])


if __name__ == "__main__":
    unittest.main()
