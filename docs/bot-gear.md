# Bot equipment

New bots, the gear trainer and automatic bag upgrades share one equipment policy.
Fishing weapon restoration also follows it. Profession tools and bags are preserved.

Items must meet their required level and the bot's actual class/race/proficiency checks.
Holiday items, VanityCollection.dbc entries, event vendor items, event creature/object loot
(including nested reference loot), seasonal/event quest rewards and test placeholders are excluded.
An unavailable vanity catalogue stops automatic selection instead of permitting unchecked items.

| Bot level | Maximum item level | Maximum quality | Maximum PvE/PvP Power per item |
| --- | ---: | --- | ---: |
| 1–10 | 20 | Uncommon | 0 |
| 11–20 | 30 | Rare | 0 |
| 21–30 | 45 | Rare | 0 |
| 31–40 | 55 | Rare | 0 |
| 41–50 | 65 | Rare | 0 |
| 51–59 | 75 | Rare | 0 |
| 60–79 | 200 | Epic | 15 |
| 80 | 232 | Epic | 25 |

These are conservative baseline ceilings, not a promise of a full set or raid-best gear.
Required level alone is insufficient: for example item 42886 has required level 0 but item level 174.
Older suitable items can fill slots whose current level has no candidates; unsafe items never serve as a fallback.

PvE bots favour PvE Power, PvP bots favour PvP Power. Opposite-only power items are rejected.
The mode is recorded when a bot joins the dungeon or battleground queue; switching mode queues a repair.
The known CoA power spells are PvE 101600–101699, PvP 101700–101799 and PvP 9930954–9930957.
Role-aware stat scoring and the bot's armour/weapon preferences still apply.

## Repair existing bots

Use the GM/server command `.fixbotgear` (console/RA: `fixbotgear`). It queues all characters
on recognised bot accounts, including offline bots. It never logs offline bots in to repair them.
One idle online bot is processed every two seconds. Bots in combat, trading, dead, teleporting or
inside a battleground/arena wait; offline bots do not block ready online bots.

- `.fixbotgear status` reports the remaining queue.
- `.fixbotgear cancel` cancels pending work and keeps completed repairs.
- `.botcmd geartrainer` uses the same queue. A single-guid trainer repairs only an idle bot.

Suitable occupied slots are kept. Unsuitable items are moved into the bot's bags, retaining their
item GUID, enchants and other instance data. If no replacement exists, the unsuitable slot is
cleared into the bags. Full bags prevent replacement and keep the bot pending.
Archived items are protected from automatic junk cleanup and economy sales.

The character database contains `coa_bot_gear_queue` and `coa_bot_gear_history`.
History records the bot, original slot, item GUID and entry for manual recovery; the original
items remain in the bags. Repair saves and queue completion share a database transaction.
Pending work resumes after a server restart. The server log records preserved gear.

## Offline factory

The offline factory applies the same level, quality, power and source restrictions.
It defaults to PvE gear. It loads its exclusions before creating any accounts or characters.
VanityCollection.dbc is read from the repack's Data/dbc directory; use `--dbc-dir` when the
MySQL executable is outside the repack. Missing or malformed DBC data aborts generation.
