/*
 * mod-coa-playerbots
 *
 * Stormbringer (class 16) is a pure Caster DPS class (Lightning/Maelstrom/Wind, all confirmed
 * in docs/roles.md) built around a real Air Elemental pet (AscensionStormbringerPet.cpp) and
 * utility summons (Wind Gate, Raging Zephyr) that this rotation doesn't touch -- those are
 * player-initiated cooldowns/utility, not "press this to deal damage."
 *
 * Every real damage candidate found (via the same AscensionCoATalentData.h + SpellInfo effect
 * scan technique as Reaper/Felsworn/Bloodmage) has a base cost of 0 -- Stormbringer's real gate
 * is proc-based, not a spendable resource bar. Four of the seven are individually gated on a
 * specific `CasterAuraSpell` requirement (a buff this rotation didn't chase down the source of
 * -- likely a native passive-talent proc, same as Reaper's Painmail, needing no code here
 * either way): Stormflow needs aura 681016, Deluge needs 707050, Ride the Lightning needs
 * 573249, Arm of Thorim needs 803102. Checked directly via Player::HasAura before ever
 * offering them, same defensive pattern as Felsworn's Felfury-gated spells -- regardless of
 * what grants the buff, casting without it would just fail SPELL_FAILED_CASTER_AURASTATE.
 * Gale, Volt (a DoT) and Forked Lightning carry no such gate and are always-available
 * fallbacks.
 */

#include "BotClassRotationsStormbringer.h"
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
constexpr uint32 SPELL_GALE = 804036;
constexpr uint32 SPELL_VOLT = 500928; // DoT
constexpr uint32 SPELL_FORKED_LIGHTNING = 801851;

constexpr uint32 SPELL_STORMFLOW = 567555; // DoT, requires aura 681016
constexpr uint32 AURA_STORMFLOW_GATE = 681016;
constexpr uint32 SPELL_DELUGE = 806400; // requires aura 707050
constexpr uint32 AURA_DELUGE_GATE = 707050;
constexpr uint32 SPELL_RIDE_THE_LIGHTNING = 800099; // requires aura 573249
constexpr uint32 AURA_RIDE_THE_LIGHTNING_GATE = 573249;
constexpr uint32 SPELL_ARM_OF_THORIM = 801847; // requires aura 803102, 20s cooldown
constexpr uint32 AURA_ARM_OF_THORIM_GATE = 803102;

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

bool IsGatedReady(Player* bot, Unit* target, uint32 spellId, uint32 gateAura)
{
    return bot->HasAura(gateAura) && IsReady(bot, target, spellId);
}
}

uint32 SelectStormbringerRotationSpell(Player* bot, Unit* target)
{
    if (bot->getClass() != CLASS_STORMBRINGER)
        return 0;

    if (bot->HasSpell(SPELL_VOLT) && !target->HasAura(SPELL_VOLT, bot->GetGUID()) && IsReady(bot, target, SPELL_VOLT))
        return SPELL_VOLT;
    if (bot->HasSpell(SPELL_STORMFLOW) && !target->HasAura(SPELL_STORMFLOW, bot->GetGUID()) &&
        IsGatedReady(bot, target, SPELL_STORMFLOW, AURA_STORMFLOW_GATE))
        return SPELL_STORMFLOW;

    if (IsGatedReady(bot, target, SPELL_ARM_OF_THORIM, AURA_ARM_OF_THORIM_GATE))
        return SPELL_ARM_OF_THORIM;
    if (IsGatedReady(bot, target, SPELL_RIDE_THE_LIGHTNING, AURA_RIDE_THE_LIGHTNING_GATE))
        return SPELL_RIDE_THE_LIGHTNING;
    if (IsGatedReady(bot, target, SPELL_DELUGE, AURA_DELUGE_GATE))
        return SPELL_DELUGE;

    for (uint32 spellId : {SPELL_GALE, SPELL_FORKED_LIGHTNING})
        if (IsReady(bot, target, spellId))
            return spellId;

    return 0;
}
}
