---
area: bots
type: added
audience: both
title: Dynamic open-world bot encounters (WorldEncounterDirector)
---
Bots travelling through the open world can now take part in rare dynamic encounters managed by `WorldEncounterDirector`: roadside duels, travelling caravans, rival adventuring parties, faction skirmishes, patrol ambushes, rare-creature hunts, deserters, watchers, assassin contracts, bounty hunters, nemesis rematches, and mystery encounters. Encounters temporarily borrow nearby ungrouped bots, respect all PvP and safe-zone rules, and return bots to their normal `WorldBrain` tasks when finished. Server owners can tune or disable individual encounter types under `BotAI.DynamicEncounters.*` in `mod_coa_playerbots.conf` and inspect or trigger encounters live with `.botcmd encounter`.
