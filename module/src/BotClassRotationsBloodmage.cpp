/*
 * mod-coa-playerbots
 *
 * Bloodmage (class 20, internally CLASS_SON_OF_ARUGAL -- see the header) is a blood-magic
 * caster: several of its real attacks (found via the same AscensionCoATalentData.h scan
 * technique as Reaper/Felsworn, cross-checked for real SpellInfo damage effects) cost the
 * caster's own HEALTH instead of a mana-like resource -- SpellInfo::PowerType == POWER_HEALTH
 * (0xFFFFFFFE, i.e. -2) rather than a normal 0-4 power index. AscensionBloodmageVitality.cpp
 * layers a "Pooled Vitality" stack-refund mechanic on top of these casts (empowered casts can
 * become free/healing), but that's a bonus synergy that triggers automatically off the same
 * real CastSpell this rotation issues -- nothing extra needed here to get it.
 *
 * IMPORTANT gotcha found writing this: Unit::GetPower(Powers power) is
 * `GetUInt32Value(UNIT_FIELD_POWER1 + power)` -- literal pointer arithmetic using `power` as an
 * array offset. For POWER_HEALTH for that offset is -2, meaning `bot->GetPower(POWER_HEALTH)`
 * as used by the existing generic-engine cost check (SelectKnownSpell in BotAI.cpp) would read
 * two UInt32 fields *before* UNIT_FIELD_POWER1 -- NOT a reliable way to read current health.
 * This rotation checks PowerType == POWER_HEALTH explicitly and compares against
 * Unit::GetHealth() directly instead. Not fixed in the shared generic engine this pass (a
 * cross-cutting fix, out of scope for a single class's rotation file) -- flagged in AGENTS.md
 * for whoever next touches a class with a real health-cost spell.
 */

#include "BotClassRotationsBloodmage.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"
#include <array>

namespace BotAI
{
namespace
{
// DoTs, maintained first if this bot's own copy isn't already ticking.
constexpr uint32 SPELL_CRIMSON_TIDE = 504282;
constexpr uint32 SPELL_VAMPYRS_KISS = 504275;

// Proc-gated: both require the "Night Hunter" buff (524861) already up.
constexpr uint32 AURA_NIGHT_HUNTER = 524861;
constexpr uint32 SPELL_NIGHT_HUNTERS_HOWL = 500124;
constexpr uint32 SPELL_ROTCLAW = 804197;

// Plain fillers, roughly biggest real cost first (the closest proxy this file has to "hits
// hardest" without a proper per-class audit).
constexpr std::array<uint32, 5> SPELL_FILLERS = {504260, 572855, 804726, 560315, 560249};
// Veinburst, Hemoburst, Vampiric Fang, Valanar's Vengeance, Keleseth's Calamity.

bool CanAfford(Player* bot, SpellInfo const* spellInfo)
{
    // See this file's own header comment -- POWER_HEALTH cannot go through
    // Unit::GetPower(Powers) safely, so it's checked directly against GetHealth() instead.
    if (spellInfo->PowerType == POWER_HEALTH)
    {
        // Real caster, not nullptr: CalcPowerCost unconditionally dereferences it (caster->IsPlayer(),
        // school/aura modifiers, ...) -- confirmed live, nullptr here crashed the whole worldserver
        // the moment a Bloodmage bot evaluated a health-costed spell in combat. The comment this
        // replaces was about Unit::GetPower(POWER_HEALTH) being unsafe, not about CalcPowerCost's
        // caster argument -- GetHealth() below is still used instead of GetPower for that reason.
        int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
        return cost <= 0 || bot->GetHealth() > uint32(cost); // strictly greater: don't suicide-cast
    }
    int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
    return cost <= 0 || bot->GetPower(Powers(spellInfo->PowerType)) >= cost;
}

bool IsCastable(Player* bot, Unit* target, uint32 spellId)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    float dist = bot->GetDistance(target);
    float maxRange = spellInfo->GetMaxRange(false, bot);
    if (maxRange > 0.0f && dist > maxRange)
        return false;
    float minRange = spellInfo->GetMinRange(false);
    if (minRange > 0.0f && dist < minRange)
        return false;

    return CanAfford(bot, spellInfo);
}

bool IsReady(Player* bot, Unit* target, uint32 spellId)
{
    return bot->HasSpell(spellId) && !bot->HasSpellCooldown(spellId) && IsCastable(bot, target, spellId);
}
}

uint32 SelectBloodmageRotationSpell(Player* bot, Unit* target)
{
    if (bot->getClass() != CLASS_SON_OF_ARUGAL)
        return 0;

    if (bot->HasSpell(SPELL_CRIMSON_TIDE) && !target->HasAura(SPELL_CRIMSON_TIDE, bot->GetGUID()) &&
        IsReady(bot, target, SPELL_CRIMSON_TIDE))
        return SPELL_CRIMSON_TIDE;
    if (bot->HasSpell(SPELL_VAMPYRS_KISS) && !target->HasAura(SPELL_VAMPYRS_KISS, bot->GetGUID()) &&
        IsReady(bot, target, SPELL_VAMPYRS_KISS))
        return SPELL_VAMPYRS_KISS;

    if (bot->HasAura(AURA_NIGHT_HUNTER))
    {
        for (uint32 spellId : {SPELL_NIGHT_HUNTERS_HOWL, SPELL_ROTCLAW})
            if (IsReady(bot, target, spellId))
                return spellId;
    }

    for (uint32 spellId : SPELL_FILLERS)
        if (IsReady(bot, target, spellId))
            return spellId;

    return 0;
}
}
