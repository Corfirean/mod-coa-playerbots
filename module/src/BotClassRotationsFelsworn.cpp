/*
 * mod-coa-playerbots
 *
 * Felsworn (class 14, internally CLASS_DEMON_HUNTER -- see the header) covers three very
 * different roles (Infernal = Caster DPS, Slayer = Melee DPS, Tyrant = Tank, all confirmed in
 * docs/roles.md), so unlike Reaper this doesn't hand-pick "the" rotation for one kit -- it just
 * offers whichever real attacks the bot's own spec happens to have learned, same as the
 * generic engine already does, but with a sane priority order and (unlike the generic engine)
 * real range/cost validation before ever returning a candidate.
 *
 * Also unlike Reaper, most of Felsworn's kit turned out to be directly visible in Spell.dbc's
 * own native effect data once the DBC field offsets were verified correctly (PowerType=41,
 * ManaCost=42, Effect[0..2]=71-73, EffectApplyAuraName[0..2]=95-97 -- see DBCStructure.h's own
 * comments) -- an 11-hit scan across Felsworn+Chronomancer's ~340 combined talent-granted
 * spells, versus Reaper's 4-of-140. Cross-checked each candidate ID against
 * AscensionFelswornAbilities.cpp/AscensionFelswornContracts.cpp to confirm they're real,
 * referenced attacks with bonus synergy layered on top via the module's own registered hooks
 * (e.g. a Fury-resource duration bonus) -- none of that needs replicating here, it triggers
 * automatically off the same real CastSpell this rotation issues.
 *
 * Real attacks used (spell family name 6 -- CLASS_DEMON_HUNTER + 6, per
 * AscensionFelswornDirge-style validation elsewhere in this module):
 *  - Bane of Chaos (704368) / Sargeras Embrace (800355): real DoTs (SPELL_AURA_PERIODIC_DAMAGE).
 *    Maintained first, whichever isn't already ticking on the target from this bot.
 *  - Infernal (560284): a real damage effect on a long (45s) cooldown, free cast -- the
 *    priority "big cooldown" pick once available.
 *  - Tyrant's Gaze (805240): Tyrant (tank) spec's own real attack.
 *  - Ruin (801895) / Felwrath (520236): 0-cooldown Energy-cost fillers (Infernal spec).
 *  - Felhoof Charge (800204): a real-damage gap closer (Slayer spec).
 *  - Manaburn (805248): shared-tree filler available regardless of spec.
 * Not attempted: everything else in the kit is either a defensive cooldown, a resource/fury
 * mechanic, or (per the Reaper lesson) implemented in a script hook with no native SpellInfo
 * damage effect at all and not worth guessing at blind.
 */

#include "BotClassRotationsFelsworn.h"
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
constexpr uint32 SPELL_BANE_OF_CHAOS = 704368;
constexpr uint32 SPELL_SARGERAS_EMBRACE = 800355;
constexpr uint32 SPELL_INFERNAL = 560284;
constexpr uint32 SPELL_TYRANTS_GAZE = 805240;
constexpr uint32 SPELL_RUIN = 801895;
constexpr uint32 SPELL_FELWRATH = 520236;
constexpr uint32 SPELL_FELHOOF_CHARGE = 800204;
constexpr uint32 SPELL_MANABURN = 805248;
// Found live: the generic fallback was landing these two on its own (not from talent-data
// mining, which missed them) with occasional SPELL_FAILED_NO_POWER, since it never checks
// affordability. Sargeron Smite shares Ruin/Felwrath's "At Least 2 Felfury" gate (aura 803468);
// Fel Fireball has a real 35-Energy cost the generic engine doesn't check either.
constexpr uint32 SPELL_SARGERON_SMITE = 501321;
constexpr uint32 SPELL_FEL_FIREBALL = 501291;
constexpr uint32 AURA_AT_LEAST_2_FELFURY = 803468;

// Same real range/cost validation SelectKnownSpell performs in BotAI.cpp -- a rotation
// function that skips this can offer a candidate UpdateOffensive will cast blind (it trusts a
// non-zero return instead of chasing/checking affordability itself), which is exactly the two
// bugs caught live testing BotClassRotationsReaper.cpp. See that file for the fuller story.
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

uint32 SelectFelswornRotationSpell(Player* bot, Unit* target)
{
    if (bot->getClass() != CLASS_DEMON_HUNTER)
        return 0;

    // DoT maintenance first -- only worth reapplying if this bot's own copy isn't already
    // ticking (HasAura's second argument restricts the check to auras cast by this caster).
    if (bot->HasSpell(SPELL_BANE_OF_CHAOS) && !target->HasAura(SPELL_BANE_OF_CHAOS, bot->GetGUID()) &&
        IsReady(bot, target, SPELL_BANE_OF_CHAOS))
        return SPELL_BANE_OF_CHAOS;
    if (bot->HasSpell(SPELL_SARGERAS_EMBRACE) && !target->HasAura(SPELL_SARGERAS_EMBRACE, bot->GetGUID()) &&
        IsReady(bot, target, SPELL_SARGERAS_EMBRACE))
        return SPELL_SARGERAS_EMBRACE;

    if (IsReady(bot, target, SPELL_INFERNAL))
        return SPELL_INFERNAL;

    // Tyrant's Gaze is a real execute: DBCStructure.h's own TargetAuraState field (verified
    // live -- SPELL_FAILED_TARGET_AURASTATE against a full-health training dummy) requires
    // AURA_STATE_HEALTHLESS_35_PERCENT on the target. Below 35% is exactly when this should
    // actually fire, not a bug to route around.
    if (target->HasAuraState(AURA_STATE_HEALTHLESS_35_PERCENT) && IsReady(bot, target, SPELL_TYRANTS_GAZE))
        return SPELL_TYRANTS_GAZE;

    // Ruin/Felwrath/Sargeron Smite are Felfury spenders: all three require CasterAuraSpell
    // 803468 ("At Least 2 Felfury") -- also caught live (SPELL_FAILED_CASTER_AURASTATE would
    // have followed once the range/cost checks passed). Real stack-gated spenders, not a bug.
    if (bot->HasAura(AURA_AT_LEAST_2_FELFURY))
    {
        for (uint32 spellId : {SPELL_RUIN, SPELL_FELWRATH, SPELL_SARGERON_SMITE})
            if (IsReady(bot, target, spellId))
                return spellId;
    }

    for (uint32 spellId : {SPELL_FELHOOF_CHARGE, SPELL_FEL_FIREBALL, SPELL_MANABURN})
        if (IsReady(bot, target, spellId))
            return spellId;

    return 0;
}
}
