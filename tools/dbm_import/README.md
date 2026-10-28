# DBM Offline Mechanic Importer

> **CRITICAL INVARIANT**:  
> **DBM IS NOT REQUIRED ON THE CLIENT.**  
> The DBM repositories are used **only** as an offline development-time metadata source.  
> Players do NOT need DBM installed, enabled, or downloaded.  
> The Worldserver core does NOT execute any DBM Lua files at runtime.  
> Once `mechanic_registry.generated.json` is generated, the DBM source directory can be safely deleted.

---

## 1. Overview & Pipeline

The DBM Offline Importer extracts boss encounter mechanics, spell identifiers, role targets, and warning classifications from Deadly Boss Mods (DBM) boss modules, producing a normalized JSON registry.

```text
DBM source repositories / Lua (offline development-time reference)
            ↓
    tools/dbm_import/ (import_dbm.py, parser.py, validate.py)
            ↓
module/data/mechanics/mechanic_registry.generated.json
            ↓
module/data/mechanics/mechanic_registry.overrides.json (Manual CoA overrides)
            ↓
server-side MechanicRegistry (C++ in-memory singleton)
            ↓
Real runtime combat events (spell cast start, aura applied, etc.)
            ↓
TacticalCalloutManager & BotDialogue
```

---

## 2. Usage

To regenerate the mechanic registry:

```bash
python tools/dbm_import/import_dbm.py \
    --dbm-root C:/path/to/DBM-reference \
    --output module/data/mechanics/mechanic_registry.generated.json \
    --core-scripts C:/games/source/server/azerothcore-wotlk-coa/src/server/scripts \
    --overrides module/data/mechanics/mechanic_registry.overrides.json \
    --report docs/testing/DBM_MECHANIC_IMPORT_REPORT.md
```

### CLI Arguments
- `--dbm-root`: Root directory containing DBM packages (`DBM-MC`, `DBM-BWL`, `DBM-Icecrown`, `DBM-Party-WotLK`, etc.).
- `--output`: Output destination for the normalized generated JSON registry.
- `--core-scripts`: Optional path to AzerothCore / CoA C++ server scripts. Spells and creatures found in core scripts are automatically elevated to `confidence: "Verified"`.
- `--overrides`: Path to manual CoA overrides file. Overrides take precedence over imported values (`Manual > Core > DBM`).
- `--report`: Path to Markdown report output.

---

## 3. Supported DBM Constructs

The parser extracts mechanics and timers while ignoring all UI elements, textures, sounds, chat spam, frames, and client options:

| DBM Construct | Mapped Semantic Mechanic | Role Mask | Default Priority |
|---|---|---|---|
| `NewSpecialWarningInterrupt` | `Interrupt` | Tank, Dps | Critical |
| `NewSpecialWarningDispel` | `Dispel` | Healer | High |
| `NewSpecialWarningGTFO` / `NewSpecialWarningMove` | `MoveFromGround` | Tank, Healer, Dps | Critical / High |
| `NewSpecialWarningRun` / `NewSpecialWarningMoveAway` | `RunAway` / `SpreadOut` | Tank, Healer, Dps | Critical |
| `NewSpecialWarningSpread` / `NewSpecialWarningClose` | `SpreadOut` | Tank, Healer, Dps | Critical / High |
| `NewSpecialWarningStack` | `StackTogether` | Tank, Healer, Dps | Critical |
| `NewSpecialWarningTaunt` | `TankSwap` | Tank | Critical |
| `NewSpecialWarningDefensive` | `Defensive` | Tank, Healer | Critical |
| `NewSpecialWarningSwitch` | `TargetSwitch` | Dps | High |
| `NewSpecialWarningReflect` | `StopDamage` | Tank, Healer, Dps | Critical |
| `NewAddsLeftAnnounce` | `KillAdds` | Dps | High |
| `NewPhaseAnnounce` / `NewStageAnnounce` | `PhaseChange` | Tank, Healer, Dps | Normal |
| `NewCDTimer` / `NewNextTimer` / `NewCastTimer` | Sets `estimatedTimerMs` | - | - |

---

## 4. Tests

Run the importer unit tests:

```bash
python tools/dbm_import/tests/test_importer.py
```
