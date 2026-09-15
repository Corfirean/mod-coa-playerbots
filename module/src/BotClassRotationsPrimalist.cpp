/*
 * mod-coa-playerbots
 *
 * Primalist (class 31, internally CLASS_WILDWALKER) real attacks, found via the same
 * AscensionCoATalentData.h + SpellInfo effect scan technique as the previous classes. Unlike
 * Reaper/Felsworn/Bloodmage/Stormbringer, none of the 16 real damage candidates found here
 * carry any CasterAuraState/TargetAuraState/CasterAuraSpell precondition -- the cleanest class
 * scanned so far, needing only the standard range/cost checks every rotation in this file set
 * already performs. Not cross-checked against mod-ascension-compat's own Primalist source
 * files this pass (AscensionPrimalistTalents.cpp/Weapons.cpp/Earthshaping.cpp/Mountain.cpp/
 * SpiritBeast.cpp) beyond confirming they exist -- the DBC-native data alone was clean enough
 * to work from directly, unlike Reaper where that data was mostly wrong.
 *
 * Covers all 4 specs (Geomancy/Mountain King/two unlabeled melee-ish trees) by offering
 * whichever real attacks the bot's own spec happens to know, same "one priority list per
 * class, let HasSpell filter it" shape as Felsworn/Bloodmage/Stormbringer. DoT (Seismic
 * Tremor) maintained first, then the single long-cooldown nuke (Magma Fissure, 90s), then
 * fillers ordered roughly by real cost (a proxy for "hits harder," same heuristic used for
 * Bloodmage since no proper per-class audit exists yet).
 */

#include "BotClassRotationsPrimalist.h"
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
constexpr uint32 SPELL_SEISMIC_TREMOR = 680442; // DoT + direct hit
constexpr uint32 SPELL_MAGMA_FISSURE = 802793;  // 90s cooldown, free

constexpr std::array<uint32, 8> SPELL_FILLERS = {
    681119, // Terrasurge, 450 rage
    805462, // Seismic Wave, 400 rage
    300693, // Seismic Smash, 400 rage, 8s cd
    800178, // Totemic Smash, 350 rage, 6s cd
    681130, // Mountain Hammer, 200 rage
    500696, // Primal Rush, free
    800144, // Spirit Charge, free
    803974, // Quake, free
};

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

    int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
    return cost <= 0 || bot->GetPower(Powers(spellInfo->PowerType)) >= cost;
}

bool IsReady(Player* bot, Unit* target, uint32 spellId)
{
    return bot->HasSpell(spellId) && !bot->HasSpellCooldown(spellId) && IsCastable(bot, target, spellId);
}
}

uint32 SelectPrimalistRotationSpell(Player* bot, Unit* target)
{
    if (bot->getClass() != CLASS_WILDWALKER)
        return 0;

    if (bot->HasSpell(SPELL_SEISMIC_TREMOR) && !target->HasAura(SPELL_SEISMIC_TREMOR, bot->GetGUID()) &&
        IsReady(bot, target, SPELL_SEISMIC_TREMOR))
        return SPELL_SEISMIC_TREMOR;

    if (IsReady(bot, target, SPELL_MAGMA_FISSURE))
        return SPELL_MAGMA_FISSURE;

    for (uint32 spellId : SPELL_FILLERS)
        if (IsReady(bot, target, spellId))
            return spellId;

    return 0;
}
}
