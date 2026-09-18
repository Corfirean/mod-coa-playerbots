/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpellResolver implementation
 */

#include "engine/SpellResolver.h"
#include "Player.h"
#include "SpellMgr.h"
#include <unordered_map>

namespace BotAI
{
    namespace
    {
        // [botGuid][rootSpellId] -> highestLearnedRank
        std::unordered_map<ObjectGuid, std::unordered_map<uint32, uint32>> s_resolvedCache;
    }

    uint32 SpellResolver::ResolveSpell(Player* bot, uint32 rootSpellId)
    {
        if (!bot || !rootSpellId)
            return 0;

        ObjectGuid guid = bot->GetGUID();
        auto botItr = s_resolvedCache.find(guid);
        if (botItr != s_resolvedCache.end())
        {
            auto spellItr = botItr->second.find(rootSpellId);
            if (spellItr != botItr->second.end())
                return spellItr->second;
        }

        PlayerSpellMap const& spellMap = bot->GetSpellMap();
        uint32 lastInChain = sSpellMgr->GetLastSpellInChain(rootSpellId);
        uint32 current = lastInChain ? lastInChain : rootSpellId;

        uint32 resolved = 0;
        while (current)
        {
            auto itr = spellMap.find(current);
            if (itr != spellMap.end() && itr->second && itr->second->Active && itr->second->State != PLAYERSPELL_REMOVED)
            {
                resolved = current;
                break;
            }

            uint32 prev = sSpellMgr->GetPrevSpellInChain(current);
            if (!prev || prev == current)
                break;
            current = prev;
        }

        if (!resolved && bot->HasSpell(rootSpellId))
        {
            auto itr = spellMap.find(rootSpellId);
            if (itr != spellMap.end() && itr->second && itr->second->Active && itr->second->State != PLAYERSPELL_REMOVED)
                resolved = rootSpellId;
        }

        s_resolvedCache[guid][rootSpellId] = resolved;
        return resolved;
    }

    void SpellResolver::Invalidate(ObjectGuid botGuid)
    {
        s_resolvedCache.erase(botGuid);
    }

    void SpellResolver::ClearAll()
    {
        s_resolvedCache.clear();
    }
}
