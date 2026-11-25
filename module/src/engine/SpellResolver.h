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
        static void SetRankLookupOptimization(bool enabled);
        static bool RankLookupOptimized();
        static void SetLearnedRankCache(bool enabled);
        static bool LearnedRankCacheEnabled();
        // Warcraft Reborn: when on, a root spell id resolves to the bot's Reborn version of it (id + 1100000) when it knows that, and
        // to the stock spell only otherwise. Reborn versions are the ones a Reborn player casts: their costs, cast times, stance
        // requirements and scripts are the realm's own, and the stock ones carry data (odd stance masks, other cooldowns) that is not.
        // Off by default: only a Reborn realm turns it on (CoaBots.Spells.PreferRebornTwins).
        static void SetPreferRebornTwins(bool enabled);
        static bool PreferRebornTwins();
        // Returns the highest currently active learned rank, including runtime changes.
        static uint32 ResolveSpell(Player* bot, uint32 rootSpellId);

        // Resolve the current native proc/form replacement without caching temporary state.
        static uint32 EffectiveSpell(Player* bot, uint32 learnedSpellId);
        static uint32 ResolveCastSpell(Player* bot, uint32 rootSpellId);

    private:
        // The resolution by the stock chain first, the Reborn twin as the fallback.
        static uint32 ResolveStockFirst(Player* bot, uint32 rootSpellId);
    public:

        // Core revisions invalidate membership; active ranks are always read live.
        static void Invalidate(ObjectGuid botGuid);

        // World-reload compatibility.
        static void ClearAll();
    };
}

#endif // COA_PLAYERBOTS_SPELL_RESOLVER_H
