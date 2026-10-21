---
area: bots
type: fixed
audience: players
title: Auto-dungeon tanks find their way through caves and corridors
---
Long walks of a bot (the auto-dungeon tank heading for a boss, among others) were made in straight-line steps of 40 yards. In a cave or a corridor the first step often ended inside rock, no path existed, and the bot never moved. When a step has no path, the bot now asks for the path to the real destination and follows the navigation mesh.
