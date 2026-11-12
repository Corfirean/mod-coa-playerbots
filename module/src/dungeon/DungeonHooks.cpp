/*
 * mod-coa-playerbots -- dungeon automation
 *
 * Ported from the objective hooks of jrad7/mod-dungeon-clear (AGPL-3.0-or-later); see DungeonHooks.h. The comments keep the reasoning of the
 * original where it explains a quirk of the dungeon's script, and are shortened elsewhere.
 */

#include "dungeon/DungeonHooks.h"
#include "dungeon/DungeonEventTables.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "Group.h"
#include "InstanceScript.h"
#include "Bag.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Opcodes.h"
#include "Player.h"
#include "Spell.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

// Appenders defined in the imported dungeon files.
#define DC_HOOK_APPENDER(name) void name(ObjectiveHookRegistry::HookTable& out);
#include "dungeon/HookRegistrations.inc"
#undef DC_HOOK_APPENDER

namespace
{
    InstanceScript* InstanceOf(Player* bot)
    {
        Map* map = bot ? bot->GetMap() : nullptr;
        InstanceMap* instanceMap = map ? map->ToInstanceMap() : nullptr;
        return instanceMap ? instanceMap->GetInstanceScript() : nullptr;
    }

    // A creature of this entry that is alive anywhere on the map (not only near the bot): a boss that walks a long lap.
    Creature* LiveBoss(Player* bot, uint32 entry)
    {
        Map* map = bot ? bot->GetMap() : nullptr;
        if (!map)
            return nullptr;
        for (auto const& [spawnId, creature] : map->GetCreatureBySpawnIdStore())
            if (creature && creature->GetEntry() == entry && creature->IsAlive() && creature->IsInWorld())
                return creature;
        return bot->FindNearestCreature(entry, 300.0f, true);
    }

    // Item-use spells fail in a shapeshift form that cannot use items.
    void DropBlockingForm(Player* bot)
    {
        if (bot->GetShapeshiftForm() != FORM_NONE)
            bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
    }

    // At most one log line per hook per ten seconds.
    bool ShouldLog(uint32 hookId)
    {
        static std::unordered_map<uint32, uint32> last;
        uint32 const now = getMSTime();
        uint32& at = last[hookId];
        if (at && now - at < 10000)
            return false;
        at = now;
        return true;
    }

    // --- Blackrock Depths, Ring of Law (hook 1): start the arena by forging the area trigger the client would send -----------------------------
    constexpr uint32 BRD_TYPE_RING_OF_LAW = 1;
    constexpr uint32 BRD_RING_IN_PROGRESS = 1;
    constexpr uint32 BRD_RING_OF_LAW_TRIGGER = 1526;

    ObjectiveArriveResult EnsureRingStarted(Player* bot)
    {
        InstanceScript* inst = InstanceOf(bot);
        if (!inst)
            return ObjectiveArriveResult::Running;
        if (inst->GetData(BRD_TYPE_RING_OF_LAW) >= BRD_RING_IN_PROGRESS)
            return ObjectiveArriveResult::Done;
        WorldPacket p(CMSG_AREATRIGGER);
        p << uint32(BRD_RING_OF_LAW_TRIGGER);
        p.rpos(0);
        bot->GetSession()->HandleAreaTriggerOpcode(p);
        if (inst->GetData(BRD_TYPE_RING_OF_LAW) >= BRD_RING_IN_PROGRESS && ShouldLog(1))
            LOG_INFO("module.coa-playerbots.dungeon", "Ring of Law started by forged area trigger {} for {}", BRD_RING_OF_LAW_TRIGGER, bot->GetName());
        return ObjectiveArriveResult::Running;
    }

    // --- Deadmines (hook 2): the Iron Clad Door is opened by firing the cannon with the gunpowder as the lock key ----------------------------
    constexpr uint32 DM_GO_CANNON = 16398;
    constexpr uint32 DM_GO_IRON_CLAD_DOOR = 16397;
    constexpr uint32 DM_ITEM_GUNPOWDER = 5397;

    ObjectiveArriveResult FireDefiasCannon(Player* bot)
    {
        if (GameObject* door = bot->FindNearestGameObject(DM_GO_IRON_CLAD_DOOR, 100.0f))
            if (door->GetGoState() != GO_STATE_READY)
                return ObjectiveArriveResult::Done;
        if (bot->IsNonMeleeSpellCast(false))
            return ObjectiveArriveResult::Running;
        GameObject* cannon = bot->FindNearestGameObject(DM_GO_CANNON, 40.0f);
        if (!cannon)
            return ObjectiveArriveResult::Running;
        Item* gunpowder = bot->GetItemByEntry(DM_ITEM_GUNPOWDER);
        if (!gunpowder)
        {
            DungeonHooks::GiveItem(bot, DM_ITEM_GUNPOWDER);
            gunpowder = bot->GetItemByEntry(DM_ITEM_GUNPOWDER);
            if (!gunpowder)
                return ObjectiveArriveResult::Running;
        }
        DropBlockingForm(bot);
        SpellCastTargets targets;
        targets.SetGOTarget(cannon);
        bot->CastItemUseSpell(gunpowder, targets, 0, 0);
        return ObjectiveArriveResult::Running;
    }

    // --- Old Hillsbrad (hook 3): the incendiary bombs ------------------------------------------------------------------------------------
    constexpr uint32 OH_ITEM_INCENDIARY_BOMBS = 25853;

    ObjectiveArriveResult GrantIncendiaryBombs(Player* bot)
    {
        if (bot->GetItemByEntry(OH_ITEM_INCENDIARY_BOMBS))
            return ObjectiveArriveResult::Done;
        DungeonHooks::GiveItem(bot, OH_ITEM_INCENDIARY_BOMBS);
        return bot->GetItemByEntry(OH_ITEM_INCENDIARY_BOMBS) ? ObjectiveArriveResult::Done : ObjectiveArriveResult::Running;
    }

    // --- The Mechanar (hook 4): the Cache of the Legion opens with a key the bots never looted ----------------------------------------------
    constexpr uint32 MECH_GO_CACHE_NORMAL = 184465;
    constexpr uint32 MECH_GO_CACHE_HEROIC = 184849;
    constexpr uint32 MECH_ITEM_CACHE_KEY = 30438;

    ObjectiveArriveResult GrantCacheKeyAndLoot(Player* bot)
    {
        GameObject* cache = bot->FindNearestGameObject(MECH_GO_CACHE_NORMAL, 25.0f);
        if (!cache)
            cache = bot->FindNearestGameObject(MECH_GO_CACHE_HEROIC, 25.0f);
        if (!cache)
            return bot->HasItemCount(MECH_ITEM_CACHE_KEY, 1) ? ObjectiveArriveResult::Done : ObjectiveArriveResult::Running;
        if (cache->getLootState() != GO_READY)
            return ObjectiveArriveResult::Done;
        if (bot->IsNonMeleeSpellCast(false))
            return ObjectiveArriveResult::Running;
        Item* key = bot->GetItemByEntry(MECH_ITEM_CACHE_KEY);
        if (!key)
        {
            DungeonHooks::GiveItem(bot, MECH_ITEM_CACHE_KEY);
            key = bot->GetItemByEntry(MECH_ITEM_CACHE_KEY);
            if (!key)
                return ObjectiveArriveResult::Running;
        }
        if (bot->GetExactDist(cache) > 4.0f)
        {
            if (!bot->isMoving())
                bot->GetMotionMaster()->MovePoint(0, cache->GetPositionX(), cache->GetPositionY(), cache->GetPositionZ());
            return ObjectiveArriveResult::Running;
        }
        DropBlockingForm(bot);
        SpellCastTargets targets;
        targets.SetGOTarget(cache);
        bot->CastItemUseSpell(key, targets, 0, 0);
        return ObjectiveArriveResult::Running;
    }

    // --- Zul'Farrak (hook 5): Zum'rah is friendly until a client area trigger turns him hostile -------------------------------------------
    constexpr uint32 ZF_ZUMRAH = 7271;
    constexpr uint32 ZF_ZUMRAH_FACTION_FRIENDLY = 35;
    constexpr uint32 ZF_ZUMRAH_FACTION_HOSTILE = 37;

    ObjectiveArriveResult WakeZumrah(Player* bot)
    {
        Creature* zumrah = bot->FindNearestCreature(ZF_ZUMRAH, 60.0f);
        if (!zumrah || !zumrah->IsAlive() || zumrah->GetFaction() != ZF_ZUMRAH_FACTION_FRIENDLY)
            return ObjectiveArriveResult::Done;
        zumrah->SetFaction(ZF_ZUMRAH_FACTION_HOSTILE);
        LOG_INFO("module.coa-playerbots.dungeon", "Zul'Farrak: Zum'rah set hostile for {} (area trigger 962 never fires for bots)", bot->GetName());
        return ObjectiveArriveResult::Done;
    }

    // --- The Arcatraz (hook 6): Warden Mellichar starts the pods when he is damaged ---------------------------------------------------------
    constexpr uint32 ARC_MELLICHAR = 20904;
    constexpr uint32 ARC_SKYRISS = 20912;
    constexpr uint32 ARC_DATA_WARDEN_MELLICHAR = 3;
    constexpr float ARC_ARENA_X = 445.9f;
    constexpr float ARC_ARENA_Y = -161.5f;
    constexpr float ARC_ARENA_Z = 42.56f;

    ObjectiveArriveResult DriveMellicharWaves(Player* bot)
    {
        InstanceScript* inst = InstanceOf(bot);
        if (!inst)
            return ObjectiveArriveResult::Running;
        if (bot->FindNearestCreature(ARC_SKYRISS, 100.0f, true))
            return ObjectiveArriveResult::Done;
        if (inst->GetBossState(ARC_DATA_WARDEN_MELLICHAR) == DONE)
            return ObjectiveArriveResult::Done;
        if (inst->GetBossState(ARC_DATA_WARDEN_MELLICHAR) == NOT_STARTED)
        {
            Creature* mellichar = bot->FindNearestCreature(ARC_MELLICHAR, 60.0f);
            if (mellichar && mellichar->IsAlive())
            {
                // Any player-sourced damage starts the intro; his AI zeroes it. Never set the boss state directly: that locks the encounter out.
                Unit::DealDamage(bot, mellichar, 1);
                if (ShouldLog(6))
                    LOG_INFO("module.coa-playerbots.dungeon", "Arcatraz: poking Warden Mellichar to open the stasis pods for {}", bot->GetName());
            }
        }
        if (!bot->isMoving() && bot->GetExactDist(ARC_ARENA_X, ARC_ARENA_Y, ARC_ARENA_Z) > 15.0f)
            bot->GetMotionMaster()->MovePoint(0, ARC_ARENA_X, ARC_ARENA_Y, ARC_ARENA_Z, FORCED_MOVEMENT_NONE, 0.0f, 0.0f, true, false);
        return ObjectiveArriveResult::Running;
    }

    // --- Sethekk Halls (hook 7): Anzu is summoned by an event the druid's item would send -----------------------------------------------------
    constexpr uint32 SH_ANZU_SEND_EVENT = 14797;
    constexpr uint32 SH_ANZU_NPC = 23035;
    constexpr uint32 SH_VOICE_NPC = 21851;
    constexpr uint32 SH_DATA_ANZU = 1;

    ObjectiveArriveResult DriveAnzuSummon(Player* bot)
    {
        InstanceScript* inst = InstanceOf(bot);
        if (!inst)
            return ObjectiveArriveResult::Running;
        if (inst->GetBossState(SH_DATA_ANZU) == DONE)
            return ObjectiveArriveResult::Done;
        if (bot->FindNearestCreature(SH_ANZU_NPC, 60.0f, true))
            return ObjectiveArriveResult::Done;
        if (bot->FindNearestCreature(SH_VOICE_NPC, 60.0f, true))
            return ObjectiveArriveResult::Running;
        inst->ProcessEvent(bot, SH_ANZU_SEND_EVENT);
        return ObjectiveArriveResult::Running;
    }

    // --- The Shattered Halls (hook 9): Nethekurse stays immune until his intro is started --------------------------------------------------
    constexpr uint32 SHH_NETHEKURSE = 16807;
    constexpr int32 SHH_ACTION_START_INTRO = 0;

    ObjectiveArriveResult StartNethekurseIntro(Player* bot)
    {
        Creature* nethekurse = bot->FindNearestCreature(SHH_NETHEKURSE, 60.0f);
        if (!nethekurse || !nethekurse->IsAlive() || !nethekurse->IsImmuneToPC() || !nethekurse->AI())
            return ObjectiveArriveResult::Done;
        nethekurse->AI()->DoAction(SHH_ACTION_START_INTRO);
        LOG_INFO("module.coa-playerbots.dungeon", "Shattered Halls: started Nethekurse's intro for {}", bot->GetName());
        return ObjectiveArriveResult::Done;
    }

    // --- The Underbog (hook 10): Ghaz'an swims in the lake until an area trigger sends him up the ramp ------------------------------------------
    constexpr uint32 UB_GHAZAN = 18105;
    constexpr int32 UB_ACTION_MOVE_TO_PLATFORM = 1;
    constexpr float UB_GHAZAN_WATER_Z = 60.0f;

    ObjectiveArriveResult SendGhazanToPlatform(Player* bot)
    {
        Creature* ghazan = LiveBoss(bot, UB_GHAZAN);
        if (!ghazan || !ghazan->IsAlive() || !ghazan->AI() || ghazan->GetPositionZ() >= UB_GHAZAN_WATER_Z)
            return ObjectiveArriveResult::Done;
        ghazan->AI()->DoAction(UB_ACTION_MOVE_TO_PLATFORM);
        LOG_INFO("module.coa-playerbots.dungeon", "Underbog: sent Ghaz'an up to his platform for {}", bot->GetName());
        return ObjectiveArriveResult::Done;
    }

    // --- Azjol-Nerub (hook 13): wait until Hadronox has webbed the doors --------------------------------------------------------------------
    constexpr uint32 AN_HADRONOX = 28921;

    ObjectiveArriveResult HadronoxHasWebbedTheDoors(Player* bot)
    {
        Creature* hadronox = LiveBoss(bot, AN_HADRONOX);
        if (!hadronox || !hadronox->IsAlive() || !hadronox->AI())
            return ObjectiveArriveResult::Done;
        return hadronox->AI()->GetData(AN_HADRONOX) == 0 ? ObjectiveArriveResult::Done : ObjectiveArriveResult::Running;
    }

    // --- Drak'Tharon Keep (hook 14): hold the Novos camp through phase 1 --------------------------------------------------------------------
    ObjectiveArriveResult HoldNovosCamp(Player* bot)
    {
        Creature* novos = bot->FindNearestCreature(DcDrakTharonKeep::NOVOS, DcDrakTharonKeep::NOVOS_SCAN, true);
        if (!novos || !novos->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE))
            return ObjectiveArriveResult::Done;
        float const toCamp = bot->GetExactDist2d(DcDrakTharonKeep::CAMP_X, DcDrakTharonKeep::CAMP_Y);
        bool const inField = bot->GetExactDist2d(novos) < DcDrakTharonKeep::FIELD_KEEPOUT;
        bool inMelee = false;
        if (Unit* victim = bot->GetVictim())
            inMelee = victim->IsAlive() && bot->IsWithinMeleeRange(victim);
        if (!inField && toCamp <= DcDrakTharonKeep::CAMP_HARD_LEASH && (toCamp <= DcDrakTharonKeep::CAMP_LEASH || inMelee))
            return ObjectiveArriveResult::Done;
        static std::unordered_map<ObjectGuid, uint32> lastIssue;
        uint32 const now = getMSTime();
        uint32& at = lastIssue[bot->GetGUID()];
        if (at && now - at < 1000)
            return ObjectiveArriveResult::Running;
        at = now;
        bot->GetMotionMaster()->MovePoint(0, DcDrakTharonKeep::CAMP_X, DcDrakTharonKeep::CAMP_Y, DcDrakTharonKeep::CAMP_Z, FORCED_MOVEMENT_NONE, 0.0f,
            0.0f, true, false);
        return ObjectiveArriveResult::Running;
    }

    // The hooks that live in the imported dungeon files (Register<Dungeon>Hooks appenders, declared above the anonymous namespace).
    ObjectiveHookRegistry::HookTable const& ImportedTable()
    {
        static ObjectiveHookRegistry::HookTable const table = []
        {
            ObjectiveHookRegistry::HookTable t;
#define DC_HOOK_APPENDER(name) name(t);
#include "dungeon/HookRegistrations.inc"
#undef DC_HOOK_APPENDER
            return t;
        }();
        return table;
    }

    using HookFn = ObjectiveArriveResult (*)(Player*);
    std::unordered_map<uint32, HookFn> const& Table()
    {
        static std::unordered_map<uint32, HookFn> const table = []
        {
            std::unordered_map<uint32, HookFn> t = {
            { 1, &EnsureRingStarted },
            { 2, &FireDefiasCannon },
            { 3, &GrantIncendiaryBombs },
            { 4, &GrantCacheKeyAndLoot },
            { 5, &WakeZumrah },
            { 6, &DriveMellicharWaves },
            { 7, &DriveAnzuSummon },
            { 9, &StartNethekurseIntro },
            { 10, &SendGhazanToPlatform },
            { 13, &HadronoxHasWebbedTheDoors },
            { 14, &HoldNovosCamp },
            };
            DungeonHooks::RegisterWaveHooks(t);
            return t;
        }();
        return table;
    }
}

namespace DungeonHooks
{
    bool GiveItem(Player* bot, uint32 itemId)
    {
        if (!bot)
            return false;
        if (bot->HasItemCount(itemId, 1))
            return true;
        if (bot->AddItem(itemId, 1))
            return true;

        // Bags full: free one slot. Lowest quality first, then the lowest sell price; consumables, quest items and bags only as a last resort.
        Item* worst = nullptr;
        uint64 worstScore = ~uint64(0);
        auto consider = [&](Item* item)
        {
            if (!item)
                return;
            ItemTemplate const* tmpl = item->GetTemplate();
            if (!tmpl)
                return;
            bool const precious = tmpl->Class == ITEM_CLASS_QUEST || tmpl->Class == ITEM_CLASS_CONTAINER || tmpl->Class == ITEM_CLASS_CONSUMABLE;
            uint64 const score = (uint64(precious ? 1 : 0) << 40) | (uint64(tmpl->Quality) << 32) | std::min<uint32>(tmpl->SellPrice, 0x7FFFFFFFu);
            if (score < worstScore)
            {
                worstScore = score;
                worst = item;
            }
        };
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Item* bagItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot))
                if (Bag* bag = bagItem->ToBag())
                    for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                        consider(bag->GetItemByPos(uint8(slot)));
        if (!worst)
            return false;
        LOG_INFO("module.coa-playerbots.dungeon", "'{}' has full bags: destroying '{}' to make room for item {}.", bot->GetName(), worst->GetTemplate()->Name1, itemId);
        bot->DestroyItem(worst->GetBagSlot(), worst->GetSlot(), true);
        return bot->AddItem(itemId, 1);
    }

    bool Has(uint32 hookId)
    {
        return Table().count(hookId) != 0 || ImportedTable().count(hookId) != 0;
    }

    ObjectiveArriveResult Run(uint32 hookId, Player* tank, DungeonBossInfo const* info)
    {
        auto it = Table().find(hookId);
        if (it == Table().end())
        {
            auto const& imported = ImportedTable();
            if (auto row = imported.find(hookId); row != imported.end())
            {
                static DungeonBossInfo const none;
                AiObjectContext context(tank);
                return row->second(tank, &context, info ? *info : none);
            }
            static std::unordered_set<uint32> warned;
            if (warned.insert(hookId).second)
                LOG_WARN("module.coa-playerbots.dungeon", "Dungeon event hook {} has no implementation here: the step is blocked.", hookId);
            return ObjectiveArriveResult::Blocked;
        }
        return it->second(tank);
    }
}
