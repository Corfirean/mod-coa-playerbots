// Imported from jrad7/mod-dungeon-clear (AGPL-3.0-or-later) by tools/dungeon-import/import_dc.py; engine-bound helpers are stubbed.
// Do not edit by hand: re-run the importer. Implementations of the stubs live in dungeon/DungeonConditions.cpp and DungeonHooks.cpp.
/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "dungeon/DcCompat.h"
#include "dungeon/DungeonEventTables.h"
#include "dungeon/DungeonRosterBuilders.h"
#include "dungeon/DungeonClearRouteRegistry.h"
#include "dungeon/DungeonWingRegistry.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "Creature.h"
#include "GameObject.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"

#include "ScriptMgr.h"
#include "Spell.h"
#include "dungeon/DungeonBossInfo.h"







// Blackrock Spire (map 229) is two dungeons on one map: Lower (LFG 32, ends at
// Overlord Wyrmthalak) and Upper (LFG 44, Pyroguard Emberseer -> General
// Drakkisath). This file is its definition unit: the wing split, and UBRS's
// encounter gates as Anchored events on one map-229 roster patch. The TU stays
// linked because DungeonWingRegistry, DungeonEventRegistry, BossRosterRegistry and
// ObjectiveHookRegistry each call into it explicitly.
//
// MOD-INDIVIDUAL-PROGRESSION IS IN THE LOOP, AND EVERY EVENT HERE MUST WORK IN ALL
// THREE OF ITS STATES. IP registers instance_blackrock_spire and
// at_dragonspire_hall under the core's ScriptNames (the last registration wins, so
// its versions run whenever it is compiled in, whatever IndividualProgression.Enable
// says), and its world SQL for map 229 stays applied if the module is later
// removed. For UBRS that changes exactly one thing, the Dragonspine Door:
//
//   stock core       the door spawns open; nothing touches it
//   IP compiled      the door spawns shut; AT 2046 with a non-LFG player carrying
//                    the Seal of Ascension (12344) runs SetData(2046, 26), which
//                    lights the six braziers and opens it ~3s later (saved)
//   IP SQL only      the door spawns shut and nothing ever opens it
//
// and the shape of at_dragonspire_hall's call (core SetData(1, 2046), IP
// SetData(2046, 15)). The hall runes, Emberseer, the Rookery, Rend/Gyth and the
// rest are byte-identical. So nothing here includes an IP header, assumes the door
// is open or shut, or assumes which at_dragonspire_hall is registered: the door
// hook enters the SAME script entry points a player's act would.
//
// A bot cannot earn the Seal. The door hook stands in for "a party member holds
// it", the way BRD's key-locked doors are waived for a party that is already
// inside ("the party has access by being here"). That also skips IP's LFG-group
// rule; DC runs are explicit `.dc on ubrs` / test runs, and LFG 44 has no
// lfg_dungeon_template row on this core, so a queue-filled UBRS run cannot exist.

namespace
{
    using namespace DcBlackrockSpire;

    uint32 StepElapsedMs(AiObjectContext* context)
    {
        DungeonEventProgress const& prog =
            context->GetValue<DungeonEventProgress&>(DcKey::EventProgress)->Get();
        return getMSTimeDiff(prog.stepStartMs, getMSTime());
    }

    // True if `slot` was stamped within the last `withinMs` (without stamping it).
    bool StampedWithin(DcRunState const& run, DcThrottle slot, uint32 withinMs)
    { return false; }

    // Run AT 2046's script for `bot` exactly as the client's CMSG_AREATRIGGER
    // would reach it (ScriptMgr::OnAreaTrigger is what the opcode handler calls
    // once the radius check passes): STORE snapshots the rune packs and CHECK
    // starts, whichever at_dragonspire_hall is registered. A repeat re-snapshots,
    // which only ever drops a mob that has left its rune — never harmful.
    bool FireAreaTriggerScript(Player* bot, uint32 triggerId)
    {
        AreaTrigger const* at = sObjectMgr->GetAreaTrigger(triggerId);
        if (!at)
        {
            LOG_ERROR("playerbots.dungeonclear",
                      "[DC:{}] Upper Blackrock Spire — area trigger {} missing from the "
                      "world DB; its encounter cannot be started", bot->GetName(), triggerId);
            return false;
        }
        return sScriptMgr->OnAreaTrigger(bot, at);
    }

    bool FireDragonspireHallTrigger(Player* bot)
    {
        return FireAreaTriggerScript(bot, AT_DRAGONSPIRE_HALL);
    }

    // --- OpenDragonspineDoor (hook 56) -------------------------------------
    // Layered so each of the header's three states passes:
    //   1. arm the hall (once per attempt): AT 2046's script, then IP's opener,
    //      SetData(2046, 26) — under IP exactly what its AT script does after the
    //      seal check; under the core a type it has no case for, a no-op
    //   2. Done once the door reads open (stock: at once; IP: ~3s)
    //   3. after DOOR_SCRIPT_WAIT_MS, click it: the player-click path for a
    //      lock-free door (template lock 0, no auto-close) — the IP-SQL-only state
    //   4. Blocked if even that has not opened it
    // The event owns the door: 164725 is IsScriptOnly, so the walker never pops
    // it before the hall is armed (it would still pause on it, but the run
    // stands at this objective before any path through the door is asked for).
    ObjectiveArriveResult OpenDragonspineDoor(Player* bot, AiObjectContext* context,
                                              DungeonBossInfo const& /*info*/)
    { return ObjectiveArriveResult::Blocked; }

    // --- HallRunesSweep (hook 57) ------------------------------------------
    // The last step of the hall event, after the seven rune-room ClearRadius
    // steps. The instance's CHECK unlights a rune once every mob STORE listed for
    // it is dead, and opens Emberseer In when all seven are out; this hook is
    // the safety net for what the room steps can let slip:
    //   * a lit rune with a living pack mob near it (a 15%-HP fleer that came
    //     home after its room completed, a Summoner add) -> Restart: the event
    //     rewinds onto the room steps, which clear it (step 0 is a MoveTo on the
    //     whole hall, Done wherever the tank is)
    //   * lit runes with nobody at them, or every rune out and the door still
    //     shut, for HALL_REARM_AFTER_MS -> CHECK is not running (the trigger was
    //     never tripped) or its snapshot holds a mob that is not coming back:
    //     re-fire AT 2046's script, which re-snapshots and restarts CHECK
    //   * Emberseer In open -> Done
    ObjectiveArriveResult HallRunesSweep(Player* bot, AiObjectContext* context,
                                         DungeonBossInfo const& /*info*/)
    { return ObjectiveArriveResult::Blocked; }

    uint32 BossState(InstanceScript* inst, uint32 slot)
    {
        return inst ? static_cast<uint32>(inst->GetBossState(slot)) : STATE_NOT_STARTED;
    }

    // Every living party member on the bot's map outside `box`, by name.
    std::string MembersOutside(Player* bot, Box const& box, uint32& count)
    {
        count = 0;
        std::string names;
        auto check = [&](Player* m)
        {
            if (!m || !m->IsInWorld() || !m->IsAlive() || m->GetMapId() != bot->GetMapId() ||
                InBox(box, m->GetPositionX(), m->GetPositionY(), m->GetPositionZ()))
                return;
            ++count;
            names += (names.empty() ? "" : ", ") + m->GetName();
        };
        if (Group* group = bot->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                check(ref->GetSource());
        else
            check(bot);
        return names;
    }

    // Shared body of the two musters (hooks 58, 60): Running while a living
    // member is outside `box`, for at most MUSTER_CAP_MS — then proceed, naming
    // who is outside, because a member that cannot get in is better fought
    // around than stalled on (stranded recovery pulls a stuck bot to the tank).
    ObjectiveArriveResult Muster(Player* bot, AiObjectContext* context, Box const& box, char const* room)
    { return ObjectiveArriveResult::Blocked; }

    // --- MusterInHallOfBinding (hook 58) -----------------------------------
    // Before the altar: the incarcerators' aggro shuts both doors into the room
    // two seconds after it, until Emberseer dies. SealedEncounterRegistry does
    // not fit — it musters on the ENGAGE, and here the seal comes ~70s before
    // any engage. Done at once if the encounter is already running or over, so
    // a rewind into a live room does not wait on a party that is fighting.
    ObjectiveArriveResult MusterInHallOfBinding(Player* bot, AiObjectContext* context,
                                                DungeonBossInfo const& /*info*/)
    {
        if (BossState(DcTargeting::GetInstanceScript(bot), DATA_PYROGUARD_EMBERSEER) != STATE_NOT_STARTED)
            return ObjectiveArriveResult::Done;
        return Muster(bot, context, HALL_OF_BINDING, "Hall of Binding");
    }

    // --- UseBlackrockAltar (hook 62) ---------------------------------------
    // The plan had a bare UseGO here. The altar is a one-participant summoning
    // ritual, and GameObject::Use only STARTS it: the user channels 16532 and
    // GameObject::Update completes the ritual five seconds later only if that
    // channel is still up (CheckRitualList). An event step that clicks and moves
    // on leaves a bot free to walk or cast through the channel, and the altar
    // silently resets. So this hook clicks, then holds the leader still while
    // it channels, and is Done only on what the ritual causes: Emberseer's
    // state leaving NOT_STARTED. If the ritual has not taken in
    // ALTAR_FALLBACK_MS it enters the same entry point the ritual's spell
    // reaches, the instance's ProcessEvent(4884) — which itself ignores the call
    // unless the state is NOT_STARTED, so a late ritual cannot double-start it.
    ObjectiveArriveResult UseBlackrockAltar(Player* bot, AiObjectContext* context,
                                            DungeonBossInfo const& /*info*/)
    { return ObjectiveArriveResult::Blocked; }

    // --- EmberseerFreed (hook 59) ------------------------------------------
    // Holds the event — and so the roster's Emberseer row — through the ~69s in
    // which he is NOT_SELECTABLE and immune to players (Growing stacks to 20,
    // then he breaks free and DoZoneInCombats). The Nethekurse / Novos shape.
    // NOT_STARTED is the encounter resetting under the party (any incarcerator
    // evading resets it, 5s later): Restart, so the event re-musters and uses the
    // altar again.
    ObjectiveArriveResult EmberseerFreed(Player* bot, AiObjectContext* context,
                                         DungeonBossInfo const& /*info*/)
    { return ObjectiveArriveResult::Blocked; }

    // --- MusterInStadium (hook 60) -----------------------------------------
    // At the muster point, before anyone steps into AT 2026: the portcullis
    // shuts 1s after it fires, for anyone inside its 20yd — a scout included —
    // and the stadium is sealed until Rend dies or the event fails. Done at once
    // if the stadium is already running or over.
    ObjectiveArriveResult MusterInStadium(Player* bot, AiObjectContext* context,
                                          DungeonBossInfo const& /*info*/)
    {
        uint32 const state = BossState(DcTargeting::GetInstanceScript(bot), DATA_WARCHIEF_REND_BLACKHAND);
        if (state == STATE_IN_PROGRESS || state == STATE_DONE)
            return ObjectiveArriveResult::Done;
        return Muster(bot, context, STADIUM_ARENA, "Blackrock Stadium");
    }

    // --- EnsureStadiumStarted (hook 61) ------------------------------------
    // On the arena stand, inside AT 2026. A client crossing the trigger runs
    // at_blackrock_stadium (and the test relay does the same for a bot-only
    // party); this is the belt and braces for a party whose leader never sent
    // the opcode, and the restart after a FAIL: the hold below rewinds the event
    // on NOT_STARTED, and this step fires the trigger again from the stand. The
    // script respawns a dead Rend, and ignores repeats (_gythEvent).
    ObjectiveArriveResult EnsureStadiumStarted(Player* bot, AiObjectContext* context,
                                               DungeonBossInfo const& /*info*/)
    { return ObjectiveArriveResult::Blocked; }
}

void RegisterBlackrockSpireHooks(ObjectiveHookRegistry::HookTable& out)
{
    ObjectiveHookRegistry::AddHook(out, HOOK_OPEN_DRAGONSPINE_DOOR, &OpenDragonspineDoor);
    ObjectiveHookRegistry::AddHook(out, HOOK_HALL_RUNES_SWEEP, &HallRunesSweep);
    ObjectiveHookRegistry::AddHook(out, HOOK_MUSTER_HALL_OF_BINDING, &MusterInHallOfBinding);
    ObjectiveHookRegistry::AddHook(out, HOOK_USE_BLACKROCK_ALTAR, &UseBlackrockAltar);
    ObjectiveHookRegistry::AddHook(out, HOOK_EMBERSEER_FREED, &EmberseerFreed);
    ObjectiveHookRegistry::AddHook(out, HOOK_MUSTER_IN_STADIUM, &MusterInStadium);
    ObjectiveHookRegistry::AddHook(out, HOOK_ENSURE_STADIUM_STARTED, &EnsureStadiumStarted);
}

void RegisterBlackrockSpireEvents(std::vector<DungeonEvent>& out)
{
    using namespace DcBlackrockSpire;

    // --- THE DRAGONSPINE DOOR, ANCHORED ahead of Emberseer (index 9) ------
    // Stand inside AT 2046, arm the hall and open the door (hook 56, see
    // OpenDragonspineDoor), then confirm on the door itself. No event-level
    // CompleteWhenGOState on the door: under the stock core it spawns open, and
    // completing on it would skip the arming the hall event depends on.
    //
    // PERSISTENT: the Scarshield packs that flank the door approach can start a
    // fight mid-event, and a rewind would re-arm a hall that is already armed.
    out.push_back(
        EventBuilder(MAP_ID, EVENT_DRAGONSPINE_DOOR, "Dragonspine Door")
            .Anchored(/*orderIndex*/ 9)
            .Persistent()
            .MoveTo(DOOR_STAND_X, DOOR_STAND_Y, DOOR_STAND_Z, /*radius*/ 4.0f)
            .Custom(HOOK_OPEN_DRAGONSPINE_DOOR)
            .Timeout(DOOR_SCRIPT_WAIT_MS + DOOR_CLICK_WAIT_MS + 10000)
            .WaitForGOState(GO_DRAGONSPINE_DOOR, GO_STATE_OPEN, /*timeoutMs*/ 15000, DOOR_SCAN)
            .Build());

    // --- THE DRAGONSPIRE HALL, ANCHORED second on index 9 -----------------
    // Emberseer In (175244) opens only when all seven hall runes are out, and a
    // rune goes out only when every Blackhand pack mob the instance listed within
    // 15yd of it is dead. So: one ClearRadius per rune room, measured from the
    // live spawns (the DM West "large room" lesson: several small volumes, never
    // one giant one), in navmesh order — the lower floor 3, 1, 4, 2, up the ramp,
    // then 7, 6, 5, ending 49yd from Emberseer In. OnlyEntries keeps each room
    // from waiting on the Rage Talon Dragonspawn in the aisle, which the ordinary
    // engage layer fights as the party passes.
    //
    // Step 0 is a MoveTo on the anchor with the whole hall as its radius (the
    // Lyceum shape): Done on arrival, so the persistent event stamps its
    // progress before any room engages, and Done wherever a sweep Restart
    // rewinds from — it never walks the tank back to the door.
    //
    // The event completes whatever step it is on once Emberseer In reads open: a
    // room finished by an evade and re-snapshot, or an instance reloaded with the
    // room DONE, short-circuits. No DrivesInCombat: the ClearRadius steps are the
    // combat driver (the Stratholme Slaughterhouse shape).
    EventBuilder hall(MAP_ID, EVENT_DRAGONSPIRE_HALL, "Dragonspire Hall");
    hall.Anchored(/*orderIndex*/ 9)
        .Persistent()
        .CompleteWhenGOState(GO_EMBERSEER_IN, GO_STATE_OPEN, HALL_SCAN)
        .MoveTo(HALL_ANCHOR_X, HALL_ANCHOR_Y, HALL_ANCHOR_Z, /*radius*/ HALL_SCAN);
    for (HallRune const& r : HALL_RUNES)
        hall.ClearRadius(r.x, r.y, r.z, HALL_RUNE_CLEAR_RADIUS, HALL_RUNE_CLEAR_ZBAND)
            .OnlyEntries({ NPC_BLACKHAND_DREADWEAVER, NPC_BLACKHAND_SUMMONER, NPC_BLACKHAND_VETERAN })
            .Timeout(180000);
    // CHECK runs every 3s, the sweep's re-arm waits 8s and re-fires at most
    // every 20s; five minutes here is a hall that will not finish.
    hall.Custom(HOOK_HALL_RUNES_SWEEP).Timeout(300000);
    out.push_back(hall.Build());

    // --- THE BLACKROCK ALTAR, ANCHORED third on index 9 -------------------
    // Pyroguard Emberseer is unselectable and immune until a player uses the
    // Blackrock Altar; seven Blackhand Incarcerators (immune to all) channel on
    // him until then. The altar starts it (hook 62): +2s the incarcerators wake
    // and DoZoneInCombat the room — their aggro SHUTS Emberseer In and the Doors
    // until he dies — and ~69s after the altar he is freed whether they are dead
    // or not. So:
    //   0. stand on the room floor north of the altar
    //   1. hook 58: everyone inside the room before it is sealed
    //   2. hook 62: use the altar and hold its 5s ritual channel
    //   3. kill the incarcerators (rank 0, Strike only; they flee at 30% toward
    //      each other, and nobody can leave the sealed room, so an evade takes a
    //      wipe)
    //   4. hook 59: hold until Emberseer is attackable; Restart if the encounter
    //      reset (any incarcerator evading resets the whole thing)
    // The event then completes and the roster's Emberseer row engages him on his
    // platform. It also completes, at any step, on Emberseer Out opening — his
    // death, or a reload with him DONE.
    //
    // PERSISTENT: the incarcerator fight and the 69s release span combat gaps.
    out.push_back(
        EventBuilder(MAP_ID, EVENT_BLACKROCK_ALTAR, "Blackrock Altar")
            .Anchored(/*orderIndex*/ 9)
            .Persistent()
            .CompleteWhenGOState(GO_EMBERSEER_OUT, GO_STATE_OPEN, BINDING_SCAN)
            .MoveTo(ALTAR_STAND_X, ALTAR_STAND_Y, ALTAR_STAND_Z, /*radius*/ 4.0f)
            .Custom(HOOK_MUSTER_HALL_OF_BINDING)
            .Timeout(MUSTER_CAP_MS + 10000)
            .Custom(HOOK_USE_BLACKROCK_ALTAR)
            .Timeout(ALTAR_FALLBACK_MS + 15000)
            // Centred on the stand point, not on Emberseer's platform: a
            // ClearRadius walks back to its centre to certify the room clear.
            .ClearRadius(ALTAR_STAND_X, ALTAR_STAND_Y, ALTAR_STAND_Z, INCARCERATOR_CLEAR_RADIUS,
                         /*zBand*/ 8.0f)
            .OnlyEntries({ NPC_BLACKHAND_INCARCERATOR })
            .Timeout(180000)
            .Custom(HOOK_EMBERSEER_FREED)
            .Timeout(120000)
            .Build());

    // --- THE BLACKROCK STADIUM, ANCHORED ahead of Rend (index 11) ---------
    // The Ring of Law shape. AT 2026 starts it: the portcullis behind the party
    // shuts, and Rend sends five waves of whelps, dragonspawn and handlers out of
    // the pen (each DoZoneInCombat 100yd, so the party always has attackers),
    // then Gyth with Rend on his back; at 25% Gyth drops Rend onto the floor.
    // Rend's death reopens the portcullis and opens TOBOSSROOMS, the way on.
    //   0. the muster point, inside the portcullis and 36yd outside the trigger
    //   1. hook 60: everyone inside the arena before anyone trips it
    //   2. the arena stand, inside the trigger (crossing it trips it for a
    //      client and for the test relay)
    //   3. hook 61: Done once Rend reads IN_PROGRESS; fires the trigger itself
    //      if nothing has
    //   4. hold the stand until Rend is DONE. The hold is the fight: the waves,
    //      Gyth and the summoned Rend come to the party and the combat engine
    //      fights them; between waves (~13s) the event keeps the party on the
    //      stand, 45yd from the pen, so no summon ever despawns out of combat.
    // A summon despawning alive (nobody near, or 30s out of combat) or Gyth or
    // Rend evading FAILs it: summons despawn, the portcullis reopens, Rend is
    // respawned and Resets to NOT_STARTED — usually within the same tick, so the
    // hold restarts on either state, and the rewind re-musters and re-fires.
    // Neither Gyth nor the summoned Rend is a roster row; the roster's Rend row
    // (the balcony spawn, bit 11) is already dead when this objective latches.
    //
    // PERSISTENT: five waves, Gyth and Rend are many fights with gaps.
    out.push_back(
        EventBuilder(MAP_ID, EVENT_BLACKROCK_STADIUM, "Blackrock Stadium")
            .Anchored(/*orderIndex*/ 11)
            .Persistent()
            .MoveTo(STADIUM_MUSTER_X, STADIUM_MUSTER_Y, STADIUM_MUSTER_Z, /*radius*/ 3.0f)
            .Custom(HOOK_MUSTER_IN_STADIUM)
            .Timeout(MUSTER_CAP_MS + 10000)
            .MoveTo(ARENA_STAND_X, ARENA_STAND_Y, ARENA_STAND_Z, /*radius*/ 4.0f)
            .Custom(HOOK_ENSURE_STADIUM_STARTED)
            .Timeout(30000)
            .MoveToHoldUntilBossState(ARENA_STAND_X, ARENA_STAND_Y, ARENA_STAND_Z, ARENA_HOLD_RADIUS,
                                      DATA_WARCHIEF_REND_BLACKHAND, DcBossStateBit(STATE_DONE))
            .RestartOnBossState(DcBossStateBit(STATE_FAIL) | DcBossStateBit(STATE_NOT_STARTED))
            // Five waves at ~13s gaps plus the fights, Gyth and Rend: 5-8 min.
            // Only out-of-combat time counts against it.
            .Timeout(900000)
            .Build());
}

// --- roster patch ----------------------------------------------------------
void RegisterBlackrockSpireRoster(std::vector<BossRosterPatch>& t)
{
    using namespace DcRoster;
    using namespace DcBlackrockSpire;

    // --- Upper Blackrock Spire (map 229) ---------------------------------
    // ONE patch for the whole map (FindPatch returns the first per map, the Dire
    // Maul precedent); the wing filter scopes each run, so LBRS sees none of it.
    // Each UBRS gate is an objective sharing the index of the boss it unlocks,
    // so the objective-before-boss tie-break walks the party to it first; two
    // objectives on one key keep insertion order (BRD's Lyceum South/North).
    //
    // Solakar Flamewreath (10264, bit 10) has no static spawn, so BossSpawnIndex
    // never lists him and there is no row to mark skipByDesign: he stays out of
    // the run by not being in it. The ubrs wing still names him, for credit.
    //
    // The Beast is re-anchored on his pull-back camp (keeping kill-bit 12), the
    // BRD Plugger shape: boss-nav walks the party there instead of to his live,
    // patrolling position, and BossPullbackRegistry has the tank fight him on it.
    BossRosterPatch p;
    p.mapId = MAP_ID;
    p.remove = { NPC_THE_BEAST };
    p.add = {
        MakeBoss(NPC_THE_BEAST, MAP_ID, "The Beast", BEAST_CAMP_X, BEAST_CAMP_Y, BEAST_CAMP_Z,
                 /*completionFrom*/ NPC_THE_BEAST),
        MakeObjective(OBJ(OBJ_DRAGONSPINE_DOOR), /*encounterIndex*/ 9, MAP_ID, "Dragonspine Door",
                      DOOR_STAND_X, DOOR_STAND_Y, DOOR_STAND_Z, /*arriveRadius*/ 6.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, EVENT_DRAGONSPINE_DOOR),
        MakeObjective(OBJ(OBJ_DRAGONSPIRE_HALL), /*encounterIndex*/ 9, MAP_ID, "Dragonspire Hall",
                      HALL_ANCHOR_X, HALL_ANCHOR_Y, HALL_ANCHOR_Z, /*arriveRadius*/ 6.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, EVENT_DRAGONSPIRE_HALL),
        MakeObjective(OBJ(OBJ_BLACKROCK_ALTAR), /*encounterIndex*/ 9, MAP_ID, "Blackrock Altar",
                      ALTAR_STAND_X, ALTAR_STAND_Y, ALTAR_STAND_Z, /*arriveRadius*/ 5.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, EVENT_BLACKROCK_ALTAR),
        MakeObjective(OBJ(OBJ_BLACKROCK_STADIUM), /*encounterIndex*/ 11, MAP_ID, "Blackrock Stadium",
                      STADIUM_MUSTER_X, STADIUM_MUSTER_Y, STADIUM_MUSTER_Z, /*arriveRadius*/ 4.0f,
                      /*gateEntry*/ 0, /*hook*/ 0, EVENT_BLACKROCK_STADIUM),
    };
    t.push_back(std::move(p));
}

// --- anchor route ----------------------------------------------------------
void RegisterBlackrockSpireRoute()
{
    using namespace DcBlackrockSpire;

    // --- Emberseer's room -> the stadium, across the Rookery ---------------
    // The Rookery floor has 149 Rookery Eggs, 1.5yd proximity traps that hatch
    // a Whelp on any player. The navmesh's corridor across it is egg-clear today
    // by 3.3yd and by accident: any change that moves it (taxing the egg
    // clusters did, sending it round the south half 0.22yd from an egg — see
    // the 229 note in DcNavPenaltyRegistry.cpp) walks the party into them. So
    // the leg is authored: out through Emberseer Out, along the measured north
    // lane (every egg >= 3.8yd off), up the north-wall ramp, then the navmesh's
    // own corridor to the stadium muster point. Anchors are walked as
    // straight legs; every leg is under 10yd and the Rookery's use a 2yd arrive
    // radius, so the tank walks the lane rather than cutting its corners.
    //
    // Keyed on the Blackrock Stadium objective, the next roster target after
    // Emberseer, and starting in Emberseer's room, where the party stands when
    // he dies; it ends on that objective's anchor, the muster point.
    // Followers still path to the tank with the stock PathGenerator; a trailing
    // bot can clip an egg, and one or two Whelps are a non-event at level 60.
    std::vector<WaypointHint> hints;
    for (uint32 i = 0; i < sizeof(ROOKERY_ROUTE) / sizeof(ROOKERY_ROUTE[0]); ++i)
    {
        WaypointHint h;
        h.x = ROOKERY_ROUTE[i].x;
        h.y = ROOKERY_ROUTE[i].y;
        h.z = ROOKERY_ROUTE[i].z;
        if (i >= ROOKERY_ROUTE_LANE_FIRST && i <= ROOKERY_ROUTE_LANE_LAST)
            h.arriveRadius = ROOKERY_ANCHOR_ARRIVE;
        hints.push_back(h);
    }
    DungeonClearRouteRegistry::Register(MAP_ID, DUNGEON_DIFFICULTY_NORMAL,
                                        DcRoster::OBJ(OBJ_BLACKROCK_STADIUM), std::move(hints));
}

// --- wing layout ---------------------------------------------------------
void RegisterBlackrockSpireWings(std::unordered_map<uint32, DungeonWingLayout>& store)
{
    // --- Blackrock Spire (map 229) -------------------------------
    // Both wings are entered through the SAME portal and are stacked: from the
    // portal (78.5, -225.0, 49.8) the nearest bosses are UBRS's (Emberseer 87yd,
    // Drakkisath 96yd; Omokk, LBRS's first, is 128yd). Nearest-boss detection
    // would put an LBRS party on the UBRS list at the door, and the last brs test
    // runs show the other half of the problem: clear LBRS, then walk back up to
    // the Dragonspine Door for Emberseer. So the wing is chosen per RUN
    // (WingSelect::Explicit, see DcRunWing), defaulting to LBRS.
    //
    // Encounter bits from DungeonEncounter.dbc, verified against acore_world
    // (blackrock_spire.h's DATA_* indices diverge from bit 10 on — use these):
    //   LBRS bits 0-8: Omokk, Vosh'gajin, Voone, Smolderweb, Urok (GO summon),
    //                  Zigris, Gizrul (spawns on Halycon's death), Halycon,
    //                  Wyrmthalak (lastEncounterDungeon = 32)
    //   UBRS bits 9-13: Emberseer, Solakar (Father Flame summon), Rend, The
    //                  Beast, Drakkisath (lastEncounterDungeon = 44)
    // Urok, Gizrul and Solakar have no static spawn, so BossSpawnIndex never
    // lists them; they are here so credit and counting still resolve.
    //
    // UBRS's scripted gates (the roster patch's objectives) are listed with it,
    // each just before the boss it unlocks.
    using DcRoster::OBJ;
    store[229] = {true, {
        {"Blackrock Spire (Lower)", {
            9196,   // Highlord Omokk
            9236,   // Shadow Hunter Vosh'gajin
            9237,   // War Master Voone
            10596,  // Mother Smolderweb
            10584,  // Urok Doomhowl
            9736,   // Quartermaster Zigris
            10268,  // Gizrul the Slavener
            10220,  // Halycon
            9568,   // Overlord Wyrmthalak
        }, "lbrs", /*lfgDungeonId*/ 32, /*terminalBossEntry*/ 9568, /*encounterMask*/ 0x01FFu},
        {"Blackrock Spire (Upper)", {
            OBJ(DcBlackrockSpire::OBJ_DRAGONSPINE_DOOR),  // Dragonspine Door (event 1)
            OBJ(DcBlackrockSpire::OBJ_DRAGONSPIRE_HALL),  // Dragonspire Hall runes (event 2)
            OBJ(DcBlackrockSpire::OBJ_BLACKROCK_ALTAR),   // Blackrock Altar, Emberseer's release (event 3)
            9816,   // Pyroguard Emberseer
            10264,  // Solakar Flamewreath
            OBJ(DcBlackrockSpire::OBJ_BLACKROCK_STADIUM),  // Blackrock Stadium, Rend and Gyth (event 4)
            10429,  // Warchief Rend Blackhand
            10430,  // The Beast
            10363,  // General Drakkisath
        }, "ubrs", /*lfgDungeonId*/ 44, /*terminalBossEntry*/ 10363, /*encounterMask*/ 0x3E00u},
    }, WingSelect::Explicit, /*defaultWing*/ "lbrs"};
}
