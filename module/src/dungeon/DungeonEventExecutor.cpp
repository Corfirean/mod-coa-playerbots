/*
 * mod-coa-playerbots -- dungeon automation
 *
 * The step vocabulary follows jrad7/mod-dungeon-clear (AGPL-3.0-or-later); the implementation is written for this module's movement layer.
 */

#include "dungeon/DungeonEventExecutor.h"
#include "dungeon/DungeonHooks.h"
#include "BotMovement.h"
#include "Config.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Item.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Opcodes.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <map>
#include <mutex>
#include <set>
#include <tuple>

namespace
{
    constexpr uint32 DEFAULT_STEP_TIMEOUT_MS = 120000;
    constexpr float DEFAULT_ARRIVE_RADIUS = 3.0f;

    enum class StepResult : uint8 { Running, Done, Blocked, Restart };

    struct Run
    {
        uint32 mapId = 0;
        uint32 eventId = 0;
        size_t step = 0;
        uint32 stepStartedMs = 0;
        uint32 lastDriveMs = 0;
        uint32 stage = 0;          // per-step sub-state (gossip: 0 hello, 1 select)
        uint32 stageAtMs = 0;
        bool used = false;         // the step's one-shot action was taken
    };

    std::mutex g_mutex;
    std::map<ObjectGuid, Run> g_runs;
    std::set<std::tuple<uint32, uint32, uint32>> g_latched;

    uint32 StepTimeout(EventStep const& s)
    {
        static uint32 const configured = sConfigMgr->GetOption<uint32>("CoaBots.Dungeon.EventStepTimeoutMs", DEFAULT_STEP_TIMEOUT_MS);
        return s.timeoutMs ? s.timeoutMs : configured;
    }

    InstanceScript* InstanceOf(Player* bot)
    {
        Map* map = bot ? bot->GetMap() : nullptr;
        InstanceMap* instanceMap = map ? map->ToInstanceMap() : nullptr;
        return instanceMap ? instanceMap->GetInstanceScript() : nullptr;
    }

    bool HasAnchor(EventStep const& s)
    {
        return s.x != 0.0f || s.y != 0.0f || s.z != 0.0f;
    }

    uint64 GoalId(DungeonEvent const& ev, size_t step)
    {
        return 0xDC00000000ull | (uint64(ev.mapId & 0xFFFF) << 24) | (uint64(ev.id & 0xFFFF) << 8) | uint64(step & 0xFF);
    }

    // Walks the tank to a point. Done when within `radius`; Blocked when the movement layer gives up.
    StepResult WalkTo(Player* tank, DungeonEvent const& ev, size_t step, float x, float y, float z, float radius)
    {
        if (tank->GetExactDist(x, y, z) <= radius)
        {
            BotMovement::ForceReleaseOwner(tank, MoveOwner::AutoDungeon);
            return StepResult::Done;
        }
        NavStatus const status = BotMovement::Navigate(tank, MoveOwner::AutoDungeon, GoalId(ev, step), x, y, z, std::max(1.0f, radius * 0.8f));
        if (status == NavStatus::Stuck)
            return StepResult::Blocked;
        if (status == NavStatus::Arrived)
            return StepResult::Done;
        return StepResult::Running;
    }

    // The tank walks to the step's anchor first when it has one and is not there yet.
    bool NeedsApproach(Player* tank, EventStep const& s, float reach)
    {
        return HasAnchor(s) && tank->GetExactDist(s.x, s.y, s.z) > reach;
    }

    bool InstanceGateOpen(Player* tank, EventStep const& s)
    {
        InstanceScript* inst = InstanceOf(tank);
        if (!inst)
            return true;
        if (s.instanceDataId >= 0 && inst->GetData(uint32(s.instanceDataId)) < s.instanceDataMin)
            return false;
        if (s.persistentDataId >= 0 && inst->GetPersistentData(uint32(s.persistentDataId)) < s.persistentDataMin)
            return false;
        if (s.bossStateId >= 0)
        {
            uint32 const state = inst->GetBossState(uint32(s.bossStateId));
            if (!(state < 32 && (s.bossStateClearMask & (1u << state))))
                return false;
        }
        return true;
    }

    bool SpawnGateOpen(Player* tank, EventStep const& s)
    {
        std::vector<uint32> const entries = EventStepGateEntries(s);
        if (entries.empty())
            return true;
        bool any = false;
        for (uint32 entry : entries)
            if (tank->FindNearestCreature(entry, 300.0f, true))
                any = true;
        return s.wantAlive ? any : !any;
    }

    bool HasMoveGate(EventStep const& s)
    {
        return s.creatureEntry || s.instanceDataId >= 0 || s.persistentDataId >= 0 || s.bossStateId >= 0;
    }

    Item* FindItem(Player* bot, uint32 itemId)
    {
        return bot->GetItemByEntry(itemId);
    }

    // Gives the bot the item when its bags lack it (quest items the bots never looted).
    Item* EnsureItem(Player* bot, uint32 itemId)
    {
        Item* item = FindItem(bot, itemId);
        if (!item)
        {
            bot->AddItem(itemId, 1);
            item = FindItem(bot, itemId);
        }
        return item;
    }

    void DropBlockingForm(Player* bot)
    {
        if (bot->GetShapeshiftForm() != FORM_NONE)
            bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
    }

    // Talks to a creature the way a client would: open the menu, then pick the option.
    void GossipHello(Player* bot, Creature* npc)
    {
        WorldPacket packet(CMSG_GOSSIP_HELLO, 8);
        packet << npc->GetGUID();
        bot->GetSession()->HandleGossipHelloOpcode(packet);
    }

    void GossipSelect(Player* bot, Creature* npc, int32 option)
    {
        uint32 const menuId = bot->PlayerTalkClass->GetGossipMenu().GetMenuId();
        WorldPacket packet(CMSG_GOSSIP_SELECT_OPTION, 8 + 4 + 4 + 1);
        packet << npc->GetGUID() << menuId << uint32(option);
        packet << std::string();
        bot->GetSession()->HandleGossipSelectOptionOpcode(packet);
    }

    void TeleportParty(Player* tank, float x, float y, float z)
    {
        tank->NearTeleportTo(x, y, z, tank->GetOrientation());
        if (Group* group = tank->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource(); member && member != tank && member->IsInWorld() && member->GetMapId() == tank->GetMapId())
                    member->NearTeleportTo(x, y, z, member->GetOrientation());
    }

    StepResult RunStep(Player* tank, Group* group, DungeonEvent const& ev, size_t index, EventStep const& s, Run& run)
    {
        float const radius = s.radius > 0.0f ? s.radius : DEFAULT_ARRIVE_RADIUS;
        switch (s.kind)
        {
            case EventStepKind::MoveTo:
            {
                if (HasMoveGate(s))
                {
                    // Garrison: walk to the point and stay until the gate opens; a displaced tank walks back.
                    bool const open = InstanceGateOpen(tank, s) && SpawnGateOpen(tank, s);
                    if (open && !s.creatureEntry)
                        return StepResult::Done;
                    if (s.creatureEntry && open)
                        return StepResult::Done;
                    StepResult const walked = WalkTo(tank, ev, index, s.x, s.y, s.z, radius);
                    return walked == StepResult::Blocked ? StepResult::Blocked : StepResult::Running;
                }
                return WalkTo(tank, ev, index, s.x, s.y, s.z, radius);
            }

            case EventStepKind::Wait:
                return getMSTime() - run.stepStartedMs >= s.durationMs ? StepResult::Done : StepResult::Running;

            case EventStepKind::UseGameObject:
            {
                if (s.skipIfDataId >= 0)
                    if (InstanceScript* inst = InstanceOf(tank); inst && inst->GetData(uint32(s.skipIfDataId)) == s.skipIfDataValue)
                        return StepResult::Done;
                if (NeedsApproach(tank, s, std::max(radius, 12.0f)))
                {
                    StepResult const walked = WalkTo(tank, ev, index, s.x, s.y, s.z, std::max(radius, 6.0f));
                    return walked == StepResult::Blocked ? StepResult::Blocked : StepResult::Running;
                }
                GameObject* go = tank->FindNearestGameObject(s.goEntry, 80.0f);
                if (!go)
                    return StepResult::Running;
                if (tank->GetExactDist(go) > 5.0f)
                {
                    StepResult const walked = WalkTo(tank, ev, index, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), 3.5f);
                    return walked == StepResult::Blocked ? StepResult::Blocked : StepResult::Running;
                }
                BotMovement::ForceReleaseOwner(tank, MoveOwner::AutoDungeon);
                if (s.itemId && !tank->GetItemByEntry(s.itemId))
                    tank->AddItem(s.itemId, 1);
                if (s.reportUse && go->AI())
                    go->AI()->GossipHello(tank, true);
                else
                    go->Use(tank);
                return StepResult::Done;
            }

            case EventStepKind::WaitForGameObjectState:
            {
                GameObject* go = tank->FindNearestGameObject(s.goEntry, 200.0f);
                return go && uint32(go->GetGoState()) == s.wantState ? StepResult::Done : StepResult::Running;
            }

            case EventStepKind::WaitForSpawn:
                return SpawnGateOpen(tank, s) ? StepResult::Done : StepResult::Running;

            case EventStepKind::KillCreature:
            {
                if (!s.creatureEntry)
                    return StepResult::Done;  // "room trash": the ordinary pull logic clears it
                Creature* target = tank->FindNearestCreature(s.creatureEntry, 300.0f, true);
                if (!target)
                    return StepResult::Done;
                if (s.engage && !tank->IsInCombat())
                {
                    if (tank->GetExactDist(target) > 30.0f)
                    {
                        StepResult const walked = WalkTo(tank, ev, index, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 25.0f);
                        return walked == StepResult::Blocked ? StepResult::Blocked : StepResult::Running;
                    }
                    if (tank->IsValidAttackTarget(target))
                        tank->Attack(target, true);
                }
                return StepResult::Running;
            }

            case EventStepKind::ClearRadius:
            {
                // Done when no hostile creature stands inside the volume. The tank walks to the centre; the pull logic fights what it meets.
                Creature* nearest = nullptr;
                float best = 1.0e9f;
                float const zBand = s.zBand > 0.0f ? s.zBand : 12.0f;
                std::list<Creature*> found;
                tank->GetCreatureListWithEntryInGrid(found, 0, radius + 40.0f);
                for (Creature* c : found)
                {
                    if (!c->IsAlive() || !tank->IsValidAttackTarget(c) || c->IsCritter() || c->IsTotem())
                        continue;
                    if (std::hypot(c->GetPositionX() - s.x, c->GetPositionY() - s.y) > radius || std::fabs(c->GetPositionZ() - s.z) > zBand)
                        continue;
                    if (!s.entryFilter.empty() && std::find(s.entryFilter.begin(), s.entryFilter.end(), c->GetEntry()) == s.entryFilter.end())
                        continue;
                    float const d = tank->GetExactDist(c);
                    if (d < best)
                    {
                        best = d;
                        nearest = c;
                    }
                }
                if (!nearest)
                    return StepResult::Done;
                if (!tank->IsInCombat() && best > 25.0f)
                {
                    StepResult const walked = WalkTo(tank, ev, index, nearest->GetPositionX(), nearest->GetPositionY(), nearest->GetPositionZ(), 22.0f);
                    return walked == StepResult::Blocked ? StepResult::Blocked : StepResult::Running;
                }
                if (!tank->IsInCombat())
                    tank->Attack(nearest, true);
                return StepResult::Running;
            }

            case EventStepKind::Gossip:
            {
                Creature* npc = tank->FindNearestCreature(s.creatureEntry, HasAnchor(s) ? 60.0f : 200.0f, true);
                if (!npc)
                    return s.skipIfMissing ? StepResult::Done : StepResult::Running;
                if (s.waitForStill && !npc->movespline->Finalized())
                    return StepResult::Running;
                if (tank->GetExactDist(npc) > 4.5f)
                {
                    StepResult const walked = WalkTo(tank, ev, index, npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(), 3.5f);
                    return walked == StepResult::Blocked ? StepResult::Blocked : StepResult::Running;
                }
                BotMovement::ForceReleaseOwner(tank, MoveOwner::AutoDungeon);
                uint32 const now = getMSTime();
                if (run.stage == 0)
                {
                    GossipHello(tank, npc);
                    run.stage = 1;
                    run.stageAtMs = now;
                    return s.gossipOption < 0 ? StepResult::Done : StepResult::Running;
                }
                if (now - run.stageAtMs < 600)
                    return StepResult::Running;
                GossipSelect(tank, npc, s.gossipOption);
                return StepResult::Done;
            }

            case EventStepKind::CastSpell:
                tank->CastSpell(tank, s.spellId, false);
                return StepResult::Done;

            case EventStepKind::UseItem:
            {
                Item* item = EnsureItem(tank, s.itemId);
                if (!item)
                    return StepResult::Running;
                DropBlockingForm(tank);
                SpellCastTargets targets;
                targets.SetUnitTarget(tank);
                tank->CastItemUseSpell(item, targets, 0, 0);
                return StepResult::Done;
            }

            case EventStepKind::UseItemOnGO:
            {
                GameObject* go = nullptr;
                if (HasAnchor(s))
                    go = tank->FindNearestGameObject(s.goEntry, 200.0f);
                if (!go)
                    return StepResult::Running;
                if (go->GetGoState() != GO_STATE_READY)
                    return StepResult::Done;
                if (tank->GetExactDist(go) > 6.0f)
                {
                    StepResult const walked = WalkTo(tank, ev, index, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ(), 4.0f);
                    return walked == StepResult::Blocked ? StepResult::Blocked : StepResult::Running;
                }
                if (tank->IsNonMeleeSpellCast(false))
                    return StepResult::Running;
                Item* item = EnsureItem(tank, s.itemId);
                if (!item)
                    return StepResult::Running;
                DropBlockingForm(tank);
                SpellCastTargets targets;
                targets.SetGOTarget(go);
                tank->CastItemUseSpell(item, targets, 0, 0);
                return StepResult::Running;
            }

            case EventStepKind::UseItemAt:
            {
                if (s.goEntry)
                    if (GameObject* receipt = tank->FindNearestGameObject(s.goEntry, 15.0f); receipt && receipt->GetExactDist(s.x, s.y, s.z) < 15.0f)
                        return StepResult::Done;
                if (tank->GetExactDist(s.x, s.y, s.z) > radius)
                {
                    StepResult const walked = WalkTo(tank, ev, index, s.x, s.y, s.z, radius);
                    return walked == StepResult::Blocked ? StepResult::Blocked : StepResult::Running;
                }
                if (tank->IsNonMeleeSpellCast(false))
                    return StepResult::Running;
                Item* item = EnsureItem(tank, s.itemId);
                if (!item)
                    return StepResult::Running;
                DropBlockingForm(tank);
                SpellCastTargets targets;
                targets.SetUnitTarget(tank);
                tank->CastItemUseSpell(item, targets, 0, 0);
                return StepResult::Running;
            }

            case EventStepKind::Jump:
            {
                if (tank->GetExactDist(s.x, s.y, s.z) <= radius)
                    return StepResult::Done;
                // While the arc is in flight the bot reads as moving: never re-issue it (the jump would restart and never land).
                if (tank->isMoving())
                    return StepResult::Running;
                uint32 const now = getMSTime();
                if (run.used && now - run.stageAtMs < 1500)
                    return StepResult::Running;  // settling after the landing
                if (run.used && ++run.stage >= 4)
                {
                    LOG_WARN("module.coa-playerbots.dungeon", "Event '{}': the jump to ({:.1f}, {:.1f}, {:.1f}) ended {:.1f} yd short, 4 times.", ev.name, s.x, s.y,
                        s.z, tank->GetExactDist(s.x, s.y, s.z));
                    return StepResult::Blocked;
                }
                BotMovement::ForceReleaseOwner(tank, MoveOwner::AutoDungeon);
                float const speed = tank->GetSpeed(MOVE_RUN);
                tank->GetMotionMaster()->Clear();
                tank->GetMotionMaster()->MoveJump(s.x, s.y, s.z, speed, s.jumpSpeedZ > 0.0f ? s.jumpSpeedZ : speed, 1);
                run.used = true;
                run.stageAtMs = now;
                return StepResult::Running;
            }

            case EventStepKind::TeleportParty:
            case EventStepKind::DropInHole:
            {
                // The checkpoint is reached on foot; then everybody is relocated to the landing (a one-way drop the bots cannot take).
                if (s.kind == EventStepKind::TeleportParty && tank->GetExactDist(s.x, s.y, s.z) > radius + 3.0f)
                {
                    StepResult const walked = WalkTo(tank, ev, index, s.x, s.y, s.z, radius);
                    return walked == StepResult::Blocked ? StepResult::Blocked : StepResult::Running;
                }
                if (tank->GetExactDist(s.landX, s.landY, s.landZ) <= radius + 3.0f)
                    return StepResult::Done;
                TeleportParty(tank, s.landX, s.landY, s.landZ);
                return StepResult::Done;
            }

            case EventStepKind::RawPath:
            {
                // Without a raw spline driver the leg is flown point by point in a straight line.
                if (s.path.empty())
                    return StepResult::Done;
                G3D::Vector3 const& last = s.path.back();
                if (tank->GetExactDist(last.x, last.y, last.z) <= radius + 1.0f)
                    return StepResult::Done;
                if (!run.used || getMSTime() - run.stageAtMs > 3000)
                {
                    tank->GetMotionMaster()->MovePoint(0, last.x, last.y, last.z, FORCED_MOVEMENT_NONE, 0.0f, 0.0f, false, false);
                    run.used = true;
                    run.stageAtMs = getMSTime();
                }
                return StepResult::Running;
            }

            case EventStepKind::EscortCreature:
            {
                // The escort is over when the dungeon's own progress says so (the counter reached the value, or the encounter bit is set).
                if (InstanceScript* inst = InstanceOf(tank))
                {
                    if (s.instanceDataId >= 0 && inst->GetData(uint32(s.instanceDataId)) >= s.instanceDataMin)
                        return StepResult::Done;
                    if (s.escortDoneBit >= 0 && (inst->GetCompletedEncounterMask() & (1u << uint32(s.escortDoneBit))))
                        return StepResult::Done;
                }
                Creature* escortee = tank->FindNearestCreature(s.creatureEntry, s.radius > 0.0f ? s.radius : 120.0f, true);
                if (s.escortDoneEntry && tank->FindNearestCreature(s.escortDoneEntry, 300.0f, true))
                    return StepResult::Done;
                if (!escortee)
                    return run.used ? StepResult::Running : StepResult::Running;
                if (!run.used && s.gossipOption >= 0)
                {
                    if (tank->GetExactDist(escortee) > 4.5f)
                    {
                        StepResult const walked = WalkTo(tank, ev, index, escortee->GetPositionX(), escortee->GetPositionY(), escortee->GetPositionZ(), 3.5f);
                        return walked == StepResult::Blocked ? StepResult::Blocked : StepResult::Running;
                    }
                    GossipHello(tank, escortee);
                    GossipSelect(tank, escortee, s.gossipOption);
                    run.used = true;
                    return StepResult::Running;
                }
                // Follow the escortee; the pull logic fights what attacks it.
                if (!tank->IsInCombat() && tank->GetExactDist(escortee) > (s.escortStandoff > 0.0f ? s.escortStandoff : 8.0f))
                    WalkTo(tank, ev, index, escortee->GetPositionX(), escortee->GetPositionY(), escortee->GetPositionZ(), 6.0f);
                return StepResult::Running;
            }

            case EventStepKind::EnsureWaterBreathing:
                return StepResult::Done;

            case EventStepKind::Custom:
            {
                ObjectiveArriveResult const r = DungeonHooks::Run(s.hookId, tank, nullptr);
                switch (r)
                {
                    case ObjectiveArriveResult::Done: return StepResult::Done;
                    case ObjectiveArriveResult::Running: return StepResult::Running;
                    case ObjectiveArriveResult::Restart: return StepResult::Restart;
                    default: return StepResult::Blocked;
                }
            }
        }
        (void)group;
        return StepResult::Blocked;
    }
}

namespace DungeonEventExecutor
{
    void TalkTo(Player* bot, Creature* npc, int32 option)
    {
        GossipHello(bot, npc);
        if (option >= 0)
            GossipSelect(bot, npc, option);
    }

    uint32 OpenMenuId(Player* bot)
    {
        return bot && bot->PlayerTalkClass ? bot->PlayerTalkClass->GetGossipMenu().GetMenuId() : 0;
    }

    void SelectOption(Player* bot, Creature* npc, int32 option)
    {
        GossipSelect(bot, npc, option);
    }

    EventResult Drive(Player* tank, Group* group, DungeonEvent const& ev)
    {
        if (!tank || !group)
            return EventResult::Blocked;

        ObjectGuid const key = group->GetGUID();
        uint32 const now = getMSTime();

        std::lock_guard<std::mutex> lock(g_mutex);
        Run& run = g_runs[key];
        bool const fresh = run.mapId != ev.mapId || run.eventId != ev.id;
        bool const gap = !ev.persistent && run.lastDriveMs && now - run.lastDriveMs > 1500;
        if (fresh || gap)
        {
            run = Run{};
            run.mapId = ev.mapId;
            run.eventId = ev.id;
            run.stepStartedMs = now;
            LOG_INFO("module.coa-playerbots.dungeon", "Event '{}' ({}/{}) starts for '{}'.", ev.name, ev.mapId, ev.id, tank->GetName());
        }
        run.lastDriveMs = now;

        if (ev.completeGoEntry)
            if (GameObject* go = tank->FindNearestGameObject(ev.completeGoEntry, ev.completeGoRadius > 0.0f ? ev.completeGoRadius : 150.0f);
                go && uint32(go->GetGoState()) == ev.completeGoState)
                return EventResult::Done;

        for (int hops = 0; hops < 4; ++hops)
        {
            if (run.step >= ev.steps.size())
                return EventResult::Done;
            EventStep const& s = ev.steps[run.step];
            StepResult const r = RunStep(tank, group, ev, run.step, s, run);
            switch (r)
            {
                case StepResult::Running:
                    if (now - run.stepStartedMs > StepTimeout(s))
                    {
                        LOG_WARN("module.coa-playerbots.dungeon", "Event '{}' step {} (kind {}) timed out after {} s for '{}'.", ev.name, run.step,
                            uint32(s.kind), (now - run.stepStartedMs) / 1000, tank->GetName());
                        return ev.required ? EventResult::Blocked : EventResult::Done;
                    }
                    return EventResult::Running;
                case StepResult::Done:
                    ++run.step;
                    run.stage = 0;
                    run.used = false;
                    run.stepStartedMs = now;
                    continue;
                case StepResult::Restart:
                    run.step = 0;
                    run.stage = 0;
                    run.used = false;
                    run.stepStartedMs = now;
                    return EventResult::Running;
                case StepResult::Blocked:
                    LOG_WARN("module.coa-playerbots.dungeon", "Event '{}' step {} (kind {}) is blocked for '{}'.", ev.name, run.step, uint32(s.kind),
                        tank->GetName());
                    return ev.required ? EventResult::Blocked : EventResult::Done;
            }
        }
        return EventResult::Running;
    }

    bool Active(ObjectGuid groupKey, uint32 mapId, uint32 eventId)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_runs.find(groupKey);
        return it != g_runs.end() && it->second.mapId == mapId && it->second.eventId == eventId && getMSTime() - it->second.lastDriveMs < 15000;
    }

    bool Progress(ObjectGuid groupKey, DungeonEventProgress& out)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_runs.find(groupKey);
        if (it == g_runs.end())
            return false;
        out.eventId = it->second.eventId;
        out.stepIndex = uint32(it->second.step);
        out.stepStartMs = it->second.stepStartedMs;
        out.lastDriveMs = it->second.lastDriveMs;
        return true;
    }

    int32 CurrentStep(ObjectGuid groupKey)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_runs.find(groupKey);
        return it == g_runs.end() ? -1 : int32(it->second.step);
    }

    void Forget(ObjectGuid groupKey)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_runs.erase(groupKey);
    }

    bool IsLatched(uint32 instanceId, uint32 mapId, uint32 eventId)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_latched.count({ instanceId, mapId, eventId }) != 0;
    }

    void Latch(uint32 instanceId, uint32 mapId, uint32 eventId)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_latched.insert({ instanceId, mapId, eventId });
    }
}
