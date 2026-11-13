#pragma once
#include "Define.h"
#define COA_SPELL_RANK_METADATA_EPOCH 1
namespace SpellRankLookup {
inline uint64 epoch = 1;
inline uint64 MetadataEpoch() { return epoch; }
inline void MetadataChanged() { ++epoch; }
}
