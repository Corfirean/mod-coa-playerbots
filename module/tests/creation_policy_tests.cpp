#include "BotCreationPolicy.h"
#include <cassert>
#include <cstdint>
#include <iostream>

int main()
{
    using namespace BotCreationPolicy;

    // 1. Implicit system creation is unconditionally forbidden.
    for (bool hasHandler : { false, true })
    {
        for (bool inBatch : { false, true })
        {
            for (bool enabled : { false, true })
            {
                Decision d = Evaluate(Source::ImplicitSystem, hasHandler, inBatch, enabled);
                assert(!d.allowed);
            }
        }
    }

    // 2. Explicit admin creation requires botsEnabled and either a ChatHandler or explicit batch context.
    assert(!Evaluate(Source::ExplicitAdminCommand, false, false, true).allowed);
    assert(!Evaluate(Source::ExplicitAdminCommand, true, false, false).allowed);
    assert(Evaluate(Source::ExplicitAdminCommand, true, false, true).allowed);
    assert(Evaluate(Source::ExplicitAdminCommand, false, true, true).allowed);

    // 3. Training arena creation requires botsEnabled and an explicit handler.
    assert(!Evaluate(Source::TrainingArena, false, false, true).allowed);
    assert(!Evaluate(Source::TrainingArena, true, false, false).allowed);
    assert(Evaluate(Source::TrainingArena, true, false, true).allowed);

    // 4. Empty-pool regression: 1000 LFG requests + 1000 BG requests must create 0 accounts and 0 characters.
    uint32_t accountsCreated = 0;
    uint32_t charactersCreated = 0;
    auto simulateQueueFillRequest = [&](Source source)
    {
        Decision d = Evaluate(source, false, false, true);
        if (d.allowed)
        {
            ++accountsCreated;
            ++charactersCreated;
        }
    };

    for (int i = 0; i < 1000; ++i)
        simulateQueueFillRequest(Source::ImplicitSystem); // LFG empty pool

    for (int i = 0; i < 1000; ++i)
        simulateQueueFillRequest(Source::ImplicitSystem); // BG empty pool

    assert(accountsCreated == 0);
    assert(charactersCreated == 0);

    std::cout << "Creation policy: implicit creation blocked across 1000 LFG + 1000 BG empty-pool requests (0 accounts, 0 chars).\n";
    return 0;
}
