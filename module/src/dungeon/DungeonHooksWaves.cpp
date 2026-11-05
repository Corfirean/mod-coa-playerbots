/*
 * mod-coa-playerbots -- dungeon automation
 *
 * The "Custom" steps of the wave dungeons: the Violet Hold (start, defend, repel the wave), the Culling of Stratholme (the ten waves) and the
 * Halls of Lightning (the slag pit). The original driver of jrad7/mod-dungeon-clear (AGPL-3.0-or-later) for each of these is a long controller
 * built on its own movement stack; these are the same decisions in their short form on our movement and combat layers:
 *
 *   - things that never come to the party (Portal Keepers, a released prisoner, a wave that does not aggro) are walked to and attacked;
 *   - the rest is left to the ordinary combat logic (the hook returns Done and yields the tick).
 *
 * Returning Running means "I am steering this tick", Done means "nothing to steer: let the combat logic fight".
 */

#include "dungeon/DungeonHooks.h"
#include "dungeon/DungeonEventExecutor.h"
#include "dungeon/DungeonEventTables.h"
#include "dungeon/DungeonRuntime.h"
#include "BotMovement.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "Opcodes.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <list>
#include <vector>

namespace
{
    using Result = ObjectiveArriveResult;

    InstanceScript* InstanceOf(Player* bot)
    {
        Map* map = bot ? bot->GetMap() : nullptr;
        InstanceMap* instanceMap = map ? map->ToInstanceMap() : nullptr;
        return instanceMap ? instanceMap->GetInstanceScript() : nullptr;
    }

    // The nearest living hostile of one of the entries. tempOnly: summons only (spawn id 0), so a creature of the same entry that stands in the
    // dungeon from the start is not mistaken for a wave member.
    Creature* NearestOf(Player* tank, std::vector<uint32> const& entries, float range, bool tempOnly, bool (*accept)(Creature const*) = nullptr)
    {
        std::list<Creature*> found;
        tank->GetCreatureListWithEntryInGrid(found, entries, range);
        Creature* best = nullptr;
        float bestDist = 1.0e9f;
        for (Creature* c : found)
        {
            if (!c || !c->IsAlive() || !tank->IsValidAttackTarget(c))
                continue;
            if (tempOnly && c->GetSpawnId() != 0)
                continue;
            if (accept && !accept(c))
                continue;
            float const d = tank->GetExactDist(c);
            if (d < bestDist)
            {
                bestDist = d;
                best = c;
            }
        }
        return best;
    }

    uint64 GoalId(uint32 hookId)
    {
        return (uint64(0xD0E0) << 16) | hookId;
    }

    // Walks to a point; true while it is still on its way.
    bool WalkTo(Player* tank, uint32 hookId, float x, float y, float z, float radius)
    {
        if (tank->GetExactDist(x, y, z) <= radius)
        {
            BotMovement::ForceReleaseOwner(tank, MoveOwner::AutoDungeon);
            return false;
        }
        BotMovement::Navigate(tank, MoveOwner::AutoDungeon, GoalId(hookId), x, y, z, std::max(2.0f, radius * 0.6f));
        return true;
    }

    bool ThrottledEvery(uint32& lastMs, uint32 periodMs)
    {
        uint32 const now = getMSTime();
        if (lastMs && now - lastMs < periodMs)
            return true;
        lastMs = now;
        return false;
    }

    // --- The Violet Hold (map 608) --------------------------------------------------------------------------------------------------------
    // Lieutenant Sinclari starts the siege: her menu 9997 ("Activate the crystals...") leads to 9998, whose only option fires
    // ACTION_START_INSTANCE on the instance script (smart_scripts of 30658). Until then nothing in the hold moves.
    constexpr uint32 VH_MENU_START = 9997;
    constexpr uint32 VH_MENU_CONFIRM = 9998;

    Result VioletHoldStart(Player* tank)
    {
        using namespace DcVioletHold;
        if (tank->GetMapId() != MAP)
            return Result::Done;
        InstanceScript* inst = InstanceOf(tank);
        if (!inst)
            return Result::Running;
        if (inst->GetData(DATA_ENCOUNTER_STATUS) != 0)
            return Result::Done;
        Creature* sinclari = tank->FindNearestCreature(NPC_SINCLARI, 90.0f, true);
        if (!sinclari)
            return Result::Running;
        if (WalkTo(tank, 15, sinclari->GetPositionX(), sinclari->GetPositionY(), sinclari->GetPositionZ(), 6.0f))
            return Result::Running;
        static uint32 last = 0;
        if (ThrottledEvery(last, 1500))
            return Result::Running;
        uint32 const menu = DungeonEventExecutor::OpenMenuId(tank);
        if (menu == VH_MENU_START || menu == VH_MENU_CONFIRM)
            DungeonEventExecutor::SelectOption(tank, sinclari, 0);
        else
            DungeonEventExecutor::TalkTo(tank, sinclari, -1);
        return Result::Running;
    }

    // Garrison the staging point of the arena between waves until the boss slot of the checkpoint is done. An instance that fell back to
    // NOT_STARTED (a wipe, a lost seal) is started again.
    Result VioletHoldDefend(Player* tank, uint32 hookId, uint32 slot)
    {
        using namespace DcVioletHold;
        if (tank->GetMapId() != MAP)
            return Result::Done;
        InstanceScript* inst = InstanceOf(tank);
        if (!inst)
            return Result::Running;
        if (inst->GetBossState(slot) == DONE)
            return Result::Done;
        if (inst->GetData(DATA_ENCOUNTER_STATUS) == 0)
            return VioletHoldStart(tank);
        WalkTo(tank, hookId, STAGE_X, STAGE_Y, STAGE_Z, 10.0f);
        return Result::Running;
    }

    Result VioletHoldDefend1(Player* tank) { return VioletHoldDefend(tank, 16, DcVioletHold::BOSS_STATE_1ST); }
    Result VioletHoldDefend2(Player* tank) { return VioletHoldDefend(tank, 17, DcVioletHold::BOSS_STATE_2ND); }
    Result VioletHoldDefend3(Player* tank) { return VioletHoldDefend(tank, 18, DcVioletHold::BOSS_STATE_CYANIGOSA); }

    // Repel the wave. The Portal Keepers stand at their portal and never come, and every portal they hold pumps adds for as long as one is
    // alive: killing the keeper is the only off-switch. A released prisoner never walks to the party either. Everything else comes by itself.
    Result VioletHoldWave(Player* tank)
    {
        using namespace DcVioletHold;
        if (tank->GetMapId() != MAP)
            return Result::Done;
        if (Creature* keeper = NearestOf(tank, VioletHoldKeeperEntries(), ARENA_SCAN, true))
        {
            DungeonRuntime::EngageCreature(tank, keeper, GoalId(19));
            return Result::Running;
        }
        if (Creature* prisoner = NearestOf(tank, VioletHoldPrisonerEntries(), ARENA_SCAN, false, &IsReleased))
        {
            DungeonRuntime::EngageCreature(tank, prisoner, GoalId(19));
            return Result::Running;
        }
        if (Creature* cyanigosa = tank->FindNearestCreature(NPC_CYANIGOSA, ARENA_SCAN, true); cyanigosa && IsReleased(cyanigosa))
        {
            DungeonRuntime::EngageCreature(tank, cyanigosa, GoalId(19));
            return Result::Running;
        }
        if (!tank->IsInCombat())
            if (Creature* add = NearestOf(tank, VioletHoldWaveEntries(), ARENA_SCAN, true))
            {
                DungeonRuntime::EngageCreature(tank, add, GoalId(19));
                return Result::Running;
            }
        return Result::Done;
    }

    // --- The Culling of Stratholme (map 595) ----------------------------------------------------------------------------------------------
    // The wave members are summons that do not reliably aggro: the nearest one is walked to and attacked. With none up the tank waits where it
    // is until the next wave is summoned (the event ends when the instance counter moves past the wave phases).
    Result CullingWaves(Player* tank)
    {
        using namespace DcCullingOfStratholme;
        if (tank->GetMapId() != MAP_ID)
            return Result::Done;
        InstanceScript* inst = InstanceOf(tank);
        if (!inst)
            return Result::Running;
        uint32 const progress = inst->GetData(DATA_ARTHAS_EVENT);
        if (progress != PROGRESS_FINISHED_CITY_INTRO && progress != PROGRESS_KILLED_MEATHOOK)
            return Result::Done;
        if (Creature* wave = NearestOf(tank, CosWaveEntries(), WAVE_SCAN, true))
        {
            if (!tank->IsInCombat() || tank->GetExactDist(wave) > 40.0f)
                DungeonRuntime::EngageCreature(tank, wave, GoalId(36));
            return Result::Running;
        }
        return Result::Running;
    }

    // --- Utgarde Pinnacle (map 575) -------------------------------------------------------------------------------------------------------
    // Svala and the Skadi gauntlet start from an area trigger that only a client would fire when its player walks into the box. The event walks
    // the tank into the box first; this sends the packet the client would (the server re-tests that the player is inside).
    Result ForgeAreaTrigger(Player* tank, uint32 trigger)
    {
        WorldPacket packet(CMSG_AREATRIGGER);
        packet << uint32(trigger);
        packet.rpos(0);
        tank->GetSession()->HandleAreaTriggerOpcode(packet);
        LOG_INFO("module.coa-playerbots.dungeon", "Area trigger {} sent for {}.", trigger, tank->GetName());
        return Result::Done;
    }

    Result SvalaAreaTrigger(Player* tank) { return ForgeAreaTrigger(tank, DcUtgardePinnacle::AREATRIGGER_SVALA); }
    Result SkadiAreaTrigger(Player* tank) { return ForgeAreaTrigger(tank, DcUtgardePinnacle::AREATRIGGER_SKADI); }

    // --- The Halls of Lightning (map 602) -------------------------------------------------------------------------------------------------
    // The slag pit is crossed under fire: the route hints carry the party over, nothing is to be driven here. Yielding hands the leg back to
    // the ordinary approach, which walks the hints without stopping for the Slags.
    Result LightningSlagTransit(Player* /*tank*/)
    {
        return Result::Done;
    }
}

namespace
{
    // --- The Halls of Stone (map 599) -----------------------------------------------------------------------------------------------------
    // The Tribunal of Ages is a fixed 300 s survival fight beside Brann at the console: the party holds the hold point and fights what the three
    // faces send at Brann. Nothing else is steered (the ordinary combat logic does the fighting); the step ends when the credit spell of the
    // encounter lands (mask bit 2). Without these hooks the event was "blocked" and latched off for the instance.
    Result HallsOfStoneTribunal(Player* tank)
    {
        using namespace DcHallsOfStone;
        if (InstanceScript* inst = InstanceOf(tank))
        {
            if (inst->GetBossState(BOSS_TRIBUNAL_OF_AGES) == DONE || (inst->GetCompletedEncounterMask() & (1u << BIT_TRIBUNAL_OF_AGES)))
                return Result::Done;
        }
        if (!tank->IsInCombat() && tank->GetExactDist(HOLD_X, HOLD_Y, HOLD_Z) > 6.0f)
            BotMovement::MoveTo(tank, MoveOwner::AutoDungeon, HOLD_X, HOLD_Y, HOLD_Z);
        return Result::Running;
    }

    // The wave event of the same fight: the adds go for Brann and the party; the combat logic takes them.
    Result HallsOfStoneWave(Player* /*tank*/)
    {
        return Result::Done;
    }
}

namespace DungeonHooks
{
    void RegisterWaveHooks(std::unordered_map<uint32, Result (*)(Player*)>& table)
    {
        table[15] = &VioletHoldStart;
        table[16] = &VioletHoldDefend1;
        table[17] = &VioletHoldDefend2;
        table[18] = &VioletHoldDefend3;
        table[19] = &VioletHoldWave;
        table[22] = &HallsOfStoneTribunal;
        table[23] = &HallsOfStoneWave;
        table[24] = &LightningSlagTransit;
        table[25] = &SvalaAreaTrigger;
        table[26] = &SkadiAreaTrigger;
        table[36] = &CullingWaves;
    }
}
