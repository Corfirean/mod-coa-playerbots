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

    // Finds a specId for the given ClassId that maps to the requested BotRole. If
    // preferredSpecId already maps to that role, returns it unchanged (so an explicit role
    // pick that already matches the bot's current spec never forces an unnecessary spec
    // switch). Otherwise returns the first matching spec found in the table, or 0 if this
    // class has no spec at all for that role (e.g. most classes have no Tank spec) --
    // callers must treat 0 as "not possible for this class," not "default/shared spec."
    uint32 FindSpecForRole(uint8 classId, BotRole role, uint32 preferredSpecId = 0);

    // Bitmask (1 << uint8(BotRole)) of every role this ClassId has at least one spec for,
    // always including Dps (specId 0, the shared/default tree, is always Dps regardless of
    // class). Used to tell a client-side UI which roles are worth offering for a given bot
    // without it needing its own copy of the spec/role table -- see docs/addon-protocol.md's
    // GETROLES verb.
    uint32 GetAvailableRolesMask(uint8 classId);
}

#endif // COA_PLAYERBOTS_CLASS_SPEC_ROLES_H
