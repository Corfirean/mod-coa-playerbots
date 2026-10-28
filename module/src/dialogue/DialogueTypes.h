/*
 * mod-coa-playerbots
 *
 * Bot Dialogue System - Semantic Beats, Locales, Channels, and Priorities.
 */

#ifndef COA_PLAYERBOTS_DIALOGUE_TYPES_H
#define COA_PLAYERBOTS_DIALOGUE_TYPES_H

#include "Define.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

// Supported dialogue locales matching Corfirean/coa-server-manager:
// "en", "ru", "de", "fr", "es", "zh"
enum class DialogueLocale : uint8
{
    EnUS = 0, // "en" / "enUS" (Default Fallback)
    RuRU,     // "ru" / "ruRU"
    DeDE,     // "de" / "deDE"
    FrFR,     // "fr" / "frFR"
    EsES,     // "es" / "esES" / "esMX"
    ZhCN,     // "zh" / "zhCN" / "zhTW"
    Count
};

enum class DialogueChannel : uint8
{
    Say = 0,
    Yell,
    Party,
    Whisper,
    Emote,
    PrivateNarrator
};

enum class DialoguePriority : uint8
{
    Ambient = 0,
    Social = 1,
    Downtime = 2,
    Normal = 2,
    CombatLow = 3,
    CombatHigh = 4,
    High = 4,
    CombatCritical = 5,
    Critical = 5,
    EncounterCinematic = 6
};

enum class DialogueBeat : uint32
{
    None = 0,

    // Social (§2)
    Greeting,
    Farewell,
    Thanks,
    Apology,
    Joke,
    Complaint,
    Impatient,
    Encourage,
    Taunt,
    CombatShout = Taunt,
    Victory,
    Defeat,
    Retreat,

    // Player-Specific (§2, §10)
    RecognizePlayer,
    NemesisGreeting,
    NemesisThreat,
    NemesisTaunt = NemesisThreat,
    NemesisVictory,
    NemesisDefeat,
    NemesisRevenge,
    NemesisConfidence,
    NemesisRecurring,
    BountyTargetFound,
    AssassinPreAttack,
    AssassinEscape,

    // Travel (§2)
    StartJourney,
    LongJourney,
    Lost,
    Regroup,
    WaitForGroup,
    Arrived,
    DangerAhead,

    // Resource / Downtime (§2, §17, §18, §19, §28, §29)
    NeedMana,
    NeedDrink,
    NeedFood,
    NeedRest,
    NeedHeal,
    LowHealth,
    LowMana,
    ReadyAgain,
    WaitingForCooldown,
    LeaderWaitAck,
    LeaderProceed,

    // Combat (§2, §20, §22)
    Pulling,
    AggroWarning,
    FocusTarget,
    KillAdds,
    Interrupt,
    Dispel,
    SpreadOut,
    StackTogether,
    MoveFromGround,
    RunAway,
    Defensive,
    HealNeeded,
    TankInDanger,
    TargetSwitch,
    EnemyCaster,
    EnemyHealer,
    CrowdControl,
    BurstNow,
    RetreatCombat,

    // Encounter-Specific (§2)
    WatcherObserved,
    WatcherSpotted,
    MysteryWhisper,
    MysteryRune,
    CaravanTravel,
    CaravanAttack,
    RivalPartyTravel,
    RivalPartyCombat,
    HunterThreat,
    PreyPanic,
    SkirmishCharge,
    AmbushReady,
    AmbushTriggered,
    RoadDuelChallenge,
    RoadDuelVictory,

    // Mystery Shath'Yar (§32, §33)
    MysteryShathYarWhisper,
    MysteryShathYarThreat,
    MysteryShathYarIncantation,
    MysteryShathYarDeparture
};

namespace DialogueUtils
{
    inline std::string_view LocaleToCode(DialogueLocale loc)
    {
        switch (loc)
        {
            case DialogueLocale::EnUS: return "enUS";
            case DialogueLocale::RuRU: return "ruRU";
            case DialogueLocale::DeDE: return "deDE";
            case DialogueLocale::FrFR: return "frFR";
            case DialogueLocale::EsES: return "esES";
            case DialogueLocale::ZhCN: return "zhCN";
            default:                   return "enUS";
        }
    }

    inline std::string_view LocaleToShortCode(DialogueLocale loc)
    {
        switch (loc)
        {
            case DialogueLocale::EnUS: return "en";
            case DialogueLocale::RuRU: return "ru";
            case DialogueLocale::DeDE: return "de";
            case DialogueLocale::FrFR: return "fr";
            case DialogueLocale::EsES: return "es";
            case DialogueLocale::ZhCN: return "zh";
            default:                   return "en";
        }
    }

    inline DialogueLocale NormalizeLocale(std::string_view locStr)
    {
        if (locStr.empty())
            return DialogueLocale::EnUS;

        if (locStr == "ru" || locStr == "ruRU" || locStr == "ru-RU" || locStr == "ru_RU")
            return DialogueLocale::RuRU;
        if (locStr == "de" || locStr == "deDE" || locStr == "de-DE" || locStr == "de_DE")
            return DialogueLocale::DeDE;
        if (locStr == "fr" || locStr == "frFR" || locStr == "fr-FR" || locStr == "fr_FR")
            return DialogueLocale::FrFR;
        if (locStr == "es" || locStr == "esES" || locStr == "es-ES" || locStr == "es_ES" || locStr == "esMX")
            return DialogueLocale::EsES;
        if (locStr == "zh" || locStr == "zhCN" || locStr == "zh-CN" || locStr == "zh_CN" || locStr == "zhTW")
            return DialogueLocale::ZhCN;
        return DialogueLocale::EnUS;
    }

    inline std::string_view BeatToString(DialogueBeat beat)
    {
        switch (beat)
        {
            case DialogueBeat::Greeting: return "Greeting";
            case DialogueBeat::Farewell: return "Farewell";
            case DialogueBeat::Thanks: return "Thanks";
            case DialogueBeat::Apology: return "Apology";
            case DialogueBeat::Joke: return "Joke";
            case DialogueBeat::Complaint: return "Complaint";
            case DialogueBeat::Impatient: return "Impatient";
            case DialogueBeat::Encourage: return "Encourage";
            case DialogueBeat::Taunt: return "Taunt";
            case DialogueBeat::Victory: return "Victory";
            case DialogueBeat::Defeat: return "Defeat";
            case DialogueBeat::Retreat: return "Retreat";

            case DialogueBeat::RecognizePlayer: return "RecognizePlayer";
            case DialogueBeat::NemesisGreeting: return "NemesisGreeting";
            case DialogueBeat::NemesisThreat: return "NemesisThreat";
            case DialogueBeat::NemesisVictory: return "NemesisVictory";
            case DialogueBeat::NemesisDefeat: return "NemesisDefeat";
            case DialogueBeat::NemesisRevenge: return "NemesisRevenge";
            case DialogueBeat::NemesisConfidence: return "NemesisConfidence";
            case DialogueBeat::NemesisRecurring: return "NemesisRecurring";
            case DialogueBeat::BountyTargetFound: return "BountyTargetFound";
            case DialogueBeat::AssassinPreAttack: return "AssassinPreAttack";
            case DialogueBeat::AssassinEscape: return "AssassinEscape";

            case DialogueBeat::StartJourney: return "StartJourney";
            case DialogueBeat::LongJourney: return "LongJourney";
            case DialogueBeat::Lost: return "Lost";
            case DialogueBeat::Regroup: return "Regroup";
            case DialogueBeat::WaitForGroup: return "WaitForGroup";
            case DialogueBeat::Arrived: return "Arrived";
            case DialogueBeat::DangerAhead: return "DangerAhead";

            case DialogueBeat::NeedMana: return "NeedMana";
            case DialogueBeat::NeedDrink: return "NeedDrink";
            case DialogueBeat::NeedFood: return "NeedFood";
            case DialogueBeat::NeedRest: return "NeedRest";
            case DialogueBeat::NeedHeal: return "NeedHeal";
            case DialogueBeat::LowHealth: return "LowHealth";
            case DialogueBeat::LowMana: return "LowMana";
            case DialogueBeat::ReadyAgain: return "ReadyAgain";
            case DialogueBeat::WaitingForCooldown: return "WaitingForCooldown";
            case DialogueBeat::LeaderWaitAck: return "LeaderWaitAck";
            case DialogueBeat::LeaderProceed: return "LeaderProceed";

            case DialogueBeat::Pulling: return "Pulling";
            case DialogueBeat::AggroWarning: return "AggroWarning";
            case DialogueBeat::FocusTarget: return "FocusTarget";
            case DialogueBeat::KillAdds: return "KillAdds";
            case DialogueBeat::Interrupt: return "Interrupt";
            case DialogueBeat::Dispel: return "Dispel";
            case DialogueBeat::SpreadOut: return "SpreadOut";
            case DialogueBeat::StackTogether: return "StackTogether";
            case DialogueBeat::MoveFromGround: return "MoveFromGround";
            case DialogueBeat::RunAway: return "RunAway";
            case DialogueBeat::Defensive: return "Defensive";
            case DialogueBeat::HealNeeded: return "HealNeeded";
            case DialogueBeat::TankInDanger: return "TankInDanger";
            case DialogueBeat::TargetSwitch: return "TargetSwitch";
            case DialogueBeat::EnemyCaster: return "EnemyCaster";
            case DialogueBeat::EnemyHealer: return "EnemyHealer";
            case DialogueBeat::CrowdControl: return "CrowdControl";
            case DialogueBeat::BurstNow: return "BurstNow";
            case DialogueBeat::RetreatCombat: return "RetreatCombat";

            case DialogueBeat::WatcherObserved: return "WatcherObserved";
            case DialogueBeat::WatcherSpotted: return "WatcherSpotted";
            case DialogueBeat::MysteryWhisper: return "MysteryWhisper";
            case DialogueBeat::MysteryRune: return "MysteryRune";
            case DialogueBeat::CaravanTravel: return "CaravanTravel";
            case DialogueBeat::CaravanAttack: return "CaravanAttack";
            case DialogueBeat::RivalPartyTravel: return "RivalPartyTravel";
            case DialogueBeat::RivalPartyCombat: return "RivalPartyCombat";
            case DialogueBeat::HunterThreat: return "HunterThreat";
            case DialogueBeat::PreyPanic: return "PreyPanic";
            case DialogueBeat::SkirmishCharge: return "SkirmishCharge";
            case DialogueBeat::AmbushReady: return "AmbushReady";
            case DialogueBeat::AmbushTriggered: return "AmbushTriggered";
            case DialogueBeat::RoadDuelChallenge: return "RoadDuelChallenge";
            case DialogueBeat::RoadDuelVictory: return "RoadDuelVictory";

            case DialogueBeat::MysteryShathYarWhisper: return "MysteryShathYarWhisper";
            case DialogueBeat::MysteryShathYarThreat: return "MysteryShathYarThreat";
            case DialogueBeat::MysteryShathYarIncantation: return "MysteryShathYarIncantation";
            case DialogueBeat::MysteryShathYarDeparture: return "MysteryShathYarDeparture";
            default: return "None";
        }
    }

    inline DialogueBeat StringToBeat(std::string_view str)
    {
        if (str == "Greeting") return DialogueBeat::Greeting;
        if (str == "Farewell") return DialogueBeat::Farewell;
        if (str == "Thanks") return DialogueBeat::Thanks;
        if (str == "Apology") return DialogueBeat::Apology;
        if (str == "Joke") return DialogueBeat::Joke;
        if (str == "Complaint") return DialogueBeat::Complaint;
        if (str == "Impatient") return DialogueBeat::Impatient;
        if (str == "Encourage") return DialogueBeat::Encourage;
        if (str == "Taunt") return DialogueBeat::Taunt;
        if (str == "Victory") return DialogueBeat::Victory;
        if (str == "Defeat") return DialogueBeat::Defeat;
        if (str == "Retreat") return DialogueBeat::Retreat;

        if (str == "RecognizePlayer") return DialogueBeat::RecognizePlayer;
        if (str == "NemesisGreeting") return DialogueBeat::NemesisGreeting;
        if (str == "NemesisThreat") return DialogueBeat::NemesisThreat;
        if (str == "NemesisVictory") return DialogueBeat::NemesisVictory;
        if (str == "NemesisDefeat") return DialogueBeat::NemesisDefeat;
        if (str == "NemesisRevenge") return DialogueBeat::NemesisRevenge;
        if (str == "NemesisConfidence") return DialogueBeat::NemesisConfidence;
        if (str == "NemesisRecurring") return DialogueBeat::NemesisRecurring;
        if (str == "BountyTargetFound") return DialogueBeat::BountyTargetFound;
        if (str == "AssassinPreAttack") return DialogueBeat::AssassinPreAttack;
        if (str == "AssassinEscape") return DialogueBeat::AssassinEscape;

        if (str == "StartJourney") return DialogueBeat::StartJourney;
        if (str == "LongJourney") return DialogueBeat::LongJourney;
        if (str == "Lost") return DialogueBeat::Lost;
        if (str == "Regroup") return DialogueBeat::Regroup;
        if (str == "WaitForGroup") return DialogueBeat::WaitForGroup;
        if (str == "Arrived") return DialogueBeat::Arrived;
        if (str == "DangerAhead") return DialogueBeat::DangerAhead;

        if (str == "NeedMana") return DialogueBeat::NeedMana;
        if (str == "NeedDrink") return DialogueBeat::NeedDrink;
        if (str == "NeedFood") return DialogueBeat::NeedFood;
        if (str == "NeedRest") return DialogueBeat::NeedRest;
        if (str == "NeedHeal") return DialogueBeat::NeedHeal;
        if (str == "LowHealth") return DialogueBeat::LowHealth;
        if (str == "LowMana") return DialogueBeat::LowMana;
        if (str == "ReadyAgain") return DialogueBeat::ReadyAgain;
        if (str == "WaitingForCooldown") return DialogueBeat::WaitingForCooldown;
        if (str == "LeaderWaitAck") return DialogueBeat::LeaderWaitAck;
        if (str == "LeaderProceed") return DialogueBeat::LeaderProceed;

        if (str == "Pulling") return DialogueBeat::Pulling;
        if (str == "AggroWarning") return DialogueBeat::AggroWarning;
        if (str == "FocusTarget") return DialogueBeat::FocusTarget;
        if (str == "KillAdds") return DialogueBeat::KillAdds;
        if (str == "Interrupt") return DialogueBeat::Interrupt;
        if (str == "Dispel") return DialogueBeat::Dispel;
        if (str == "SpreadOut") return DialogueBeat::SpreadOut;
        if (str == "StackTogether") return DialogueBeat::StackTogether;
        if (str == "MoveFromGround") return DialogueBeat::MoveFromGround;
        if (str == "RunAway") return DialogueBeat::RunAway;
        if (str == "Defensive") return DialogueBeat::Defensive;
        if (str == "HealNeeded") return DialogueBeat::HealNeeded;
        if (str == "TankInDanger") return DialogueBeat::TankInDanger;
        if (str == "TargetSwitch") return DialogueBeat::TargetSwitch;
        if (str == "EnemyCaster") return DialogueBeat::EnemyCaster;
        if (str == "EnemyHealer") return DialogueBeat::EnemyHealer;
        if (str == "CrowdControl") return DialogueBeat::CrowdControl;
        if (str == "BurstNow") return DialogueBeat::BurstNow;
        if (str == "RetreatCombat") return DialogueBeat::RetreatCombat;

        if (str == "WatcherObserved") return DialogueBeat::WatcherObserved;
        if (str == "WatcherSpotted") return DialogueBeat::WatcherSpotted;
        if (str == "MysteryWhisper") return DialogueBeat::MysteryWhisper;
        if (str == "MysteryRune") return DialogueBeat::MysteryRune;
        if (str == "CaravanTravel") return DialogueBeat::CaravanTravel;
        if (str == "CaravanAttack") return DialogueBeat::CaravanAttack;
        if (str == "RivalPartyTravel") return DialogueBeat::RivalPartyTravel;
        if (str == "RivalPartyCombat") return DialogueBeat::RivalPartyCombat;
        if (str == "HunterThreat") return DialogueBeat::HunterThreat;
        if (str == "PreyPanic") return DialogueBeat::PreyPanic;
        if (str == "SkirmishCharge") return DialogueBeat::SkirmishCharge;
        if (str == "AmbushReady") return DialogueBeat::AmbushReady;
        if (str == "AmbushTriggered") return DialogueBeat::AmbushTriggered;
        if (str == "RoadDuelChallenge") return DialogueBeat::RoadDuelChallenge;
        if (str == "RoadDuelVictory") return DialogueBeat::RoadDuelVictory;

        if (str == "MysteryShathYarWhisper") return DialogueBeat::MysteryShathYarWhisper;
        if (str == "MysteryShathYarThreat") return DialogueBeat::MysteryShathYarThreat;
        if (str == "MysteryShathYarIncantation") return DialogueBeat::MysteryShathYarIncantation;
        if (str == "MysteryShathYarDeparture") return DialogueBeat::MysteryShathYarDeparture;

        return DialogueBeat::None;
    }
}

#endif // COA_PLAYERBOTS_DIALOGUE_TYPES_H
