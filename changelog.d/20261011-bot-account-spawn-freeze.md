---
area: bots
type: fixed
audience: players
title: Random bot spawning and auto-fill no longer leak accounts or freeze the server
---
BG and LFG auto-fill now reuses existing offline bots instead of creating duplicate characters. Account lookup is indexed to prevent hitches, bot accounts are cleaned up on purge, and `CoaBots.Enable = 0` completely disables all bot background activity.
