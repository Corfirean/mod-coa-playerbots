/*
 * mod-coa-playerbots
 *
 * Mapping between Ascension custom ClassId (12-32) + SpecId and BotRole (Dps/Tank/Healer).
 * Sourced from exiles-db community wiki scrape and exact spellId cross-referencing
 * against AscensionCoATalentData.h, live-validated against Ascension CoA core.
 */

#ifndef COA_PLAYERBOTS_CLASS_SPEC_ROLES_H
#define COA_PLAYERBOTS_CLASS_SPEC_ROLES_H

#include "Define.h"
#include "BotAI.h"

namespace BotAI
{
    // Returns the default BotRole for the given Ascension ClassId (12-32) and SpecId.
    // Returns BotRole::Dps for SpecId 0 (shared tree) or any unmapped/unknown spec.
    BotRole GetRoleForClassSpec(uint8 classId, uint32 specId);

    // Returns the human-readable spec name (e.g. "Vanguard", "Heretic", "Flameweaving")
    // for diagnostics/logging if known, or nullptr if unknown.
    char const* GetSpecName(uint8 classId, uint32 specId);
}

#endif // COA_PLAYERBOTS_CLASS_SPEC_ROLES_H
