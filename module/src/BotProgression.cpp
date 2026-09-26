#include "BotProgression.h"
#include "BotAI.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedGossip.h"
#include "SharedDefines.h"
#include "SpellMgr.h"
#include "TemporarySummon.h"
#include "Trainer.h"
#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    // npc_ascension_training_book (mod-ascension-compat): the gossip a player uses to receive the
    // class abilities their level allows while CoA.AutoProgression is off.
    constexpr uint32 BOOK_OF_ASCENSION_CREATURE = 75115;
    constexpr uint32 BOOK_RESTORE_ABILITIES_ACTION = GOSSIP_ACTION_INFO_DEF + 1;

    // Book of Artisans, the "Personal Guide" profession trainer (creature_default_trainer 200001).
    constexpr uint32 BOOK_OF_ARTISANS_CREATURE = 57500;

    // Companion summon spells, as taught by the items 750750 (Book of Artisans) and 98457
    // (Book of Ascension) -- verified in item_template.spellid_2.
    constexpr uint32 SPELL_COMPANION_BOOK_OF_ARTISANS = 750750;
    constexpr uint32 SPELL_COMPANION_BOOK_OF_ASCENSION = 979602;

    constexpr uint32 SKILL_WOODCUTTING = 732;

    struct RaceMounts
    {
        uint8 race;
        uint32 ground60;
        uint32 ground100;
    };

    // Resolved from each mount item's spellid_2 in this realm's item_template rather than recalled,
    // which caught at least one id that differs from memory (Swift Gray Ram is 23239 here).
    constexpr std::array<RaceMounts, 10> RACE_MOUNTS =
    {{
        { RACE_HUMAN,         458,   23229 },
        { RACE_ORC,           6654,  23250 },
        { RACE_DWARF,         6777,  23239 },
        { RACE_NIGHTELF,      10793, 23221 },
        { RACE_UNDEAD_PLAYER, 17462, 17465 },
        { RACE_TAUREN,        18990, 23249 },
        { RACE_GNOME,         10873, 23225 },
        { RACE_TROLL,         8395,  23241 },
        { RACE_BLOODELF,      34795, 33660 },
        { RACE_DRAENEI,       34406, 35713 },
    }};

    constexpr uint32 FLYING_ALLIANCE_150 = 32235; // Golden Gryphon
    constexpr uint32 FLYING_ALLIANCE_280 = 32242; // Swift Blue Gryphon
    constexpr uint32 FLYING_HORDE_150 = 32243;    // Tawny Wind Rider
    constexpr uint32 FLYING_HORDE_280 = 32246;    // Swift Red Wind Rider

    // One tool per profession that needs one, chosen by the engine's TotemCategory rather than by
    // name -- several CoA tools are heirlooms with level 10 required, which only matters for
    // equipping, not for a tool that just has to be in the bags.
    constexpr std::array<std::pair<uint32, uint32>, 7> PROFESSION_TOOLS =
    {{
        { SKILL_MINING,        2901 },   // Mining Pick
        { SKILL_SKINNING,      7005 },   // Skinning Knife
        { SKILL_BLACKSMITHING, 5956 },   // Blacksmith Hammer
        { SKILL_ENGINEERING,   621922 }, // Arclight Spanner
        { SKILL_ENGINEERING,   10498 },  // Gyromatic Micro-Adjustor
        { SKILL_INSCRIPTION,   39505 },  // Virtuoso Inking Set
        { SKILL_WOODCUTTING,   6954 },   // Lumber Axe
    }};

    constexpr uint32 FISHING_POLE = 6256;

    // The riding spells mod-ascension-compat's InitializeRiding teaches every character when
    // CoA.MaxRidingFromStart is on: Apprentice, Journeyman, Expert, Artisan riding and
    // Cold Weather Flying.
    constexpr std::array<uint32, 5> RIDING_SPELLS = { 33388, 33391, 34090, 34091, 54197 };

    // Enchanting rods by the skill at which recipes start asking for them. A higher rod's
    // TotemCategory also satisfies the lower ones, so only the best one needs adding; older rods are
    // kept like every other tool (a bot never throws a profession tool away).
    constexpr std::array<std::pair<uint16, uint32>, 10> ENCHANTING_RODS =
    {{
        { 0,   6218 },  // Runed Copper Rod
        { 100, 6339 },  // Runed Silver Rod
        { 150, 11130 }, // Runed Golden Rod
        { 200, 11145 }, // Runed Truesilver Rod
        { 275, 16207 }, // Runed Arcanite Rod
        { 300, 22461 }, // Runed Fel Iron Rod
        { 335, 22462 }, // Runed Adamantite Rod
        { 350, 22463 }, // Runed Eternium Rod
        { 400, 44451 }, // Runed Cobalt Rod
        { 425, 44452 }, // Runed Titanium Rod
    }};

    constexpr std::array<uint32, 16> PROFESSION_SKILLS =
    {
        SKILL_ALCHEMY, SKILL_BLACKSMITHING, SKILL_ENCHANTING, SKILL_ENGINEERING, SKILL_LEATHERWORKING,
        SKILL_TAILORING, SKILL_HERBALISM, SKILL_MINING, SKILL_SKINNING, SKILL_JEWELCRAFTING, SKILL_INSCRIPTION,
        SKILL_COOKING, SKILL_FIRST_AID, SKILL_FISHING, SKILL_WOODCUTTING, 757 /* Woodworking */,
    };

    uint16 ProfessionCapFor(uint8 level)
    {
        return uint16(std::max<uint32>(1, std::min<uint32>(450, uint32(level) * 6)));
    }

    bool HasItem(Player* bot, uint32 itemId)
    {
        return bot->HasItemCount(itemId, 1, true);
    }

    // Straight into the bags, never an equipment slot. Several CoA tools are real weapons (the Mining
    // Pick is a main-hand heirloom), and StoreNewItemInBestSlots equips into any empty slot first --
    // confirmed live: bots whose gear-up found no weapon ended up fighting with their Mining Pick.
    void GiveOnce(Player* bot, uint32 itemId, uint32 count = 1)
    {
        if (HasItem(bot, itemId) || !sObjectMgr->GetItemTemplate(itemId))
            return;

        ItemPosCountVec dest;
        if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, itemId, count) == EQUIP_ERR_OK)
            bot->StoreNewItem(dest, itemId, true);
    }

    struct Consumable
    {
        uint32 entry;
        uint32 requiredLevel;
        uint32 vendors;
    };

    struct ConsumableTable
    {
        std::vector<Consumable> food;
        std::vector<Consumable> drink;
    };

    // Vendor-sold food (spellcategory 11) and drink (59) from the world DB, loaded once. The drink
    // category also holds alcohol that restores next to no mana, so among items of one level the one
    // sold by the most vendors wins -- that is ordinary water and bread in practice.
    ConsumableTable const& Consumables()
    {
        static ConsumableTable table = []
        {
            ConsumableTable loaded;
            QueryResult result = WorldDatabase.Query(
                "SELECT it.entry, it.RequiredLevel, it.spellcategory_1, COUNT(*) FROM item_template it "
                "JOIN npc_vendor v ON v.item = it.entry WHERE it.class = 0 AND it.subclass = 5 "
                "AND it.spellcategory_1 IN (11, 59) GROUP BY it.entry, it.RequiredLevel, it.spellcategory_1");
            if (result)
            {
                do
                {
                    Field* fields = result->Fetch();
                    Consumable item{ fields[0].Get<uint32>(), fields[1].Get<uint32>(),
                        uint32(fields[3].Get<uint64>()) };
                    (fields[2].Get<uint32>() == 11 ? loaded.food : loaded.drink).push_back(item);
                } while (result->NextRow());
            }
            LOG_INFO("module.coa-playerbots", ">> Loaded {} vendor food and {} vendor drink items for bot supplies.",
                loaded.food.size(), loaded.drink.size());
            return loaded;
        }();
        return table;
    }

    uint32 PickConsumable(std::vector<Consumable> const& items, uint8 level)
    {
        Consumable const* best = nullptr;
        for (Consumable const& item : items)
        {
            if (!item.requiredLevel || item.requiredLevel > level)
                continue;
            if (!best || item.requiredLevel > best->requiredLevel ||
                (item.requiredLevel == best->requiredLevel && item.vendors > best->vendors))
                best = &item;
        }
        return best ? best->entry : 0;
    }
}

namespace BotProgression
{
    void LearnAbilitiesFromBook(Player* bot)
    {
        if (!bot || !bot->IsInWorld())
            return;

        TempSummon* book = bot->SummonCreature(BOOK_OF_ASCENSION_CREATURE, bot->GetPositionX() + 1.0f,
            bot->GetPositionY(), bot->GetPositionZ(), bot->GetOrientation(), TEMPSUMMON_TIMED_DESPAWN, 5000);
        if (!book)
        {
            LOG_WARN("module.coa-playerbots", "BotProgression: could not summon a Book of Ascension for '{}'.",
                bot->GetName());
            return;
        }

        uint32 before = uint32(bot->GetSpellMap().size());
        sScriptMgr->OnGossipSelect(bot, book, GOSSIP_SENDER_MAIN, BOOK_RESTORE_ABILITIES_ACTION);
        book->DespawnOrUnsummon();

        uint32 after = uint32(bot->GetSpellMap().size());
        if (after > before)
            LOG_INFO("module.coa-playerbots",
                "BotProgression: '{}' learned {} spell(s) from its Book of Ascension at level {}.",
                bot->GetName(), after - before, bot->GetLevel());
    }

    uint32 LearnRecipesFromBook(Player* bot)
    {
        if (!bot)
            return 0;

        Trainer::Trainer* trainer = sObjectMgr->GetTrainer(BOOK_OF_ARTISANS_CREATURE);
        if (!trainer)
            return 0;

        // Recipes chain through ReqAbility, so a pass can unlock the next rank; repeat until a pass
        // teaches nothing. Bounded, since each pass either learns something or ends the loop.
        uint32 learned = 0;
        for (uint32 pass = 0; pass < 8; ++pass)
        {
            uint32 learnedThisPass = 0;
            for (Trainer::Spell const& spell : trainer->GetSpells())
            {
                if (!trainer->CanTeachSpell(bot, &spell))
                    continue;

                if (spell.IsCastable())
                    bot->CastSpell(bot, spell.SpellId, true);
                else
                    bot->learnSpell(spell.SpellId, false);
                ++learnedThisPass;
            }

            learned += learnedThisPass;
            if (!learnedThisPass)
                break;
        }

        if (learned)
            LOG_INFO("module.coa-playerbots", "BotProgression: '{}' learned {} recipe(s) from its Book of Artisans.",
                bot->GetName(), learned);
        return learned;
    }

    void GrantCompanions(Player* bot)
    {
        for (uint32 spellId : { SPELL_COMPANION_BOOK_OF_ARTISANS, SPELL_COMPANION_BOOK_OF_ASCENSION })
            if (!bot->HasSpell(spellId) && sSpellMgr->GetSpellInfo(spellId))
                bot->learnSpell(spellId, false);
    }

    void GrantMounts(Player* bot)
    {
        // mod-ascension-compat grants full riding in its OnPlayerLogin, but returns early for
        // socketless sessions before reaching InitializeRiding -- so on this realm every player
        // rides from level 1 while no bot ever had a riding skill at all, and CoA's own mount
        // wrapper refuses to mount anyone below 75 riding. Mirror that rule here, reading the same
        // option so bots follow the server if it is ever turned off (CoA.MaxRidingFromStart since the core
        // renamed its AscensionCompat.* settings to CoA.*).
        static bool const maxRidingFromStart = sConfigMgr->GetOption<bool>("CoA.MaxRidingFromStart", true);
        if (maxRidingFromStart && bot->GetBaseSkillValue(SKILL_RIDING) < 300)
        {
            for (uint32 spellId : RIDING_SPELLS)
                if (!bot->HasSpell(spellId) && sSpellMgr->GetSpellInfo(spellId))
                    bot->learnSpell(spellId, false);
            bot->SetSkill(SKILL_RIDING, 4, 300, 300);
        }

        std::vector<uint32> mounts;
        for (RaceMounts const& entry : RACE_MOUNTS)
        {
            if (entry.race != bot->getRace())
                continue;
            mounts.push_back(entry.ground60);
            mounts.push_back(entry.ground100);
        }

        bool alliance = Player::TeamIdForRace(bot->getRace()) == TEAM_ALLIANCE;
        mounts.push_back(alliance ? FLYING_ALLIANCE_150 : FLYING_HORDE_150);
        mounts.push_back(alliance ? FLYING_ALLIANCE_280 : FLYING_HORDE_280);

        for (uint32 spellId : mounts)
            if (!bot->HasSpell(spellId) && sSpellMgr->GetSpellInfo(spellId))
                bot->learnSpell(spellId, false);
    }

    void GrantProfessionTools(Player* bot)
    {
        for (auto const& [skill, itemId] : PROFESSION_TOOLS)
            if (bot->HasSkill(skill))
                GiveOnce(bot, itemId);

        if (bot->HasSkill(SKILL_FISHING) && !HasItem(bot, FISHING_POLE))
            GiveOnce(bot, FISHING_POLE);

        if (!bot->HasSkill(SKILL_ENCHANTING))
            return;

        uint16 enchanting = bot->GetSkillValue(SKILL_ENCHANTING);
        uint32 wanted = 0;
        for (auto const& [minSkill, rod] : ENCHANTING_RODS)
            if (enchanting >= minSkill)
                wanted = rod;

        GiveOnce(bot, wanted);
    }

    void ProvisionFood(Player* bot, uint32 stackSize)
    {
        ConsumableTable const& table = Consumables();
        uint8 level = bot->GetLevel();
        for (uint32 itemId : { PickConsumable(table.food, level), PickConsumable(table.drink, level) })
            if (itemId && !HasItem(bot, itemId))
                bot->StoreNewItemInBestSlots(itemId, stackSize);
    }

    void OnLevelUp(Player* bot, uint8 newLevel)
    {
        if (!bot)
            return;

        // Caps follow the level; values climb part of the way, so a bot's crafting and gathering
        // grow while it levels without jumping to the cap.
        uint16 cap = ProfessionCapFor(newLevel);
        for (uint32 skill : PROFESSION_SKILLS)
        {
            if (!bot->HasSkill(skill))
                continue;
            uint16 value = bot->GetSkillValue(skill);
            uint16 grown = uint16(std::min<uint32>(cap, uint32(value) + 4));
            bot->SetSkill(uint16(skill), bot->GetSkillStep(uint16(skill)), grown, cap);
        }

        LearnAbilitiesFromBook(bot);
        LearnRecipesFromBook(bot);
        GrantProfessionTools(bot);
        ProvisionFood(bot, 20);
    }
}
