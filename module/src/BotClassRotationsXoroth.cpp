/*
 * mod-coa-playerbots
 *
 * Knight of Xoroth (class 17, internally CLASS_FLESHWARDEN) real attacks, found via the same
 * AscensionCoATalentData.h + SpellInfo effect scan technique as the previous classes -- 7 of
 * ~170 talent-granted spells had a real native damage/DoT effect. All cost Rage (PowerType 1).
 * Two real preconditions found, both defended against the same way as previous classes:
 * Hellmaw/Implosion require CasterAuraSpell 500906; Seeking Flame requires a specific
 * shapeshift/stance (SpellInfo::Stances 0x200000), checked via the real
 * SpellInfo::CheckShapeshift the same way BotClassRotationsRunemaster.cpp does for Fracture.
 *
 * Roles for this class are all "inferred" rather than confirmed in docs/roles.md (Defiance =
 * Tank, Hellfire = Caster DPS, War = Melee DPS, all lower-confidence heuristic guesses) -- the
 * spell selection below doesn't depend on knowing which is which, same "one priority list,
 * let HasSpell filter it" shape as every other multi-spec class in this file set.
 */

#include "BotClassRotationsXoroth.h"
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
// Long-cooldown DoTs, maintained first if this bot's own copy isn't already ticking.
constexpr uint32 SPELL_CHAINS_OF_MALICE = 803185; // 90s cd
constexpr uint32 SPELL_CURSE_OF_XOROTH = 804169;  // 90s cd

constexpr uint32 AURA_HELLMAW_GATE = 500906;
constexpr uint32 SPELL_HELLMAW = 806965;   // dmg + dot, requires AURA_HELLMAW_GATE
constexpr uint32 SPELL_IMPLOSION = 524897; // 40s cd, requires AURA_HELLMAW_GATE

constexpr uint32 SPELL_SEEKING_FLAME = 805671; // requires a specific stance

constexpr std::array<uint32, 2> SPELL_FILLERS = {
    805074, // Call: Hellfire Abyssal, 1000 rage, 45s cd
    800081, // Chainwhip, 100 rage, 20s cd
};

bool IsCastable(Player* bot, Unit* target, uint32 spellId)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    if (spellInfo->Stances && spellInfo->CheckShapeshift(bot->GetShapeshiftForm()) != SPELL_CAST_OK)
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

uint32 SelectXorothRotationSpell(Player* bot, Unit* target)
{
    if (bot->getClass() != CLASS_FLESHWARDEN)
        return 0;

    if (bot->HasSpell(SPELL_CHAINS_OF_MALICE) && !target->HasAura(SPELL_CHAINS_OF_MALICE, bot->GetGUID()) &&
        IsReady(bot, target, SPELL_CHAINS_OF_MALICE))
        return SPELL_CHAINS_OF_MALICE;
    if (bot->HasSpell(SPELL_CURSE_OF_XOROTH) && !target->HasAura(SPELL_CURSE_OF_XOROTH, bot->GetGUID()) &&
        IsReady(bot, target, SPELL_CURSE_OF_XOROTH))
        return SPELL_CURSE_OF_XOROTH;

    if (bot->HasAura(AURA_HELLMAW_GATE))
    {
        for (uint32 spellId : {SPELL_HELLMAW, SPELL_IMPLOSION})
            if (IsReady(bot, target, spellId))
                return spellId;
    }

    if (IsReady(bot, target, SPELL_SEEKING_FLAME))
        return SPELL_SEEKING_FLAME;

    for (uint32 spellId : SPELL_FILLERS)
        if (IsReady(bot, target, spellId))
            return spellId;

    return 0;
}
}
