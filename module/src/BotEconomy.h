/*
 * mod-coa-playerbots
 *
 * Bot economy, step 1: deciding what a bot would sell, and why, without selling anything yet.
 *
 * A bot carries things it will never use again: grey junk, gear it has outgrown or cannot wear,
 * potions and food beyond what it needs, valuable finds. This layer looks into a bot's bags and
 * sorts every stack into Keep / Vendor / Auction by a fixed set of rules (BotEconomy.cpp), puts a
 * price on it (the auction houses' current lowest buyout when there is one, the vendor price
 * otherwise) and writes what it found to a journal the owner can read. For now it only plans: the
 * journal rows are marked as "not executed". Selling, travelling to a city and buying come later
 * and will reuse the same plan.
 *
 * The rules never touch what a bot needs: equipped gear, quest items, profession tools, bags,
 * trade goods (they go to the guild bank), recipes, keys, the hearthstone, anything the owner
 * protected with `.botcmd economy protect <item>`, and gear that is, or could become, an upgrade.
 */

#ifndef COA_PLAYERBOTS_BOT_ECONOMY_H
#define COA_PLAYERBOTS_BOT_ECONOMY_H

#include "Define.h"
#include <string>
#include <vector>

class ChatHandler;
class Player;
class WorldSession;

namespace BotEconomy
{
    enum class Verdict : uint8
    {
        Keep,
        Vendor,
        Auction,
    };

    // One stack (or several stacks of the same item, merged) the plan would sell.
    struct Line
    {
        uint32 entry = 0;
        uint32 count = 0;
        Verdict verdict = Verdict::Keep;
        char const* reason = "";
        // What one item is expected to fetch, and where that number comes from.
        uint32 unitCopper = 0;
        char const* priceSource = "";
        std::string name;

        uint64 TotalCopper() const { return uint64(unitCopper) * count; }
    };

    // Everything in the bot's bags that would be sold (Keep lines are left out). Read-only.
    std::vector<Line> PlanSale(Player* bot);

    // Called every world tick with the online bots: plans, a few bots at a time, and journals.
    void Update(uint32 diff, std::vector<WorldSession*> const& sessions);

    // Called for an idle, ungrouped bot before its other solo activities. Returns true while the bot is on a trip to
    // a city with an auction house (it walks or flies there, sells, and comes back to its normal life); the caller
    // then skips everything else this tick. Only active when CoaBots.Economy.DryRun = 0.
    bool UpdateTrip(Player* bot, uint32 diff);

    // "Collect gold" orders from the guild task board: a free guild-mate bot is asked to earn `goldAmount` gold for the
    // guild bank. It goes about its usual life (kills, quests and loot give it money, its trips to town turn its finds
    // into money sooner) and pays everything above its own reserve into the bank until the amount is in. 0 gold cancels
    // the guild's gold orders. The requester must be the guild master or an officer (not checked for a console caller).
    void PlaceGoldOrder(Player* requester, uint32 goldAmount, ChatHandler* handler);

    // The roster line for a bot with a gold order ("earning gold for the guild (12/100 g - 12%)"), empty otherwise.
    std::string GoldOrderTask(Player* bot);

    // Food and drink that cost something. With CoaBots.Economy.ConsumeSupplies = 1 a bot that sits down to eat or drink
    // really uses up one item of its level range (the same instant-regeneration auras as before, 433 and 431, are
    // applied); with none it just rests the slow way and wants a trip to town, where it buys more with its own money.
    // With the setting off this simply casts the aura, as bots always did.
    void StartRestAura(Player* bot, bool food);

    // Guild resource limits ("Stock" tab of the task board). The guild's officers say how many of a resource the guild bank
    // should keep; a free guild-mate bot takes the surplus out, carries it to a city with an auction house and sells it
    // (the money goes to the bank by the usual trip share). Only resources already in the bank are listed.
    //
    // GetStockLines: replies for GETSTOCK, `STOCK:<entry>,<count>,<limit>,<name>|...` (chunked by length).
    // SetStockLimit: keep >= 0 sets the limit, keep < 0 clears it. Guild master or officers only (not checked for console).
    std::vector<std::string> GetStockLines(Player* requester);
    void SetStockLimit(Player* requester, uint32 itemEntry, int64 keep, ChatHandler* handler);

    // `.botcmd economy ...` (plan <guid>, journal [n], status, protect <item>, unprotect <item>).
    void HandleCommand(ChatHandler* handler, std::string const& args);
}

#endif // COA_PLAYERBOTS_BOT_ECONOMY_H
