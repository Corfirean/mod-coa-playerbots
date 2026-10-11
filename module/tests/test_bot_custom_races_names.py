#!/usr/bin/env python3
import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
BOT_NAME_DATA_H = REPO_ROOT / "module" / "src" / "BotNameData.h"

class TestBotCustomRacesNames(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = BOT_NAME_DATA_H.read_text(encoding="utf-8")

    def test_all_names_meet_wow_character_requirements(self):
        """Every name must be 3-12 letters, Titlecased, no triple consecutive letters."""
        # Extract all string arrays: static constexpr char const* kName[] = { ... };
        array_pattern = re.compile(
            r"static\s+constexpr\s+char\s+const\*\s+(\w+)\[\]\s*=\s*\{([^}]+)\};",
            re.DOTALL
        )
        found_arrays = 0
        total_names = 0
        for match in array_pattern.finditer(self.text):
            array_name = match.group(1)
            raw_items = match.group(2)
            names = re.findall(r'"([A-Za-z]+)"', raw_items)
            found_arrays += 1
            total_names += len(names)
            self.assertGreater(len(names), 0, f"Array {array_name} is empty")

            for name in names:
                # 1. Letters only, Titlecased
                self.assertTrue(
                    re.match(r"^[A-Z][a-z]+$", name),
                    f"Name '{name}' in {array_name} does not match [A-Z][a-z]+"
                )
                # 2. Length 3 to 12
                self.assertTrue(
                    3 <= len(name) <= 12,
                    f"Name '{name}' in {array_name} has invalid length {len(name)} (must be 3-12)"
                )
                # 3. No three consecutive identical letters
                for i in range(len(name) - 2):
                    self.assertFalse(
                        name[i].lower() == name[i + 1].lower() == name[i + 2].lower(),
                        f"Name '{name}' in {array_name} contains 3 identical consecutive letters"
                    )

        self.assertGreater(found_arrays, 20)
        self.assertGreater(total_names, 2000)

    def test_unique_custom_races_have_dedicated_pools(self):
        """Verify dedicated pools exist for unique races."""
        expected_unique = [
            "kOgreMale", "kOgreFemale", "kOgreSurname",
            "kSethrakMale", "kSethrakFemale", "kSethrakSurname",
            "kNagaMale", "kNagaFemale", "kNagaSurname",
            "kVulperaMale", "kVulperaFemale", "kVulperaSurname",
            "kPandarenMale", "kPandarenFemale", "kPandarenSurname",
            "kTuskarrMale", "kTuskarrFemale", "kTuskarrSurname",
            "kVrykulMale", "kVrykulFemale", "kVrykulSurname",
            "kGoblinMale", "kGoblinFemale", "kGoblinSurname",
            "kJinyuMale", "kJinyuFemale", "kJinyuSurname",
            "kGnollMale", "kGnollFemale", "kGnollSurname",
            "kSaberonMale", "kSaberonFemale", "kSaberonSurname",
            "kDracthyrMale", "kDracthyrFemale", "kDracthyrSurname",
            "kFurbolgMale", "kFurbolgFemale", "kFurbolgSurname",
            "kMurlocMale", "kMurlocFemale", "kMurlocSurname",
            "kHarpyMale", "kHarpyFemale", "kHarpySurname",
        ]
        for name in expected_unique:
            self.assertIn(f"static constexpr char const* {name}[]", self.text)

    def test_kracepools_includes_unique_races(self):
        """Verify kRacePools registers the unique race IDs."""
        expected_race_ids = [
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
            15, 18, 19, 20, 21, 30, 50, 53, 54, 56, 78, 82, 83, 97
        ]
        for r_id in expected_race_ids:
            self.assertRegex(
                self.text,
                rf"\{{\s*{r_id}\s*,\s*\{{\s*k\w+Male",
                f"Race ID {r_id} missing from kRacePools"
            )

    def test_resolve_naming_race_covers_related_races(self):
        """Verify ResolveNamingRace maps related races to parent races."""
        # Check that switch case mapping exists in ResolveNamingRace
        self.assertIn("unsigned char ResolveNamingRace(unsigned char race)", self.text)
        # Forest/Ice/Drakkari/Zandalari Trolls -> 8
        for troll_id in [12, 24, 25, 28, 40, 43, 44, 45]:
            self.assertIn(f"case {troll_id}:", self.text)
        # Fel/Mag'har Orcs -> 2
        for orc_id in [23, 34, 49, 52]:
            self.assertIn(f"case {orc_id}:", self.text)
        # High/Void/Nightborne/San'layn Elves -> 10
        for elf_id in [14, 59, 60, 63, 72, 77, 80, 81]:
            self.assertIn(f"case {elf_id}:", self.text)
        # Earthen / Dark Iron Dwarves -> 3
        for dwarf_id in [27, 48, 68, 69]:
            self.assertIn(f"case {dwarf_id}:", self.text)
        # Taunka / Highmountain Tauren -> 6
        for tauren_id in [17, 41, 66]:
            self.assertIn(f"case {tauren_id}:", self.text)
        # Broken / Eredar / Lightforged Draenei -> 11
        for draenei_id in [22, 36, 55, 62]:
            self.assertIn(f"case {draenei_id}:", self.text)


if __name__ == "__main__":
    unittest.main()
