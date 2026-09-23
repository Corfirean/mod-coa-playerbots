/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: BotAction
 */

#ifndef COA_PLAYERBOTS_BOT_ACTION_H
#define COA_PLAYERBOTS_BOT_ACTION_H

#include "Define.h"
#include "engine/AbilityDescriptor.h"

class Unit;

namespace BotAI
{
    struct BotAction
    {
        uint32 spellId = 0;
        // The profile-authored root spell id this resolved from (see SpellResolver) -- distinct
        // from spellId once rank resolution is involved. Internal throttle must key off this, not
        // the resolved rank, so a rank-up doesn't silently reset an ability's own throttle.
        uint32 rootSpellId = 0;
        Unit* target = nullptr;
        float score = -1.0f;
        AbilityTag tags = AbilityTag::None;
        char const* name = "";
        char const* reason = "";
        // Copied from the winning AbilityDescriptor so the caller can apply throttle to exactly
        // this one ability after a successful cast, instead of every throttled ability in the
        // profile.
        uint32 internalThrottleMs = 0;

        bool IsValid() const { return spellId != 0 && target != nullptr && score > 0.0f; }
    };
}

#endif // COA_PLAYERBOTS_BOT_ACTION_H
