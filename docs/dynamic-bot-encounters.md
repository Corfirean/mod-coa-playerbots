# Dynamic Bot Encounters (`WorldEncounterDirector`)

## 1. Architecture Overview

`WorldEncounterDirector` orchestrates rare, organic open-world events using **existing online playerbots** without creating a second `WorldBrain`.

Core principle:
> Dynamic Encounters temporarily borrow the open-world intent of eligible ungrouped playerbots (`WorldExecutor::PauseTask` + `BrainState::pausedByEncounter = true`), drive them through a finite encounter state machine (`Candidate -> Prepare -> Active -> Resolve -> Cooldown`) using `MoveOwner::Encounter`, and cleanly restore their normal `WorldBrain` task when the encounter finishes or is preempted.

### Module Structure (`module/src/world/encounters/`)

| File | Responsibility |
| --- | --- |
| `EncounterTypes.h` | Core enums (`EncounterType`, `EncounterPhase`, `EncounterStopReason`, `EncounterRole`, `NarratorChannel`), constants, and string/parse helpers. |
| `EncounterParticipant.h` | `EncounterParticipant` state (movement token, origin coordinates, temporary visual/stealth/stand state) and `EncounterOwnershipTable` + `EncounterPriorityArbiter`. |
| `EncounterState.h` | Finite state machine (`Candidate -> Prepare -> Active -> Resolve -> Cooldown`) with per-phase deadlines and terminal stop reasons. |
| `EncounterInstance.h` | Encounter instance container (participants, target player/creature GUID, waypoints, duel/stalk tracking, diagnostic formatter). |
| `EncounterConfig.h` / `.cpp` | `BotAI.DynamicEncounters.*` configuration loader and settings store (`EncounterSettings`). |
| `EncounterBudget.h` | `EncounterBudgetTracker`: global, per-map, and per-player active caps + per-player, per-hostile, per-bot, per-type, and bounty cooldowns. |
| `EncounterEligibility.h` | Strict eligibility checks for target real players and candidate bots (PvP rules, contested/sanctuary/capital checks, level deltas, party size caps, ongoing termination checks). |
| `EncounterPersonality.h` | Maps existing `WorldPersona` (`grinding`, `questing`, `gathering`, `fishing`, `patience`, `sociability`, `lean`) to encounter affinity weights and cowardly retreat thresholds. |
| `EncounterNarrator.h` / `.cpp` | Lightweight, non-spammy atmospheric message layer (`/say`, `/yell`, `/emote`, `/whisper`, and contextual player atmospheric hints) with per-player and per-bot cooldowns. |
| `EncounterMemory.h` | Lightweight in-memory notoriety tracker (`BountyHunter`) and player-bot PvP grudge tracker (`Nemesis`) with TTL and bounded eviction. |
| `EncounterHandlers.h` | Handler declarations and shared participant restoration helpers. |
| `EncounterHandlersAmbient.cpp` | Handlers for `RoadDuel`, `Caravan`, `Deserter`, `RivalAdventuringParty`, and `RareHunter`. |
| `EncounterHandlersHostile.cpp` | Handlers for `AssassinContract`, `TheWatcher`, `FactionSkirmish`, `PatrolAmbush`, `BountyHunter`, `Nemesis`, and `HunterAndPrey`. |
| `EncounterHandlersMystery.cpp` | Handler for `MysteryEncounter` (non-combat atmospheric observer with optional visual/rune). |
| `WorldEncounterDirector.h` / `.cpp` | Staggered director loop, spatial candidate discovery, lifecycle management, `WorldBrain` integration hooks, and `.botcmd encounter` admin/debug interface. |

---

## 2. Lifecycle & State Machine

Every encounter progresses through:

```text
Candidate -> Prepare -> Active -> Resolve -> Cooldown
```

### Automatic Termination & Cleanup Conditions

An encounter terminates immediately and releases all participants when any of the following occurs:
- Target real player logs out (`PlayerLogout`);
- Participant bot logs out or despawns (`BotLogoutOrDespawn`);
- Participant or target dies (`ParticipantDeath`, `TargetRareDead`) — **no corpse camping**;
- Map change or entry into an instance/dungeon/battleground (`MapChanged`, `EnteredDungeonOrBG`);
- Manual bot command issued (`ManualBotCommand`);
- Bot joins a player-controlled group (`JoinedPlayerGroup`);
- Navigation returns `NavStatus::Stuck` (`Unreachable`);
- Phase timeout expires (`Timeout`);
- Distance exceeds `MaxChaseDistanceYards` (`DistanceTooFar`);
- Target enters a sanctuary, capital, safe zone, or loses PvP eligibility (`EnteredSafeZone`, `PvPEligibilityLost`);
- Target player's group grows beyond `MaxPlayerPartySizeForSoloHostile` during a solo hostile encounter (`PartyTooLarge`).

On termination (`FinalizeEncounter`):
1. `EncounterHandlers::RestoreParticipantState` releases `part.moveToken` and `MoveOwner::Encounter`, clears any encounter-applied stealth or sit state, restores original `DisplayId` if morphed, and despawns any temporary GameObject.
2. `EncounterOwnershipTable::ReleaseAllForEncounter` removes all bot reservations for the encounter.
3. `EncounterBudgetTracker::OnEncounterFinished` decrements active counters and starts per-bot, per-player, and per-type cooldowns.
4. On the bot's next `WorldBrain::Update` tick, `WorldEncounterDirector::UpdateBot` clears `brainState.pausedByEncounter` and calls `WorldExecutor::ResumeTask(bot, brainState)`.

---

## 3. Priority Rules & Ownership

1. **Single-encounter ownership**: `EncounterOwnershipTable` guarantees a bot belongs to at most one encounter at a time.
2. **Preemption (`EncounterPriorityArbiter`)**:
   - Player-controlled groups, dungeons/raids, battlegrounds/arenas, manual bot commands, corpse runs, active looting, and active `WorldSocial` assistance (`socialActive`) **always** take priority over Dynamic Encounters.
   - Bots in temporary `WorldParties` are not pulled into solo encounters (`AssassinContract`, `TheWatcher`, `Deserter`, `MysteryEncounter`).
3. **Movement Arbitration (`MoveOwner::Encounter`)**:
   - Added `MoveOwner::Encounter` to `BotMovementPrimitives.h` with priority `65` (above `World = 60` and `Errand = 50`, below `Quest = 70`, `Social = 80`, `Follow = 90`, `Loot = 100`, `Combat = 110`, `ManualStay = 120`, `Corpse = 130`).
   - Combat AI (`MoveOwner::Combat`) naturally overrides `MoveOwner::Encounter` during active combat chases without losing the encounter's high-level ownership.

---

## 4. Implemented Encounter Types (13 Types)

1. **`assassin_contract` (`AssassinContract`)**: Opposite-faction stealth-capable or high-aggression bot approaches a real player along a flanking arc, uses stealth when available, stalks briefly, optionally triggers a contextual player atmospheric hint once close, and attacks at an opportune moment. Ends immediately on kill, death, retreat, or safe-zone entry.
2. **`watcher` (`TheWatcher`)**: Cautious/patient bot observes a real player from 28–44 yards, periodically repositioning or stealthing. Retreats and finishes if the player closes within 15 yards.
3. **`faction_skirmish` (`FactionSkirmish`)**: 2–4 Alliance bots vs 2–4 Horde bots clash in a contested area. Real players are not forced targets and may ignore or join organically.
4. **`patrol_ambush` (`PatrolAmbush`)**: One faction's patrol walks toward a chokepoint while the opposing faction waits in ambush and strikes when the patrol enters 26 yards.
5. **`bounty_hunter` (`BountyHunter`)**: Triggered when a real player kills `Bounty.KillThreshold` (default 3) opposite-faction bots within `Bounty.WindowSec` (default 20 min). 1–2 opposite-faction bounty hunters pursue the player.
6. **`nemesis` (`Nemesis`)**: Lightweight in-memory grudge tracker (`EncounterMemoryStore`). When a bot and real player have prior PvP history, that specific bot can recognize the player (`/say`) and initiate a repeat confrontation or rival greeting.
7. **`road_duel` (`RoadDuel`)**: Two same-faction bots near a road/hub face off, bow, exchange controlled non-lethal blows down to a 20% HP floor, emote cheer/salute, restore health, and resume their tasks.
8. **`caravan` (`Caravan`)**: 2–5 same-faction bots travel together in formation toward a distant hub/POI, defending one another if attacked along the route.
9. **`deserter` (`Deserter`)**: A lone bot walks to a quiet roadside spot, sits down, speaks a weary atmospheric line if a real player is nearby, rests briefly, and moves on.
10. **`hunter_and_prey` (`HunterAndPrey`)**: One bot (`Prey`) flees along a weaving path while an opposing bot (`Hunter`) pursues; if the hunter catches up within 15 yards, PvP combat starts; if the prey survives 45 seconds, it escapes.
11. **`rival_party` (`RivalAdventuringParty`)**: 3–5 same-faction bots travel together through a quest/grind zone, engaging level-appropriate hostile mobs along their path.
12. **`rare_hunter` (`RareHunter`)**: When a live rare/rare-elite creature (`rank == CREATURE_ELITE_RARE` or `CREATURE_ELITE_RAREELITE`) is discovered nearby, 1–3 adventurous bots converge on it and engage it.
13. **`mystery` (`MysteryEncounter`)**: Very rare non-combat encounter. A lone bot observes from a distance, kneels to channel a built-in spell visual (and optional temporary rune GameObject), triggers a quiet atmospheric message, and departs.

---

## 5. Configuration (`BotAI.DynamicEncounters.*`)

All parameters live in `mod_coa_playerbots.conf` under `BotAI.DynamicEncounters.*` and can be reloaded live via `.botcmd encounter reload`:

- `BotAI.DynamicEncounters.Enable = 1`
- `BotAI.DynamicEncounters.<Type>.Enable = 1` (individual toggle for all 13 types)
- `BotAI.DynamicEncounters.ThinkIntervalMs = 5000`
- `BotAI.DynamicEncounters.RareScanIntervalMs = 20000`
- `BotAI.DynamicEncounters.MaxGlobalActive = 12`
- `BotAI.DynamicEncounters.MaxActivePerMap = 4`
- `BotAI.DynamicEncounters.MaxActiveNearPlayer = 1`
- `BotAI.DynamicEncounters.PlayerCooldownSec = 900`
- `BotAI.DynamicEncounters.PlayerHostileCooldownSec = 1500`
- `BotAI.DynamicEncounters.BotCooldownSec = 600`
- `BotAI.DynamicEncounters.TypeCooldownSec = 300`
- `BotAI.DynamicEncounters.MaxLevelDiffHostile = 4`
- `BotAI.DynamicEncounters.MaxLevelDiffAmbient = 8`
- `BotAI.DynamicEncounters.MaxPlayerPartySizeForSoloHostile = 2`
- `BotAI.DynamicEncounters.SearchRadiusYards = 220`
- `BotAI.DynamicEncounters.MaxChaseDistanceYards = 280`
- `BotAI.DynamicEncounters.MinDistanceFromCapitalYards = 180`
- `BotAI.DynamicEncounters.PrepareTimeoutSec = 90`
- `BotAI.DynamicEncounters.ActiveTimeoutSec = 180`
- `BotAI.DynamicEncounters.ResolveTimeoutSec = 20`
- `BotAI.DynamicEncounters.Narrator.Enable = 1`
- `BotAI.DynamicEncounters.Narrator.PlayerCooldownSec = 120`
- `BotAI.DynamicEncounters.Narrator.BotCooldownSec = 45`
- `BotAI.DynamicEncounters.Narrator.MaxRangeYards = 45`
- `BotAI.DynamicEncounters.Bounty.KillThreshold = 3`
- `BotAI.DynamicEncounters.Bounty.WindowSec = 1200`
- `BotAI.DynamicEncounters.Bounty.CooldownSec = 1800`
- `BotAI.DynamicEncounters.Nemesis.MemoryTtlSec = 3600`
- `BotAI.DynamicEncounters.Nemesis.MaxTrackedPairs = 256`
- `BotAI.DynamicEncounters.Nemesis.MinKillsForTrigger = 1`

---

## 6. Performance Model

- **No per-tick global scans**: `WorldEncounterDirector::Update` runs once every `ThinkIntervalMs` (default 5000 ms) from `WorldBrain::GlobalUpdate`.
- **Single-anchor spatial queries**: Each evaluation cycle samples at most 1 real player and 1 open-world anchor using a round-robin cursor and queries `Cell::VisitObjects` only within `SearchRadiusYards` (default 220 yards).
- **Zero database I/O**: All state (active encounters, cooldowns, notoriety, Nemesis pairs) is strictly in-memory with bounded sizes (`MaxTrackedPairs = 256`) and automatic TTL expiration.
- **Lazy hub lookup**: `Caravan` and `RivalAdventuringParty` reuse `BotWorldPoi::Query` to pick nearby destinations without grid scans.

---

## 7. Admin & Debug Commands

All commands are available under `.botcmd encounter`:

```text
.botcmd encounter status
.botcmd encounter list
.botcmd encounter start <type> [player]
.botcmd encounter stop <id|all>
.botcmd encounter debug <on|off>
.botcmd encounter cooldown reset [player]
.botcmd encounter reload
```

Supported `<type>` names:
`assassin_contract`, `watcher`, `faction_skirmish`, `patrol_ambush`, `bounty_hunter`, `nemesis`, `road_duel`, `caravan`, `deserter`, `hunter_and_prey`, `rival_party`, `rare_hunter`, `mystery`.

---

## 8. Live Validation Checklist

1. Check director status: `.botcmd encounter status`
2. Enable debug logging: `.botcmd encounter debug on`
3. Start an ambient encounter near your character: `.botcmd encounter start road_duel` or `.botcmd encounter start caravan`
4. Inspect active encounters: `.botcmd encounter list`
5. Verify participant bot brain status via `.botcmd brain` (shows task paused for dynamic encounter)
6. Stop encounter via `.botcmd encounter stop all` and confirm bots immediately resume their prior `WorldBrain` task.
7. Reset cooldowns for rapid testing: `.botcmd encounter cooldown reset`

---

## 9. Known Limitations

- **In-memory Nemesis & Bounty history**: Grudge and notoriety records are stored in memory with configurable TTLs (`Nemesis.MemoryTtlSec`, `Bounty.WindowSec`) and intentionally reset on server restart to avoid extra database schema/query overhead.
- **Capital distance estimation**: Safe-zone and capital protection primarily relies on DBC `AreaTableEntry` flags (`AREA_FLAG_CAPITAL`, `AREA_FLAG_SANCTUARY`), `PLAYER_FLAGS_RESTING`, and `BotAI::IsInCity`.
