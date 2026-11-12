/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpellResolver implementation
 */

#include "engine/SpellResolver.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#if __has_include("SpellBookRevision.h") && __has_include("SpellRankLookup.h")
#include "SpellBookRevision.h"
#include "SpellRankLookup.h"
#if defined(COA_SPELL_BOOK_REVISION) && defined(COA_SPELL_RANK_METADATA_EPOCH)
#define COA_LEARNED_RANK_CACHE 1
#endif
#endif
#include <unordered_map>
#include <vector>

namespace BotAI
{
    namespace
    {
        bool optimizeRankLookup = false;
        bool cacheLearnedRanks = false;
#ifdef COA_LEARNED_RANK_CACHE
        struct RankChainEntry
        {
            static constexpr uint8 MAX_INLINE_RANKS = 16;
            uint8 count = 0;
            uint32 ids[MAX_INLINE_RANKS]{};
            std::vector<uint32> overflow;

            void push_back(uint32 id)
            {
                if (count < MAX_INLINE_RANKS)
                    ids[count++] = id;
                else
                    overflow.push_back(id);
            }
        };

        struct LearnedRanks;
        thread_local Player const* tlsCachedBot = nullptr;
        thread_local LearnedRanks* tlsCachedRanks = nullptr;

        struct LearnedRanks : DataMap::Base
        {
            uint64 book = 0, metadata = 0;
            std::unordered_map<uint32, RankChainEntry> roots;

            LearnedRanks() { roots.reserve(64); }
            ~LearnedRanks() override
            {
                if (tlsCachedRanks == this)
                {
                    tlsCachedBot = nullptr;
                    tlsCachedRanks = nullptr;
                }
            }
        };

        uint32 ResolveCached(Player* bot, uint32 root)
        {
            LearnedRanks* cache = (tlsCachedBot == bot && tlsCachedRanks)
                ? tlsCachedRanks
                : bot->CustomData.GetDefault<LearnedRanks>("bot_rank_cache");
            tlsCachedBot = bot;
            tlsCachedRanks = cache;
            uint64 book = SpellBookRevision::Get(bot->CustomData);
            uint64 metadata = SpellRankLookup::MetadataEpoch();
            if (cache->book != book || cache->metadata != metadata)
            {
                cache->roots.clear();
                cache->book = book;
                cache->metadata = metadata;
            }
            if (cache->roots.size() >= 256 && !cache->roots.count(root)) cache->roots.clear();
            auto [itr, fresh] = cache->roots.try_emplace(root);
            auto const& spells = bot->GetSpellMap();
            if (fresh)
            {
                uint32 current = sSpellMgr->GetLastSpellInChain(root);
                if (!current) current = root;
                while (current)
                {
                    if (spells.count(current)) itr->second.push_back(current);
                    uint32 prev = sSpellMgr->GetPrevSpellInChain(current);
                    if (!prev || prev == current) break;
                    current = prev;
                }
            }
            for (uint8 i = 0; i < itr->second.count; ++i)
            {
                uint32 id = itr->second.ids[i];
                auto learned = spells.find(id);
                if (learned != spells.end() && learned->second && learned->second->Active && learned->second->State != PLAYERSPELL_REMOVED)
                    return id;
            }
            for (uint32 id : itr->second.overflow)
            {
                auto learned = spells.find(id);
                if (learned != spells.end() && learned->second && learned->second->Active && learned->second->State != PLAYERSPELL_REMOVED)
                    return id;
            }
            auto fallback = spells.find(root);
            return fallback != spells.end() && fallback->second && fallback->second->Active &&
                fallback->second->State != PLAYERSPELL_REMOVED && bot->HasSpell(root) ? root : 0;
        }
#endif
    }

    void SpellResolver::SetRankLookupOptimization(bool enabled) { optimizeRankLookup = enabled; }
    bool SpellResolver::RankLookupOptimized() { return optimizeRankLookup; }
    void SpellResolver::SetLearnedRankCache(bool enabled)
    {
#ifdef COA_LEARNED_RANK_CACHE
        cacheLearnedRanks = enabled;
        if (enabled) SpellBookRevision::EnableTracking();
#else
        static_cast<void>(enabled);
        cacheLearnedRanks = false;
#endif
    }
    bool SpellResolver::LearnedRankCacheEnabled() { return cacheLearnedRanks; }

    uint32 SpellResolver::ResolveSpell(Player* bot, uint32 rootSpellId)
    {
        if (!bot || !rootSpellId)
            return 0;
#ifdef COA_LEARNED_RANK_CACHE
        if (cacheLearnedRanks) return ResolveCached(bot, rootSpellId);
#endif

        // Native talents, temporary grants and specialization changes can mutate
        // active learned ranks without a level change. Walk the short native rank
        // chain against the live book rather than retaining a stale rank or zero.
        PlayerSpellMap const& spellMap = bot->GetSpellMap();
        SpellInfo const* currentInfo = nullptr;
        uint32 current = rootSpellId;
        if (optimizeRankLookup)
        {
            currentInfo = sSpellMgr->GetSpellInfo(rootSpellId);
            if (currentInfo)
            {
                if (SpellInfo const* last = currentInfo->GetLastRankSpell()) currentInfo = last;
                current = currentInfo->Id;
            }
        }
        else
        {
            uint32 lastInChain = sSpellMgr->GetLastSpellInChain(rootSpellId);
            current = lastInChain ? lastInChain : rootSpellId;
        }

        uint32 resolved = 0;
        while (current)
        {
            auto itr = spellMap.find(current);
            if (itr != spellMap.end() && itr->second && itr->second->Active && itr->second->State != PLAYERSPELL_REMOVED)
            {
                resolved = current;
                break;
            }

            uint32 prev = 0;
            if (optimizeRankLookup)
            {
                currentInfo = currentInfo ? currentInfo->GetPrevRankSpell() : nullptr;
                if (currentInfo) prev = currentInfo->Id;
            }
            else prev = sSpellMgr->GetPrevSpellInChain(current);
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
        // Core revisions cover structural changes; active flags are checked on every decision.
    }

    void SpellResolver::ClearAll()
    {
#ifdef COA_LEARNED_RANK_CACHE
        SpellRankLookup::MetadataChanged();
#endif
    }
}
