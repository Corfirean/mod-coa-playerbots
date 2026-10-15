# Log-informed Ranger and Mechanics correction

Evidence: `old-logs.zip`, particularly 2026-08-15-11.24.03 (Ranger) and
2026-05-27-21.30.02 (Tinker). Guild identity and optimality are unverified.

- Logged Quick Shot 572732 is local rank 7 of 500074, Hunting Shot 547205 is
  rank 5 of 801191, Skullpiercer 501721 is rank 8 of 802036. Retain root IDs
  and resolve the highest learned rank; do not hardcode players' observed ranks.
- Quick Shot followed by Advantage 804329 and Skullpiercer followed by its
  removal demonstrates a builder/spender sequence. Keep native costs and aura
  gates; remove the erroneous family-wide Advantage requirement from the bot
  query adapter, matching native CheckCast's CasterAuraSpell condition.
- Replace stock mana Aimed Shot with CoA Hunting Shot. Ranger readiness checks
  its actual native power bar. Skullpiercer is usable outside execute range.
- Logged Build: Mechsuit 801384 is a native combat spell; 92141 is the local
  specialization spell. Use 801384 for preparation and baseline recovery.
  Makeshift Dynamite is present in the logs, but native Scrap generation
  requires Shotgun root 500549 or Sticky Bomb while specialization marker
  92141 is active. Use instant Sticky Bomb for setup; arena reset preserves/restores the
  learned specialization marker without granting Scrap or the combat form.

These changes correct verified mechanics rather than fitting scores to cast
frequency. Other classes retain their profiles until their class/spec, local
spell chains, and relevant encounter context are verified. Raid damage is not
an arena benchmark. All baseline and validation telemetry is retained.

## Follow-up cycle (2026-10-06)

The first full validation completed 12 episodes. All four candidates cast, but
Mechanics repeatedly built Mechsuit, whose aura disappeared about 100 ms after
application while Scrap remained at ten. Native Mechsuit uses mounted aura
803451; combat dismount and stale-mount cleanup removed it and its lifecycle
removed Mechsuit. Exempt the native `IsInTinkerMechsuit()` combat state from
travel-mount teardown, without changing native form or resource rules.

Resolve Mechanics Shotgun through native root 500549 instead of legacy 801647.
Ability range evaluation now matches `Spell::CheckRange` combat reach/melee
leeway instead of comparing raw distance against DBC range. Native cast checks
remain authoritative. This addresses false rejections near large arena targets;
unlearned abilities and genuine aura/resource requirements remain enforced.

Cycle two restores each candidate and helper's item entries from the first
episode of the preceding full cycle, verifies the equip response, and checks
completed report gear against the saved manifest. This fixes the gear baseline
for the next comparison; spellbook and other normalization fields must still
be compared before attributing DPS differences. Short smoke seed 4003 is
separate from the full three-by-180-second series using seeds 3000..3002.

The first smoke exposed another blocker: the Mechanics profile had none of the
native Mechsuit action-bar attacks. Add Gatling Gun (500213), Combustion (801387)
and Laser Beam (805372), resolving learned ranks and retaining native cooldowns
and Scrap costs. Do not select Shotgun while inside Mechsuit. A second smoke
uses seed 4004 and retains all seed-4003 reports.

Ranger's repeated `SPELL_FAILED_ONLY_SHAPESHIFT` (94) attempts also starved its
legal filler. Pre-filter the native `CheckShapeshift` requirement, honoring
`SPELL_AURA_MOD_IGNORE_SHAPESHIFT` exactly as the native cast does. This changes
selection rather than granting stances or bypassing requirements.

## Combat-loop reconstruction

Full reference-file cast sequences are preserved in private
`reports/reference-rotation-sequences.json`. Nimrose uses Quick Shot, Hunting
Shot, Skullpiercer and Instinct; Elfbar uses Sticky Bomb, Sentry Turret,
deconstruction, Mechsuit and Laser Beam. These are observations of these
players, not proof of every spec's optimal build or transferable raid DPS.

Confirmed blockers and module-only corrections:

- This core's `ChaseMovementGenerator::PositionOkay` checks the maximum range,
  not the minimum. An explicit grounded move takes ranged bots out of the dead
  zone before stationary attacks. Support movement now resolves its Support
  profile rather than searching for a nonexistent DPS profile of that spec.
- Fallback repeatedly cast Woodsman's Adaptation (803666), costing 20 Focus,
  even while its aura was already present. These four profiles own their combat
  waits through an opt-in strategy policy; other specs retain legacy fallback.
  The Ranger profile maintains Adaptation once and recovers Focus with Instinct.
- Ranged Rangers build Advantage before spending it: prioritize Quick Shot
  below two stacks, allow Skullpiercer at three; preserve native costs and gates.
- Elude is forbidden in combat. Brigand prepares it before pull; combat-only
  selection filters out-of-combat abilities. Assault is available as a normal
  melee attack. Native stealth checks remain in force after the opener.
- Mechanics stockpiles forty Scrap, uses Sticky Bomb and native Scrap Shot
  when Sticky Bomb is on cooldown, then closes in once Mechsuit is active.
  Native ranged minimum distance applies even when the DBC minimum is zero.
  Combustion is eligible on a single target rather than disabled as pack-only.
- The open-world stalemate rule is inappropriate for an intentionally immortal
  training boss. Suppress only that trigger for owned training actors; retain
  lethal escape and all ordinary-world behavior. This also changes helper
  behavior, so before/after party outcomes are not pure candidate-rotation effects.

Telemetry now records position, target distance, native movement-block state,
motion generator and protected cast. Autorepeat counts in historical `castingMs`;
it must not be interpreted as productive cast time or ability usage.

All short diagnostics use new seeds/statuses and preserve earlier attempts.
Long validation retains gear and roster snapshots, checks native attack casts
and resource cycles rather than counting repeated buffs as success.

Ranged retreat starts below 60% of the preferred distance; chase stops at 70%.
This hysteresis prevents a rounding-edge loop that stopped Farstrider's action
selection for roughly twenty seconds at a time. Diagnostic seed 4104 restored
continuous selection (maximum observed decision gap 670ms) and 24 Quick Shots
over sixty seconds. Mechanics still fails sustained threat retention in the
stress scenario; productive attacks alone do not establish tank readiness.

## Mechanics role correction

Mechanics (spec 50) is ranged DPS, not a tank. The runtime role mapping and
profile previously contradicted the DPS classification in roles.md. Correct
the mapping and profile to DPS, maintain ranged distance in both Scrap and
Mechsuit windows, and remove taunt priorities. Historical THREAT_STRESS
measurements remain preserved but cannot establish ranged-DPS quality or
a threat-retention defect. Validate separately in ST_STATIC with no helpers;
this change in scenario/roster precludes a pure before/after DPS claim.

User reference: https://ascensionsidekick.com/tinker/mechanics (2026-10-06).
