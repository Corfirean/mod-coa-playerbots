/*
 * mod-coa-playerbots -- dungeon automation
 */

#include "dungeon/DungeonRuntime.h"
#include "dungeon/DcDifficulty.h"
#include "dungeon/DungeonClearRouteRegistry.h"
#include "dungeon/DungeonEventExecutor.h"
#include "dungeon/DungeonEventRegistry.h"
#include "dungeon/DungeonHooks.h"
#include "dungeon/DungeonRoster.h"
#include "dungeon/RoomAggroRegistry.h"
#include "dungeon/SealedEncounterRegistry.h"
#include "BotMovement.h"
#include "Config.h"
#include "Creature.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "StringFormat.h"
#include "Timer.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <mutex>

namespace
{
    constexpr uint32 UNREACHABLE_MS = 6 * 60 * 1000;
    constexpr uint32 STALL_MS = 180 * 1000;
    constexpr float DEFAULT_OBJECTIVE_RADIUS = 8.0f;

    // Per group: how close the tank has got to the current anchor and for how long it has not got closer.
    struct Progress
    {
        uint32 entry = 0;
        float best = 1.0e9f;
        uint32 lastTickMs = 0;
        uint32 stalledMs = 0;
    };
    std::mutex g_mutex;
    std::map<ObjectGuid, Progress> g_progress;

    // Since when the tank waits for the group to gather before a sealed room (per group).
    std::map<ObjectGuid, uint32> g_musterSince;

    // Per group and boss: how many of the boss's authored route hints the tank has passed.
    std::map<std::pair<ObjectGuid, uint32>, size_t> g_hintCursor;

    // Walks the authored hops (intermediate points, jumps over navmesh gaps) to a boss. Returns true while a hop is being worked on, false when
    // the route is exhausted or there is none and the ordinary approach should take over.
    bool FollowRouteHints(Player* tank, ObjectGuid key, uint32 bossEntry)
    {
        Map* map = tank->GetMap();
        std::vector<WaypointHint> const* hints = DungeonClearRouteRegistry::Get(map->GetId(), map->GetDifficulty(), bossEntry);
        if (!hints || hints->empty())
            return false;
        std::lock_guard<std::mutex> lock(g_mutex);
        size_t& cursor = g_hintCursor[{ key, bossEntry }];
        while (cursor < hints->size() && tank->GetExactDist((*hints)[cursor].x, (*hints)[cursor].y, (*hints)[cursor].z) <= (*hints)[cursor].arriveRadius)
            ++cursor;
        if (cursor >= hints->size())
            return false;
        WaypointHint const& hop = (*hints)[cursor];
        bool const jump = HasFlag(hop.flags, AnchorFlag::JUMP_DOWN) || HasFlag(hop.flags, AnchorFlag::JUMP_GAP);
        if (jump && cursor > 0 && tank->GetExactDist((*hints)[cursor - 1].x, (*hints)[cursor - 1].y, (*hints)[cursor - 1].z) < 12.0f)
        {
            // At the lip of the gap: take the jump to this hop.
            if (!tank->isMoving() || tank->GetMotionMaster()->GetCurrentMovementGeneratorType() != EFFECT_MOTION_TYPE)
            {
                float const speed = tank->GetSpeed(MOVE_RUN);
                tank->GetMotionMaster()->MoveJump(hop.x, hop.y, hop.z, speed, speed);
            }
            return true;
        }
        BotMovement::Navigate(tank, MoveOwner::AutoDungeon, uint64(bossEntry) << 12 | 0x500 | (cursor & 0xFF), hop.x, hop.y, hop.z, std::max(2.0f, hop.arriveRadius * 0.7f));
        return true;
    }

    // Whether the tank is not getting any closer to the anchor; true once it has been so for a long time.
    bool Stalled(ObjectGuid key, uint32 entry, float dist)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        Progress& p = g_progress[key];
        uint32 const now = getMSTime();
        if (p.entry != entry)
        {
            p = Progress{ entry, dist, now, 0 };
            return false;
        }
        p.stalledMs += std::min<uint32>(now - p.lastTickMs, 1000);
        p.lastTickMs = now;
        if (dist < p.best - 8.0f)
        {
            p.best = dist;
            p.stalledMs = 0;
        }
        return p.stalledMs > STALL_MS;
    }

    void ResetProgress(ObjectGuid key)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_progress.erase(key);
    }

    bool HostileInRoom(Player* tank, Creature* c)
    {
        return c && c->IsAlive() && c->IsInWorld() && tank->IsValidAttackTarget(c) && !c->IsCritter() && !c->IsTotem() && !c->IsPet() &&
            !c->HasUnitFlag(UNIT_FLAG_NON_ATTACKABLE) && !c->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE);
    }

    // The hostile creatures of the room around a room-aggro boss (the boss itself and creatures outside the script's whitelist excluded).
    Creature* NearestRoomMember(Player* tank, RoomAggroBoss const& row, float bx, float by)
    {
        std::list<Creature*> found;
        tank->GetCreatureListWithEntryInGrid(found, 0, row.radius + 80.0f);
        Creature* best = nullptr;
        float bestDist = 1.0e9f;
        for (Creature* c : found)
        {
            if (!HostileInRoom(tank, c) || c->GetEntry() == row.bossEntry)
                continue;
            if (std::hypot(c->GetPositionX() - bx, c->GetPositionY() - by) > row.radius)
                continue;
            if (!row.memberEntries.empty() && std::find(row.memberEntries.begin(), row.memberEntries.end(), c->GetEntry()) == row.memberEntries.end())
                continue;
            if (row.hasYBand && (c->GetPositionY() <= row.minY || c->GetPositionY() >= row.maxY))
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

    // Walks to a creature and attacks it; the ordinary pull logic takes over once it is engaged.
    bool EngageImpl(Player* tank, Creature* target, uint64 goalId)
    {
        if (tank->GetExactDist(target) > 28.0f)
        {
            NavStatus const status = BotMovement::Navigate(tank, MoveOwner::AutoDungeon, goalId, target->GetPositionX(), target->GetPositionY(),
                target->GetPositionZ(), 22.0f);
            return status != NavStatus::Stuck;
        }
        BotMovement::ForceReleaseOwner(tank, MoveOwner::AutoDungeon);
        tank->Attack(target, true);
        return true;
    }

    // A group that fell off a ledge (Hellfire Ramparts' zone-in room has a drop along its south-west side, the Halls of Lightning a pit under the
    // first walkway) stands on the floor far below the dungeon with no path back. The players of the group would run back or be summoned; the
    // group is put back at the dungeon's entrance, at most once a minute.
    bool RescueFromBelow(Player* tank, Group* group, float targetZ)
    {
        AreaTriggerTeleport const* entrance = sObjectMgr->GetMapEntranceTrigger(tank->GetMapId());
        if (!entrance)
            return false;
        float const floorZ = std::min(targetZ, entrance->target_Z);
        if (tank->GetPositionZ() > floorZ - 25.0f)
            return false;
        static std::map<ObjectGuid, uint32> last;
        uint32 const now = getMSTime();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            uint32& at = last[group->GetGUID()];
            if (at && now - at < 60000)
                return false;
            at = now;
        }
        LOG_WARN("module.coa-playerbots.dungeon", "'{}' and the group are {:.0f} yd below the floor of the dungeon (z {:.0f}); putting them back at the entrance.", tank->GetName(),
            floorZ - tank->GetPositionZ(), tank->GetPositionZ());
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member && member->IsInWorld() && member->IsAlive() && member->GetMapId() == tank->GetMapId())
                member->NearTeleportTo(entrance->target_X, entrance->target_Y, entrance->target_Z, entrance->target_Orientation);
        BotMovement::ForceReleaseOwner(tank, MoveOwner::AutoDungeon);
        ResetProgress(group->GetGUID());
        return true;
    }

    // inCombat: only the events that are meant to be driven during a fight (continuous wave encounters).
    bool RunConditionalEvents(Player* tank, Group* group, bool inCombat = false)
    {
        Map* map = tank->GetMap();
        ObjectGuid const key = group->GetGUID();
        for (DungeonEvent const* ev : DungeonEventRegistry::Conditional(map->GetId(), DcDifficulty::Of(map)))
        {
            if (inCombat && !ev->drivesInCombat)
                continue;
            if (!ev->repeatable && DungeonEventExecutor::IsLatched(map->GetInstanceId(), ev->mapId, ev->id))
                continue;
            bool const active = DungeonEventExecutor::Active(key, ev->mapId, ev->id);
            AiObjectContext context(tank);
            if (!active && !(ev->condition && ev->condition(tank, &context)))
                continue;
            switch (DungeonEventExecutor::Drive(tank, group, *ev))
            {
                case EventResult::Running:
                    return true;
                case EventResult::Done:
                    if (!ev->repeatable)
                        DungeonEventExecutor::Latch(map->GetInstanceId(), ev->mapId, ev->id);
                    DungeonEventExecutor::Forget(key);
                    return true;
                case EventResult::Blocked:
                    LOG_WARN("module.coa-playerbots.dungeon", "Conditional event '{}' is blocked; it will not be offered again in this instance.", ev->name);
                    DungeonEventExecutor::Latch(map->GetInstanceId(), ev->mapId, ev->id);
                    DungeonEventExecutor::Forget(key);
                    return true;
            }
        }
        return false;
    }
}

namespace DungeonRuntime
{
    bool Enabled()
    {
        static bool const enabled = sConfigMgr->GetOption<bool>("CoaBots.Dungeon.UseRoster", true);
        return enabled;
    }

    bool HasRoster(Map* map, Player* ref)
    {
        return !DungeonRoster::Full(map, ref).empty();
    }

    bool IsCleared(Map* map, Player* ref)
    {
        return DungeonRoster::Remaining(map, ref, ObjectGuid::Empty).empty();
    }

    bool RoomTrashRemaining(Player* tank)
    {
        Group* group = tank ? tank->GetGroup() : nullptr;
        if (!group || !tank->GetMap())
            return false;
        DungeonBossInfo target;
        if (!DungeonRoster::PickTarget(tank->GetMap(), tank, group->GetGUID(), target) || target.kind != DungeonAnchorKind::Boss)
            return false;
        RoomAggroBoss const* row = RoomAggroRegistry::Find(tank->GetMap()->GetId(), target.entry);
        if (!row)
            return false;
        float bx = target.x, by = target.y;
        if (Creature* boss = tank->FindNearestCreature(target.entry, 400.0f, true))
        {
            bx = boss->GetPositionX();
            by = boss->GetPositionY();
        }
        return NearestRoomMember(tank, *row, bx, by) != nullptr;
    }

    bool EngageCreature(Player* tank, Creature* target, uint64 goalId)
    {
        return EngageImpl(tank, target, goalId);
    }

    bool AdvanceInCombat(Player* tank, Group* group)
    {
        return tank && group && tank->GetMap() && RunConditionalEvents(tank, group, true);
    }

    void Forget(Group* group)
    {
        if (!group)
            return;
        ObjectGuid const key = group->GetGUID();
        DungeonRoster::Forget(key);
        DungeonEventExecutor::Forget(key);
        ResetProgress(key);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_musterSince.erase(key);
    }

    bool Advance(Player* tank, Group* group, std::function<void(char const*)> const& note)
    {
        Map* map = tank->GetMap();
        ObjectGuid const key = group->GetGUID();

        if (RunConditionalEvents(tank, group))
            return true;

        DungeonBossInfo target;
        if (!DungeonRoster::PickTarget(map, tank, key, target))
        {
            note("nothing left on the roster");
            return false;
        }

        float x = target.x, y = target.y, z = target.z;
        DungeonRoster::ResolvePosition(map, target, x, y, z);

        auto giveUp = [&](char const* why)
        {
            LOG_WARN("module.coa-playerbots.dungeon", "'{}' leaves '{}' ({}) for {} min: {}.", tank->GetName(), target.name, target.entry, UNREACHABLE_MS / 60000, why);
            BotMovement::ForceReleaseOwner(tank, MoveOwner::AutoDungeon);
            DungeonRoster::MarkUnreachable(key, target.entry, UNREACHABLE_MS);
            DungeonEventExecutor::Forget(key);
            ResetProgress(key);
        };

        if (target.kind == DungeonAnchorKind::Objective)
        {
            float const arrive = target.arriveRadius > 0.0f ? target.arriveRadius : DEFAULT_OBJECTIVE_RADIUS;
            DungeonEvent const* ev = target.eventId ? DungeonEventRegistry::Find(map->GetId(), target.eventId) : nullptr;
            bool const running = ev && DungeonEventExecutor::Active(key, map->GetId(), target.eventId) && ev->persistent;

            if (!running && tank->GetExactDist(x, y, z) > arrive)
            {
                note("heading for an objective");
                NavStatus const status = BotMovement::Navigate(tank, MoveOwner::AutoDungeon, uint64(target.entry) << 8 | 0xA1, x, y, z, std::max(2.0f, arrive * 0.6f));
                if (status == NavStatus::Stuck)
                    { if (!RescueFromBelow(tank, group, z)) giveUp("no way there"); }
                else if (Stalled(key, target.entry, tank->GetExactDist(x, y, z)))
                    { if (!RescueFromBelow(tank, group, z)) giveUp("no progress"); }
                return true;
            }

            note("performing the event of an objective");
            EventResult result = EventResult::Done;
            if (ev)
                result = DungeonEventExecutor::Drive(tank, group, *ev);
            else if (target.onArriveHook)
            {
                ObjectiveArriveResult const r = DungeonHooks::Run(target.onArriveHook, tank, &target);
                result = r == ObjectiveArriveResult::Done ? EventResult::Done : r == ObjectiveArriveResult::Blocked ? EventResult::Blocked : EventResult::Running;
            }
            switch (result)
            {
                case EventResult::Running:
                    return true;
                case EventResult::Done:
                    LOG_INFO("module.coa-playerbots.dungeon", "Objective '{}' ({}) is done.", target.name, target.entry);
                    DungeonRoster::MarkObjectiveDone(map, target.entry);
                    DungeonEventExecutor::Forget(key);
                    ResetProgress(key);
                    return true;
                case EventResult::Blocked:
                    giveUp("its event is blocked");
                    return true;
            }
            return true;
        }

        // A boss. A room-aggro boss pulls the whole room when it is engaged: the room is cleared first.
        if (RoomAggroBoss const* row = RoomAggroRegistry::Find(map->GetId(), target.entry))
        {
            float bx = x, by = y;
            if (Creature* boss = tank->FindNearestCreature(target.entry, 400.0f, true))
            {
                bx = boss->GetPositionX();
                by = boss->GetPositionY();
            }
            if (tank->GetExactDist2d(bx, by) < row->radius + 60.0f)
                if (Creature* member = NearestRoomMember(tank, *row, bx, by))
                {
                    note("clearing the room of a boss first");
                    if (!EngageImpl(tank, member, uint64(target.entry) << 8 | 0xA2))
                        giveUp("the room cannot be reached");
                    return true;
                }
        }

        // A boss whose room locks behind the party: everybody gathers first.
        if (SealedEncounterRow const* sealed = SealedEncounterRegistry::Find(map->GetId(), target.entry))
        {
            if (SealedEncounterRegistry::InApproachRange(*sealed, tank->GetPositionX(), tank->GetPositionY(), tank->GetPositionZ(), x, y, z))
            {
                bool allIn = true;
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    if (Player* member = ref->GetSource(); member && member != tank && member->IsAlive() && member->GetMapId() == map->GetId() &&
                        member->GetExactDist(tank) > (sealed->musterSpread > 0.0f ? sealed->musterSpread : 12.0f))
                        allIn = false;
                uint32 const now = getMSTime();
                std::lock_guard<std::mutex> lock(g_mutex);
                uint32& since = g_musterSince[key];
                if (!allIn && (!since || now - since < DC_SEALED_MUSTER_TIMEOUT_MS))
                {
                    if (!since)
                        since = now;
                    BotMovement::ForceReleaseOwner(tank, MoveOwner::AutoDungeon);
                    note("gathering the group before a sealed room");
                    return true;
                }
                since = 0;
            }
        }

        note("heading for the next boss");
        if (FollowRouteHints(tank, key, target.entry))
            return true;
        float const dist = tank->GetExactDist(x, y, z);
        NavStatus const status = BotMovement::Navigate(tank, MoveOwner::AutoDungeon, uint64(target.entry) << 8 | 0xA0, x, y, z, 8.0f);
        if (status == NavStatus::Stuck)
            { if (!RescueFromBelow(tank, group, z)) giveUp("no way there"); }
        else if (!tank->IsInCombat() && Stalled(key, target.entry, dist))
            { if (!RescueFromBelow(tank, group, z)) giveUp("no progress"); }
        return true;
    }
}
