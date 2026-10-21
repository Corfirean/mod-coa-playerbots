#include "BotGearPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace BotGearPolicy;
    assert(!FitsLevel(5, 0, 174, 3));
    assert(!FitsLevel(5, 1, 435, 2));
    assert(FitsLevel(5, 5, 12, 2));
    assert(!FitsLevel(5, 6, 12, 2));
    assert(!FitsLevel(5, 5, 12, 3));
    assert(!FitsLevel(0, 0, 1, 1));
    assert(!FitsLevel(80, 80, 0, 4));
    assert(!FitsLevel(80, 80, 813, 4));
    for (unsigned level = 1; level <= 80; ++level)
    {
        assert(FitsLevel(level, level, MaxItemLevel(level), MaxQuality(level)));
        assert(!FitsLevel(level, level, MaxItemLevel(level) + 1, 1));
        assert(!FitsLevel(level, level + 1, 1, 1));
    }
    assert(PvpPower(101715) == 15);
    assert(PvePower(101615) == 15);
    assert(PvpPower(101615) == 0);
    assert(PvePower(101715) == 0);
    assert(PvpPower(9930957) == 10);
    std::cout << "Gear policy: reported low-level gear regression and all 80 level boundaries passed.\n";
}
