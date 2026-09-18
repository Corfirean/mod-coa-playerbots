#include "BotZoneProgression.h"
#include "BotMgr.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include <algorithm>
#include <vector>

namespace BotZoneProgression
{
    // Real, verified coordinates pulled directly from acore_world.game_tele
    static constexpr ZoneHub STARTING_HUBS[] = {
        // Human (race 1): NorthshireValley
        { "NorthshireValley",    12,   0, -8921.09f,   -119.135f,  82.195f,  5.82878f, TEAM_ALLIANCE, 1, 10 },
        // Orc (race 2): ValleyOfTrials
        { "ValleyOfTrials",      14,   1,  -601.294f,  -4296.76f,  37.8115f, 1.65401f, TEAM_HORDE,    1, 10 },
        // Dwarf (race 3): ColdridgeValley
        { "ColdridgeValley",     1,    0, -6231.77f,    332.993f, 383.171f, 0.480178f, TEAM_ALLIANCE, 1, 10 },
        // Night Elf (race 4): Shadowglen
        { "Shadowglen",         141,   1, 10334.0f,     833.902f, 1326.11f, 3.62142f,  TEAM_ALLIANCE, 1, 10 },
        // Undead (race 5): Deathknell
        { "Deathknell",          85,   0,  1843.5f,    1590.0f,    93.2971f, 3.08757f, TEAM_HORDE,    1, 10 },
        // Tauren (race 6): CampNarache
        { "CampNarache",        215,   1, -2919.35f,   -264.535f,  53.6197f, 0.409027f, TEAM_HORDE,    1, 10 },
        // Gnome (race 7): ColdridgeValley
        { "ColdridgeValley",     1,    0, -6231.77f,    332.993f, 383.171f, 0.480178f, TEAM_ALLIANCE, 1, 10 },
        // Troll (race 8): ValleyOfTrials
        { "ValleyOfTrials",      14,   1,  -601.294f,  -4296.76f,  37.8115f, 1.65401f, TEAM_HORDE,    1, 10 },
        // Blood Elf (race 10): SunstriderIsle
        { "SunstriderIsle",    3430, 530, 10331.1f,   -6235.42f,   26.7759f, 1.94594f, TEAM_HORDE,    1, 10 },
        // Draenei (race 11): AmmenVale
        { "AmmenVale",         3524, 530, -4021.4f,  -13582.1f,    54.7153f, 2.06953f, TEAM_ALLIANCE, 1, 10 },
    };

    static constexpr ZoneHub PROGRESSION_HUBS[] = {
        // --- Bracket 2: Levels 10-20 ---
        { "Westfall",             40,   0, -10235.2f,   1222.47f,   43.6252f, 6.2427f,   TEAM_ALLIANCE, 10, 20 },
        { "Thelsamar",            38,   0,  -5352.54f, -2948.53f,  323.78f,   5.34258f,  TEAM_ALLIANCE, 10, 20 },
        { "Auberdine",           148,   1,   6501.4f,    481.607f,   6.27062f, 1.70033f, TEAM_ALLIANCE, 10, 20 },
        { "BloodmystIsle",      3525, 530,  -1993.62f, -11475.8f,   63.9657f, 5.29437f,  TEAM_ALLIANCE, 10, 20 },
        { "TheCrossroads",        17,   1,   -452.84f, -2650.76f,   95.5209f, 0.241081f, TEAM_HORDE,    10, 20 },
        { "TheSepulcher",        130,   0,    504.534f, 1539.08f,  129.502f,  1.35812f,  TEAM_HORDE,    10, 20 },
        { "Ghostlands",         3433, 530,   7360.86f, -6803.3f,    44.2942f, 5.83679f,  TEAM_HORDE,    10, 20 },

        // --- Bracket 3: Levels 20-30 ---
        { "Lakeshire",            44,   0,  -9266.59f, -2188.77f,   64.0892f, 2.10205f,  TEAM_ALLIANCE, 20, 30 },
        { "Darkshire",            10,   0, -10573.0f,  -1182.51f,   28.0148f, 0.309022f, TEAM_ALLIANCE, 20, 30 },
        { "MenethilHarbor",       11,   0,  -3769.32f,  -744.26f,    8.01027f, 1.95752f, TEAM_ALLIANCE, 20, 30 },
        { "TarrenMill",          267,   0,    -34.1467f, -923.366f, 54.5576f, 0.15019f,  TEAM_HORDE,    20, 30 },
        { "StonetalonMountains", 406,   1,   1570.92f,  1031.52f,  137.959f,  3.33006f,  TEAM_HORDE,    20, 30 },
        { "CampTaurajo",          17,   1,  -2363.11f, -1913.78f,   95.7829f, 0.165556f, TEAM_HORDE,    20, 30 },
        { "Astranaar",           331,   1,   2676.19f,  -422.905f, 107.123f,  0.648691f, TEAM_ALLIANCE, 20, 30 },
        { "SplintertreePost",    331,   1,   2270.94f, -2538.19f,   93.9198f, 0.060429f, TEAM_HORDE,    20, 30 },

        // --- Bracket 4: Levels 30-40 ---
        { "Southshore",          267,   0,   -853.221f, -533.529f,   9.98556f, 0.242866f, TEAM_ALLIANCE, 30, 40 },
        { "RefugePointe",         45,   0,  -1246.61f, -2529.32f,   20.6098f, 0.741709f, TEAM_ALLIANCE, 30, 40 },
        { "Hammerfall",           45,   0,   -941.007f, -3526.66f,  70.935f,  3.48668f,  TEAM_HORDE,    30, 40 },
        { "FreewindPost",        400,   1,  -5431.78f, -2449.38f,   89.2848f, 2.32854f,  TEAM_HORDE,    30, 40 },
        { "GromgolBaseCamp",      33,   0, -12388.9f,    172.578f,   2.83358f, 1.91753f, TEAM_HORDE,    30, 40 },
        { "Desolace",            405,   1,   -606.395f, 2211.75f,   92.9818f, 0.809746f, TEAM_NEUTRAL,  30, 40 },

        // --- Bracket 5: Levels 40-50 ---
        { "Gadgetzan",           440,   1,  -7177.15f, -3785.34f,    8.36981f, 6.10237f, TEAM_NEUTRAL,  40, 50 },
        { "CampMojache",         357,   1,  -4396.7f,    224.841f,  25.4136f, 4.93684f,  TEAM_HORDE,    40, 50 },
        { "ThoriumPoint",         51,   0,  -6506.47f, -1149.95f,  307.708f,  4.18256f,  TEAM_NEUTRAL,  40, 50 },
        { "BootyBay",             33,   0, -14297.2f,    530.993f,   8.77916f, 3.98863f, TEAM_NEUTRAL,  40, 50 },

        // --- Bracket 6: Levels 50-58 ---
        { "WesternPlaguelands",   28,   0,   1728.65f, -1602.25f,   63.429f,  1.6558f,   TEAM_NEUTRAL,  50, 58 },
        { "LightsHopeChapel",    139,   0,   2279.65f, -5310.01f,   87.0759f, 5.07618f,  TEAM_NEUTRAL,  50, 58 },
        { "UnGoroCrater",        490,   1,  -7943.22f, -2119.09f, -218.343f,  6.0727f,   TEAM_NEUTRAL,  50, 58 },
        { "Everlook",            618,   1,   6725.69f, -4619.44f,  720.909f,  4.66802f,  TEAM_NEUTRAL,  50, 58 },
        { "CenarionHold",       1377,   1,  -6818.09f,   733.814f,  41.5661f, 2.3082f,   TEAM_NEUTRAL,  50, 58 },

        // --- Bracket 7: Levels 58-68 (Outland, Map 530) ---
        { "HonorHold",          3483, 530,   -748.211f, 2681.52f,  100.35f,   5.7479f,   TEAM_ALLIANCE, 58, 63 },
        { "Thrallmar",          3483, 530,    156.251f, 2673.45f,   85.1587f, 0.382074f, TEAM_HORDE,    58, 63 },
        { "CenarionRefuge",     3521, 530,   -223.541f, 5487.99f,   23.2281f, 0.886755f, TEAM_NEUTRAL,  60, 64 },
        { "Shattrath",          3703, 530,  -1838.16f,  5301.79f,  -12.428f,  5.9517f,   TEAM_NEUTRAL,  62, 68 },
        { "Telaar",             3518, 530,  -2560.76f,  7300.72f,   13.9485f, 2.18422f,  TEAM_ALLIANCE, 64, 67 },
        { "Garadar",            3518, 530,  -1321.34f,  7239.12f,   32.7371f, 4.04169f,  TEAM_HORDE,    64, 67 },
        { "Area52",             3523, 530,   3043.33f,  3681.33f,  143.065f,  5.07464f,  TEAM_NEUTRAL,  65, 68 },

        // --- Bracket 8: Levels 68-80 (Northrend, Map 571) ---
        { "ValianceKeep",       3537, 571,   2213.95f,  5273.15f,   11.2565f, 5.89294f,  TEAM_ALLIANCE, 68, 72 },
        { "WarsongHold",        3537, 571,   2741.29f,  6097.16f,   76.9055f, 0.731543f, TEAM_HORDE,    68, 72 },
        { "Valgarde",            495, 571,    564.401f, -4944.94f,  18.5962f, 5.36544f,  TEAM_ALLIANCE, 68, 72 },
        { "VengeanceLanding",    495, 571,   1942.86f,  -6167.11f,  23.724f,  2.64258f,  TEAM_HORDE,    68, 72 },
        { "WintergardeKeep",      65, 571,   3682.71f,   -722.635f, 212.729f, 5.7991f,   TEAM_ALLIANCE, 71, 76 },
        { "AgmarsHammer",         65, 571,   3841.51f,   1534.04f,  89.7246f, 4.78105f,  TEAM_HORDE,    71, 76 },
        { "Dalaran",            4395, 571,   5807.98f,    588.487f, 660.94f,  1.66594f,  TEAM_NEUTRAL,  70, 80 },
    };

    ZoneHub const* GetStartingHubForRace(uint8 race)
    {
        for (ZoneHub const& hub : STARTING_HUBS)
        {
            switch (race)
            {
                case RACE_HUMAN:
                    if (hub.zoneId == 12) return &hub;
                    break;
                case RACE_ORC:
                case RACE_TROLL:
                    if (hub.zoneId == 14) return &hub;
                    break;
                case RACE_DWARF:
                case RACE_GNOME:
                    if (hub.zoneId == 1) return &hub;
                    break;
                case RACE_NIGHTELF:
                    if (hub.zoneId == 141) return &hub;
                    break;
                case RACE_UNDEAD_PLAYER:
                    if (hub.zoneId == 85) return &hub;
                    break;
                case RACE_TAUREN:
                    if (hub.zoneId == 215) return &hub;
                    break;
                case RACE_BLOODELF:
                    if (hub.zoneId == 3430) return &hub;
                    break;
                case RACE_DRAENEI:
                    if (hub.zoneId == 3524) return &hub;
                    break;
                default:
                    break;
            }
        }
        return &STARTING_HUBS[0]; // fallback
    }

    ZoneHub const* GetRandomHubForLevel(uint8 level, TeamId team)
    {
        if (level <= 10)
            return &STARTING_HUBS[0];

        std::vector<ZoneHub const*> candidates;
        candidates.reserve(8);

        for (ZoneHub const& hub : PROGRESSION_HUBS)
        {
            if (level >= hub.minLevel && level <= hub.maxLevel)
            {
                if (hub.team == TEAM_NEUTRAL || hub.team == team)
                    candidates.push_back(&hub);
            }
        }

        if (candidates.empty())
        {
            // If level > 80 or edge case, use Dalaran or high level hub
            if (level >= 70)
                return &PROGRESSION_HUBS[sizeof(PROGRESSION_HUBS)/sizeof(PROGRESSION_HUBS[0]) - 1]; // Dalaran
            return nullptr;
        }

        return candidates[urand(0, candidates.size() - 1)];
    }

    bool IsZoneAppropriateForLevel(uint32 zoneId, uint8 level)
    {
        // Confirmed live: zone 876 (GM Island) matched no deny-list below and fell through to
        // the permissive "no explicit hub entry" default, so bots parked there (leftover from
        // being cloned off a hand-made test character -- see BuildClassTemplateRoster) never
        // got relocated regardless of level. Not a real content zone at any level.
        if (zoneId == 876)
            return false;

        // Major faction capital cities are always permissible for all levels
        switch (zoneId)
        {
            case 1519: // Stormwind City
            case 1537: // Ironforge
            case 1657: // Darnassus
            case 3557: // The Exodar
            case 1637: // Orgrimmar
            case 1638: // Thunder Bluff
            case 1497: // Undercity
            case 3487: // Silvermoon City
                return true;
            default:
                break;
        }

        // Bracket 1: 1-10 Starter areas
        if (level <= 10)
        {
            switch (zoneId)
            {
                case 12:   // Elwynn Forest
                case 1:    // Dun Morogh
                case 141:  // Teldrassil
                case 3524: // Azuremyst Isle
                case 14:   // Durotar
                case 85:   // Tirisfal Glades
                case 215:  // Mulgore
                case 3430: // Eversong Woods
                    return true;
                default:
                    return false;
            }
        }

        // Dalaran (Northrend) is level 70-80
        if (zoneId == 4395 && level < 70)
            return false;

        // Eastern Plaguelands is level 53-60
        if (zoneId == 139 && level < 48)
            return false;

        // Western Plaguelands is level 50-58
        if (zoneId == 28 && level < 45)
            return false;

        // Durotar / Elwynn / Dun Morogh / Tirisfal / Mulgore / Teldrassil are 1-10 starter zones
        // Bots above level 20 should not be wandering in level 1-10 starting zones
        if (level > 20)
        {
            switch (zoneId)
            {
                case 14:   // Durotar
                case 12:   // Elwynn Forest
                case 1:    // Dun Morogh
                case 85:   // Tirisfal Glades
                case 215:  // Mulgore
                case 141:  // Teldrassil
                case 3524: // Azuremyst Isle
                case 3430: // Eversong Woods
                    return false;
                default:
                    break;
            }
        }

        // Outland zones (Map 530) are 58+
        if (level < 58)
        {
            switch (zoneId)
            {
                case 3483: // Hellfire Peninsula
                case 3521: // Zangarmarsh
                case 3519: // Terokkar Forest
                case 3703: // Shattrath City
                case 3518: // Nagrand
                case 3522: // Blade's Edge Mountains
                case 3523: // Netherstorm
                case 3520: // Shadowmoon Valley
                    return false;
                default:
                    break;
            }
        }

        // Northrend zones (Map 571) are 68+
        if (level < 68)
        {
            switch (zoneId)
            {
                case 3537: // Borean Tundra
                case 495:  // Howling Fjord
                case 65:   // Dragonblight
                case 394:  // Grizzly Hills
                case 66:   // Zul'Drak
                case 3711: // Sholazar Basin
                case 67:   // The Storm Peaks
                case 210:  // Icecrown
                case 2817: // Crystalsong Forest
                case 4395: // Dalaran
                    return false;
                default:
                    break;
            }
        }

        // Check if any matching hub covers this zone in its level range
        for (ZoneHub const& hub : PROGRESSION_HUBS)
        {
            if (hub.zoneId == zoneId)
            {
                // Give a generous +/- 4 level buffer around hub ranges
                if (level + 4 >= hub.minLevel && level <= hub.maxLevel + 4)
                    return true;
            }
        }

        // If zone has no explicit hub entry, allow it as long as it didn't fail earlier constraints
        return true;
    }

    bool IsRealPlayerNearby(Player* bot, float radius)
    {
        if (!bot || !bot->IsInWorld())
            return false;

        std::list<Player*> players;
        Acore::AnyPlayerInObjectRangeCheck checker(bot, radius, false);
        Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(bot, players, checker);
        Cell::VisitObjects(bot, searcher, radius);

        for (Player* p : players)
        {
            if (p == bot)
                continue;

            // If account is not a bot account, it is a real player!
            if (!BotMgr::IsBotAccountId(p->GetSession()->GetAccountId()))
                return true;
        }

        return false;
    }

    bool RelocateBot(Player* bot, bool force)
    {
        if (!bot || !bot->IsInWorld() || !bot->IsAlive())
            return false;

        // Do not relocate if bot is in combat
        if (bot->IsInCombat())
            return false;

        // Do not relocate from dungeons, raids, or battlegrounds
        Map const* map = bot->GetMap();
        if (!map || map->IsDungeon() || map->IsBattleground())
            return false;

        // Do not relocate grouped bots (they follow their group leader)
        if (bot->GetGroup())
            return false;

        // Do not stack teleports if already mid-teleport
        if (bot->IsBeingTeleportedNear() || bot->IsBeingTeleportedFar())
            return false;

        // Unless forced (e.g. at bot creation), guard against teleporting while a real player is nearby
        if (!force && IsRealPlayerNearby(bot, 50.0f))
            return false;

        uint8 level = bot->GetLevel();
        TeamId team = Player::TeamIdForRace(bot->getRace());

        ZoneHub const* dest = nullptr;
        if (level <= 10)
            dest = GetStartingHubForRace(bot->getRace());
        else
            dest = GetRandomHubForLevel(level, team);

        if (!dest)
            return false;

        // Stop current movement
        bot->StopMoving();
        bot->GetMotionMaster()->Clear();

        LOG_INFO("module.coa-playerbots", "BotZoneProgression: relocating bot '{}' (level {}, race {}) from map {} zone {} to {} (map {}, x {:.1f}, y {:.1f}, z {:.1f}, zone {}).",
            bot->GetName(), level, uint32(bot->getRace()), bot->GetMapId(), bot->GetZoneId(), dest->name, dest->mapId, dest->x, dest->y, dest->z, dest->zoneId);

        // Update homebind with real destination WorldLocation and real areaId
        bot->SetHomebind(WorldLocation(dest->mapId, dest->x, dest->y, dest->z, dest->o), dest->zoneId);

        // Request teleport and queue the ack for the next tick
        bot->TeleportTo(dest->mapId, dest->x, dest->y, dest->z, dest->o);
        sBotMgr->QueueTeleportAck(bot->GetSession());

        return true;
    }

    namespace
    {
        std::vector<ObjectGuid> g_pendingRelocations;
        uint32 g_relocationThrottleMs = 0;
        constexpr uint32 RELOCATION_BATCH_SIZE = 3;
        constexpr uint32 RELOCATION_INTERVAL_MS = 500;
    }

    void QueueRelocation(Player* bot)
    {
        if (!bot)
            return;

        ObjectGuid guid = bot->GetGUID();
        if (std::find(g_pendingRelocations.begin(), g_pendingRelocations.end(), guid) != g_pendingRelocations.end())
            return;

        g_pendingRelocations.push_back(guid);
    }

    void ProcessPendingRelocations(uint32 diff)
    {
        if (g_pendingRelocations.empty())
            return;

        if (g_relocationThrottleMs > diff)
        {
            g_relocationThrottleMs -= diff;
            return;
        }
        g_relocationThrottleMs = RELOCATION_INTERVAL_MS;

        uint32 processed = 0;
        while (processed < RELOCATION_BATCH_SIZE && !g_pendingRelocations.empty())
        {
            ObjectGuid guid = g_pendingRelocations.back();
            g_pendingRelocations.pop_back();
            ++processed;

            if (Player* bot = ObjectAccessor::FindPlayer(guid))
                RelocateBot(bot);
            // A bot that's gone offline/despawned since queuing is simply skipped -- nothing to
            // relocate, and it'll re-queue itself (or not need to) the next time it logs in.
        }
    }
}
