# Open-world AI audit (2026-09-24) — the state before the WorldBrain rework

Read before touching `src/world/`. This is the ownership map of the open-world layer as it was
**before** the rework in `docs/open-world-ai.md`, with the file-level evidence for every problem
the rework was built to remove. Line numbers are from the pre-rework commit `079dc1c`.

## Who decided what

| Question | Owner before the rework | Evidence |
| :--- | :--- | :--- |
| What kind of activity is the bot doing? | `UpdateSoloIntent` (`BotAI.cpp:477`): a personality-weighted 15–35 minute "intent" (Quest/Gather/Fish/Grind/Explore) | its only effect is the *order* of the `Try*` calls in `UpdateOffensive`'s idle branch (`BotAI.cpp:3392-3413`) |
| Which concrete errand? | `BotWorldBehavior::UpdateBeforeSolo/UpdateAfterSolo` (`world/BotWorldBehavior.cpp:874/1002`), independently | runs before *and* after the solo scans, starts its own walks |
| Where is the quest objective? | `BotQuestTracker::FindNearestQuestObjective` (`BotQuestTracker.cpp:111`): the **nearest static spawn coordinate** of any wanted entry on the map | returns a `Position`, nothing else |
| Who walks the bot there? | `TryStartQuesting` step 2 (`BotAI.cpp:2822`) sets `isQuestTrackingWalk` + a `Position`; `TryContinueQuestTrackingWalk` (`BotAI.cpp:2659`) walks it | the walk state holds a coordinate only: no quest id, no objective, no target, no phase |
| Who attacks quest mobs? | `TryGrindWhenSolo` (`BotAI.cpp:1777`): prefers wanted entries within 30 yd | the grind scan is the hidden quest executor |
| Who decides an objective is done? | nobody explicitly: `CollectQuestObjectiveEntries` is re-derived on every scan and a done objective simply stops appearing | `BotAI.cpp:1408` |
| Who returns the bot to the quest giver? | `TryStartQuesting` step 1 (`FindNearestQuestTurnIn`) — only when the intent order reaches `TryStartQuesting` at all | Grind intent never calls it (`BotAI.cpp:3402`) → a grinder never turns anything in |
| What happens on arrival? | `TryContinueQuestTrackingWalk` ends the walk within `INTERACTION_DISTANCE` (5 yd) of the spawn coordinate and re-anchors the grind anchor there | `BotAI.cpp:2661-2670` |
| Who owns movement? | `BotMovement` claims by `MoveOwner` priority — but every call was `Clear()` + `MovePoint()` | `BotMovement.cpp:65-75` |

## Confirmed problems (all live-observed symptoms have a code cause here)

1. **Quest tracking knew a coordinate, not a goal.** `isQuestTrackingWalk` + `questTrackingTargetPos`
   were the entire state. On arrival nothing knew what the trip was for, so the bot "finished" by
   standing on the spawn point. `TryStartQuesting` then picked the nearest static spawn again — the
   one it was standing on — and ended the walk immediately: the *permanently standing on a spawn*
   symptom.
2. **Kill execution lived in grinding.** Quest mobs were only attacked by `TryGrindWhenSolo`'s
   30-yard scan, which never ran while a tracking walk was active (`BotAI.cpp:3343`) and, for the
   Quest intent, only on ticks where `TryStartQuesting`'s own 5 s scan throttle made it return
   false first.
3. **Static spawns used as destinations.** `FindNearestQuestObjective` sends every bot with the
   same quest to the *same* nearest spawn coordinate — no notion of the spawn being dead, taken,
   unreachable, or occupied by 40 other bots. It also ran a full scan of every spawn of every
   wanted entry on every call.
4. **Movement re-issued every tick.** `TryContinueQuestTrackingWalk` called `MoveBotToPoint` every
   tick (`BotAI.cpp:2693`) and `TryGrindWhenSolo` did the same while outside its leash
   (`BotAI.cpp:1803`); `BotMovement::MoveTo` cleared and re-issued `MovePoint` on every call —
   path generation and spline restart each tick.
5. **No stuck detection for quest walks.** A tracking walk toward an unreachable point had no timeout
   or progress check at all (only the ambient layer and gathering had budgets).
6. **Five subsystems moving one bot.** SoloIntent's scan order, the quest walks, the grind anchor
   (walk-back), the gather walk, and ambient errands each issued their own movement; the old grind
   anchor could march a bot back across the area after any of the others moved it.
7. **Everything on offer was accepted.** `ProcessQuestGiver` took every quest `CanTakeQuest` allowed
   — escorts, scripted events, other-continent quests — which then sat in the log forever, and the
   reward was always choice 0 (`PickQuestRewardIndex`).
8. **Quest drops were never looted.** Every loot path (`TryProcessPendingLoot`, gathering) iterated
   `loot.items` only; quest-required drops live in `loot.quest_items`, reached through slots after
   the regular items (`Loot::LootItemInSlot`). Collect quests could not progress even when the right
   mob died.
9. **Chest-type quest objects were unusable.** `GameObject::Use()` has no chest case; the old code
   (correctly) skipped chests, so "collect N items from crates" quests had no path at all.
10. **Explore quests could never complete.** Area triggers are detected client-side
    (`CMSG_AREATRIGGER`); a null-socket bot never sends it, and nothing sent it on its behalf.
11. **Level relocation ignored quest state.** `BotZoneProgression::RelocateBot` teleported a bot out
    of a zone mid-questline as soon as its level passed the zone's band.

## What was kept

`MoveOwner` claims (made persistent/idempotent), `BotWorldPoi` (still the ambient layer's index; the
quest layer has its own indices), personalities and SoloIntent (now a bias the brain reads),
gathering/fishing/grinding executors (now run only on the brain's directive), the loot queue, mount
and taxi support, `Player` quest APIs, zone hubs (now a fallback), the ambient errands.
