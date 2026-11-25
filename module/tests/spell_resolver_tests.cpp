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
    // Reborn realm: a root resolves to its Reborn version (id + 1100000) when known, to the stock spell otherwise.
    for (bool cached : {false, true})
    {
        BotAI::SpellResolver::SetLearnedRankCache(cached);
        BotAI::SpellResolver::SetRankLookupOptimization(false);
        BotAI::SpellResolver::SetPreferRebornTwins(true);
        assert(BotAI::SpellResolver::PreferRebornTwins());
        Player twinPlayer;
        PlayerSpell stock, twin;
        twinPlayer.book[900] = &stock;
        SpellBookRevision::Changed(twinPlayer.CustomData);
        assert(BotAI::SpellResolver::ResolveSpell(&twinPlayer, 900) == 900);          // only the stock spell known
        twinPlayer.book[1100900] = &twin;
        SpellBookRevision::Changed(twinPlayer.CustomData);
        assert(BotAI::SpellResolver::ResolveSpell(&twinPlayer, 900) == 1100900);      // both known: the Reborn one
        assert(BotAI::SpellResolver::ResolveSpell(&twinPlayer, 1100900) == 1100900);  // a Reborn root stays itself
        twin.Active = false;
        SpellBookRevision::Changed(twinPlayer.CustomData);
        assert(BotAI::SpellResolver::ResolveSpell(&twinPlayer, 900) == 900);          // the Reborn one inactive: the stock one
        twin.Active = true;
        BotAI::SpellResolver::SetPreferRebornTwins(false);
        SpellBookRevision::Changed(twinPlayer.CustomData);
        assert(BotAI::SpellResolver::ResolveSpell(&twinPlayer, 900) == 900);          // off: the stock one first, as before
        BotAI::SpellResolver::SetPreferRebornTwins(true);
        Player onlyTwin;
        onlyTwin.book[1100900] = &twin;
        SpellBookRevision::Changed(onlyTwin.CustomData);
        assert(BotAI::SpellResolver::ResolveSpell(&onlyTwin, 900) == 1100900);        // a bot with only the Reborn spell resolves it
    }
    BotAI::SpellResolver::SetPreferRebornTwins(false);

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
