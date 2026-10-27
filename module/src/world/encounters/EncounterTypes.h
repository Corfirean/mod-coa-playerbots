/*
 * mod-coa-playerbots
 *
 * Dynamic Bot Encounters -- core types, state machine enums, roles, stop reasons,
 * configuration parameters, and telemetry counters.
 *
 * Header-only and core-independent (uses only Define.h and ObjectGuid.h) so that
 * standalone unit tests can exercise all state machine, eligibility, budget,
 * ownership, and memory rules without a running worldserver.
 */

#ifndef COA_PLAYERBOTS_ENCOUNTER_TYPES_H
#define COA_PLAYERBOTS_ENCOUNTER_TYPES_H

#include "Define.h"
#include "ObjectGuid.h"
#include <array>
#include <cstring>
#include <string>
#include <string_view>

enum class EncounterType : uint8
{
    None = 0,
    AssassinContract = 1,       // 7.1 Hostile opposite-faction stealth/stalker targeting a real player
    TheWatcher = 2,             // 7.2 Non-combat/semi-combat distant stalker observing a real player
    FactionSkirmish = 3,        // 7.3 2-5 Alliance vs 2-5 Horde bots fighting in contested territory
    PatrolAmbush = 4,           // 7.4 Patrol party ambushed by opposing faction bots along a route
    BountyHunter = 5,           // 7.5 Direct hunter dispatched after a real player's PvP bounty score
    Nemesis = 6,                // 7.6 Repeat rival bot with accumulated grudge history against a player
    RoadDuel = 7,               // 7.7 Friendly non-lethal duel between two similar-level bots near a road/hub
    Caravan = 8,                // 7.8 3-6 friendly bots travelling together between settlements/hubs
    Deserter = 9,               // 7.9 Lone weary bot resting off-route, uttering atmospheric lines
    HunterAndPrey = 10,         // 7.10 Open-world chase between two opposing bots (Prey flees, Hunter pursues)
    RivalAdventuringParty = 11, // 7.11 3-5 bots moving between quest areas/hubs and fighting mobs
    RareHunter = 12,            // 7.12 1-3 nearby bots converging on foot/mount to hunt a live rare mob
    MysteryEncounter = 13,      // 7.13 Rarest event: mysterious watcher leaves a rune/visual sign and departs
    Count
};

constexpr size_t ENCOUNTER_TYPE_COUNT = static_cast<size_t>(EncounterType::Count);

inline char const* EncounterTypeName(EncounterType type)
{
    switch (type)
    {
        case EncounterType::None:                  return "none";
        case EncounterType::AssassinContract:      return "assassin";
        case EncounterType::TheWatcher:            return "watcher";
        case EncounterType::FactionSkirmish:       return "skirmish";
        case EncounterType::PatrolAmbush:          return "ambush";
        case EncounterType::BountyHunter:          return "bounty";
        case EncounterType::Nemesis:               return "nemesis";
        case EncounterType::RoadDuel:              return "duel";
        case EncounterType::Caravan:               return "caravan";
        case EncounterType::Deserter:              return "deserter";
        case EncounterType::HunterAndPrey:         return "hunter_prey";
        case EncounterType::RivalAdventuringParty: return "rival_party";
        case EncounterType::RareHunter:            return "rare_hunter";
        case EncounterType::MysteryEncounter:      return "mystery";
        default:                                   return "unknown";
    }
}

inline EncounterType ParseEncounterType(std::string_view token)
{
    if (token == "assassin" || token == "assassin_contract")
        return EncounterType::AssassinContract;
    if (token == "watcher" || token == "the_watcher")
        return EncounterType::TheWatcher;
    if (token == "skirmish" || token == "faction_skirmish")
        return EncounterType::FactionSkirmish;
    if (token == "ambush" || token == "patrol_ambush")
        return EncounterType::PatrolAmbush;
    if (token == "bounty" || token == "bounty_hunter")
        return EncounterType::BountyHunter;
    if (token == "nemesis" || token == "nemesis_lite")
        return EncounterType::Nemesis;
    if (token == "duel" || token == "road_duel")
        return EncounterType::RoadDuel;
    if (token == "caravan" || token == "travelling_party")
        return EncounterType::Caravan;
    if (token == "deserter")
        return EncounterType::Deserter;
    if (token == "hunter" || token == "prey" || token == "hunter_prey" || token == "hunter_and_prey")
        return EncounterType::HunterAndPrey;
    if (token == "rival" || token == "rival_party" || token == "adventuring_party")
        return EncounterType::RivalAdventuringParty;
    if (token == "rare" || token == "rare_hunter")
        return EncounterType::RareHunter;
    if (token == "mystery" || token == "mystery_encounter")
        return EncounterType::MysteryEncounter;
    return EncounterType::None;
}

inline bool IsPlayerTargetedEncounter(EncounterType type)
{
    switch (type)
    {
        case EncounterType::AssassinContract:
        case EncounterType::TheWatcher:
        case EncounterType::BountyHunter:
        case EncounterType::Nemesis:
        case EncounterType::MysteryEncounter:
            return true;
        default:
            return false;
    }
}

inline bool IsHostilePlayerEncounter(EncounterType type)
{
    switch (type)
    {
        case EncounterType::AssassinContract:
        case EncounterType::BountyHunter:
        case EncounterType::Nemesis:
            return true;
        default:
            return false;
    }
}

inline bool IsPvPEncounter(EncounterType type)
{
    switch (type)
    {
        case EncounterType::AssassinContract:
        case EncounterType::FactionSkirmish:
        case EncounterType::PatrolAmbush:
        case EncounterType::BountyHunter:
        case EncounterType::Nemesis:
        case EncounterType::HunterAndPrey:
            return true;
        default:
            return false;
    }
}

enum class EncounterPhase : uint8
{
    Candidate = 0, // Initial selection & validation before claiming bots
    Prepare = 1,   // Moving into position, stalking, or converging on area
    Active = 2,    // Main execution (combat, duel, observation, caravan march, rune cast)
    Resolve = 3,   // Post-action cleanup, emotes (/cheer, /salute), disengage/retreat
    Cooldown = 4,  // Finalized; participants released back to WorldBrain, cooldown recorded
    Terminated = 5 // Aborted early or fully retired
};

inline char const* EncounterPhaseName(EncounterPhase phase)
{
    switch (phase)
    {
        case EncounterPhase::Candidate:  return "Candidate";
        case EncounterPhase::Prepare:    return "Prepare";
        case EncounterPhase::Active:     return "Active";
        case EncounterPhase::Resolve:    return "Resolve";
        case EncounterPhase::Cooldown:   return "Cooldown";
        case EncounterPhase::Terminated: return "Terminated";
        default:                         return "Unknown";
    }
}

enum class EncounterRole : uint8
{
    None = 0,
    Assassin = 1,
    Watcher = 2,
    SkirmisherAlliance = 3,
    SkirmisherHorde = 4,
    PatrolLeader = 5,
    PatrolMember = 6,
    Ambusher = 7,
    BountyHunter = 8,
    NemesisRival = 9,
    DuelistA = 10,
    DuelistB = 11,
    CaravanLeader = 12,
    CaravanMember = 13,
    Deserter = 14,
    Hunter = 15,
    Prey = 16,
    RivalLeader = 17,
    RivalMember = 18,
    RareHunter = 19,
    MysteryStranger = 20
};

inline char const* EncounterRoleName(EncounterRole role)
{
    switch (role)
    {
        case EncounterRole::None:               return "none";
        case EncounterRole::Assassin:           return "assassin";
        case EncounterRole::Watcher:            return "watcher";
        case EncounterRole::SkirmisherAlliance: return "skirmisher_alliance";
        case EncounterRole::SkirmisherHorde:    return "skirmisher_horde";
        case EncounterRole::PatrolLeader:       return "patrol_leader";
        case EncounterRole::PatrolMember:       return "patrol_member";
        case EncounterRole::Ambusher:           return "ambusher";
        case EncounterRole::BountyHunter:       return "bounty_hunter";
        case EncounterRole::NemesisRival:       return "nemesis";
        case EncounterRole::DuelistA:           return "duelist_a";
        case EncounterRole::DuelistB:           return "duelist_b";
        case EncounterRole::CaravanLeader:      return "caravan_leader";
        case EncounterRole::CaravanMember:      return "caravan_member";
        case EncounterRole::Deserter:           return "deserter";
        case EncounterRole::Hunter:             return "hunter";
        case EncounterRole::Prey:               return "prey";
        case EncounterRole::RivalLeader:        return "rival_leader";
        case EncounterRole::RivalMember:        return "rival_member";
        case EncounterRole::RareHunter:         return "rare_hunter";
        case EncounterRole::MysteryStranger:    return "mystery_stranger";
        default:                                return "unknown";
    }
}

enum class EncounterStopReason : uint8
{
    None = 0,
    Completed = 1,
    Timeout = 2,
    PlayerLogout = 3,
    BotLogoutOrDespawn = 4,
    ParticipantDeath = 5,
    MapChanged = 6,
    EnteredInstanceOrBG = 7,
    ManualBotCommand = 8,
    JoinedPlayerGroup = 9,
    Unreachable = 10,
    TooFar = 11,
    PvPEligibilityLost = 12,
    EnteredSafeZone = 13,
    PlayerInFlight = 14,
    PlayerGroupTooLarge = 15,
    CowardlyRetreat = 16,
    TargetRareDead = 17,
    AdminStopped = 18
};

inline char const* EncounterStopReasonName(EncounterStopReason reason)
{
    switch (reason)
    {
        case EncounterStopReason::None:                return "none";
        case EncounterStopReason::Completed:           return "completed";
        case EncounterStopReason::Timeout:             return "timeout";
        case EncounterStopReason::PlayerLogout:        return "player_logout";
        case EncounterStopReason::BotLogoutOrDespawn:  return "bot_logout";
        case EncounterStopReason::ParticipantDeath:    return "participant_death";
        case EncounterStopReason::MapChanged:          return "map_changed";
        case EncounterStopReason::EnteredInstanceOrBG: return "entered_instance_or_bg";
        case EncounterStopReason::ManualBotCommand:    return "manual_command";
        case EncounterStopReason::JoinedPlayerGroup:   return "joined_player_group";
        case EncounterStopReason::Unreachable:         return "unreachable";
        case EncounterStopReason::TooFar:              return "too_far";
        case EncounterStopReason::PvPEligibilityLost:  return "pvp_eligibility_lost";
        case EncounterStopReason::EnteredSafeZone:     return "entered_safe_zone";
        case EncounterStopReason::PlayerInFlight:      return "player_in_flight";
        case EncounterStopReason::PlayerGroupTooLarge: return "player_group_too_large";
        case EncounterStopReason::CowardlyRetreat:     return "cowardly_retreat";
        case EncounterStopReason::TargetRareDead:      return "target_rare_dead";
        case EncounterStopReason::AdminStopped:        return "admin_stopped";
        default:                                       return "unknown";
    }
}

inline bool IsSuccessfulStopReason(EncounterStopReason reason)
{
    return reason == EncounterStopReason::Completed ||
           reason == EncounterStopReason::CowardlyRetreat ||
           reason == EncounterStopReason::TargetRareDead;
}

struct EncounterConfig
{
    bool enabled = true;
    std::array<bool, ENCOUNTER_TYPE_COUNT> typeEnabled = {
        false, // None
        true,  // AssassinContract
        true,  // TheWatcher
        true,  // FactionSkirmish
        true,  // PatrolAmbush
        true,  // BountyHunter
        true,  // Nemesis
        true,  // RoadDuel
        true,  // Caravan
        true,  // Deserter
        true,  // HunterAndPrey
        true,  // RivalAdventuringParty
        true,  // RareHunter
        true   // MysteryEncounter
    };

    // Global & staggered evaluation intervals
    uint32 thinkIntervalMs = 5000;          // Director staggered evaluation cadence (5s)
    uint32 rareScanIntervalMs = 30000;      // Rare creature scan interval (30s)

    // Budget limits (conservative defaults)
    uint32 maxGlobalActive = 8;             // Maximum concurrent encounters across the whole world
    uint32 maxMapActive = 4;                // Maximum concurrent encounters on a single map
    uint32 maxPerPlayerActive = 1;          // Maximum concurrent encounters near/targeting one real player

    // Cooldowns (milliseconds)
    uint32 playerPersonalCooldownMs = 20 * 60 * 1000; // 20 min between personal encounters for a real player
    uint32 botCooldownMs = 15 * 60 * 1000;            // 15 min before the same bot can join another encounter
    uint32 globalTypeCooldownMs = 5 * 60 * 1000;      // 5 min default per-type cooldown
    uint32 bountyCooldownMs = 45 * 60 * 1000;         // 45 min long cooldown after a Bounty Hunter attempt
    uint32 mysteryCooldownMs = 60 * 60 * 1000;        // 60 min global cooldown for Mystery Encounter

    // Eligibility & PvP safety
    uint32 maxLevelDiff = 4;                // Max level difference for PvP/personal encounters
    uint32 maxPlayerPartySizeForSoloAssassin = 2; // Assassin aborts if target player has >2 party members
    float searchRadiusYards = 220.0f;       // Candidate search radius around player / area
    float maxChaseDistanceYards = 280.0f;   // Abort encounter if distance exceeds this limit
    float minCityDistanceYards = 160.0f;    // Minimum distance from capitals/sanctuaries for skirmishes

    // Participant counts
    uint32 skirmishMinPerSide = 2;
    uint32 skirmishMaxPerSide = 5;
    uint32 caravanMinMembers = 3;
    uint32 caravanMaxMembers = 6;
    uint32 rivalPartyMinMembers = 3;
    uint32 rivalPartyMaxMembers = 5;
    uint32 rareHunterMaxBots = 3;

    // Timeouts
    uint32 prepareTimeoutMs = 90 * 1000;    // 90s to converge / stalk / prepare
    uint32 activeTimeoutMs = 120 * 1000;    // 120s active execution budget
    uint32 resolveTimeoutMs = 10 * 1000;    // 10s post-encounter resolve/emote phase
    uint32 caravanActiveTimeoutMs = 240 * 1000; // 4 min travel window for caravans/rival parties

    // Narrator
    bool narratorEnabled = true;
    uint32 narratorPlayerCooldownMs = 3 * 60 * 1000; // 3 min between narrator lines to the same player
    uint32 narratorBotCooldownMs = 60 * 1000;        // 1 min between narrator lines from the same bot
    float narratorMaxDistanceYards = 55.0f;          // Contextual visibility range for atmospheric messages

    // Bounty & Nemesis-lite
    uint32 bountyKillScore = 35;            // Score added when a real player kills an enemy bot
    uint32 bountyThreshold = 100;           // Score needed to trigger Bounty Hunter (e.g. ~3 bot kills)
    uint32 bountyDecayPerMinute = 2;        // Passive notoriety decay per minute
    uint32 nemesisTtlMs = 2 * 60 * 60 * 1000; // 2 hours TTL for Nemesis memory before expiry
    uint32 nemesisDecayIntervalMs = 15 * 60 * 1000; // Decay 1 grudge point every 15 minutes
    uint32 nemesisMinGrudgeScore = 2;       // Minimum grudge score to trigger Nemesis behavior/lines

    // Mystery Encounter configuration
    uint32 mysteryChancePermil = 15;        // 1.5% chance when rolling personal encounter types
    uint32 mysterySpellVisualId = 0;        // Optional safe spell visual / cast ID (0 = built-in safe visual/emote)
    uint32 mysteryRuneGameObjectEntry = 0;  // Optional safe temporary GameObject entry (0 = none)
    uint32 mysteryMorphDisplayId = 0;       // Optional display ID override during Mystery Encounter (0 = keep outfit)

    bool IsTypeEnabled(EncounterType type) const
    {
        size_t idx = static_cast<size_t>(type);
        return enabled && idx > 0 && idx < ENCOUNTER_TYPE_COUNT && typeEnabled[idx];
    }
};

struct EncounterMetrics
{
    uint64 directorEvaluations = 0;
    uint32 lastEvaluationMs = 0;
    uint32 avgEvalIntervalMs = 0;

    // Performance & candidate telemetry (§24)
    uint32 lastUpdateDurationUs = 0;
    uint32 avgUpdateDurationUs = 0;
    uint32 peakUpdateDurationUs = 0;
    uint64 totalCandidatesVisited = 0;
    uint32 activeTemporaryGameObjects = 0;

    // Cleanup verification telemetry (§2, §3, §28)
    uint64 cleanupChecks = 0;
    uint64 cleanupPasses = 0;
    uint64 cleanupFailures = 0;

    uint64 totalCreated = 0;
    uint64 totalCompleted = 0;
    uint64 totalCancelled = 0;
    uint64 totalFailed = 0;

    std::array<uint64, ENCOUNTER_TYPE_COUNT> createdByType{};
    std::array<uint64, ENCOUNTER_TYPE_COUNT> completedByType{};
    std::array<uint64, ENCOUNTER_TYPE_COUNT> cancelledByType{};
    std::array<uint64, ENCOUNTER_TYPE_COUNT> failedByType{};
};

#endif // COA_PLAYERBOTS_ENCOUNTER_TYPES_H
