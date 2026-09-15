# Client AddOn: CoABotUI (CoA Bot Companion Control)

## Overview

`CoABotUI` is a World of Warcraft 3.3.5a (WotLK) client-side AddOn providing an in-game floating control panel for companion bots. It allows players to manage bot roles, trigger pulls, coordinate movement, and issue combat commands without needing GM permissions, slash commands, or the RA console.

Communication with the server runs over standard WoW addon messages according to the wire specification defined in `docs/addon-protocol.md`.

---

## File Layout

### Source Tree
Within the `mod-coa-playerbots` repository:
```text
mod-coa-playerbots/
├── addon/
│   └── CoABotUI/
│       ├── CoABotUI.toc        # AddOn manifest (Interface 30300)
│       └── CoABotUI.lua        # UI frames, events, roster scanner, wire sender
└── docs/
    ├── addon-protocol.md       # Server <-> client wire protocol specification
    └── addon-client.md         # This installation and architecture guide
```

### Target Client Installation
To install in the WoW client, copy the folder `CoABotUI` to:
- `C:\games\Ascension\Interface\AddOns\CoABotUI\`
- Or `C:\games\wow 3.3.5\Interface\AddOns\CoABotUI\`

Ensure the directory name matches the TOC filename:
- `Interface/AddOns/CoABotUI/CoABotUI.toc`
- `Interface/AddOns/CoABotUI/CoABotUI.lua`

---

## Technical Specifications

- **Client Interface Version**: `30300` (World of Warcraft client patch 3.3.5a).
- **SavedVariables**: `CoABotUIDB`
  - `point`, `relativePoint`, `xOfs`, `yOfs`: Persisted screen coordinates for the floating panel.
  - `isShown`: Frame visibility state across UI reloads.
  - `isCollapsed`: Minimized title-bar state.
  - `testMode`: Boolean flag for previewing mock bots while solo.
  - `debug`: Chat frame logging of outgoing wire messages.
  - `roles`: Table caching bot role selections (`roles[botGuidLow] = "tank"`).
- **Network Channel**:
  - AddOn Message Prefix: `COABOT` (registered via `RegisterAddonMessagePrefix` where available).
  - Outgoing Method: `SendAddonMessage("COABOT", body, "WHISPER", UnitName("player"))`.
  - Body Format: `<VERB>:<botGuidLow>[:<arg>]`.

---

## Bot Roster Detection & Teammate Safety Analysis

### 1. Safety for Real Human Teammates
Treating every group member as a candidate bot is completely safe:
1. **Self-Whisper Channel**: The addon whisper is sent to `UnitName("player")` (the commanding player themselves), NOT to the other player. Teammates never receive the network packet.
2. **Engine Prefix Handling**: Even if an addon message were broadcast, WoW 3.3.5a clients automatically drop unhandled addon messages at the engine layer without printing anything to chat or producing notifications.
3. **Server-Side Authorization**: The server listener verifies that `botGuidLow` belongs to `BotMgr::_botSessions`. Commands referencing real players are rejected silently without server errors.

### 2. Candidate Bot Discovery (v1)
- Scans `GetNumRaidMembers()` or `GetNumPartyMembers()`.
- Excludes the player unit (`UnitIsUnit(unit, "player")`).
- Extracts low GUID from `UnitGUID(unit)` (e.g. `0x000000000000000E` -> `14`).
- Resolves class color using `RAID_CLASS_COLORS`.

### 3. Server-Side Handshake (v2 Proposal)
Once the server-side listener is active, an optional query/response can be added:
- Addon sends `QUERYBOTS` on `PLAYER_ENTERING_WORLD` or group changes.
- Server replies with `ROSTER:<guid1>:<role1>,<guid2>:<role2>`.
- The UI can then grey out non-bot players or hide them from the control panel.

---

## UI Components & Features

1. **Floating & Draggable Frame (`CoABotUIMainFrame`)**:
   - Movable with mouse drag on the header/frame.
   - Clamped to screen to prevent losing the frame off-display.
   - Position saved automatically to `CoABotUIDB` on drag release.
   - Height dynamically adapts to the number of group members.
2. **Title Bar**:
   - `[Test Mode]` toggle button.
   - `[-]` / `[+]` collapse button to minimize the panel to just the title bar.
   - `[X]` close button to hide the panel.
3. **Global Action Bar (`All Bots`)**:
   - `[All Follow]`: Iterates active bots and sends `FOLLOW:<guid>`.
   - `[All Stay]`: Iterates active bots and sends `STAY:<guid>`.
   - `[All Pull]`: Iterates active bots and sends `PULL:<guid>`.
   - `[All Stop]`: Iterates active bots and sends `STOPATTACK:<guid>`.
4. **Per-Bot Rows**:
   - **Name & GUID**: Class-colored name + Low GUID display.
   - **Role Selector Button**: Displays current role (`Auto`, `Tank`, `Healer`, `DPS`, `Support`). Clicking opens a 5-option selector popup that updates the setting and sends `SETROLE:<guid>:<role>`.
   - **Individual Action Buttons**: `Follow`, `Stay`, `Pull`, `Stop`.
5. **Target Status Bar**:
   - Dynamically tracks the commanding player's current selection.
   - Shows `Target: <TargetName> (Ready for Pull)` in red for hostile targets or green for neutral/friendly targets, providing real-time feedback for the `PULL` command.
6. **Empty State & Test Mode**:
   - When not in a group, displays a friendly notice with an `[Enable Test Mode]` button.
   - Test mode injects mock bots (`Startest`, `WdoctorBot`, `WhunterBot`, `XorothBot`, `Shaniel`) allowing full UI verification while solo.
7. **Role-Gating (added 2026-09-15)**: each bot row's role selector requests `GETROLES` once
   per session and caches the `ROLES` reply. Any role the bot's class can't actually hold
   (`SETROLE` would otherwise refuse it silently server-side) is greyed out in the popup with
   an "(n/a)" suffix and does nothing on click, instead of implying an action that would
   silently no-op. Until the reply arrives, every option stays clickable (fails open) rather
   than blocking the player on a round-trip.
8. **Utility Bar -- Quick Fill / Guild Tasks (added 2026-09-15)**: a second row below "All
   Bots" with two buttons that don't target a specific bot:
   - `[Quick Fill Group]`: sends `QUICKFILL`, asking the server to top the player's group up
     to 5 (tank + healer + 3 dps) from online bots, guildmates preferred.
   - `[Guild Tasks]`: opens the Guild Task Board window (below).
9. **Guild Task Board (`CoABotUITaskBoard`, added 2026-09-15)**: a separate, draggable window --
   - Lists every online bot in the player's guild (via `GUILDROSTER`/one `ROSTER` reply per
     bot): class-colored name, level, current task (idle/gathering/crafting, colored), and
     known professions with skill levels.
   - `[Refresh]` re-sends `GUILDROSTER`.
   - **Craft Order form**: an item-id box (accepts a shift-clicked item link -- hooks the
     global `ChatEdit_InsertLink` while focused, the standard 3.3.5 trick for a custom
     item-link input -- or a typed/pasted numeric id) and a count box; `[Order]` sends
     `CRAFTORDER:<itemId>:<count>`. The server finds whichever guild-mate bot knows the recipe;
     the addon doesn't need to know or show which bot that'll be ahead of time.
   - Rows are fixed-position (recycled, same pattern as the main panel's bot rows) rather than
     a scroll frame -- fine for a handful of guild bots, but rows will visually overlap the
     craft-order form below if a guild has more than ~5-6 online bots at once. Revisit with a
     real `UIPanelScrollFrameTemplate` if that turns out to matter in practice.

---

## In-Game Commands

| Command | Description |
|---|---|
| `/coabot` | Toggle panel visibility on/off |
| `/coabot show` | Show panel |
| `/coabot hide` | Hide panel |
| `/coabot test` | Toggle test mode (mock bots for solo testing) |
| `/coabot reset` | Reset panel position to screen center |
| `/coabot debug` | Toggle chat printout of outgoing `COABOT` wire messages |

---

## Status & Next Steps

- **Client AddOn Status**: Fully built (movement/role commands, role-gating, Quick Fill, and
  the Guild Task Board/crafting orders), syntax-validated (`luaparse`, Lua 5.1 grammar) and
  the new wire-parsing logic (`SplitColonKeepEmpty`, `FormatProfessions`, item-link
  extraction) unit-tested against realistic server-generated strings in isolation (a real Lua
  VM via `fengari`, not the WoW client), and deployed to client directories. **Not yet
  click-tested with a real WoW client** -- no automation exists in this environment for
  driving an actual game client, so the UI's on-screen behavior (layout, greying, the task
  board, the craft-order form) has been reviewed and reasoned through but not visually
  confirmed. All server-side verbs it depends on (`FOLLOW`/`STAY`/`PULL`/`STOPATTACK`/
  `SETROLE`/`LEARNSPEC`/`GETROLES`/`QUICKFILL`/`CRAFTORDER`/`GUILDROSTER`) are independently
  live-tested server-side (RA console + a real database check each time) -- see `AGENTS.md`'s
  dated entries.
- **Server Integration Status**: full listener live in `mod-coa-playerbots` (`BotAddonChat.cpp`),
  every verb server-tested. The one missing link is a real client session to confirm the
  add-on's half of the round trip end to end.
