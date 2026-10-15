#pragma once
#include "Define.h"
#include "SpellInfo.h"
class SpellMgr {
public:
    uint32 GetLastSpellInChain(uint32 root) const { return root == 100 ? 102 : root; }
    uint32 GetPrevSpellInChain(uint32 spell) const { return spell == 102 ? 101 : spell == 101 ? 100 : 0; }
    SpellInfo const* GetSpellInfo(uint32 spell) const { static SpellInfo info; return spell ? &info : nullptr; }
};
inline SpellMgr manager;
inline SpellMgr* sSpellMgr = &manager;
