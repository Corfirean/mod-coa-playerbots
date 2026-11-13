#pragma once
#include "Define.h"
struct SpellInfo {
    uint32 Id = 0;
    SpellInfo const* previous = nullptr;
    SpellInfo const* last = nullptr;
    SpellInfo const* GetLastRankSpell() const { return last; }
    SpellInfo const* GetPrevRankSpell() const { return previous; }
};
