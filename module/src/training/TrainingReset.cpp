#include "training/TrainingSession.h"
#include "BotAI.h"
#include "CharmInfo.h"
#include "Item.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "MotionMaster.h"

namespace CoaTraining
{
Json Gear(Player* bot)
{
    Json rows{Json::Array{}};
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Json row;
        row["slot"] = slot;
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            row["entry"] = item->GetEntry();
            row["randomProperty"] = item->GetItemRandomPropertyId();
            Json enchants{Json::Array{}};
            for (uint8 e = 0; e < MAX_ENCHANTMENT_SLOT; ++e)
                enchants.Push(item->GetEnchantmentId(EnchantmentSlot(e)));
            row["enchants"] = enchants;
        }
        else row["entry"] = 0;
        rows.Push(row);
    }
    return rows;
}

std::string Role(Player* bot)
{
    switch (BotAI::GetRole(bot->GetGUID()))
    {
        case BotRole::Tank: return "Tank";
        case BotRole::Healer: return "Healer";
        case BotRole::Support: return "Support";
        default: return "Dps";
    }
}

void Reset(Player* bot)
{
    bot->InterruptNonMeleeSpells(true);
    bot->CombatStop(true);
    bot->RemoveAllControlled();
    bot->GetMotionMaster()->Clear();
    std::vector<uint32> remove;
    for (auto const& [id, application] : bot->GetAppliedAuras())
        // Mechanics' specialization marker is not flagged passive in the DBC,
        // but native Scrap generation explicitly requires it. It is not a combat buff.
        if (!application->GetBase()->IsPassive() &&
            !(bot->getClass() == 28 && id == 92141 && bot->HasSpell(92141)))
            remove.push_back(id);
    // Remove one captured instance. Core-owned lifecycle scripts may recreate helpers on removal;
    // RemoveAurasDueToSpell retries until none remain and can loop forever on such helpers.
    for (uint32 id : remove)
        if (Aura* aura = bot->GetAura(id)) aura->Remove();
    if (bot->getClass() == 28 && bot->HasSpell(92141) && !bot->HasAura(92141))
        bot->AddAura(92141, bot);
    if (!bot->IsAlive()) bot->ResurrectPlayer(1.0f, false);
    bot->RemoveAllSpellCooldown();
    for (auto const& [id, spell] : bot->GetSpellMap())
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(id))
            bot->GetGlobalCooldownMgr().CancelGlobalCooldown(info);
    bot->DurabilityRepairAll(false, 1.0f, false);
    bot->SetHealth(bot->GetMaxHealth());
    for (uint8 i = 0; i < MAX_POWERS; ++i)
    {
        Powers power = Powers(i);
        bot->SetPower(power, power == POWER_RAGE || power == POWER_RUNIC_POWER ? 0 : bot->GetMaxPower(power));
    }
    bot->SetTarget(ObjectGuid::Empty);
    BotAI::Forget(bot->GetGUID());
}
}
