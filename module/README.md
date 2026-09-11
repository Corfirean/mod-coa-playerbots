# mod-coa-playerbots — real module, milestone 1: bot joins a real group

**Status: succeeded, 2026-09-11.** Builds on `pilot/` (proved a bot can exist via a
null-socket `WorldSession`). This milestone proves a bot can be a **real group
member** alongside the GM's actual client — confirmed both server-side (`.group
list`) and visually (bot showing in the real party frame). **Auto-accept, loot-roll
auto-Greed, teleport-to-leader, follow, and `.botcmd despawn` all added and confirmed
working (loot-roll pending a live test) same day.**

### The teleport gotcha (worth knowing before touching bot movement again)

`Player::TeleportTo()` only **requests** a move — for a same-map ("near") teleport,
the real position isn't applied until the client sends `MSG_MOVE_TELEPORT_ACK`
(`WorldSession::HandleMoveTeleportAck` → `Player::UpdatePosition`); for a cross-map
("far") one it's `HandleMoveWorldportAck`. A bot has no client to ever send that ack,
so it silently stayed semaphore-locked at its old position forever — `TeleportTo()`
itself never errors, which is why this wasn't visible from logs alone (diagnostic
logging confirmed every earlier step succeeded before finding this). `MoveFollow` had
nothing real to work from as a result — the bot looked simply frozen.

**Fix, attempt 1 (crashed)**: after `TeleportTo()`, check
`bot->IsBeingTeleportedNear()` / `IsBeingTeleportedFar()` and call the matching ack
directly — `HandleMoveWorldportAck()` already has a no-packet "for server-side calls"
overload for the far case; the near case needs a minimal packed-guid `WorldPacket`
(`bot->GetGUID().WriteAsPacked()` + two unused `uint32`s), same "call the real
handler directly" pattern this module already uses for login/group-accept/loot-roll.
This part was right — but calling it **synchronously, in the same tick as
`TeleportTo()` itself**, crashed live with an `IsInGrid()` assertion failure inside
`Map::PlayerRelocation` (full stack in the crash dump:
`GridObject<Player>::RemoveFromGrid` ← `Map::PlayerRelocation` ← `Unit::UpdatePosition`
← `Player::UpdatePosition` ← `HandleMoveTeleportAck` ← `BotMgr::DoAcceptInvite`). A real
client's ack only ever arrives after its own network round trip — never in the same
tick as the teleport request — so firing it inline races whatever per-tick
grid/relocation bookkeeping `TeleportTo()`'s near-teleport branch expects to have
already happened. (`Unit::NearTeleportTo` doesn't sidestep this either: for a `Player`
it's just a thin wrapper around the same `TeleportTo()` — only `Creature` gets a truly
synchronous path.)

**Fix, attempt 2 (confirmed working live, no crash)**: `DoAcceptInvite` now only
calls `TeleportTo()` and queues the session in a `_pendingTeleportAck` list;
`BotMgr::Update()` drains that queue (via `FinishPendingTeleport`, which also starts
the `MoveFollow` once the teleport has actually landed) at the very top of the
function — before that same tick's own invite-check loop gets a chance to queue a
*fresh* one. That guarantees at least one full world tick of separation between
`TeleportTo()` and its ack, mirroring the real network delay instead of trying to
remove it. **Any future code that force-moves a bot needs this same
"queue the ack, fire it next tick" shape** — it's a property of `TeleportTo()`
generally, not specific to the group-join flow, and skipping the one-tick gap crashes
the server, not just "doesn't work."

## What changed from `pilot/`

Renamed, not rewritten — the proven login mechanism (`sWorld->AddQueryHolderCallback`,
the `WorldScript::OnUpdate` heartbeat) is untouched:

- `PilotBotMgr` → `BotMgr`, `.pilot spawnbot` → `.botcmd spawnbot` (unchanged logic).
- `.botcmd acceptinvite <charLowGuid>` — finds the bot's session, confirms
  `Player::GetGroupInvite()` is set, builds a 4-byte padding `WorldPacket`, and calls
  `WorldSession::HandleGroupAcceptOpcode` directly. That handler only does
  `recvData.read_skip<uint32>()` before its real logic (`RemoveInvite`, validation,
  `Group::Create`-if-new, `AddMember`, `BroadcastGroupUpdate`) — calling it directly
  reuses all of that real validation instead of re-deriving it. **Kept as a manual
  override/debug tool**, no longer needed for normal use (see below).
- **Auto-accept**: `BotMgr::Update()` now checks every active bot's
  `Player::GetGroupInvite()` on **every tick** (not throttled, unlike the heartbeat —
  a human expects a near-instant response to an invite) and calls the same
  `HandleGroupAcceptOpcode` logic automatically the moment one is pending. GM invites
  the bot by name from a real client exactly like inviting another player; the bot
  joins with no further action needed. Confirmed working live.

**No core patch was needed for grouping.** The hypothesis from `AGENTS.md` — that
`Group::AddMember(Player*)` needs nothing bot-specific — held. `Group.cpp`'s
Category-C double-invite fix was never applied and never needed; grouping worked
cleanly on CoA's stock, unpatched `Group.cpp`.

## The real blocker, and it wasn't in core at all

Two client-visible failures happened before this worked, neither was a core-patch
gap:

1. **"Cannot find player 'Test'."** The bot's test character happened to be guid 1
   (`Test`), which is on the **same account** (`LOCAL`, account id 1) as the GM's own
   login. Two simultaneous character sessions on one account is a state AzerothCore's
   normal login flow never produces (character-select is exclusive per account) — our
   bot's fake-session login bypasses that exclusivity, and the resulting collision left
   the bot Player in a visibly wrong state (`.pinfo` showed `GM Mode active, Phase: -1`
   on a character that should've been an ordinary Phase 1 player). **Fix: bots must be
   on a different account than whichever account the human is playing on** — exactly
   why real Playerbots always uses separate bot accounts (`masterAccountId` is tracked
   as distinct from the bot's own account in their code). Practical fix used here: moved
   a second test character (`Shaniel`, guid 2) to this repack's existing second account
   (`ADMIN`, id 2) with `UPDATE characters SET account=2 WHERE guid=2;`, then spawned
   the bot as `Shaniel` instead. `.pinfo` then showed the expected `Phase: 1`.
2. **Friends List "Invite" greyed out ("in N minutes").** A client-side-only cooldown
   on that specific UI element (WotLK's Friends List throttles its own Invite button),
   unrelated to the server, the bot, or account setup. Fixed by typing `/invite
   <name>` directly in chat instead — works immediately, no cooldown.
3. **GM-invoked invites skip the faction check.** `WorldSession::HandleGroupInviteOpcode`
   only enforces same-faction grouping `if (!invitingPlayer->IsGameMaster() && ...)` —
   a GM-level inviter bypasses it entirely. Confirmed by reading the handler directly;
   the test's cross-faction character pairing (Tauren GM character, Night Elf bot) was
   never actually a problem, though the user reasonably suspected it might be.

**Takeaway for future bot-account setup**: this project needs its own dedicated bot
account(s), never reusing whatever account the human tester is logged into that
session — worth building a proper `.botcmd createaccount`-style helper (or at least a
documented manual step) before scaling past one bot, rather than rediscovering this
per test session.

## Verification performed

- `.botcmd spawnbot 2` → bot `Shaniel` logs in cleanly on account `ADMIN` (separate
  from the GM's `LOCAL` account), confirmed via `.pinfo Shaniel` (`Phase: 1`, no GM-mode
  artifact).
- GM (real client, playing `Test`, account `LOCAL`) sends `/invite Shaniel` from
  chat — succeeds (`Shaniel added to friends. You have invited Shaniel to join your
  group.`).
- `.botcmd acceptinvite 2` → bot accepts via the real `HandleGroupAcceptOpcode` path.
- `.group list Test` and `.group list Shaniel` both report `Group type: Party and
  consists of 2 players.`
- **User visually confirmed** Shaniel showing as the second party member in their
  actual party frame — the one signal that genuinely needs a real client, not RA.
- Server stayed healthy throughout (uptime climbing, update-diff mean ~4-6ms, no new
  errors) — same stability bar as the pilot.

## Next milestones (not done yet)

Loot-roll participation, guild support, gearing/talents, auto-accept-on-invite (still
manual via `.botcmd acceptinvite`), bot character creation from scratch (still reusing
existing test characters), a real dedicated bot account, any AI/rotation logic. See
`AGENTS.md` for the full-parity scope this is incrementally building toward.
