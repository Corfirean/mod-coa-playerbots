/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpecStrategyRegistry implementation
 */

#include "engine/SpecStrategyRegistry.h"
#include <vector>

namespace BotAI
{
    namespace
    {
        std::vector<SpecStrategy> s_strategies;
    }

    void SpecStrategyRegistry::RegisterStrategy(SpecStrategy strategy)
    {
        s_strategies.push_back(std::move(strategy));
    }

    SpecStrategy const* SpecStrategyRegistry::FindStrategy(uint8 classId, uint32 specId, BotRole role)
    {
        for (SpecStrategy const& s : s_strategies)
        {
            if (s.classId == classId && s.specId == specId && s.role == role)
                return &s;
        }
        return nullptr;
    }

    SpecStrategy const* SpecStrategyRegistry::FindStrategy(uint8 classId, uint32 specId)
    {
        for (SpecStrategy const& s : s_strategies)
        {
            if (s.classId == classId && s.specId == specId)
                return &s;
        }
        return nullptr;
    }

    bool SpecStrategyRegistry::HasStrategy(uint8 classId, uint32 specId, BotRole role)
    {
        return FindStrategy(classId, specId, role) != nullptr;
    }
}
