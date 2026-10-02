# Bot economy

Design agreed with the owner on 2026-10-02. Step 1 (rules, journal, dry run) is in code; the rest is planned.

## Principles

- Bots look like players. They **walk, take flight paths or use the hearthstone**; no magical teleports for trade trips
  (`RequestTravel(..., teleportFallback = false)`).
- Selling at the auction happens only **in a city with an auction house**, without an NPC: the bot calls the auctioneer
  functions on the server. The cities come from the POI index (`PoiKind::Auctioneer`), not from a hard-coded zone list, so
  new capitals work automatically.
- A bot goes to town when what it carries is worth a trip or its bags are full, and does everything in one trip (auction,
  repair, vendor, mail). Trips of different bots are not synchronised; a crowd in a city is fine, a mass run at one moment is not.
- Bots keep their money and give a **small percentage** to the guild bank (planned default 10 %, with a reserve for
  consumables).
- Listing prices: the buyout is the lowest current buyout minus the undercut, with a random spread of 5-10 % so the market
  looks alive. The bid is set by the auction house; only the buyout is chosen. Never below an owner-set minimum.
- Buying potions and food: only items that fit the bot's level (the starting kit gives every bot level-1 food, even at 60+),
  within a daily budget per bot and per guild, never buying out players' listings.
- Guild resource limits ("Stock" tab in the addon): shows only resources already in the guild bank; the owner or an officer
  sets how many to keep; a free bot withdraws the surplus and sells it. Only the guild master and officers may set limits
  (checked on the server).
- Statistics in the addon: what the guild's bots earned per day.

## Step 1 (in code): `BotEconomy`

Looks into a bot's bags and sorts every stack into Keep / Vendor / Auction. Nothing is sold yet.

Never touched: equipped gear, quest items, profession tools, bags, trade goods (they go to the guild bank), recipes, keys,
ammo, glyphs, the hearthstone, items the owner protected (`.botcmd economy protect <item>`), gear that is or could become an
upgrade (same 5 % margin as the upgrade pass), potions/food the bot will grow into.

Planned for sale: grey junk (vendor); gear it cannot wear or that does not beat what it wears (auction when unbound and of
uncommon quality or better, else vendor); potions/food/bandages above `CoaBots.Economy.KeepConsumables` of its own level
range, or all of those far below its level (auction when listable); gems and other finds of uncommon quality or better.

Prices in the plan: the lowest buyout over all auction houses minus `UndercutPercent`, or twice the vendor price when nobody
sells it. The plan is written to `mod_coa_bot_trade_journal` (marked not executed) when a bot's plan changed and is worth at
least `MinTripCopper`.

Commands: `.botcmd economy [status | plan <bot guid> | journal [n] | protect <item> | unprotect <item>]`.

## Steps still to do

1. The "sell in town" need: walk or fly to an auctioneer city, list, guild percentage, the "collect gold" order.
2. Buying consumables.
3. The Stock tab and limits.
4. Listing prices with the 5-10 % spread, relisting, stats.
