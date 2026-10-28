# DBM Offline Mechanic Import Pipeline

> # DBM IS NOT REQUIRED ON THE CLIENT
>
> The Deadly Boss Mods (DBM) repositories are used **only** as an offline development-time metadata source.
>
> - The generated mechanic registry is **purely server-side data**.
> - Players do **not** need DBM installed, enabled, or downloaded.
> - The WoW client `Interface/AddOns` folder does **not** contain DBM.
> - The Worldserver core does **not** execute any Lua scripts from DBM at runtime.
> - Once `mechanic_registry.generated.json` is generated, DBM reference sources can be deleted.

---

## 1. Architectural Overview

```text
DBM source repositories / Lua (development-time offline reference only)
            ↓
    tools/dbm_import/ (import_dbm.py, parser.py, validate.py)
            ↓
module/data/mechanics/mechanic_registry.generated.json
            ↓
module/data/mechanics/mechanic_registry.overrides.json (Manual CoA overrides)
            ↓
server-side MechanicRegistry (in-memory C++ lookup singleton)
            ↓
Real runtime combat events (spell cast start, aura applied, creature death)
            ↓
TacticalCalloutManager (deduplication & role checking)
            ↓
BotDialogue (localized lines, anti-spam, personality)
```

In runtime, there are zero calls or references to DBM Lua.

---

## 2. Core Invariant: Event-Driven Execution

> **Having a mechanic in the registry NEVER causes a bot to speak in advance.**

A tactical callout is only emitted when all of the following conditions are met:
1. A real combat event occurs on the server (e.g., boss starts casting spell `69076` or aura `71289` is applied).
2. The spell ID and encounter/creature are looked up in `MechanicRegistry`.
3. The mechanic is active (`disabled == false`) and has no unhandled contradictions (`confidence != Conflict`).
4. The bot has a role matching the mechanic's `roleMask` (Tank, Healer, DPS) and possesses the required capability (e.g., has an interrupt spell for `Interrupt`).
5. The mechanic instance has not already been called out by another group member within the deduplication window (`TTL = 6000ms`).

---

## 3. Data Hierarchy & Confidence Model

DBM is not authoritative over server scripts. Conflicting data is resolved through strict precedence:

```text
ManualOverride (CoA-specific authored data in mechanic_registry.overrides.json)
    >
CoreScript (Authoritative C++ server scripts in src/server/scripts)
    >
DBM (Imported offline metadata from DBM Lua modules)
```

### Confidence Levels
- `Authoritative`: Explicit manual CoA override (`source = Manual`).
- `Verified`: Spell and/or creature ID cross-referenced and confirmed in C++ server scripts (`source = Core` / `DBM`).
- `Imported`: Parsed directly from DBM source modules without explicit core match (`source = DBM`).
- `Heuristic`: Inferred through heuristics.
- `Conflict`: Ambiguous or contradictory definitions (e.g. Spread vs Stack on same spell). Excluded from automatic critical callouts until overridden.

---

## 4. Semantic Mechanics Mapping

DBM constructs are translated into semantic mechanics and corresponding `DialogueBeat` values:

| DBM Construct | Semantic Type | Dialogue Beat | Default Role Mask | Default Priority |
|---|---|---|---|---|
| `NewSpecialWarningInterrupt` | `Interrupt` | `Interrupt` | Tank, Dps | Critical |
| `NewSpecialWarningDispel` | `Dispel` | `Dispel` | Healer | High |
| `NewSpecialWarningGTFO` / `Move` | `MoveFromGround` | `MoveFromGround` | Everyone | Critical / High |
| `NewSpecialWarningRun` | `RunAway` | `RunAway` | Everyone | Critical |
| `NewSpecialWarningMoveAway` / `Spread` | `SpreadOut` | `SpreadOut` | Everyone | Critical |
| `NewSpecialWarningStack` | `StackTogether` | `StackTogether` | Everyone | Critical |
| `NewSpecialWarningTaunt` | `TankSwap` | `TankSwap` | Tank | Critical |
| `NewSpecialWarningDefensive` | `Defensive` | `Defensive` | Tank, Healer | Critical |
| `NewSpecialWarningSwitch` | `TargetSwitch` | `TargetSwitch` | Dps | High |
| `NewSpecialWarningReflect` | `StopDamage` | `StopDamage` | Everyone | Critical |
| `NewAddsLeftAnnounce` | `KillAdds` | `KillAdds` | Dps | High |
| `NewPhaseAnnounce` / `Stage` | `PhaseChange` | `PhaseChange` | Everyone | Normal |
| `NewSpecialWarningSpell` | `IncomingAoE` | `IncomingAoE` | Everyone | High |

---

## 5. In-Game Management Commands

```text
.botcmd mechanics status
    Displays total loaded records, verified count, overrides, disabled records, and unique spells/encounters.

.botcmd mechanics reload
    Hot-reloads mechanic_registry.generated.json and mechanic_registry.overrides.json from disk.

.botcmd mechanics spell <spellId>
    Inspects registry metadata for a specific spell (e.g., .botcmd mechanics spell 69076).

.botcmd mechanics encounter <encounterId>
    Inspects all mechanics registered for an encounter (e.g., .botcmd mechanics encounter 845).
```

---

## 6. Client Independence Verification

1. **Client verification**: `Interface/AddOns` in the game client contains no DBM folders.
2. **Runtime verification**: `worldserver.exe` does not link to Lua or load `.lua` files.
3. **Build verification**: Server builds and unit tests pass with all DBM sources removed from disk.
