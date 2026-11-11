#pragma once
#include "DataMap.h"
#define COA_SPELL_BOOK_REVISION 1
namespace SpellBookRevision {
inline void EnableTracking() {}
inline uint64 Get(DataMap const& data) { return data.revision; }
inline void Changed(DataMap& data) { ++data.revision; }
}
