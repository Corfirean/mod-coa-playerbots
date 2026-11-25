#include "BotLevelBracketPolicy.h"
#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    using namespace BotLevelBracketPolicy;

    std::vector<Bracket> const defaultBrackets = {
        { 1, 20, 40 },
        { 21, 60, 33 },
        { 61, 79, 20 },
        { 80, 80, 7 }
    };

    // Cap 60: 61-79 and 80-80 must be excluded completely before weighted selection.
    std::vector<Bracket> cap60 = FilterForCap(defaultBrackets, 60);
    assert(cap60.size() == 2);
    assert(cap60[0].low == 1 && cap60[0].high == 20 && cap60[0].weight == 40);
    assert(cap60[1].low == 21 && cap60[1].high == 60 && cap60[1].weight == 33);

    // Cap 50: 21-60 is clipped to 21-50 (30/40 of 33 = 24), 61-79 and 80-80 excluded.
    std::vector<Bracket> cap50 = FilterForCap(defaultBrackets, 50);
    assert(cap50.size() == 2);
    assert(cap50[1].low == 21 && cap50[1].high == 50 && cap50[1].weight == 24);

    // Cap 80: all brackets retained unchanged.
    std::vector<Bracket> cap80 = FilterForCap(defaultBrackets, 80);
    assert(cap80.size() == 4);

    // Fallback when all brackets exceed cap.
    std::vector<Bracket> highOnly = { { 70, 80, 100 } };
    std::vector<Bracket> cap60Fallback = FilterForCap(highOnly, 60);
    assert(cap60Fallback.size() == 1);
    assert(cap60Fallback[0].low == 1 && cap60Fallback[0].high == 60);

    std::cout << "Level bracket policy: cap filtering, proportional clipping, and fallback passed.\n";
    return 0;
}
