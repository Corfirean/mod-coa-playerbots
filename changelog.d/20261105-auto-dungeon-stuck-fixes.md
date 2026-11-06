---
area: bots
type: fixed
audience: players
title: Bot groups no longer stand idle in Uldaman, Mechanar, Arcatraz, Azjol-Nerub, Halls of Stone and other dungeons
---
Three ways a dungeon run could stop for good are fixed. The Lost Dwarves of Uldaman are friendly in the database, so the group could not start the fight: the tank now turns them hostile when it reaches them. A boss that walks around its room (Soccothrates in Arcatraz) is now approached where it stands, not at its spawn point. An objective that fails twice (the Cache of the Legion chest in Mechanar, Lieutenant Drake in Old Hillsbrad) is written off, so the group goes on to the rest of the dungeon instead of coming back to it again and again.

A group that stopped on a hand-authored route point in Azjol-Nerub, Halls of Lightning, Blackrock Spire and similar dungeons now walks the point in a straight line when the pathfinder refuses it.

Talking to an NPC in a scripted event (Brann in the Halls of Stone, Thrall, Arthas) now picks the menu option by its real id, so the NPC reacts instead of silently ignoring the choice. The Tribunal of Ages in the Halls of Stone has its driver. A repeatable event that only yields (the Halls of Lightning slag pit) no longer takes every tick from the route.

Old Hillsbrad's bomb steps look for the barrel of their own house instead of the one nearest the tank. A walk in a scripted event that stops getting closer (or that the pathfinder refuses) is finished in a straight line. A group thrown off the route (into the Halls of Lightning's slag pit) goes back to the nearest point of it, and the pit's Slags are no longer pulled from the route.

When neither the pathfinder nor a straight walk reaches an object a scripted step needs (Old Hillsbrad's north-east barrels), the party is put next to it. The gossip choice of a scripted step is logged.

- Utgarde Pinnacle: bots now turn towards Grauf before firing the harpoon launcher (the harpoon is a cone spell cast by the player), so each shot hits Grauf for 35 % of his health; the idle-combat reset no longer runs while Skadi's gauntlet is in progress (it made Skadi evade and reset the fight).

- Gundrak: Drakkari Elemental and Drakkari Colossus got boss tuning in BotScaling (bots deal more, take less), since the elemental trash killed every level 60 group.

- Utgarde Pinnacle: Skadi is not a target while he idles at his spawn in front of the gauntlet (DcNeverTargetRegistry row). Attacking him there kept the party in a combat that never became the encounter, so the objective that trips the gauntlet was never reached. With the harpoon facing fix the dungeon is now cleared 4 of 4 times in the sandbox.
- Diagnostics: the bot debug state lists who a bot is in combat with, its attackers and its harmful auras.
