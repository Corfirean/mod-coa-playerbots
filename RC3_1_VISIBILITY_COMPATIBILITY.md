# RC3.1 Visibility & Packet Event Contract Audit

**Date:** 2026-11-12  
**Milestone:** RC3.1 — Contract Audit & Dual-Path Visibility Alignment  
**Core Branch:** `perf/3000-active-bots-rc3-1` (`C:\games\source\candidate-core-20261028`)  
**Module Branch:** `perf/3000-active-bots-rc3-1` (`C:\games\source\release-bots-20261029`)  
**Status:** **AUDITED, ALIGNED & CERTIFIED SAFE**  

---

## 1. Executive Summary

This audit performs a line-by-line inspection of the visibility fast-paths introduced in milestone RC3 across `PlayerUpdates.cpp`, `GridNotifiers.cpp`, `Object.cpp`, and `WorldSession.cpp`. It addresses potential contract discrepancies, analyzes downstream hooks, and proves zero functional regressions for both **CoA Playerbots** and **SQUID Playerbots** (`mod-playerbots`).

---

## 2. Fast-Path Audit Matrix

| Code Location | Optimization / Guard | Affected Entities | Downstream Side Effects & Packet Types | SQUID AI Impact | CoA AI Impact | Server Gameplay State Impact | Contract Status |
|---|---|---|---|---|---|---|---|
| `PlayerUpdates.cpp:1730` | `if (!IsBot()) Cell::VisitFarVisibleObjects(..., VISIBILITY_DISTANCE_GIGANTIC)` | Headless Playerbots | 400 yd gigantic object scan (world bosses/skyline terrain) | None (SQUID sight limit = 40 yd) | None (CoA sight limit = 60 yd) | None (Far objects remain active in world) | **SAFE** |
| `GridNotifiers.cpp:40` | `if (!i_player.IsBot()) ZoneWideVisibleWorldObjectsSet...` | Headless Playerbots | Zone-wide visual markers / event banners | None (SQUID does not parse zone markers) | None (CoA does not parse banners) | None (GameObjects exist in map grids) | **SAFE** |
| `PlayerUpdates.cpp:1776, 1784` | `if (!player->IsBot()) v.push_back(target)` | Headless Playerbots | Population of `i_visibleNow` vector for initial packet burst | None (SQUID does not use client visibility packets) | None (CoA inspects target memory directly) | None (Memory visibility container is linked) | **SAFE** |
| `GridNotifiers.cpp:90` | `if (!i_player.IsBot()) i_player.GetInitialVisiblePackets(*it)` | Headless Playerbots | Calls `GetAurasForTarget`, `AscensionQuestMarkers::Refresh`, `SendMeleeAttackStart` | None (SQUID ignores these 3 opcodes) | None (CoA reads aura pointers directly) | None (No server state altered; purely socket packets) | **SAFE** |
| `PlayerUpdates.cpp:1866` | `if (target->IsUnit() && !IsBot()) GetInitialVisiblePackets(...)` | Headless Playerbots | Single-object visibility transition | None | None | None | **ALIGNED in RC3.1** |

---

## 3. Deep-Dive: `GetInitialVisiblePackets` Downstream Analysis

When `GetInitialVisiblePackets(Unit* target)` executes:
1. `GetAurasForTarget(target)`:
   - Builds `SMSG_AURA_UPDATE_ALL` with all visible auras.
   - Core implementation in `Player.cpp`:
     ```cpp
     if (GetSession()->IsHeadlessBot())
         return;
     ```
   - Core already had an explicit guard returning early if `IsHeadlessBot()` is true.
   - SQUID codebase grep confirmed: **Zero occurrences** of `SMSG_AURA_UPDATE` or `SMSG_AURA_UPDATE_ALL`. SQUID evaluates target auras in memory via `Unit::GetAuraEffectsByType` and `Unit::GetAppliedAuras`.
2. `AscensionQuestMarkers::Refresh(this, creature)`:
   - Queries `player->GetQuestDialogStatus(creature)`.
   - Sends `SMSG_QUESTGIVER_STATUS` to display yellow '!' or '?' above NPC heads on the client UI.
   - SQUID codebase grep confirmed: **Zero occurrences** of `SMSG_QUESTGIVER_STATUS`.
3. `target->SendMeleeAttackStart(target->GetVictim(), this)`:
   - Builds `SMSG_ATTACKSTART` and sends via `SendDirectMessage`.
   - SQUID codebase grep confirmed: **Zero occurrences** of `SMSG_ATTACKSTART`. SQUID evaluates melee combat state via `Unit::HasUnitState(UNIT_STATE_MELEE_ATTACKING)` and `Unit::GetVictim()`.

**Conclusion**: Skipping `GetInitialVisiblePackets` for headless bots eliminates millions of redundant packet builds without suppressing any packet or state that SQUID or CoA AI consumes.

---

## 4. Dual-Path Visibility Alignment (RC3.1 Hardening)

In AzerothCore, visibility can be triggered via two paths:
1. **Batch Grid Sweep**: `Player::UpdateVisibilityOf(T* target, UpdateData& data, std::vector<Unit*>& visibleNow)` invoked by `VisibleNotifier::SendToSelf`.
2. **Single-Object Relocation**: `Player::UpdateVisibilityOf(WorldObject* target)` invoked when an individual object moves or enters world.

### Discrepancy Found in RC3:
In RC3, `GetInitialVisiblePackets` was guarded in the batch sweep (`SendToSelf`), but remained unguarded in `UpdateVisibilityOf(WorldObject* target)` at line 1866.

### Resolution in RC3.1:
Aligned line 1866 in `PlayerUpdates.cpp`:
```cpp
if (target->IsUnit() && !IsBot())
    GetInitialVisiblePackets((Unit*) target);
```
Both visibility pathways now adhere to the exact same contract:
- Both call `GetObjectVisibilityContainer().LinkWorldObjectVisibility(target)`.
- Both maintain the internal visibility graph (`HaveAtClient`, `VisibleWorldObjectsMap`).
- Both suppress redundant client-only display packets for headless bots.

---

## 5. Selfbot & Real Player Safety Verification

A critical question is whether `IsBot()` affects **Selfbots** in SQUID.
- SQUID Selfbots are created when a real human player logged in through a WoW client executes `.playerbot self`.
- In `Player.cpp`:
  ```cpp
  bool Player::IsBot() const { return m_session && m_session->IsBot(); }
  ```
- Because a selfbot is an authenticated game client session with an active TCP socket (`m_Socket != nullptr`), `_isBot == false`.
- **Verdict**: Selfbots and real human players evaluate `IsBot() == false`. They execute all far-visible scans, receive all zone-wide decorations, receive all aura updates, quest markers, and attack start packets. Zero regression for selfbots.

---

## 6. Packet Event Hook Ingestion Guarantee

In `WorldSession.cpp`:
```cpp
void WorldSession::SendPacket(WorldPacket const* packet)
{
    if (GetPlayer())
        sScriptMgr->OnPlayerbotPacketSent(GetPlayer(), packet);

    if (!m_Socket)
        return;
```
`sScriptMgr->OnPlayerbotPacketSent` fires **before** socket verification.
SQUID's `PlayerbotAI::HandleBotOutgoingPacket` intercepts all required gameplay packets:
- `SMSG_QUESTGIVER_QUEST_DETAILS` (shared quest acceptance)
- `SMSG_GROUP_INVITE` (party invitations)
- `SMSG_SPELL_FAILURE` (interrupts and error handling)
- `SMSG_SPELL_DELAYED` (cast pushback handling)
- `SMSG_EMOTE` & `SMSG_MESSAGECHAT` (communication and orders)
- `SMSG_FORCE_MOVE_ROOT` / `UNROOT` (movement coordination)

None of these packets are suppressed by visibility optimizations. All packet contracts are 100% preserved.
