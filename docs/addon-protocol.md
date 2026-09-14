# Bot control addon <-> server protocol

Companion spec for the client-side "bot control panel" addon (see AGENTS.md's dated entry
for context: the user wants a floating in-game panel to manage grouped bots -- role/spec,
inventory/equipment, and movement/combat commands like pull/follow/stay/stop-attack --
without needing GM access or the RA console). This doc is the contract between that addon
(built by Gemini) and a new server-side listener (built by Claude, in `mod-coa-playerbots`)
so both sides can be developed independently against the same interface.

## Why addon messages, not slash commands or GM commands

- `.botcmd` is `SEC_PLAYER`-gated at best and mostly `RBAC_PERM_COMMAND_DEBUG` -- fine for
  our own testing, wrong for a real player's UI (bots should be controllable by whichever
  real player actually owns/plays with them, with no GM rights).
- A real client CAN send `CMSG_MESSAGECHAT` with `CHAT_MSG_ADDON` and an arbitrary addon
  prefix (`SendAddonMessage` in Lua) -- this is the standard, supported way any WoW UI addon
  talks to server-side custom logic. AzerothCore already dispatches these through
  `WorldSession::HandleMessagechatOpcode` and `ScriptMgr`'s `OnPlayerChat`
  (`AddonHandled`/`PlayerScript` addon hooks) -- no new opcode plumbing needed.

## Wire format

- Addon message **prefix**: `COABOT` (registered client-side via
  `RegisterAddonMessagePrefix("COABOT")`, matched server-side against the incoming
  `CHAT_MSG_ADDON` prefix).
- Addon message **body**: `<VERB>:<botGuidLow>[:<arg>]`, ASCII, colon-delimited, no spaces.
  Kept flat and grep-able instead of a serialization format -- this channel only ever carries
  short one-shot commands, not state sync.
- Channel: `SendAddonMessage("COABOT", body, "WHISPER", UnitName("player"))` (a self-whisper
  is the simplest way to guarantee the message reaches this player's own `WorldSession`
  without needing an actual second recipient online).

## Verbs (v1 scope)

| Verb | Args | Maps to | Notes |
|---|---|---|---|
| `FOLLOW` | botGuidLow | new `BotAI::SetManualCommand(guid, Follow)` | Resume following the group leader (already the default idle behavior, but explicit so it can cancel `STAY`/`PULL`). |
| `STAY` | botGuidLow | new `BotAI::SetManualCommand(guid, Stay)` | Bot holds current position -- no follow, no auto-engage on the leader's target, still self-defends if attacked (reuses the existing attacker-retaliation fallback). |
| `PULL` | botGuidLow | new `BotAI::SetManualCommand(guid, Pull)`, target = sender's current target | Bot immediately engages whatever unit the *commanding player* currently has selected (read via the WHISPER sender's `GetSelectedUnit()`server-side, not a guid over the wire -- avoids the addon needing to resolve/transmit a target guid itself). |
| `STOPATTACK` | botGuidLow | `Unit::AttackStop()` + clear any manual Pull state, fall back to `Follow` | |
| `SETROLE` | botGuidLow, role name (`dps`/`tank`/`healer`/`support`/`auto`) | existing `BotMgr::SetRole`/`ClearRoleOverride` | Reuses code already live-tested this session. |
| `LEARNSPEC` | botGuidLow, specId | existing `BotMgr::LearnSpecialization` | |

Inventory/equipment management (the other half of the user's ask) is deliberately **out of
v1 scope** -- needs its own item-guid wire format and is lower-value than movement/role
control for a first usable panel. Revisit once v1 round-trips cleanly.

## Server-side authorization (non-negotiable, implement before wiring any verb)

Every incoming `COABOT` message must be rejected unless **all** of:
1. `botGuidLow` resolves to a session in `BotMgr`'s own `_botSessions` list (i.e. it really is
   one of our bots, not an arbitrary player character guid).
2. The bot and the sending player are in the **same group**, checked via
   `Player::GetGroup()` equality -- a player must not be able to command someone else's bots.

Silently drop (no error message spam) anything that fails either check -- an addon bug or a
stale/renamed bot shouldn't be able to flood chat with server errors.

## Addon UX expectations (for the Gemini-built client side)

- A single floating, dragable panel (remember position via `SavedVariables`), listing the
  bots currently in the player's group (detect via existing group-roster APIs + a naming/GUID
  convention TBD -- ask Claude once the server side's bot-roster-exposure approach is decided,
  don't guess a matching scheme independently).
- Per-bot row: role indicator/dropdown (dps/tank/healer/support/auto), and Follow/Stay/Pull/
  Stop buttons. Pull should default to "pull my current target" (matches the PULL verb above
  -- no target-picker UI needed for v1).
- No addon-side error handling needed for the auth checks above (they fail silently
  server-side by design) -- but do disable/grey out controls for anyone not in the same group
  as a listed bot, so the UI doesn't imply an action that'll silently no-op.

## Status

As of 2026-09-13:
- **Client AddOn (`CoABotUI`)**: Implemented, syntax-validated, and deployed to client `Interface/AddOns/` directories (see `docs/addon-client.md`). Provides floating draggable panel, role selector dropdowns, action buttons, global party commands, and test mode preview.
- **Server-Side Listener**: Implemented (`mod-coa-playerbots/module/src/BotAddonChat.cpp`) -- a `PlayerScript::OnPlayerCanUseChat(..., Player* receiver)` hook recognizes the `COABOT\t` prefix on a self-whisper and dispatches all six v1 verbs (`FOLLOW`/`STAY`/`PULL`/`STOPATTACK` to two new `BotAI::SetManualCommand`/`StopAttack` APIs; `SETROLE`/`LEARNSPEC` to existing `BotMgr` methods), enforcing both authorization checks from this doc. Built, deployed, and code-reviewed line-by-line against `CoABotUI.lua`'s actual wire-sending code (confirmed matching). Not yet click-tested with a real WoW client end-to-end -- see AGENTS.md's "Support role finished, group-teleport-follow, and death/resurrect handling" entry for what's confirmed vs. still needs a real client session.
