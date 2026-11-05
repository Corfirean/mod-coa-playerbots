---
area: bots
type: fixed
audience: players
title: Bot groups no longer stand idle in Uldaman, Mechanar, Arcatraz, Azjol-Nerub, Halls of Stone and other dungeons
---
Three ways a dungeon run could stop for good are fixed. The Lost Dwarves of Uldaman are friendly in the database, so the group could not start the fight: the tank now turns them hostile when it reaches them. A boss that walks around its room (Soccothrates in Arcatraz) is now approached where it stands, not at its spawn point. An objective that fails twice (the Cache of the Legion chest in Mechanar, Lieutenant Drake in Old Hillsbrad) is written off, so the group goes on to the rest of the dungeon instead of coming back to it again and again.

A group that stopped on a hand-authored route point in Azjol-Nerub, Halls of Lightning, Blackrock Spire and similar dungeons now walks the point in a straight line when the pathfinder refuses it.

Talking to an NPC in a scripted event (Brann in the Halls of Stone, Thrall, Arthas) now picks the menu option by its real id, so the NPC reacts instead of silently ignoring the choice. The Tribunal of Ages in the Halls of Stone has its driver. A repeatable event that only yields (the Halls of Lightning slag pit) no longer takes every tick from the route.
