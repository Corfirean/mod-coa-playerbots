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
#include "engine/SpellResolver.h"
#include "engine/SpellPredicates.h"
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

// Legacy priorities still use root IDs, but dispatch must resolve the live
// active rank and effective native replacement through the shared cast guards.
uint32 ReadySpell(Player* bot, Unit* target, uint32 spellId)
{
    uint32 first = sSpellMgr->GetFirstSpellInChain(spellId);
    uint32 resolved = SpellResolver::ResolveCastSpell(bot, first ? first : spellId);
    return IsKnownSpellCastable(bot, resolved, target, false) ? resolved : 0;
}
}

uint32 SelectFelswornRotationSpell(Player* bot, Unit* target)
{
    if (bot->getClass() != CLASS_DEMON_HUNTER)
        return 0;

    for (uint32 root : {SPELL_BANE_OF_CHAOS, SPELL_SARGERAS_EMBRACE})
        if (uint32 spell = ReadySpell(bot, target, root))
            if (!target->HasAura(spell, bot->GetGUID()))
                return spell;

    if (uint32 spell = ReadySpell(bot, target, SPELL_INFERNAL))
        return spell;

    // Native execute eligibility is retained.
    if (target->HasAuraState(AURA_STATE_HEALTHLESS_35_PERCENT))
        if (uint32 spell = ReadySpell(bot, target, SPELL_TYRANTS_GAZE))
            return spell;

    // Keep the existing native Felfury threshold; never force a resource grant.
    if (bot->HasAura(AURA_AT_LEAST_2_FELFURY))
        for (uint32 root : {SPELL_RUIN, SPELL_FELWRATH, SPELL_SARGERON_SMITE})
            if (uint32 spell = ReadySpell(bot, target, root))
                return spell;

    for (uint32 root : {SPELL_FELHOOF_CHARGE, SPELL_FEL_FIREBALL, SPELL_MANABURN})
        if (uint32 spell = ReadySpell(bot, target, root))
            return spell;

    return 0;
}
}
