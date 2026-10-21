---
area: bots
type: fixed
audience: players
title: Auto-dungeon tanks find their way through caves and corridors
---
A bot could not start a walk whose route was longer than about 300 yards: the game refuses such a path for bots, and in a winding dungeon like Ragefire Chasm the way to the first boss alone is about 470 yards, so the auto-dungeon tank stood at the entrance. A long walk is now split along the navigation mesh into legs of up to 150 yards, and when a straight-line leg point lies inside rock the bot asks for the path to the real destination instead.
