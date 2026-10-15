#include "engine/SpellResolver.h"
#include "Player.h"
#include <cassert>
#include <iostream>
int main() {
    Player player;
    PlayerSpell root, middle, highest;
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 0);
    player.book[100] = &root;
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 100);
    player.book[101] = &middle;
    player.book[102] = &highest;
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 102);
    highest.Active = false;
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 101);
    middle.State = PLAYERSPELL_REMOVED;
    assert(BotAI::SpellResolver::ResolveSpell(&player, 100) == 100);
    player.replacement = 900;
    assert(BotAI::SpellResolver::ResolveCastSpell(&player, 100) == 900);
    player.replacement = 0;
    root.Active = false;
    assert(BotAI::SpellResolver::ResolveCastSpell(&player, 100) == 0);
    std::cout << "Live spellbook mutation regression passed\n";
}
