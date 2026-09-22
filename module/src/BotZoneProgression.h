#ifndef BOT_ZONE_PROGRESSION_H
#define BOT_ZONE_PROGRESSION_H

#include "Common.h"
#include "SharedDefines.h"
#include <vector>

class Player;

namespace BotZoneProgression
{
    struct ZoneHub
    {
        char const* name;
        uint32 zoneId;
        uint32 mapId;
        float x;
        float y;
        float z;
        float o;
        TeamId team;       // TEAM_ALLIANCE, TEAM_HORDE, or TEAM_NEUTRAL
        uint8 minLevel;
        uint8 maxLevel;
    };

    // Returns the race's designated 1-10 starting hub (all coordinates from game_tele)
    ZoneHub const* GetStartingHubForRace(uint8 race);

    // Returns an appropriate hub for the given level and faction
    ZoneHub const* GetRandomHubForLevel(uint8 level, TeamId team);

    // Every hub a character of this level and race would already have passed through: its race's
    // starting hub, and each progression hub of its faction (or neutral) whose bracket starts at or
    // below its level. Used to give a fresh bot the flight paths it would have picked up.
    std::vector<ZoneHub const*> HubsUpToLevel(uint8 level, uint8 race);

    // Checks if the specified zone is appropriate for the bot's current level
    bool IsZoneAppropriateForLevel(uint32 zoneId, uint8 level);

    // Checks if any non-bot (real human) player is within radius yards
    bool IsRealPlayerNearby(Player* bot, float radius = 50.0f);

    // Relocates a solo bot to a level-appropriate zone hub
    bool RelocateBot(Player* bot, bool force = false);

    // Confirmed live: every pre-existing mismatched bot fires its relocation check on the same
    // first tick after a restart (or right after this feature's first deploy), and each
    // RelocateBot touches the CharacterDatabase (SetHomebind). With ~650 bots mismatched at once,
    // this burst competed with a real player's own async character-list query on the same
    // CharacterDatabase worker pool and delayed it by minutes. Queue the check instead of acting
    // on it immediately; ProcessPendingRelocations drains it at a deliberately slow pace (same
    // throttled-queue shape as BotSpawnRandom's spawn/auto-login batching) so a bulk correction
    // never competes with real player traffic again.
    void QueueRelocation(Player* bot);
    void ProcessPendingRelocations(uint32 diff);
}

#endif // BOT_ZONE_PROGRESSION_H
