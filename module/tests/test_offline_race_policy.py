import importlib.util
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest


path = Path(__file__).resolve().parents[2] / "tools/offline_bot_factory.py"
spec = importlib.util.spec_from_file_location("offline_race_factory", path)
factory = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = factory
spec.loader.exec_module(factory)


class OfflineRacePolicyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.modules = self.root / "Core/configs/modules"
        self.modules.mkdir(parents=True)
        dbc = self.root / "Data/dbc_races/ChrRaces.dbc"
        dbc.parent.mkdir(parents=True)
        rows = b"".join(struct.pack("<I", race) + bytes(272) for race in [1, 82, 83, 97])
        dbc.write_bytes(struct.pack("<4sIIII", b"WDBC", 4, 69, 276, 1) + rows + b"\0")

    def configure(self, text):
        (self.modules / "coa_custom_races.conf").write_text(text, encoding="utf-8")
        return factory.factory_race_policy(self.root)

    def test_global_switches_owner_exclusions_and_crashing_races(self):
        allow = self.configure('CoACustomRaces.Enable = 1\nCoACustomRaces.BotsEnable = 1\nCoACustomRaces.DisabledRaceIds = "82"\n')
        self.assertFalse(allow(82))
        self.assertTrue(allow(83))
        self.assertFalse(allow(97))
        self.assertTrue(allow(1))
        (self.modules / "mod_coa_playerbots.conf").write_text('CoaBots.ExcludedBotRaces = "83"\n')
        self.assertFalse(factory.factory_race_policy(self.root)(83))

    def test_disabled_or_missing_switches_allow_only_stock(self):
        self.assertFalse(factory.factory_race_policy(self.root)(82))
        self.assertTrue(factory.factory_race_policy(self.root)(1))
        for text in ['CoACustomRaces.Enable = 1\n', 'CoACustomRaces.BotsEnable = 1\n']:
            self.assertFalse(self.configure(text)(82))

    def test_invalid_owner_lists_rejected_without_partial_policy(self):
        for value in ['82,82', '82,1', '82,250', '82,junk', '82,', '-82']:
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.configure(f'CoACustomRaces.DisabledRaceIds = "{value}"\n')

    def test_roster_filters_before_selecting_first_template(self):
        class Database:
            def run(self, *_args, **_kwargs):
                return [['12', '1', '82', '5'], ['12', '2', '83', '5']]
        roster = factory.build_template_roster(Database(), set(), lambda race: race != 82)
        self.assertEqual([entry.race for teams in roster.values() for entry in teams.values()], [83])

    def test_bound_catalog_rejects_version_changes(self):
        path = self.root / "Data/dbc_races/ChrRaces.dbc"
        fingerprint = hashlib.sha256(path.read_bytes()).hexdigest()
        text = f'CoACustomRaces.Enable = 1\nCoACustomRaces.BotsEnable = 1\nCoACustomRaces.DisabledRaceIds = "82"\nCoACustomRaces.DisabledRaceCatalogSha256 = "{fingerprint}"\n'
        self.assertFalse(self.configure(text)(82))
        path.write_bytes(path.read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "catalog changed"):
            factory.factory_race_policy(self.root)


if __name__ == "__main__":
    unittest.main()
