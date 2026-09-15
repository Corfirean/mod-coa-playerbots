/*
 * mod-coa-playerbots
 *
 * Chronomancer (class 22) turned out to have a genuinely thin direct-damage kit, not just poor
 * DBC visibility like Reaper -- its "Time" spec (0 real damage/DoT candidates out of dozens of
 * talent-granted spells scanned) is almost certainly the confirmed Healer spec in
 * docs/roles.md, and even the two DPS specs (Infinite/Caster DPS, Artificer/Ranged DPS) only
 * have 2 real attacks each. This class is architecturally support/utility-heavy (teleport-swap
 * positioning, absorb shields, cooldown/resource manipulation -- see
 * AscensionChronomancerTalents.cpp/Ripple.cpp), not a spell-spam caster.
 *
 * IMPORTANT correction found while researching this class: earlier rotations in this session
 * (Reaper/Felsworn/Bloodmage/Stormbringer/Primalist/Runemaster/Xoroth) scanned for
 * SPELL_EFFECT_WEAPON_PERCENT_DAMAGE using the WRONG numeric value (62, which is actually
 * SPELL_EFFECT_POWER_BURN -- a legitimate but different damage effect). The real value is 31.
 * This didn't break anything already shipped (every rotation was verified by live testing, not
 * by trusting the scan alone), but it means those earlier scans may have missed some real
 * weapon-percent-damage candidates -- worth a second pass if any of those classes' rotations
 * ever seem incomplete. This file's own scan used the corrected value and is what surfaced
 * "Shatter Echo" (effects 121+31 together, a real attack) in the first place.
 *
 * Real candidates: Melt Reality (a DoT, spec32/Infinite) and Chromatic Shard (spec32) both carry
 * ManaCost=0 in the raw DBC field but actually cost real mana via SpellInfo::CalcPowerCost's
 * percentage-based calculation (799 mana each on a 6253 max-mana level-80 character) -- "no
 * precondition" in the sense of no aura/stance gate, but NOT free. This class's kit reads as a
 * couple of expensive, deliberate casts rather than a filler-spam rotation, which fits its
 * support/utility-heavy design (see above). Shatter Echo (spec33/Artificer, a real weapon
 * attack, 3s cooldown) and Arc Collision (spec33, a DoT) both require CasterAuraSpell 804455 --
 * confirmed present in the DBC data and gated via the same HasAura() pattern proven correct on
 * Felsworn/Xoroth, but this specific aura's real source spell wasn't identified in this pass
 * (not GM-`.cast`-able directly, so likely only granted as a proc from something else in the
 * Artificer tree) -- live-confirmed only for Melt Reality; Shatter Echo/Arc Collision remain
 * unexercised live pending that follow-up.
 */

#include "BotClassRotationsChronomancer.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"

namespace BotAI
{
namespace
{
constexpr uint32 SPELL_MELT_REALITY = 806335; // DoT, no precondition
constexpr uint32 SPELL_CHROMATIC_SHARD = 801292; // no precondition

constexpr uint32 AURA_ARC_GATE = 804455;
constexpr uint32 SPELL_SHATTER_ECHO = 804503; // 3s cd, requires AURA_ARC_GATE
constexpr uint32 SPELL_ARC_COLLISION = 524853; // DoT, 28 mana, requires AURA_ARC_GATE

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

uint32 SelectChronomancerRotationSpell(Player* bot, Unit* target)
{
    if (bot->getClass() != CLASS_CHRONOMANCER)
        return 0;

    if (bot->HasSpell(SPELL_MELT_REALITY) && !target->HasAura(SPELL_MELT_REALITY, bot->GetGUID()) &&
        IsReady(bot, target, SPELL_MELT_REALITY))
        return SPELL_MELT_REALITY;

    if (bot->HasAura(AURA_ARC_GATE))
    {
        if (IsReady(bot, target, SPELL_SHATTER_ECHO))
            return SPELL_SHATTER_ECHO;
        if (bot->HasSpell(SPELL_ARC_COLLISION) && !target->HasAura(SPELL_ARC_COLLISION, bot->GetGUID()) &&
            IsReady(bot, target, SPELL_ARC_COLLISION))
            return SPELL_ARC_COLLISION;
    }

    if (IsReady(bot, target, SPELL_CHROMATIC_SHARD))
        return SPELL_CHROMATIC_SHARD;

    return 0;
}
}
