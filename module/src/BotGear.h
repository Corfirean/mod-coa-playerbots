#ifndef COA_BOT_GEAR_H
#define COA_BOT_GEAR_H

#include "Define.h"
#include <string>

class Player;
class Item;
class ChatHandler;
struct ItemTemplate;

namespace BotGear
{
bool Ready();
bool Allowed(Player* bot, ItemTemplate const* item);
bool AllowedAtLevel(uint8 level, ItemTemplate const* item);
float PowerScore(Player* bot, ItemTemplate const* item);
bool PreserveSlot(Player* bot, uint8 slot);
bool Archived(Item const* item);
void Queue(Player* bot);
void Command(ChatHandler* handler, std::string const& args);
void Update(uint32 diff);
}
#endif
