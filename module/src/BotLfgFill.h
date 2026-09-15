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
// Registers a bot (already sent through sLFGMgr->JoinLfg by the caller) to be watched for its
// own LFG_STATE_PROPOSAL and auto-accepted the same way this file's own fill candidates are --
// without this, a bot that only ever joins through the debug `.botcmd joinlfg` entry point
// (not through FillRole) never gets its own proposal accepted, since nothing else is watching
// it, and a proposal only completes once *every* member (fill bots included) has accepted.
void WatchForProposal(Player* bot);
}

#endif // COA_PLAYERBOTS_BOT_LFG_FILL_H
