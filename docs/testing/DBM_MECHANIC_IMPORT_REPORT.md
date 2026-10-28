# DBM Offline Mechanic Import Report

> **Notice**: DBM is used purely as an offline metadata source. It is **NOT** a client or runtime dependency.

## 1. Summary Statistics

- **Packages Scanned**: 29
- **Boss Modules Scanned**: 182
- **Total Mechanics Imported**: 498
- **Verified (Cross-Referenced with Core C++ Scripts)**: 467
- **Conflicts Detected**: 0
- **Manual CoA Overrides Applied**: 1
- **Disabled Mechanics**: 0
- **Unique Spell IDs**: 383
- **Unique Encounters**: 159

## 2. Breakdown by Semantic Mechanic Type

| Semantic Mechanic Type | Count |
|------------------------|-------|
| `RunAway` | 126 |
| `PhaseChange` | 61 |
| `MoveFromGround` | 57 |
| `IncomingAoE` | 49 |
| `Dispel` | 44 |
| `SpreadOut` | 41 |
| `Interrupt` | 36 |
| `StackTogether` | 25 |
| `TargetSwitch` | 19 |
| `TankSwap` | 16 |
| `Defensive` | 12 |
| `StopDamage` | 10 |
| `KillAdds` | 2 |

## 3. Client & Runtime Independence Verification

- **DBM client addon required**: **NO**
- **DBM server runtime dependency**: **NO**
- **DBM Lua executed by worldserver**: **NO**
- **DBM files shipped to WoW AddOns**: **NO**
- **DBM required after registry generation**: **NO**
