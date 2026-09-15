/*
 * mod-coa-playerbots
 *
 * Necromancer (class 23) is architecturally a summon/pet class -- its real kit spans 7
 * dedicated mod-ascension-compat files (~2500 lines: AscensionNecromancer.cpp/Abilities/Auras/
 * Contracts/Events/Summons.cpp) covering which minion to raise and how it behaves, a
 * fundamentally different problem ("which pet to summon") than "which spell to cast" every
 * other rotation in this file set answers. This rotation deliberately does NOT attempt pet
 * management -- it only covers the one real player-cast direct-damage spell found, so a
 * Necromancer bot at least has *something* better than the class-agnostic generic fallback
 * while summoning/commanding pets remains unaddressed.
 *
 * Found via AscensionNecromancerData.h's own `NecromancerCoefficients` table -- a real,
 * pinned-client-data-generated list of (spellId, effect index, spell power/intellect/attack
 * power coefficients, school, isHealing) used by this module's own damage/healing formulas.
 * Unlike the DBC talent-table scans this session's other rotations relied on, this table is
 * curated by the module's own build tooling from real client contracts, so it's a much more
 * direct way to separate "real damage/heal spell" from "passive talent" for this specific
 * class -- filtering to non-healing, first-effect (index 0) entries and cross-checking against
 * a real Necromancer test character's own known spells (character_spell) surfaced "Lichfrost"
 * (ranks 501969-501980 plus 801722, all the same spell): a real SPELL_EFFECT_SCHOOL_DAMAGE
 * nuke, free cost, no cooldown, no precondition. Not every coefficient-table entry is a
 * direct player cast -- several (e.g. "Command: Skeletal Mage") are pet-command spells or
 * summon-linked scaling data, cross-referenced out via NecromancerSummons in the same header.
 *
 * Ice Barrage (803779) was also found (real SCHOOL_DAMAGE, PowerType 6/"Runic"-equivalent) but
 * not confirmed known by any available test character this pass -- included as a secondary
 * option since the real range/cost checks below make it safe to offer regardless (falls
 * through to 0 like everything else if the bot doesn't actually know it or can't afford it).
 */

#include "BotClassRotationsNecromancer.h"
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
// All known real ids for "Lichfrost" -- appears to be a multi-rank/multi-variant spell chain
// (NecromancerCoefficients lists it a dozen times with matching coefficients), not a single
// rank progression resolvable via GetLastSpellInChain. Try each the bot actually knows.
constexpr std::array<uint32, 13> LICHFROST_IDS = {
    501969, 501970, 501971, 501972, 501973, 501974,
    501975, 501976, 501977, 501978, 501979, 501980, 801722,
};

constexpr uint32 SPELL_ICE_BARRAGE = 803779;

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

uint32 SelectNecromancerRotationSpell(Player* bot, Unit* target)
{
    if (bot->getClass() != CLASS_NECROMANCER)
        return 0;

    for (uint32 spellId : LICHFROST_IDS)
        if (IsReady(bot, target, spellId))
            return spellId;

    if (IsReady(bot, target, SPELL_ICE_BARRAGE))
        return SPELL_ICE_BARRAGE;

    return 0;
}
}
