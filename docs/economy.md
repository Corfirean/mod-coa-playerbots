# Bot economy

Design agreed with the owner on 2026-10-02. Steps 1-3 (rules, journal, dry run; trips to town and selling; the gold order) are in code; the rest is planned.

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

## Step 2 (in code): trips to town

Off by default: `CoaBots.Economy.DryRun = 1`. With `DryRun = 0` (and `Enable = 1`):

- The periodic scan marks a bot as wanting a trip when its plan is worth at least `MinTripCopper`. `BotEconomy::UpdateTrip`
  runs for an idle, ungrouped bot before its other solo activities (BotAI.cpp, `UpdateSoloWorld`), picks the nearest friendly
  auctioneer on its map from the POI index and sets off: a flight path when the destination is far and a route is known
  (`BotWorldBehavior::RequestTravel(..., teleportFallback = false)`), otherwise on foot (`BotMovement::Navigate`, mounted
  when the distance is long). The trip outranks ordinary walks (it releases a leftover quest/grind/gather/fish/ambient claim),
  but lets a requested flight run its course. Giving up: 25 minutes, death, a map change, `NavStatus::Stuck`.
- At the auctioneers (within 30 yd): collects money and returned items from the bot's auction mail, then for each planned line
  lists it (a deposit is paid, 24 h by default, buyout = plan price with a random 5-10 % spread, never below the vendor price
  plus 5 %, start bid 85 % of the buyout) or sells it at the vendor price. A listing worth less than `MinListingCopper` or
  netting less than a vendor sale goes to the vendor. Then the guild gets `GuildSharePercent` of what the trip earned, keeping
  `MoneyReserveCopper` for the bot. The bot is saved.
- Limits: `MaxTrips` bots on their way at once, `TripCooldownMin` between trips, `MaxListings` running auctions per bot.
- `.botcmd economy trips` lists the bots on their way and `status` shows the totals.

Checked in a sandbox (25-45 bots, a copy of a real server's database): bots walked, rode and flew to the cities, sold at the
vendor price and listed auctions that are stored correctly. Not checked: money and items coming back from an expired or sold
auction (the collection code follows the core's mail handlers), the guild share (no bot guild in the test), cross-continent
trips (a bot only goes to an auctioneer on its own map), the hearthstone.

## Step 3 (in code): the "collect gold" order

From the task board of the addon (a field and "Order" / "Cancel orders" buttons under the title; verb `GOLDORDER:<gold>`,
`0` cancels) or `.botcmd economy goldorder <player guid> <gold>`. A free online guild-mate bot is asked to earn that many gold
for the guild bank. It lives its usual life; money from kills, quests and loot, and from its trips to town (which start from a
smaller load while an order is open), is paid into the guild bank every minute, everything above `MoneyReserveCopper`, until the
amount is in. Its ROSTER task reads `earning gold for the guild (X/Y g - Z%)`. Only the guild master and officers (rank 0 or 1)
may place or cancel orders, checked on the server. Orders are stored in `mod_coa_bot_gold_orders` and survive a restart; a bot
that leaves the guild drops its order. A bot with a gold order does not also pay the 10 % trip share (it pays everything).

Checked in the sandbox: placing an order, the roster text, the deposit into the guild bank (5 g arrived and the order finished),
cancelling. The addon controls are written but not click-tested with a real client.

## Steps still to do

1. Buying consumables (food and potions of the bot's level, daily budget, never buying out players' listings).
2. The Stock tab and limits (guild master and officers only).
3. Statistics in the addon; relisting; the hearthstone for the way back.
