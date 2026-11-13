#include "engine/SpellResolver.h"
#include "Player.h"
#include "SpellMgr.h"
#include "SpellBookRevision.h"
#include "SpellRankLookup.h"
#include <cassert>
#include <iostream>

int main()
{
    for (bool cached : {false, true})
        for (bool optimized : {false, true})
        {
            BotAI::SpellResolver::SetLearnedRankCache(cached);
            BotAI::SpellResolver::SetRankLookupOptimization(optimized);
            assert(BotAI::SpellResolver::LearnedRankCacheEnabled() == cached);
            Player player;
            PlayerSpell root, middle, highest;
            assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 0);
            player.book[100] = &root;
            SpellBookRevision::Changed(player.CustomData);
            assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 100);
            player.book[101] = &middle;
            player.book[102] = &highest;
            SpellBookRevision::Changed(player.CustomData);
            assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 102);
            assert(BotAI::SpellResolver::ResolveSpell(&player, 101) == 102);
            highest.Active = false;
            assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 101);
            middle.State = PLAYERSPELL_REMOVED;
            assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 100);
            player.replacement = 900;
            assert(BotAI::SpellResolver::ResolveCastSpell(&player, 100) == 900);
            player.replacement = 0;
            root.Active = false;
            assert(BotAI::SpellResolver::ResolveCastSpell(&player, 100) == 0);
            player.book[900] = &highest;
            highest.Active = true;
            highest.State = 0;
            SpellBookRevision::Changed(player.CustomData);
            assert(BotAI::SpellResolver::ResolveSpell(&player, 900) == 900);
            assert(BotAI::SpellResolver::ResolveSpell(&player, 999) == 0);
            player.book[999] = &highest;
            SpellBookRevision::Changed(player.CustomData);
            assert(BotAI::SpellResolver::ResolveSpell(&player, 999) == 999);
            assert(BotAI::SpellResolver::ResolveSpell(nullptr, 100) == 0);
            assert(BotAI::SpellResolver::ResolveSpell(&player, 0) == 0);
        }
    BotAI::SpellResolver::SetLearnedRankCache(true);
    Player player;
    PlayerSpell learned;
    player.book[100] = &learned;
    SpellBookRevision::Changed(player.CustomData);
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 100);
    auto queries = manager.chainQueries;
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 100);
    assert(manager.chainQueries == queries);
    player.book.erase(100);
    player.book[102] = &learned;
    SpellBookRevision::Changed(player.CustomData);
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 102);
    learned.Active = false;
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 0);
    learned.Active = true;
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 102);
    manager.lastRank = 101;
    SpellRankLookup::MetadataChanged();
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 0);
    manager.lastRank = 102;
    BotAI::SpellResolver::ClearAll();
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 102);
    for (uint32 id = 2000; id < 2260; ++id)
        assert(BotAI::SpellResolver::ResolveSpell(&player, id) == 0);
    queries = manager.chainQueries;
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 102);
    assert(manager.chainQueries > queries);
    std::cout << "Live spellbook mutation and learned rank cache regressions passed\n";
}
