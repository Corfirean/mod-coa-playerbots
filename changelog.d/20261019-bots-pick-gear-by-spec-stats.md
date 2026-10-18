---
area: bots
type: fixed
audience: players
title: Bots choose armor and weapons for the stat their specialization actually uses
---
Bots picked gear from a guess made per class, with the ids of Reaper, Primalist and Runemaster mixed up and no way to tell the specs of one class apart. Sanguine, Artificer, Infinite and Time need Spirit, Accursed, Zealot, Engravement and Riftblade need Agility, Stormbringer and Venomancer casters need Intellect, and the Sun Cleric damage specs were not listed at all, so these bots often wore items with none of their main stat. Gear is now scored by the main and secondary stat of the bot's active specialization (taken from the Sidekick stat priorities) and by attack power or spell power only where the spec uses it.
