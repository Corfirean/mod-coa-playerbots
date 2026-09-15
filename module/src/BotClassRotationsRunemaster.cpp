/*
 * mod-coa-playerbots
 *
 * Runemaster (class 32, internally CLASS_SPIRIT_MAGE) real attacks, found via the same
 * AscensionCoATalentData.h + SpellInfo effect scan technique as the previous classes. Like
 * Primalist, none of the 6 real damage candidates found here carry any
 * CasterAuraState/TargetAuraState/CasterAuraSpell precondition -- only the standard range/cost
 * checks are needed. Not cross-checked against mod-ascension-compat's own Runemaster source
 * (AscensionRunemasterBrand/Echoes/Glyphs/Manuscription/Scaling/Talents/Travel/Zenith.cpp, the
 * largest remaining-class source tree at ~82KB across 15 files) beyond confirming it exists --
 * the DBC-native data alone was clean enough to work from directly.
 *
 * Covers all 3 specs (Arcane/Riftblade/Runic -- only Riftblade has a *confirmed*, not inferred,
 * role in docs/roles.md: Melee DPS) by offering whichever real attacks the bot's own spec
 * happens to know. DoT (Hoarfrost) maintained first, then the one real cooldown (Fist of the
 * Ancients, 18s), then fillers.
 */

#include "BotClassRotationsRunemaster.h"
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
constexpr uint32 SPELL_HOARFROST = 801104; // DoT
constexpr uint32 SPELL_FIST_OF_THE_ANCIENTS = 712326; // 18s cooldown, free

constexpr std::array<uint32, 4> SPELL_FILLERS = {
    804550, // Thaumaturgy, 25 mana, 15s cd
    500118, // Frigid Blast, free
    801179, // Glyphic Ruin, free
    803018, // Fracture, free
};

bool IsCastable(Player* bot, Unit* target, uint32 spellId)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    // Fracture requires a specific shapeshift/stance (SpellInfo::Stances, a real bitmask field
    // -- caught live: SPELL_FAILED_ONLY_SHAPESHIFT against a bot not currently in whatever
    // "rune form" this is). Reuses the same real check the engine's own cast validation would
    // run (SpellInfo::CheckShapeshift) rather than hand-rolling the bit/form mapping -- this
    // rotation doesn't know or need to know which form it is, just whether the bot is in it.
    if (spellInfo->CheckShapeshift(bot->GetShapeshiftForm()) != SPELL_CAST_OK)
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

uint32 SelectRunemasterRotationSpell(Player* bot, Unit* target)
{
    if (bot->getClass() != CLASS_SPIRIT_MAGE)
        return 0;

    if (bot->HasSpell(SPELL_HOARFROST) && !target->HasAura(SPELL_HOARFROST, bot->GetGUID()) &&
        IsReady(bot, target, SPELL_HOARFROST))
        return SPELL_HOARFROST;

    if (IsReady(bot, target, SPELL_FIST_OF_THE_ANCIENTS))
        return SPELL_FIST_OF_THE_ANCIENTS;

    for (uint32 spellId : SPELL_FILLERS)
        if (IsReady(bot, target, spellId))
            return spellId;

    return 0;
}
}
