/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpellResolver
 * Resolves root spell IDs to the highest learned rank with per-bot caching.
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
        // Returns the highest learned rank of rootSpellId for this bot (cached).
        static uint32 ResolveSpell(Player* bot, uint32 rootSpellId);

        // Clears cached spell ranks for a specific bot (e.g. on level up or spell learn).
        static void Invalidate(ObjectGuid botGuid);

        // Clears all caches (e.g. on world reload).
        static void ClearAll();
    };
}

#endif // COA_PLAYERBOTS_SPELL_RESOLVER_H
