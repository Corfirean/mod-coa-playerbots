/*
 * mod-coa-playerbots
 *
 * Auto-fills both factions' normal (non-arena, non-rated) Battleground queues with bots the
 * moment anyone (real player or bot) joins one, so a match is always full on both sides and
 * pops immediately instead of waiting for real population. See BotBattlegroundFill.cpp's
 * header comment for the real engine flow this replicates.
 */

#ifndef COA_PLAYERBOTS_BOT_BATTLEGROUND_FILL_H
#define COA_PLAYERBOTS_BOT_BATTLEGROUND_FILL_H

#include "Define.h"

class ChatHandler;
class Player;

void AddSC_coa_bot_bg_fill_script();

namespace BotBGFill
{
// Debug/testing entry point (`.botcmd joinbg <guid> <bgTypeId>`): makes a bot solo-join a
// normal Battleground queue through the exact same real calls
// WorldSession::HandleBattlemasterJoinOpcode makes for a real client (see BotBattlegroundFill.cpp's
// header comment) -- there's no GM command for this already, since a real client only ever
// reaches it via a battlemaster NPC's gossip menu, which a synthetic ChatHandler can't drive.
// Ending with the real sScriptMgr->OnPlayerJoinBG(bot) call means this also naturally exercises
// the auto-fill hook, the same as a real player queuing would. Returns false (handler already
// told why) on any of the same real eligibility checks the opcode handler itself makes.
bool JoinBotToBattlegroundQueue(Player* bot, uint32 bgTypeId, ChatHandler* handler);
}

#endif // COA_PLAYERBOTS_BOT_BATTLEGROUND_FILL_H
