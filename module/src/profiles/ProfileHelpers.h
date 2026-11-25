/*
 * mod-coa-playerbots
 *
 * Small helpers the class combat profiles share.
 */

#ifndef COA_PLAYERBOTS_PROFILE_HELPERS_H
#define COA_PLAYERBOTS_PROFILE_HELPERS_H

#include "Pet.h"
#include "Player.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include <initializer_list>

namespace BotAI
{
    // True when the bot carries an aura of any rank of one of these spells (each given by its first rank), stock or Reborn twin.
    // For buffs that replace one another (seals, aspects, armors, shields): a profile lists them with the same group and
    // casts only while none is up, instead of one descriptor undoing another's buff every tick.
    inline bool HasAuraOfAny(Unit const* bot, std::initializer_list<uint32> firstRanks)
    {
        constexpr uint32 REBORN_TWIN_OFFSET = 1100000;
        for (auto const& entry : bot->GetOwnedAuras())
        {
            uint32 first = sSpellMgr->GetFirstSpellInChain(entry.first);
            if (!first)
                first = entry.first;
            for (uint32 id : firstRanks)
                if (first == id || first == id + REBORN_TWIN_OFFSET || entry.first == id || entry.first == id + REBORN_TWIN_OFFSET)
                    return true;
        }
        return false;
    }
}

#endif // COA_PLAYERBOTS_PROFILE_HELPERS_H
