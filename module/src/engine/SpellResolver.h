/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpellResolver
 * Resolves root spell IDs against the current active learned spellbook.
 */

#ifndef COA_PLAYERBOTS_SPELL_RESOLVER_H
#define COA_PLAYERBOTS_SPELL_RESOLVER_H

#include "Define.h"
#include "ObjectGuid.h"

class Player;

namespace BotAI
{
    class SpellResolver
    {
    public:
        // Returns the highest currently active learned rank, including runtime changes.
        static uint32 ResolveSpell(Player* bot, uint32 rootSpellId);

        // Resolve the current native proc/form replacement without caching temporary state.
        static uint32 EffectiveSpell(Player* bot, uint32 learnedSpellId);
        static uint32 ResolveCastSpell(Player* bot, uint32 rootSpellId);

        // Lifecycle compatibility; live resolution needs no cache invalidation.
        static void Invalidate(ObjectGuid botGuid);

        // World-reload compatibility.
        static void ClearAll();
    };
}

#endif // COA_PLAYERBOTS_SPELL_RESOLVER_H
