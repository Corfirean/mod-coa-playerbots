# What a normal AzerothCore module can/cannot do (no core patch)

Recorded 2026-09-11, researched directly against `azerothcore-wotlk-coa`
source (file:line citations below). This is the evidence behind
`docs/architecture.md`'s claim that combat AI is free but player-parity
(gear/talents/group) is not.

## Hooks available to a module, unmodified

Hook classes live under `src/server/game/Scripting/ScriptDefines/`, each
`public ScriptObject`:

- **`UnitScript`** (`UnitScript.h`) — fires for both `Player` and `Creature`.
  `OnUnitUpdate(Unit*, uint32 diff)` (line 130) is a genuine per-tick hook,
  wired at `Unit.cpp:509`. Also: `OnUnitEnterCombat`/`OnUnitExitCombat`/
  `OnUnitDeath` (135-137), `OnAuraApply`/`OnAuraRemove` (102/104),
  `OnDamage`/`OnHeal`/`ModifyMeleeDamage`/`ModifySpellDamageTaken` (67-95).
- **`PlayerScript`** (`PlayerScript.h`) — ~140 Player-only hooks:
  `OnPlayerBeforeUpdate`/`OnPlayerUpdate` (281-282, wired at
  `PlayerUpdates.cpp:58,315`), `OnPlayerSpellCast` (329),
  `OnPlayerEnterCombat`/`OnPlayerLeaveCombat` (740/747),
  `OnPlayerCanGroupInvite`/`OnPlayerCanGroupAccept` (513/515),
  `OnPlayerGroupRollRewardItem` (458), `OnPlayerLootItem` (440),
  `OnPlayerLearnSpell`/`OnPlayerCanLearnTalent` (306/275).
- **`CreatureScript`** (`CreatureScript.h`) — thin, but has the one hook that
  matters most: `virtual CreatureAI* GetAI(Creature*) const` (line 57), the
  sanctioned way to attach a fully custom AI class to a creature
  (`RegisterCreatureAI` macro, line 71).
- **`GroupScript`** (`GroupScript.h`) — `OnAddMember`/`OnInviteMember`/
  `OnRemoveMember`/`OnChangeLeader`/`OnDisband`/`OnCreate` (48-64) — all
  **notifications after the fact**, keyed by `ObjectGuid`. Not a mutation
  point that lets a module inject a non-`Player` member.
- **`LootScript`** (`LootScript.h`) — only `OnLootMoney` (44). No roll hook.
- **`WorldScript`** (`WorldScript.h`) — `OnUpdate(uint32 diff)` (71, wired at
  `World.cpp:1342`) — global per-tick hook, good for a bot-manager singleton.

**Per-tick entry points available to a module, unmodified**:
`WorldScript::OnUpdate` (global) + `UnitScript::OnUnitUpdate`/
`PlayerScript::OnPlayerUpdate` (per-entity) + `CreatureAI::UpdateAI`
(per-creature, see below). Enough to drive autonomous decision logic with
zero core changes.

## Creature vs Player: equipment/talents/bags

`Creature : public Unit` (`Creature.h:46`); `Player : public Unit,
public GridObject<Player>` (`Player.h:1087`) — **siblings**, not
parent/child. Creature inherits none of Player's inventory/talent machinery.

- Creature's only "equipment": `LoadEquipment`, `m_equipmentId`
  (`Creature.h:67,197-199,495`) drives 3 *virtual item display* slots
  (`Unit::SetVirtualItem`, `Unit.h:1898`) from `creature_equip_template`. No
  `Item` objects, no stats, no bag.
- Real equipment is Player-exclusive: `EquipmentSlots` enum
  (`Player.h:661-684`, `EQUIPMENT_SLOT_END=19`), backing storage
  `Item* m_items[PLAYER_SLOTS_COUNT]` (`Player.h:2901`), bag/bank/keyring/
  currency slots (686-727) — all private `Player` members, no `Unit`
  declaration at all.
- `Unit` declares **no virtual** inventory API. `GetWeaponForAttack` is a
  concrete non-virtual `Player` method (`Player.h:1278`); code needing a
  weapon `Item` explicitly downcasts, e.g.
  `Unit.cpp:14153: Item* Weapon = ToPlayer()->GetWeaponForAttack(...)`
  (also `Unit.cpp:648,3906,3908,4157`) — hardcoded "if this happens to be a
  Player" logic, not a virtual seam.
- Talents: `PlayerTalentMap m_talents` (`Player.h:2924`), `GetTalentMap()`
  (2703), `GetSpecsCount`/`SetSpecsCount` (1772-1773) — Player-only, no
  `Unit`/`Creature` analog.

**Conclusion**: giving a spawned `Creature` real 19-slot gear, talents, and
bags is not reachable through public API + hooks — it needs the missing
fields ported onto `Creature.h`/`Unit.h` directly (a core patch), or you
sidestep it entirely by using a real `Player` (Playerbots' choice — see
`architecture.md`).

## Group membership and loot rolls — type-locked to Player, not just missing hooks

- `Group::AddMember(Player* player, uint8 roles = 0)` (`Group.h:209`, impl
  `Group.cpp:425`) takes `Player*` as the **parameter type**. Inside:
  `player->SetGroup(this, subGroup)` (`Group.cpp:472`) — `Player::SetGroup`/
  `GetGroup` are backed by `GroupReference m_group`, a **Player-only member**
  (`Player.h:3015`, accessor `Player.h:2561`).
- `GroupReference : public Reference<Group, Player>`
  (`GroupReference.h:26`) — template-locked, no `Unit`/`Creature` overload
  anywhere in the chain. `Group::GetFirstMember()` returns
  `GroupReference*` whose `GetSource()` yields `Player*`
  (`Group.cpp:1146`, etc.).
- Loot rolling (`Group::GroupLoot`/`NeedBeforeGreed`, `Group.cpp:1120-1283+`)
  iterates that Player-typed list and hard-requires a live session:
  `if (!member || !member->GetSession()) continue;` (`Group.cpp:1147,1237`),
  same at `1305,1384`. Sending a roll ultimately does
  `player->GetSession()->SendPacket(...)`.

**Conclusion**: a `Creature*` cannot join a `Group` through any existing
public API — this is a type-system wall, not a missing-hook gap. Confirms
why the fake-`Player` approach (not a Creature-based one) is the only way to
get real group/loot participation without patching `Group.cpp`/
`GroupReference.h` directly.

## WorldSession: no designed "bot session" hook, but a real, usable loophole

No `ScriptMgr` hook creates or manages a bot session. But `WorldSession`
tolerates a null socket pervasively enough that a module *can* construct one
without any header changes:

- `WorldSession::WorldSession(uint32 id, std::string&& name, uint32
  accountFlags, std::shared_ptr<WorldSocket> sock, ...)` (`WorldSession.h:412`)
  — `sock` is a nullable `shared_ptr`; nothing in the signature forces a live
  socket.
- `SendPacket`: `if (!m_Socket) return;` (`WorldSession.cpp:305-306`).
- `IsSocketClosed()`: `return !m_Socket || !m_Socket->IsOpen();`
  (`WorldSession.cpp:630-632`).
- `Update()` guards every socket-touching branch with `m_Socket &&`
  (`WorldSession.cpp:383,407`).
- `HandleSocketClosed()` guards with `if (m_Socket && ...)`
  (`WorldSession.cpp:618-628`), returns `false` safely for a null socket.
- Session registration is public: `WorldSessionMgr::AddSession(WorldSession*)`
  (`WorldSessionMgr.h:57`, impl `WorldSessionMgr.cpp:242-245`, just queues
  it).

**Conclusion**: a module *can* construct a `WorldSession` with a null
`WorldSocket`, attach a real `Player` to it, and register it via
`sWorldSessionMgr->AddSession(...)` — all public API, no header changes.
This is exactly the mechanism that lets a bot satisfy
`Group::AddMember(Player*)` and the `member->GetSession()` loot checks: the
bot genuinely *is* a `Player*` with a (dead) `WorldSession*`. It's incidental
robustness code being (ab)used deliberately, not a documented bot API — which
is exactly why the *edges* of this (login flow, a few null-check gaps the
convention didn't cover) still needed the measured ~2700-line patch rather
than zero.

## CreatureAI/SmartAI — autonomous combat needs zero core changes

- `UnitAI::UpdateAI(uint32 diff) = 0` (`CoreAI/UnitAI.h:207`) — pure
  virtual, called every tick; `CreatureAI` inherits it unchanged.
- `UnitAI::AttackStart`, `SelectTarget`/`SelectTargetList`
  (threat-based, `UnitAI.h:206,232,276`), `DoCast`/`DoCastVictim`/
  `DoCastAOE`/`DoCastRandomTarget`/`DoSpellAttackIfReady`
  (`UnitAI.h:397-413`), `DoMeleeAttackIfReady()` (411) — a complete public,
  unmodified toolkit for target selection, spellcasting, melee.
- `CreatureAI` (`CreatureAI.h:68`) adds `JustEngagedWith`, `EnterEvadeMode`,
  `JustDied`, `KilledUnit`, `AttackedBy`, `SpellHit`/`SpellHitTarget`,
  `MovementInform`, `WaypointReached` (125-268), all `virtual` with empty
  default bodies.
- `SmartAI : public CreatureAI` (`SmartScripts/SmartAI.h:45`) overrides
  exactly these virtuals and attaches purely through the sanctioned
  `CreatureScript::GetAI()` hook / `RegisterCreatureAI` macro — a
  database-driven (`smart_scripts` table), zero-core-patch mechanism
  AzerothCore already ships.

**Conclusion**: "a companion creature follows you and casts
spells/melees in combat" needs zero core changes — `CreatureAI`/`SmartAI`
are built for exactly this. This only matters if a *Creature-based* (not
fake-Player) companion is ever the chosen shape for something — see the
Scoping section in `architecture.md`.
