#pragma once
#include "ObjectGuid.h"
#include <unordered_map>
enum { PLAYERSPELL_REMOVED = 2 };
struct PlayerSpell { bool Active = true; int State = 0; };
using PlayerSpellMap = std::unordered_map<uint32, PlayerSpell*>;
class Player {
public:
    PlayerSpellMap book;
    uint32 replacement = 0;
    ObjectGuid GetGUID() const { return ObjectGuid(17); }
    PlayerSpellMap const& GetSpellMap() const { return book; }
    bool HasSpell(uint32 spell) const { return book.count(spell); }
    uint32 GetTemporarySpellReplacement(uint32) const { return replacement; }
};
