/*
 * mod-coa-playerbots
 *
 * Auto-fills a solo player's Dungeon Finder queue with bots for whichever of
 * Tank/Healer/3xDamage their own role selection doesn't already cover, so a 5-man group is
 * ready the moment they queue instead of waiting on real population. See BotLfgFill.cpp's
 * header comment for the real engine flow this reuses and its scope limits.
 */

#ifndef COA_PLAYERBOTS_BOT_LFG_FILL_H
#define COA_PLAYERBOTS_BOT_LFG_FILL_H

#include "Define.h"

class Player;

void AddSC_coa_bot_lfg_fill_script();

namespace BotLfgFill
{
void WatchForProposal(Player* bot);

struct RealMemberRole
{
    uint8 roles{0}; // Bitmask of lfg::PLAYER_ROLE_TANK, PLAYER_ROLE_HEALER, PLAYER_ROLE_DAMAGE
};

struct RoleAssignmentResult
{
    bool hasTank{false};
    bool hasHealer{false};
    uint32 realDpsCount{0};
    uint32 tankBotsNeeded{0};
    uint32 healerBotsNeeded{0};
    uint32 dpsBotsNeeded{0};
};

RoleAssignmentResult SolvePartyRoles(std::vector<RealMemberRole> const& members);
}

#endif // COA_PLAYERBOTS_BOT_LFG_FILL_H
