"""The Reborn smoke check reads `.botcmd abilities` output; this keeps its parsing and its verdicts honest without a server."""
import importlib.util
import pathlib
import unittest

CHECK = pathlib.Path(__file__).resolve().parents[2] / "tools" / "reborn_bot_check" / "check.py"
spec = importlib.util.spec_from_file_location("reborn_check", CHECK)
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)

HUNTER = """Abilities of 'Mattingly': class 3, level 60, spec 2, role DPS, shapeshift form 0.
  ruleset: server=Reborn bot=Reborn (RealmID=3, CoA.ClassModel='wcr', GameModes.Realm='').
  profile 'Hunter_MM_Dps': 17 abilities (spec 2).
  [ok] Hunter's Mark root 1130 -> 1130
  [NOT KNOWN] Kill Shot root 53351: HasSpell(root)=false HasSpell(twin 1153351)=false SpellInfo(root)=true SpellInfo(twin)=true
  => 14/17 profile abilities resolved (0 via Reborn twin, 0 blocked by current form).
  spellbook: 640 active spells, 120 usable (non-passive), 88 with id >= 1100000.
  talent ranks picked: 51 (free-pick build); pet: Mangy Wolf (level 60, 0 talent points unspent).
  gear: 16/17 slots filled, average item level 62, ammo 2515 x1000."""

BARE_WARLOCK = """Abilities of 'Nobody': class 9, level 40, spec 1, role DPS, shapeshift form 0.
  NO combat profile for this class/spec/role: the bot uses the generic legacy rotation.
  talent ranks picked: 2 (free-pick build); pet: none.
  gear: 6/17 slots filled, average item level 9."""

ROGUE = """Abilities of 'Sneak': class 4, level 30, spec 1, role DPS, shapeshift form 0.
  => 7/9 profile abilities resolved (0 via Reborn twin, 0 blocked by current form).
  talent ranks picked: 21 (free-pick build); pet: none.
  gear: 15/17 slots filled, average item level 30, main imbue 2060011 (3000s left), off imbue none."""


class RebornCheckTests(unittest.TestCase):
    def test_a_healthy_hunter_passes(self):
        info = check.parse(HUNTER)
        self.assertEqual((info["spec"], info["talents"], info["pet"], info["ammo"], info["slots"], info["ilvl"]),
                         (2, 51, "Mangy Wolf", 1000, 16, 62))
        self.assertEqual(check.judge(3, 60, info), [])

    def test_a_bare_warlock_is_flagged_everywhere(self):
        problems = check.judge(9, 40, check.parse(BARE_WARLOCK))
        text = " | ".join(problems)
        for expected in ("talent ranks", "no combat profile", "gear slots", "without a demon"):
            self.assertIn(expected, text)

    def test_a_hunter_without_ammo_or_with_unspent_pet_points_fails(self):
        out = HUNTER.replace("0 talent points unspent", "5 talent points unspent").replace(", ammo 2515 x1000", "")
        problems = " | ".join(check.judge(3, 60, check.parse(out)))
        self.assertIn("unspent talent points", problems)
        self.assertIn("without ammunition", problems)

    def test_rogue_poison_is_read_per_hand(self):
        info = check.parse(ROGUE)
        self.assertEqual((info["imbue_main"], check.judge(4, 30, info)), ("2060011", []))
        bare = check.parse(ROGUE.replace("main imbue 2060011 (3000s left)", "main imbue none"))
        self.assertIn("poison", " ".join(check.judge(4, 30, bare)))


if __name__ == "__main__":
    unittest.main()
