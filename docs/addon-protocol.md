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
| `SETROLE` | botGuidLow, role name (`dps`/`tank`/`healer`/`support`/`auto`) | existing `BotMgr::SetRole`/`ClearRoleOverride` | Reuses code already live-tested this session. As of 2026-09-15: also auto-switches the bot's Ascension spec to match the requested role (via `BotMgr::LearnSpecialization`) if its current spec doesn't already map to that role, and silently refuses the whole request (spec+role both left unchanged) if the bot's class has no spec at all for that role -- see `ClassSpecRoles::FindSpecForRole`. `handler` is always `nullptr` on this wire path (see `BotAddonChat.cpp`), so refusal produces no message back to the player; the addon should pre-filter role options per bot rather than rely on server feedback (see "Addon UX expectations"). |
| `LEARNSPEC` | botGuidLow, specId | existing `BotMgr::LearnSpecialization` | |
| `GETROLES` | botGuidLow | server replies (see below), no bot state change | Added 2026-09-15. Query verb: ask which roles this bot's class can actually hold, so the addon can grey out impossible role buttons before the player ever clicks one (`SETROLE` refuses silently -- see its note above). |
| `QUICKFILL` | `0` (placeholder, ignored -- see note) | `BotMgr::QuickFillGroup(commander, nullptr)` | Added 2026-09-15. Acts on the **sender's own group as a whole**, not one specific bot -- send as `QUICKFILL:0` (the `0` only exists to satisfy the wire format's "at least 2 colon-parts" minimum; no per-bot authorization check applies, since there's no bot guid to check). Brings the sender's group up to 5 (tank + healer + 3 dps), inviting online bots for whichever roles are still short -- guildmates of the sender preferred, then closest level/average-ilvl. Issues a real `HandleGroupInviteOpcode` per invited bot from the *sender's own session*; the existing per-tick auto-accept (`BotMgr::Update`) picks up each resulting pending invite and teleports the bot in, same as any other bot-to-bot invite already in this module -- no new accept-side code. |
| `CRAFTORDER` | itemEntry (reuses the botGuidLow slot), count (optional, default 1) | `BotMgr::CraftOrder(sender, itemEntry, count, nullptr)` | Added 2026-09-15. Also doesn't target one specific bot -- the requester is always the sender; `BotMgr::CraftOrder` itself finds the guild-mate bot to craft it (any online bot in the sender's guild that knows a recipe spell producing `itemEntry`, preferring one that already has the reagents). Crafts immediately if possible; otherwise waits (retried every tick) for the crafter to acquire the missing reagents by whatever means (its own gathering AI, a manual grant, etc.). Finished items are mailed to the sender via a real `MailDraft`, regardless of online status. |
| `GUILDROSTER` | `0` (placeholder, same convention as `QUICKFILL`) | server replies (see below), no state change | Added 2026-09-15. Task-board query: send `GUILDROSTER:0` to get one `ROSTER:...` reply per online bot in the sender's guild. |

Inventory/equipment management (the other half of the user's ask) is deliberately **out of
v1 scope** -- needs its own item-guid wire format and is lower-value than movement/role
control for a first usable panel. Revisit once v1 round-trips cleanly.

## Server -> client replies (added 2026-09-15)

Every verb above is client -> server only. `GETROLES` is the first verb that needs an answer
back, so this direction now exists too: `BotAddonChat.cpp`'s `SendCoaBotReply(Player* recipient,
std::string const& body)` builds a `CHAT_MSG_WHISPER`/`LANG_ADDON` packet addressed from the
player to themself (`ChatHandler::BuildChatPacket` + `Player::SendDirectMessage`, the same
self-whisper-as-addon-channel trick as the outgoing direction, and the same technique
`mod-ascension-compat`'s `CoABugReport.cpp` already uses for its own server->client replies) and
hands it straight to the recipient's own session -- no real second recipient, no opcode
changes. Body keeps the same `COABOT\t<VERB>:...` shape as the client->server direction, so the
addon's existing `CHAT_MSG_ADDON` event handler covers both without a second code path.

| Reply verb | Args | Sent when |
|---|---|---|
| `ROLES` | botGuidLow, comma-separated role list (e.g. `dps,tank`) | In response to `GETROLES`. Built from `ClassSpecRoles::GetAvailableRolesMask(bot->getClass())` -- always includes `dps` (every class has at least a default/shared spec), plus whichever of `tank`/`healer`/`support` that class has a real spec for. |
| `ROSTER` | botGuidLow, name, classId, level, task, comma-separated `profession=skill` pairs | One per online guild-mate bot, in response to `GUILDROSTER`. `task` is a free-text string (`idle`, `crafting Nx item M`, or `gathering Nx item M`) reflecting an active `CraftOrder`/`GuildGather` order on that bot right now -- not meant to be machine-parsed further, just displayed. The professions field can be empty (no known profession skills). |

Addon-side TODO (not yet implemented client-side as of 2026-09-15): send `GETROLES` once per
bot when populating its row, cache the `ROLES` reply, and grey out/disable any role button not
in that list. Also not yet built: the task-board UI itself (`GUILDROSTER`/`ROSTER` consumer),
the crafting-order submission form (`CRAFTORDER`), and the `QUICKFILL` button.

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
