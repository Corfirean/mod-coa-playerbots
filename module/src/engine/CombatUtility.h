/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatUtility -- the Global Combat Utility Layer
 *
 * Taunt/Interrupt/Cleanse shouldn't depend on whether a class's Data-Driven profile happens to
 * tag AbilityTag::Taunt/Interrupt/Cleanse -- most don't yet (see the ProfileRegistry audit this
 * rework started from). This runs generically off the bot's own real, already-known spellbook
 * (via engine/SpellPredicates.h's shape predicates -- the same "what does a real cast-bar see"
 * approach the rest of this AI already uses), ahead of role rotation, so every class gets this
 * floor of behavior immediately. It does not replace a profile's own hand-authored Taunt/
 * Interrupt entries where they exist -- those still get evaluated normally by RoleEngine on any
 * tick this layer declines to act (see item 3/22 of the combat-engine rework).
 *
 * Scope note: "Emergency survival" and "Critical heal" (the top two tiers of item 3's own
 * priority sketch) are deliberately not implemented here yet -- there's no reliable generic way
 * to detect "a defensive cooldown" from raw SpellInfo shape the way taunt/interrupt/heal/dispel
 * can be (unlike those, a defensive CD's real signature is too varied and easy to false-positive
 * on unrelated buffs). Those need HealUrgencyScore (item 10) and Defensive Intelligence (item 19)
 * from Phase 2, not a generic spell-shape heuristic.
 */

#ifndef COA_PLAYERBOTS_COMBAT_UTILITY_H
#define COA_PLAYERBOTS_COMBAT_UTILITY_H

#include "Define.h"

class Player;

namespace BotAI
{
    struct CombatContext;

    class CombatUtility
    {
    public:
        // Tries taunt, then interrupt, then cleanse, in that order, against ctx. Returns true
        // (and sets nextCastAllowedMs) if it cast something this tick -- the caller should treat
        // that exactly like a role-engine cast (consume the tick, skip everything below it).
        // Returns false without touching nextCastAllowedMs when it declines to act (nothing to
        // do, already on the shared reaction gate, or mid-cast) -- the caller's own RoleEngine
        // gate remains the sole owner of decrementing it in that case.
        static bool Execute(Player* bot, CombatContext const& ctx, uint32 diff, uint32& nextCastAllowedMs);
    };
}

#endif // COA_PLAYERBOTS_COMBAT_UTILITY_H
