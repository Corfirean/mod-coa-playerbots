#!/usr/bin/env python3
"""
Unit tests for DBM offline mechanic importer, parser, conflict detector, and validator.
"""

import unittest
import tempfile
import os
import json
import sys

# Add parent directory to path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from parser import DbmParser
from validate import MechanicValidator

class TestDbmImporter(unittest.TestCase):
    def setUp(self):
        self.parser = DbmParser()

    def test_synthetic_boss_module(self):
        """Test parsing of a synthetic boss Lua with various DBM warning constructs."""
        lua_content = """
        local mod = DBM:NewMod("TestBoss", "DBM-Test", 1)
        mod:SetCreatureID(12345)
        mod:SetEncounterID(999)

        mod:RegisterEventsInCombat(
            "SPELL_CAST_START 11111 22222",
            "SPELL_AURA_APPLIED 33333 44444"
        )

        -- 1. Interrupt
        local specWarnKick = mod:NewSpecialWarningInterrupt(11111, "HasInterrupt", nil, nil, 1, 2)
        -- 2. Movement / GTFO
        local specWarnFire = mod:NewSpecialWarningGTFO(22222, nil, nil, nil, 1, 8)
        -- 3. Dispel
        local specWarnMagic = mod:NewSpecialWarningDispel(33333, "MagicDispeller", nil, nil, 1, 2)
        -- 4. Tank Taunt
        local specWarnTaunt = mod:NewSpecialWarningTaunt(44444, "Tank", nil, nil, 1, 2)
        -- 5. Stage / Phase
        local warnPhase2 = mod:NewPhaseAnnounce(2, 2)
        -- 6. Adds
        local warnAdds = mod:NewAddsLeftAnnounce(55555, 2)
        -- 7. Unknown API call (should be ignored gracefully without crash)
        local weirdCall = mod:NewCompletelyUnknownDBMFunction(99999, "foo", "bar")
        """

        with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as f:
            f.write(lua_content)
            temp_path = f.name

        try:
            res = self.parser.parse_file(temp_path, "DBM-Test")
            self.assertIsNotNone(res)
            self.assertEqual(res["module"], "TestBoss")
            self.assertEqual(res["encounterId"], 999)
            self.assertEqual(res["creatureIds"], [12345])

            mechanics = {m["spellId"]: m for m in res["mechanics"]}
            
            # Check Interrupt
            self.assertIn(11111, mechanics)
            self.assertEqual(mechanics[11111]["mechanicType"], "Interrupt")
            self.assertEqual(mechanics[11111]["priority"], "Critical")
            self.assertIn("Dps", mechanics[11111]["roleMask"])

            # Check GTFO / MoveFromGround
            self.assertIn(22222, mechanics)
            self.assertEqual(mechanics[22222]["mechanicType"], "MoveFromGround")

            # Check Dispel
            self.assertIn(33333, mechanics)
            self.assertEqual(mechanics[33333]["mechanicType"], "Dispel")
            self.assertIn("Healer", mechanics[33333]["roleMask"])

            # Check TankSwap
            self.assertIn(44444, mechanics)
            self.assertEqual(mechanics[44444]["mechanicType"], "TankSwap")
            self.assertIn("Tank", mechanics[44444]["roleMask"])

            # Check Adds
            self.assertIn(55555, mechanics)
            self.assertEqual(mechanics[55555]["mechanicType"], "KillAdds")

        finally:
            os.remove(temp_path)

    def test_malformed_lua_and_comments(self):
        """Test parser tolerance against broken syntax and heavy commenting."""
        lua_content = """
        --[[ Multi line comment with pseudo code
        mod:NewSpecialWarningInterrupt(99999)
        ]]
        local mod = DBM:NewMod("BrokenBoss", "DBM-Test")
        -- single line comment with mod:NewSpecialWarningGTFO(88888)
        mod:SetCreatureID(77777)
        local valid = mod:NewSpecialWarningRun(66666, nil, nil, nil, 4, 2)
        random unclosed junk ... %$#@@
        """
        with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as f:
            f.write(lua_content)
            temp_path = f.name

        try:
            res = self.parser.parse_file(temp_path, "DBM-Test")
            self.assertIsNotNone(res)
            # 99999 and 88888 in comments must be excluded
            spell_ids = [m["spellId"] for m in res["mechanics"]]
            self.assertNotIn(99999, spell_ids)
            self.assertNotIn(88888, spell_ids)
            self.assertIn(66666, spell_ids)
        finally:
            os.remove(temp_path)

    def test_conflict_detection(self):
        """Test that contradictory mechanics (Spread vs Stack) are marked as Conflict."""
        raw = [
            {
                "key": "Boss_123_SpreadOut",
                "encounterId": 10,
                "spellId": 123,
                "mechanicType": "SpreadOut",
                "notes": ""
            },
            {
                "key": "Boss_123_StackTogether",
                "encounterId": 10,
                "spellId": 123,
                "mechanicType": "StackTogether",
                "notes": ""
            }
        ]
        validator = MechanicValidator()
        processed, stats = validator.validate_and_merge(raw)

        self.assertEqual(stats["conflictCount"], 2)
        for r in processed:
            self.assertEqual(r["confidence"], "Conflict")
            self.assertIn("CONFLICT", r["notes"])

    def test_overrides_application(self):
        """Test that manual CoA overrides take precedence over imported values."""
        overrides = [
            {
                "key": "Boss_777_Interrupt",
                "mechanicType": "Defensive",
                "priority": "Low",
                "disabled": True
            }
        ]
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump(overrides, f)
            ov_path = f.name

        try:
            validator = MechanicValidator(overrides_path=ov_path)
            raw = [
                {
                    "key": "Boss_777_Interrupt",
                    "spellId": 777,
                    "mechanicType": "Interrupt",
                    "priority": "Critical",
                    "source": "DBM",
                    "confidence": "Imported",
                    "notes": ""
                }
            ]
            processed, stats = validator.validate_and_merge(raw)
            self.assertEqual(stats["overrideCount"], 1)
            self.assertEqual(stats["disabledCount"], 1)
            self.assertEqual(processed[0]["mechanicType"], "Defensive")
            self.assertEqual(processed[0]["priority"], "Low")
            self.assertEqual(processed[0]["source"], "Manual")
            self.assertEqual(processed[0]["confidence"], "Authoritative")
            self.assertTrue(processed[0]["disabled"])
        finally:
            os.remove(ov_path)

if __name__ == "__main__":
    unittest.main()
