/*
 * mod-coa-playerbots
 *
 * Reaper (class 30) has an explicit, live-confirmed bug under the generic engine
 * (BotAI.cpp): "Reaperbot's resource-builder spells never actually kill anything" (see
 * AGENTS.md). Investigating why led to a more general finding worth recording here --
 * AscensionCoATalentData.h's raw (ClassId, SpecId, SpellIds) rows are NOT a reliable guide to
 * a class's real "press this to deal damage" attacks: scanning every Reaper spell in that
 * table's own real SpellInfo effect data (HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE) and friends,
 * the same predicate BotAI.cpp's own IsUsableOffensiveSpell uses) found real damage effects
 * on only 4 of ~140 entries. The rest are mod-ascension-compat's own custom mechanics --
 * dedicated per-class C++ files (AscensionReaperSoulStrike.cpp, AscensionReaperDirge.cpp,
 * AscensionReaperDeathwind.cpp, AscensionReaperPainmail.cpp, AscensionReaperReflexes.cpp) that
 * key off SpellFamilyName/SpellFamilyFlags and implement the actual behavior in a spell
 * script or proc handler, not in the spell's own native DBC effect fields. A DBC-only scan
 * (however careful) cannot find these; only reading that source directly can.
 *
 * Of Reaper's five custom mechanics, only two are a unit-targeted cast this AI can issue at
 * all:
 *  - Soul Strike (AscensionReaperSoulStrike.cpp): a real weapon-damage attack
 *    (SPELL_EFFECT_WEAPON_PERCENT_DAMAGE + SPELL_EFFECT_NORMALIZED_WEAPON_DMG), six ranks.
 *    Its own registered on-hit handler (HandleAscensionReaperSoulStrikeHit) heals the caster
 *    off a leech/missing-health formula automatically on every hit -- this rotation only
 *    needs to cast it; the sustain comes free from the server's own script.
 *  - Dirge (AscensionReaperDirge.cpp): a dual-wield finisher gated on
 *    SPELL_ATTR3_REQUIRES_OFF_HAND_WEAPON with a specific mainhand EquippedItemSubClassMask
 *    -- exactly the shape of ability that wakes the SPELL_FAILED_EQUIPPED_ITEM_CLASS bug
 *    documented in AGENTS.md if picked blind. Checked explicitly against
 *    Player::GetWeaponForAttack before ever being selected, so it never wakes that bug.
 *
 * The other three are deliberately NOT cast here:
 *  - Deathwind (AscensionReaperDeathwind.cpp): a persistent ground-targeted area leech aura
 *    (TARGET_UNIT_DEST_AREA_ENEMY) -- BotAI.cpp has no destination-targeted casting path yet,
 *    only unit-targeted (see SelectKnownSpell's own NeedsExplicitUnitTarget() filter).
 *  - Painmail (AscensionReaperPainmail.cpp): a passive proc that stacks a self-buff whenever
 *    an attack from a fixed list (which already includes every Soul Strike rank) lands --
 *    triggers automatically off the two casts above, nothing to actively cast.
 *  - Spiritual Reflexes (AscensionReaperReflexes.cpp): a passive dodge-triggered proc below
 *    35% health. Also nothing to actively cast.
 *
 * Two more real bugs caught live testing this against an actual Training Dummy (not a fleeing
 * critter -- see below), both now fixed here: this rotation used to return a candidate with no
 * regard for whether it was actually in range (SPELL_FAILED_OUT_OF_RANGE forever against a
 * target outside melee reach, since returning a spell here skips UpdateOffensive's own
 * melee-chase fallback) or affordable (Soul Strike costs a real 400 points of its own resource
 * -- PowerType 6, "Runic Power" per DBCStructure.h's own label, repurposed here for Reaper's
 * "Souls" -- and a bot that just entered combat starts near-empty on it, so every attempt
 * failed SPELL_FAILED_NO_POWER). Both are fixed below via the same real range/cost checks
 * SelectKnownSpell already performs in BotAI.cpp (SpellInfo::GetMaxRange/GetMinRange,
 * Player::CalcPowerCost) -- this rotation returns 0 rather than a doomed candidate when either
 * check fails, so the caller falls through to its own chase logic or the generic fallback.
 *
 * With both fixed, a still-unaffordable Soul Strike correctly falls through to generic
 * SelectSpell -- which, on the one live test run this far, picked a spell (573316) that itself
 * failed SPELL_FAILED_CASTER_AURASTATE (a precondition IsUsableOffensiveSpell doesn't check).
 * That's a separate, pre-existing gap in the *generic* engine, the same category as the
 * already-documented facing/weapon-class bugs -- not something a single class's rotation file
 * should try to fix, and not evidence of a problem with the Soul Strike/Dirge logic above.
 * Whether Reaper has a real, affordable-from-empty resource generator worth adding here as a
 * third priority tier is still an open question -- Reaperbot's own spellbook has thousands of
 * entries (many unrelated vanity/collection spells) and the obvious-looking "Soul Generator"
 * (520056) turned out to be a passive percent-modifier talent, not a cast. Left for whoever
 * picks this up next, with the DBC field indices below (verified against this repo's own
 * DBCStructure.h comments, not guessed) as a starting point: PowerType=41, ManaCost=42,
 * Effect[0..2]=71-73, EffectApplyAuraName[0..2]=95-97, SpellFamilyName=208.
 */

#include "BotClassRotationsReaper.h"
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
constexpr std::array<uint32, 6> SOUL_STRIKE_RANKS = {500517, 500518, 500519, 500520, 500521, 500646};
constexpr uint32 SPELL_DIRGE = 801328;

// Both of Reaper's real attacks are melee weapon swings with a real (short) min/max range of
// their own in Spell.dbc -- BotAI.cpp's own SelectKnownSpell checks this before ever returning
// a candidate, and callers rely on that (a spell offered here is assumed already in range, so
// UpdateOffensive casts it directly instead of chasing first). Skipping this check was a real
// bug caught live: Reaperbot kept trying Soul Strike from outside melee range and failing with
// SPELL_FAILED_OUT_OF_RANGE forever, because nothing ever told UpdateOffensive to chase in
// first. Mirrors SelectKnownSpell's own range check exactly.
bool InSpellRange(Player* bot, Unit* target, uint32 spellId)
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
    return true;
}

// Soul Strike costs a real 400 points of its own resource (PowerType 6 -- "Runic Power" by
// DBCStructure.h's own label, repurposed for Reaper's "Souls" bar) -- a second real bug caught
// live: a bot that just entered combat starts near-empty on this resource and every Soul Strike
// attempt failed with SPELL_FAILED_NO_POWER forever, the exact "resource-builder spells never
// actually kill anything" symptom this class was already known for (AGENTS.md). Uses the same
// real Player::CalcPowerCost SelectKnownSpell already relies on for this in BotAI.cpp, rather
// than hand-deriving the 400 figure -- so this stays correct if the real cost ever changes.
// Returns 0 (not affordable) so the caller falls through to whatever else it can afford
// instead of failing a cast outright -- generic SelectSpell can still find a resource-building
// attack the bot's own spellbook happens to have, same as before this rotation existed.
bool CanAffordSpell(Player* bot, SpellInfo const* spellInfo)
{
    int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
    return cost <= 0 || bot->GetPower(Powers(spellInfo->PowerType)) >= cost;
}

bool IsCastable(Player* bot, Unit* target, uint32 spellId)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    return spellInfo && InSpellRange(bot, target, spellId) && CanAffordSpell(bot, spellInfo);
}

// Ranks are listed low-to-high; returns the highest in-range, affordable one this bot both
// knows and currently has off cooldown, or (if every known castable rank is on cooldown right
// now) the highest known castable rank regardless -- same "still report a real candidate" shape
// SelectKnownSpell uses. Returns 0 if no known rank is both in range and affordable, so the
// caller falls through to its own melee-chase/generic-fallback logic instead of failing a cast
// outright.
uint32 HighestKnownReadySoulStrike(Player* bot, Unit* target)
{
    uint32 highestKnown = 0;
    for (uint32 spellId : SOUL_STRIKE_RANKS)
    {
        if (!bot->HasSpell(spellId) || !IsCastable(bot, target, spellId))
            continue;
        highestKnown = spellId;
        if (!bot->HasSpellCooldown(spellId))
            return spellId;
    }
    return highestKnown;
}
}

uint32 SelectReaperRotationSpell(Player* bot, Unit* target)
{
    if (bot->getClass() != CLASS_REAPER)
        return 0;

    if (bot->HasSpell(SPELL_DIRGE) && !bot->HasSpellCooldown(SPELL_DIRGE) &&
        IsCastable(bot, target, SPELL_DIRGE) &&
        bot->GetWeaponForAttack(BASE_ATTACK, true) && bot->GetWeaponForAttack(OFF_ATTACK, true))
        return SPELL_DIRGE;

    return HighestKnownReadySoulStrike(bot, target);
}
}
