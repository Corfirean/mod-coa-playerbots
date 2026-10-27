# Bot Dialogue, Tactical Callouts, Encounter Presentation & Localization

> **Branch:** `feat/dynamic-bot-encounters`  
> **Repository:** `mod-coa-playerbots`  
> **Target Environment:** Project Ascension / Chronicles of Azeroth (CoA) 3.3.5a  

---

## 1. Overview & Architectural Philosophy

The **Bot Dialogue & Tactical Callout System** is a unified, data-driven layer that gives playerbots authentic, contextual voices across open-world exploration, party dungeons/raids, Dynamic Encounters, and combat encounters.

### The Semantic Beat Principle
Gameplay logic **never generates raw hardcoded strings**. Instead, game logic emits semantic dialogue beats:

```cpp
// Correct: Game systems emit semantic intent
BotDialogue::Emit(DialogueBeat::SpreadOut, context);
BotDialogue::Emit(DialogueBeat::NeedMana, context);
BotDialogue::Emit(DialogueBeat::NemesisGreeting, context);
```

The Dialogue Layer evaluates:
1. **Whether to speak at all** (cooldowns, anti-repeat ring buffers, priority arbitration).
2. **Who speaks** (personality weighting, role filtering, caster vs melee).
3. **Which phrase is selected** (weighted random pool selection).
4. **Localization and placeholder formatting** (player locale, race apposition, zone/spell interpolation).
5. **Channel dispatch** (`Say`, `Yell`, `Party`, `Whisper`, `Emote`, `PrivateNarrator`).

---

## 2. Supported Locales & Fallback Chain

The localization layer was built to seamlessly support all locales used by `coa-server-manager` (`src/i18n/index.tsx`):

| Language | Code | DialogueLocale Enum | Fallback Order |
|---|---|---|---|
| **English** | `en` / `enUS` | `DialogueLocale::EnUS` | Primary canonical default |
| **Russian** | `ru` / `ruRU` | `DialogueLocale::RuRU` | `ruRU -> enUS -> key` |
| **German** | `de` / `deDE` | `DialogueLocale::DeDE` | `deDE -> enUS -> key` |
| **French** | `fr` / `frFR` | `DialogueLocale::FrFR` | `frFR -> enUS -> key` |
| **Spanish** | `es` / `esES` | `DialogueLocale::EsES` | `esES -> enUS -> key` |
| **Chinese** | `zh` / `zhCN` | `DialogueLocale::ZhCN` | `zhCN -> enUS -> key` |

If a specific translation is missing for the requested locale, the fallback engine resolves:
`Requested Locale` &rarr; `Server Default Locale` &rarr; `English (enUS)` &rarr; `Any Available Locale` &rarr; `"[dialogue.id]"` (safe debug string; never crashes).

---

## 3. Placeholders & Race Apposition

### Supported Placeholders
- `{playerName}`: Target or interacted player name.
- `{playerRace}`: Localized race display name (e.g. *Human*, *Человек*, *Mensch*, *人类*).
- `{botName}`: Speaking bot's character name.
- `{targetName}`: Current target or adversary name.
- `{zoneName}`: Current zone/area name.
- `{spellName}`: Cast or interruptible spell name.
- `{enemyName}`: Enemy or spawned add name.
- `{groupLeader}`: Group leader's name.

### Grammatical Declension Strategy
To avoid unnatural Russian/German grammatical declension errors (e.g. inflecting race names in prepositional or instrumental cases), dialogue lines are authored using **apposition and independent clauses**:
- *Good:* `"Ты опять здесь, {playerRace}?"` / `"В этот раз тебе не победить, {playerRace}."`
- *Avoided:* `"Я убью этого {playerRace}а"` (fails across gender and inflections).

---

## 4. Dialogue Pools & Data Structure

All dialogue lines are stored in `module/data/dialogue/*.json`:

1. **`generic.json`**: Social chatter (Greetings, Farewells, Thanks, Jokes, Complaints, Impatience, Encouragement, Taunts, Victories, Defeats).
2. **`group.json`**: Downtime, travel, and readiness (NeedMana, NeedDrink, NeedFood, NeedRest, ReadyAgain, WaitingForCooldown, LeaderWaitAck, LeaderProceed, LongJourney, DangerAhead).
3. **`combat.json`**: Tactical combat callouts (Pulling, AggroWarning, FocusTarget, KillAdds, Interrupt, Dispel, SpreadOut, StackTogether, MoveFromGround, TankInDanger, TargetSwitch, CrowdControl, BurstNow, RetreatCombat).
4. **`encounters.json`**: Dynamic Encounters (Nemesis, Assassin, Road Duel, Hunter & Prey, Bounty Hunter, Caravan, Rival Party, Patrol Ambush).
5. **`mystery.json`**: Isolated Shath'Yar & Old God whispers (`nonCanonicalStyled: true`). Completely segregated so standard bots never speak void whispers.

---

## 5. Tactical Callouts & DBM Deduplication

The `TacticalCalloutManager` arbitrates mechanic-driven callouts:
- **Deduplication Key:** `type + mechanicId + sourceGuid + castId`.
- **Suppression Window:** 6.0 to 12.0 seconds per mechanic instance.
- When a boss casts a lethal spell (e.g. Pyroblast), 5 bots in the party may simultaneously recognize the need to interrupt. The deduplicator ensures **only one bot shouts the callout**, preventing party spam.
- **Priority-Gated Arbitration:** High/Critical callouts (e.g. `TankInDanger`, `Interrupt`, `MoveFromGround`) bypass normal ambient chatter cooldowns and suppress lower-priority fluff.

---

## 6. Encounter Presentation & Visual Resolver

### The Watcher
- **Appearance Preset:** Applies dark hooded stalker appearance (`DISPLAY_ID_WATCHER_HOOD = 23883`).
- **Look-at-Player:** Bot maintains continuous head and body tracking towards the target player.
- **Vanish on Approach:** If the player charges within 24 yards, the Watcher triggers `SPELL_VISUAL_SMOKE_PUFF`, emits `DialogueBeat::WatcherSpotted`, and vanishes into stealth/retreat.
- **Clean Restoration:** Reverts display ID and state cleanly upon encounter finalization.

### Mystery Cultist (Variants A..E)
- **Appearance Preset:** Applies dark acolyte / cultist robe & hood preset (`DISPLAY_ID_MYSTERY_CULTIST = 21137`).
- **5 Unpredictable Presentation Variants:**
  - **Variant A (Kneel & Scrying Rune):** `EMOTE_ONESHOT_KNEEL`, arcane/shadow rune ground visual (`SPELL_VISUAL_RUNE_GROUND = 5106`), `MysteryShathYarWhisper`.
  - **Variant B (Dark Incantation):** `EMOTE_ONESHOT_SPELLCAST_OMNI`, void pulse (`SPELL_VISUAL_MYSTERY_PULSE = 67556`), `MysteryShathYarIncantation`.
  - **Variant C (Silent Vigil):** `EMOTE_ONESHOT_POINT`, silent gaze, `MysteryWhisper`.
  - **Variant D (Cursed Ground):** `EMOTE_ONESHOT_BOW`, shadow burst (`SPELL_VISUAL_SHADOW_BURST = 30206`), `MysteryShathYarThreat`.
  - **Variant E (Aberration Burst):** `EMOTE_ONESHOT_ROAR`, smoke burst, `MysteryRune`.
- **Departure:** At conclusion or on intrusion, the cultist emits `MysteryShathYarDeparture`, plays smoke puff, and dissolves into the void.

---

## 7. In-Game GM Commands

| Command | Permission | Description |
|---|---|---|
| `.botcmd dialogue reload` | GM (RBAC) | Hot-reloads all JSON dialogue pools without server restart. |
| `.botcmd dialogue debug on\|off` | GM (RBAC) | Toggles verbose logging of dialogue selection, scoring, and suppression reasons. |
| `.botcmd dialogue say <beat>` | GM (RBAC) | Forces the selected bot or player to emit the specified semantic beat. |
| `.botcmd dialogue status` | GM (RBAC) | Displays the number of loaded beats and memory status. |

---

## 8. Verification & Test Matrix

- **Standalone Unit Tests (`encounter_tests.exe`):**
  - **9,916 checks, 0 failures.**
  - Tests covering: JSON schema parsing, 6-locale fallback chain, `{playerRace}` localized apposition, memory anti-repeat ring buffers, mana resource gates (<20%), tactical deduplication keys, Shath'Yar pool isolation, and appearance preset cleanup.
- **Core Engine Compilation:**
  - `worldserver.exe` compiled cleanly with MSVC C++20 and deployed to runtime server.
