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
| `AUTODUNGEON` | `1` or `0` (on/off -- this slot carries the actual value, unlike the `0` placeholder other group-wide verbs use) | `BotMgr::SetAutoDungeonMode(sender, enabled)` | Added 2026-09-16. Acts on the sender's whole group. While on, a Tank-role bot idle inside a dungeon/raid instance (no target, group leader not already fighting) scans for the nearest hostile pack and pulls it on its own, instead of waiting for the real player to engage first -- see `BotAI.cpp`'s `TryAutoPullInInstance` for why this deliberately does not attempt to encode per-dungeon boss order or encounter mechanics, leaning entirely on the instance's own real progression gating instead. Server-side state is in-memory only (keyed by leader guid) and does not survive a worldserver restart, so the addon re-sends its last saved value once on `PLAYER_ENTERING_WORLD` after login/reload. |
| `GATHERORDER` | itemEntry (reuses the botGuidLow slot, same convention as `CRAFTORDER`), count (optional, default 1) | `BotMgr::GatherOrder(sender, itemEntry, count, nullptr)` | Added 2026-09-17. Addon-facing counterpart to the `.botcmd guildgather` GM command: `GatherOrder` auto-picks a suitable online guild-mate bot itself (preferring one not already busy with a gather/craft order) rather than requiring a specific bot guid, then places the same background order via the existing `BotMgr::GuildGather`. No profession/skill filtering needed on bot selection -- every bot already has every gathering profession maxed (`GrantAllProfessions`). The bot's own AI is opportunistic, not targeted (`TryStartGathering` opens whatever herbalism/mining node it finds, kill-loot is likewise whatever a kill drops) -- this order just watches inventory for the requested item/count and auto-deposits into the guild bank once satisfied, same as the pre-existing GM command's behavior. |
| `GETGATHERCATALOG` | `0` (placeholder, same convention as `QUICKFILL`) | server replies (see below), no state change | Added 2026-09-17. Catalog query backing the addon's "Order Materials" icon-menu picker: replies with `GCAT:...` bodies covering every Trade Goods item this realm's own loot tables (`gameobject_loot_template`/`skinning_loot_template`/`fishing_loot_template`/`creature_loot_template`) confirm a bot can actually obtain on its own -- see `BotMgr::GetGatherCatalog`'s comment for why a plain class/subclass filter alone would have wrongly included refined/crafted items (bars, bolts, cured leather) that no gathering/looting action can ever produce. |
| `GETRECIPECATALOG` | `0` (placeholder, same convention as `QUICKFILL`) | server replies (see below), no state change | Added 2026-09-17. Catalog query for the same picker's "Recipes" tab: replies with `RCAT:...` bodies listing only items an online guild-mate bot can *actually* craft right now (same "knows a recipe" definition `CraftOrder` itself uses -- `Player::HasSpell` on a real `SPELL_EFFECT_CREATE_ITEM` spell), per the user's explicit ask to show real, available recipes rather than every recipe that exists in the game. |
| `FORMATION` | `0` (placeholder), formation name (`rolebased`/`shieldwall`/`arrow`/`circle`/`line`/`chaos`, plus `ParseFormation`'s synonyms) | `BotMgr::SetGroupFormation` + re-`MoveFollow` every already-following group bot | Added 2026-09-23. Group-wide, restricted to the group leader (checked server-side against `Group::GetLeaderGUID()`, silently dropped otherwise) since formation is shared group state -- mirrors the pre-existing `.botcmd formation` GM command's own leader-only rule (`BotCommand.cpp`'s `HandleBotFormationCommand`), just reachable without GM rights now. |
| `GETFORMATION` | `0` (placeholder) | server replies (see below), no state change | Added 2026-09-23. Query verb: lets the addon show the group's currently active formation (e.g. after a UI reload) instead of guessing. Works for any group member, not just the leader (read-only). |
| `GETSPECS` | botGuidLow | server replies (see below), no state change | Added 2026-09-23. Query verb backing a specific-spec picker beyond the coarse `SETROLE` menu: lists every real spec `ClassSpecRoles::GetAllSpecs(bot->getClass())` knows for the bot's class. Naturally empty for vanilla classes 1-11 (`SPEC_ROLE_TABLE` only covers Ascension's custom classes 12-32) -- the addon should just fall back to the existing role-only flow when it gets no `SPEC:` replies at all. Applying a chosen spec reuses the pre-existing `LEARNSPEC` verb -- no new apply-side verb needed. |
| `TELEPORT` | `0` (placeholder, same convention as `QUICKFILL`) | `BotMgr::TeleportBotsToPlayer(commander, nullptr)` | Added 2026-09-23. "Bring bots to me": group-wide, no bot-guid target. Refuses (no-op) if the commander is in combat; per-bot, skips (rather than aborts the whole request) any bot that is itself already in combat, so pulling the group together never yanks one bot out of a fight it's already in. Uses the same `TeleportTo()`-then-queue-ack-for-next-tick sequencing `DoAcceptInvite`/`TryFollowLeaderAcrossMaps` already established; `FinishPendingTeleport`'s existing post-landing re-follow resumes formation/following with no new code there. |

| `SETGEARPREF` | botGuidLow, kind (`armor`/`weapon`), type name (see table below) or `auto` | `BotMgr::SetGearPreference` | Added 2026-09-23. Sets which basic armor type (`cloth`/`leather`/`mail`/`plate`) or basic melee weapon type (`sword`/`mace`/`axe`/`fist`/`dagger`/`staff`) this bot should favor -- persisted via `PlayerSetting` (`coa.gear_pref_armor`/`coa.gear_pref_weapon`), survives a relog. `auto` clears it back to "greed on/equip whatever's legal." An unrecognized type name is silently dropped, same fail-closed policy as `SETROLE`'s refusals. Only affects the 4 basic armor types and 6 basic weapon types listed -- shields, jewelry, ranged weapons, relics, and everything else are unaffected and always Greed/equip normally. |
| `GETGEAR` | botGuidLow | server replies (see below), no state change | Added 2026-09-23. Query verb backing the addon's gear-inspector panel: lists everything currently equipped, which armor/weapon types this bot's class can legally wear at all (`BotMgr::GetLegalArmorSubclasses`/`GetLegalWeaponSubclasses`, probed live via the real `CanEquipNewItem` proficiency check since no classId-to-proficiency table exists for Ascension's custom classes), and the bot's current preference for each. |

Inventory/equipment management beyond the gear-preference verbs above (viewing/setting armor and
weapon *type* preference, and having that preference bias both loot rolls and auto-equip) is
still **out of v1.x scope** -- actually picking/swapping specific items by guid needs its own
item-guid wire format and is lower-value than the type-preference panel for a first pass. Revisit
once that round-trips cleanly.

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
| `ROLES` | botGuidLow, comma-separated role list (e.g. `dps,tank`), current effective role (e.g. `healer`), current specId, current spec name | In response to `GETROLES`. The role list is built from `ClassSpecRoles::GetAvailableRolesMask(bot->getClass())` -- always includes `dps` (every class has at least a default/shared spec), plus whichever of `tank`/`healer`/`support` that class has a real spec for. The 4th field (added 2026-09-15) is `BotAI::GetRole(bot)`'s actual current effective role (manual override if set, else auto-detected from active spec) -- lets the addon show what a bot left on "Auto" is really playing as right now, instead of just the bare label "Auto". The 5th/6th fields (added 2026-09-23) are the bot's actual active specId (`core.ascension_active_spec` PlayerSetting) and its display name (`ClassSpecRoles::GetSpecName`) -- empty name for a vanilla class (1-11) or specId 0, since a class can have more than one spec per role and the role alone doesn't say which is active (e.g. "Tank: Vanguard" vs. just "Tank"). |
| `ROSTER` | botGuidLow, name, classId, level, task, comma-separated `profession=skill` pairs, taskItemEntry | One per online guild-mate bot, in response to `GUILDROSTER`. `task` is a free-text string (`idle`, `crafting Nx item M`, or `gathering Nx item M`) reflecting an active `CraftOrder`/`GuildGather` order on that bot right now -- not meant to be machine-parsed further, just displayed. The professions field can be empty (no known profession skills). The trailing `taskItemEntry` field (added 2026-09-23) is the real item id behind that task text, or `0` when idle -- lets the addon show a real `GameTooltip` (icon/stats) on hover instead of just the plain-text name already baked into `task`. |
| `GCAT` | category (`herb`/`ore`/`cloth`/`leather`/`meat`/`fish`), pipe-separated `itemEntry,itemName` pairs | Several per `GETGATHERCATALOG` request (see `BotMgr::GetGatherCatalog`'s `AppendCatalogChunks` -- packed by byte length, not a fixed item count, to stay under the real chat-length cap regardless of how long a particular item's name is). Client-side, chunks accumulate into `orderCatalog[category]` (see `ParseCatalogChunk`) rather than replacing it -- a category's full item list is the union of every `GCAT:` reply for that category this session. |
| `RCAT` | pipe-separated `itemEntry,itemName` pairs | Same chunking/accumulation as `GCAT`, in response to `GETRECIPECATALOG` -- accumulates into `orderCatalog["recipe"]`. |
| `FORMATION` | formation name | In response to `GETFORMATION`, reflecting the group leader's currently active `BotGroupFormation` (`FormationToString`). |
| `SPEC` | botGuidLow, specId, role (`dps`/`tank`/`healer`/`support`), spec name | One per real spec the bot's class has, in response to `GETSPECS`. Apply a choice with the pre-existing `LEARNSPEC:botGuidLow:specId` verb. |
| `GEAR` | botGuidLow, equipment slot (raw `EquipmentSlots` enum value), itemEntry, item name | One per currently-equipped item (empty slots, the shirt slot, and the tabard slot are skipped), in response to `GETGEAR`. |
| `GEARPREFS` | botGuidLow, comma-separated legal armor types (e.g. `leather,mail`), comma-separated legal weapon types (e.g. `sword,dagger,staff`), current armor preference (a type name or `auto`), current weapon preference (a type name or `auto`) | One per `GETGEAR` request, sent after that request's `GEAR:` lines. The legal-type lists are exactly what `SETGEARPREF` will accept for this bot (plus `auto`, always valid) -- the addon should only ever offer these as choices, not the full type list, since an illegal choice would just get silently dropped. |

**Client-side status as of 2026-09-15: all built.** `GETROLES` is requested once per bot row
(cached, never re-requested this session) and its `ROLES` reply greys out role-menu buttons
the bot's class can't hold; `QUICKFILL` and the Guild Task Board (`GUILDROSTER`/`ROSTER`
consumer + `CRAFTORDER` submission form) are both live in `CoABotUI.lua` -- see
`docs/addon-client.md`. Not yet click-tested with a real client (see that doc's status note).
The 4th (`currentRole`) field is cached client-side (`currentRoleCache`) and refreshes the
role-button label whenever a fresh `ROLES` reply comes in.

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

As of 2026-09-17: test mode (the `MOCK_BOTS` preview roster) removed from the client addon entirely. The Craft Order form's raw item-id/shift-click text entry (`itemBox`/`countBox`) is replaced by an "Order Materials..." button opening a new icon-menu picker (`CoABotUIOrderPicker`) with category tabs (Herbs/Ore/Cloth/Leather/Meat/Fish/Recipes), a search filter, and one row per real item (icon via the client's own `GetItemIcon`, no icon data sent over the wire) with a quantity box and per-row Order button -- sends `GATHERORDER:itemEntry:count` for the six gather categories or `CRAFTORDER:itemEntry:count` for Recipes. Backed by two new catalog query verbs (`GETGATHERCATALOG`/`GETRECIPECATALOG`) and the new `GATHERORDER` verb (an addon-facing, auto-bot-selecting wrapper around the pre-existing `.botcmd guildgather`, which previously required knowing a specific bot's raw guid). Built and compiled clean server-side; not yet click-tested with a real client.
