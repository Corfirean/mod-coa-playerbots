import importlib.util
from pathlib import Path
import random
import sys
import unittest

path = Path(__file__).resolve().parents[2] / "tools" / "offline_bot_factory.py"
spec = importlib.util.spec_from_file_location("offline_bot_factory", path)
factory = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = factory
spec.loader.exec_module(factory)


class GearSelectionTests(unittest.TestCase):
    def pick(self, rows, level=5):
        cache = factory.GearPoolCache(None)
        cache.query = lambda *_: rows
        return cache.pick(4, [1], 10, level, 4, random.Random(1))

    def test_reported_gloves_are_rejected(self):
        self.assertEqual(self.pick([(42886,174,0,3,0,0), (100,12,5,2,0,0)]), 100)

    def test_no_unsafe_fallback(self):
        self.assertIsNone(self.pick([(42886,174,0,3,0,0)]))

    def test_required_level_and_quality(self):
        self.assertIsNone(self.pick([(1,12,6,2,0,0), (2,12,5,3,0,0)]))

    def test_power_and_context(self):
        self.assertIsNone(self.pick([(1,12,5,2,1,0)]))
        self.assertIsNone(self.pick([(1,200,60,3,0,10)],60))
        self.assertEqual(self.pick([(1,200,60,3,10,0)],60),1)
        self.assertIsNone(self.pick([(1,200,60,3,16,0)],60))

    def test_old_gear_only_used_if_level_window_is_empty(self):
        self.assertEqual(self.pick([(1,20,0,3,0,0),(2,20,20,2,0,0)],20),2)
        self.assertEqual(self.pick([(1,20,0,2,0,0)],20),1)

    def test_quality_roll_uses_core_quality_ids(self):
        rng = random.Random(2)
        self.assertEqual({factory.roll_quality_cap(5,rng) for _ in range(1000)}, {1,2,3})


if __name__ == "__main__":
    unittest.main()
