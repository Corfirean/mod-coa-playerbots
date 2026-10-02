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

    // `.botcmd economy ...` (plan <guid>, journal [n], status, protect <item>, unprotect <item>).
    void HandleCommand(ChatHandler* handler, std::string const& args);
}

#endif // COA_PLAYERBOTS_BOT_ECONOMY_H
