---
area: bots
type: fixed
audience: players
title: Random bot spawning and auto-fill no longer leak accounts or freeze the server
---
Battleground and Dungeon Finder auto-fill now reuses existing offline bots instead of creating duplicate characters. Account lookup is now indexed to prevent tick hitches, and bot accounts are fully cleaned up when purged.
