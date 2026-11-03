/*
 * mod-coa-playerbots -- dungeon automation
 *
 * Implementations of the compat layer declared in DcCompat.h.
 */

#include "dungeon/DcCompat.h"
#include "dungeon/DungeonBossInfo.h"
#include "dungeon/DungeonEventExecutor.h"
#include "dungeon/DungeonRoster.h"
#include "Creature.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "Timer.h"
#include <mutex>

template <>
AiObjectContext::Value<std::optional<DungeonBossInfo>>* AiObjectContext::GetValue<std::optional<DungeonBossInfo>>(int key)
{
    thread_local Value<std::optional<DungeonBossInfo>> v;
    v.value.reset();
    if (key == DcKey::NextDungeonBoss && _bot && _bot->GetMap() && _bot->GetGroup())
    {
        DungeonBossInfo target;
        if (DungeonRoster::Peek(_bot->GetMap(), _bot, _bot->GetGroup()->GetGUID(), target))
            v.value = target;
    }
    return &v;
}

template <>
AiObjectContext::Value<DungeonEventProgress&>* AiObjectContext::GetValue<DungeonEventProgress&>(int /*key*/)
{
    thread_local DungeonEventProgress progress;
    thread_local Value<DungeonEventProgress&> v;
    progress = DungeonEventProgress{};
    if (_bot && _bot->GetGroup())
        DungeonEventExecutor::Progress(_bot->GetGroup()->GetGUID(), progress);
    v.ptr = &progress;
    return &v;
}

bool DcRunState::Throttled(DcThrottle slot, uint32 windowMs)
{
    uint32& stamp = throttleMs[static_cast<int>(slot)];
    uint32 const now = getMSTime();
    if (stamp && now - stamp < windowMs)
        return true;
    stamp = now ? now : 1;
    return false;
}

namespace DcRun
{
    DcRunState& Of(AiObjectContext* context)
    {
        static std::mutex mutex;
        static std::unordered_map<ObjectGuid, DcRunState> states;
        std::lock_guard<std::mutex> lock(mutex);
        ObjectGuid const key = context && context->Bot() ? context->Bot()->GetGUID() : ObjectGuid::Empty;
        return states[key];
    }
}

namespace DcTargeting
{
    InstanceScript* GetInstanceScript(Player* bot)
    {
        Map* map = bot ? bot->GetMap() : nullptr;
        InstanceMap* instanceMap = map ? map->ToInstanceMap() : nullptr;
        return instanceMap ? instanceMap->GetInstanceScript() : nullptr;
    }

    Creature* FindLiveCreatureOnMap(Player* bot, uint32 entry)
    {
        Map* map = bot ? bot->GetMap() : nullptr;
        if (!map)
            return nullptr;
        for (auto const& [spawnId, creature] : map->GetCreatureBySpawnIdStore())
            if (creature && creature->GetEntry() == entry && creature->IsAlive() && creature->IsInWorld())
                return creature;
        return bot->FindNearestCreature(entry, 300.0f, true);
    }

    Creature* GetLiveBoss(Player* bot, AiObjectContext* /*context*/, uint32 entry)
    {
        return FindLiveCreatureOnMap(bot, entry);
    }
}

void ObjectiveHookRegistry::AddHook(HookTable& table, uint32 id, Hook hook)
{
    if (!id || table.count(id))
    {
        LOG_ERROR("module.coa-playerbots.dungeon", "Dungeon event hook id {} is reserved or registered twice.", id);
        return;
    }
    table.emplace(id, std::move(hook));
}
