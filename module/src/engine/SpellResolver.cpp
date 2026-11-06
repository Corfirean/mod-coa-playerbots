/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpellResolver implementation
 */

#include "engine/SpellResolver.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellInfo.h"

namespace BotAI
{
    uint32 SpellResolver::ResolveSpell(Player* bot, uint32 rootSpellId)
    {
        if (!bot || !rootSpellId)
            return 0;

        // Native talents, temporary grants and specialization changes can mutate
        // active learned ranks without a level change. Walk the short native rank
        // chain against the live book rather than retaining a stale rank or zero.
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

        // Support for Warcraft Reborn spell twins (1100000 offset) and crossovers
        constexpr uint32 REBORN_SPELL_OFFSET = 1100000;
        if (!resolved)
        {
            uint32 twin = rootSpellId >= REBORN_SPELL_OFFSET ? rootSpellId - REBORN_SPELL_OFFSET : rootSpellId + REBORN_SPELL_OFFSET;
            auto itr = spellMap.find(twin);
            if (itr != spellMap.end() && itr->second && itr->second->Active && itr->second->State != PLAYERSPELL_REMOVED)
            {
                resolved = twin;
            }
            else
            {
                uint32 twinLast = sSpellMgr->GetLastSpellInChain(twin);
                uint32 tCurrent = twinLast ? twinLast : twin;
                while (tCurrent)
                {
                    auto tItr = spellMap.find(tCurrent);
                    if (tItr != spellMap.end() && tItr->second && tItr->second->Active && tItr->second->State != PLAYERSPELL_REMOVED)
                    {
                        resolved = tCurrent;
                        break;
                    }

                    uint32 tPrev = sSpellMgr->GetPrevSpellInChain(tCurrent);
                    if (!tPrev || tPrev == tCurrent)
                        break;
                    tCurrent = tPrev;
                }
            }
        }

        return resolved;
    }

    uint32 SpellResolver::EffectiveSpell(Player* bot, uint32 learnedSpellId)
    {
        if (!bot || !learnedSpellId)
            return 0;
        uint32 replacement = bot->GetTemporarySpellReplacement(learnedSpellId);
        return replacement && sSpellMgr->GetSpellInfo(replacement) ? replacement : learnedSpellId;
    }

    uint32 SpellResolver::ResolveCastSpell(Player* bot, uint32 rootSpellId)
    {
        return EffectiveSpell(bot, ResolveSpell(bot, rootSpellId));
    }

    void SpellResolver::Invalidate(ObjectGuid /*botGuid*/)
    {
        // Compatibility for lifecycle callers: resolution now reads the live book.
    }

    void SpellResolver::ClearAll()
    {
    }
}
