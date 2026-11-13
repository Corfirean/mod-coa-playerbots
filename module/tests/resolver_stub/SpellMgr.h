#pragma once
#include "Define.h"
#include "SpellInfo.h"
class SpellMgr {
public:
    uint32 lastRank = 102;
    mutable uint64 chainQueries = 0;
    uint32 GetLastSpellInChain(uint32 root) const { ++chainQueries; return root >= 100 && root <= 102 ? lastRank : root; }
    uint32 GetPrevSpellInChain(uint32 spell) const { ++chainQueries; return spell == 102 ? 101 : spell == 101 ? 100 : 0; }
    SpellInfo const* GetSpellInfo(uint32 spell) const {
        static SpellInfo root{100}, middle{101, &root}, highest{102, &middle}, standalone{900};
        root.last = middle.last = highest.last = lastRank == 102 ? &highest : &middle;
        return spell == 100 ? &root : spell == 101 ? &middle : spell == 102 ? &highest : spell == 900 ? &standalone : nullptr;
    }
};
inline SpellMgr manager;
inline SpellMgr* sSpellMgr = &manager;
