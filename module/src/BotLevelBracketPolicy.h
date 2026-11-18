/*
 * mod-coa-playerbots
 *
 * Level bracket filtering and cap normalization (PB-003):
 * Brackets strictly above MaxPlayerLevel are excluded BEFORE weighted selection,
 * and partially overlapping brackets are clipped with proportional weight scaling
 * so bots do not pile up on the level cap.
 */

#ifndef COA_PLAYERBOTS_BOT_LEVEL_BRACKET_POLICY_H
#define COA_PLAYERBOTS_BOT_LEVEL_BRACKET_POLICY_H

#include <algorithm>
#include <cstdint>
#include <vector>

namespace BotLevelBracketPolicy
{
    struct Bracket
    {
        uint8_t low;
        uint8_t high;
        uint32_t weight;
    };

    inline std::vector<Bracket> FilterForCap(std::vector<Bracket> const& raw, uint8_t maxLevel)
    {
        if (maxLevel == 0)
            maxLevel = 1;

        std::vector<Bracket> result;
        result.reserve(raw.size());

        for (Bracket const& b : raw)
        {
            if (b.weight == 0 || b.low == 0 || b.low > b.high || b.low > maxLevel)
                continue;

            uint8_t const clippedHigh = std::min<uint8_t>(b.high, maxLevel);
            uint32_t const oldSpan = uint32_t(b.high - b.low) + 1u;
            uint32_t const newSpan = uint32_t(clippedHigh - b.low) + 1u;
            uint32_t scaledWeight = b.weight;
            if (newSpan < oldSpan)
                scaledWeight = std::max<uint32_t>(1u, (b.weight * newSpan) / oldSpan);

            result.push_back({ b.low, clippedHigh, scaledWeight });
        }

        if (result.empty())
            result.push_back({ 1, maxLevel, 1 });

        return result;
    }
}

#endif
