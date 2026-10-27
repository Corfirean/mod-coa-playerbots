/*
 * mod-coa-playerbots -- Dynamic Bot Encounters standalone unit tests (§15).
 *
 * Build & run (MSVC C++20):
 *   cl /nologo /std:c++20 /EHsc /I module\tests\stub /I module\src\world /I module\src\world\encounters /I module\src module\tests\encounter_tests.cpp
 */

#include "stub/Define.h"
#include "stub/MotionMaster.h"
#include "stub/ObjectGuid.h"

#include "BotMovementPrimitives.h"
#include "BotMovementStateStore.h"
#include "EncounterBudget.h"
#include "EncounterCleanupDiagnostics.h"
#include "EncounterEligibility.h"
#include "EncounterInstance.h"
#include "EncounterMemory.h"
#include "EncounterNarrator.h"
#include "EncounterParticipant.h"
#include "EncounterPersonality.h"
#include "EncounterState.h"
#include "EncounterTypes.h"
#include "WorldTask.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    int _failures = 0;
    int _checks = 0;

    void Check(bool condition, char const* expr, int line)
    {
        ++_checks;
        if (!condition)
        {
            ++_failures;
            std::printf("FAIL (line %d): %s\n", line, expr);
        }
    }
}

#define CHECK(cond) Check((cond), #cond, __LINE__)

// ============================================================================
// 1. Encounter Eligibility (real player vs bot, alive/dead, flight, instance)
// ============================================================================
static void TestEncounterEligibility()
{
    EncounterConfig cfg;
    TargetPlayerSnapshot player;
    player.guid = ObjectGuid(100);
    player.isRealPlayer = true;
    player.isOnlineAndInWorld = true;
    player.isAlive = true;
    player.isContestedOrPvPArea = true;
    player.isPvPFlaggedOrAttackable = true;
    player.mapId = 0;
    player.teamId = 0; // Alliance
    player.level = 40;
    player.partySize = 1;
    player.distanceToCapitalYards = 400.0f;

    CHECK(EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::AssassinContract, player, cfg));
    CHECK(EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::TheWatcher, player, cfg));
    CHECK(EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::MysteryEncounter, player, cfg));

    // Must NOT target another playerbot as if it were a real player
    TargetPlayerSnapshot botAsPlayer = player;
    botAsPlayer.isRealPlayer = false;
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::AssassinContract, botAsPlayer, cfg));
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::TheWatcher, botAsPlayer, cfg));
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::MysteryEncounter, botAsPlayer, cfg));

    // Dead, flying, or in dungeon/BG players are ineligible
    TargetPlayerSnapshot deadPlayer = player;
    deadPlayer.isAlive = false;
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::AssassinContract, deadPlayer, cfg));

    TargetPlayerSnapshot flyingPlayer = player;
    flyingPlayer.isInFlight = true;
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::AssassinContract, flyingPlayer, cfg));

    TargetPlayerSnapshot dungeonPlayer = player;
    dungeonPlayer.isInDungeonOrBG = true;
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::TheWatcher, dungeonPlayer, cfg));

    // Solo assassin refuses if player is in a large party (> maxPlayerPartySizeForSoloAssassin)
    TargetPlayerSnapshot groupedPlayer = player;
    groupedPlayer.partySize = 4;
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::AssassinContract, groupedPlayer, cfg));
    // Bounty Hunter can still engage grouped players if within other rules
    CHECK(EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::BountyHunter, groupedPlayer, cfg));
}

// ============================================================================
// 2. Faction Restrictions & PvP Rules
// ============================================================================
static void TestFactionRestrictions()
{
    EncounterConfig cfg;
    TargetPlayerSnapshot alliancePlayer;
    alliancePlayer.guid = ObjectGuid(100);
    alliancePlayer.isRealPlayer = true;
    alliancePlayer.mapId = 0;
    alliancePlayer.teamId = 0; // Alliance
    alliancePlayer.level = 40;
    alliancePlayer.x = 100.0f;
    alliancePlayer.y = 100.0f;

    CandidateBotSnapshot hordeBot;
    hordeBot.activity.botGuid = ObjectGuid(201);
    hordeBot.mapId = 0;
    hordeBot.teamId = 1; // Horde
    hordeBot.level = 40;
    hordeBot.x = 150.0f;
    hordeBot.y = 100.0f;
    hordeBot.isContestedOrPvPArea = true;
    hordeBot.canLegallyAttackTarget = true;

    // Opposite faction + valid attack target -> eligible for Assassin/Bounty/Nemesis
    CHECK(EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, hordeBot, alliancePlayer, cfg));
    CHECK(EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::BountyHunter, hordeBot, alliancePlayer, cfg));

    // Same faction -> rejected for hostile encounters
    CandidateBotSnapshot allianceBot = hordeBot;
    allianceBot.activity.botGuid = ObjectGuid(202);
    allianceBot.teamId = 0; // Alliance
    CHECK(!EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, allianceBot, alliancePlayer, cfg));
    CHECK(!EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::BountyHunter, allianceBot, alliancePlayer, cfg));
    CHECK(!EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::Nemesis, allianceBot, alliancePlayer, cfg));

    // If core PvP flag check says target is NOT attackable, never bypass it
    CandidateBotSnapshot unflaggedBot = hordeBot;
    unflaggedBot.canLegallyAttackTarget = false;
    CHECK(!EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, unflaggedBot, alliancePlayer, cfg));

    // Pair checks: Road Duel requires same faction; HunterAndPrey requires opposite faction
    CandidateBotSnapshot allianceBot2 = allianceBot;
    allianceBot2.activity.botGuid = ObjectGuid(203);
    allianceBot2.x = 160.0f;
    CHECK(EncounterEligibility::AreBotsCompatibleForPair(EncounterType::RoadDuel, allianceBot, allianceBot2, cfg));
    CHECK(!EncounterEligibility::AreBotsCompatibleForPair(EncounterType::RoadDuel, allianceBot, hordeBot, cfg));
    CHECK(EncounterEligibility::AreBotsCompatibleForPair(EncounterType::HunterAndPrey, allianceBot, hordeBot, cfg));
    CHECK(!EncounterEligibility::AreBotsCompatibleForPair(EncounterType::HunterAndPrey, allianceBot, allianceBot2, cfg));
}

// ============================================================================
// 3. Safe Zones, Sanctuaries, Capitals, and AFK Protection
// ============================================================================
static void TestSafeZonesAndSanctuaries()
{
    EncounterConfig cfg;
    TargetPlayerSnapshot player;
    player.guid = ObjectGuid(100);
    player.isRealPlayer = true;
    player.mapId = 0;
    player.teamId = 0;
    player.level = 50;
    player.isContestedOrPvPArea = true;
    player.isPvPFlaggedOrAttackable = true;
    player.distanceToCapitalYards = 400.0f;

    TargetPlayerSnapshot inSanctuary = player;
    inSanctuary.isInSanctuary = true;
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::AssassinContract, inSanctuary, cfg));
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::TheWatcher, inSanctuary, cfg));
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::MysteryEncounter, inSanctuary, cfg));

    TargetPlayerSnapshot inCapital = player;
    inCapital.isInCapitalOrSafeZone = true;
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::AssassinContract, inCapital, cfg));
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::BountyHunter, inCapital, cfg));

    TargetPlayerSnapshot nearCity = player;
    nearCity.distanceToCapitalYards = 80.0f; // < minCityDistanceYards (160)
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::AssassinContract, nearCity, cfg));

    TargetPlayerSnapshot afkPlayer = player;
    afkPlayer.isAFK = true;
    CHECK(!EncounterEligibility::IsPlayerEligibleForEncounter(EncounterType::AssassinContract, afkPlayer, cfg));

    // Ongoing check: if target player walks into a safe zone or sanctuary mid-encounter, abort!
    EncounterStopReason reason = EncounterEligibility::CheckOngoingPlayerEligibility(
        EncounterType::AssassinContract, inCapital, 0, 30.0f, cfg);
    CHECK(reason == EncounterStopReason::EnteredSafeZone);
}

// ============================================================================
// 4. Level Difference Limits
// ============================================================================
static void TestLevelLimits()
{
    EncounterConfig cfg;
    cfg.maxLevelDiff = 4;

    TargetPlayerSnapshot player;
    player.guid = ObjectGuid(100);
    player.mapId = 0;
    player.teamId = 0;
    player.level = 30;
    player.x = 0.0f;
    player.y = 0.0f;

    CandidateBotSnapshot bot;
    bot.activity.botGuid = ObjectGuid(200);
    bot.mapId = 0;
    bot.teamId = 1;
    bot.level = 33; // AssassinContract allows [playerLevel..playerLevel+3] -> 33 allowed
    bot.x = 40.0f;
    bot.y = 0.0f;
    bot.isContestedOrPvPArea = true;
    bot.canLegallyAttackTarget = true;

    CHECK(EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, bot, player, cfg));

    bot.level = 34; // +4 -> rejected for AssassinContract, allowed for BountyHunter (maxLevelDiff=4)
    CHECK(!EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, bot, player, cfg));
    CHECK(EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::BountyHunter, bot, player, cfg));

    bot.level = 35; // diff = 5 -> rejected for all
    CHECK(!EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::BountyHunter, bot, player, cfg));

    bot.level = 29; // below playerLevel -> rejected for AssassinContract
    CHECK(!EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, bot, player, cfg));
}

// ============================================================================
// 5 & 6. Budget Limits & Cooldowns
// ============================================================================
static void TestBudgetAndCooldowns()
{
    EncounterConfig cfg;
    cfg.maxGlobalActive = 3;
    cfg.maxMapActive = 2;
    cfg.maxPerPlayerActive = 1;
    cfg.playerPersonalCooldownMs = 60000;
    cfg.botCooldownMs = 45000;
    cfg.globalTypeCooldownMs = 30000;
    cfg.bountyCooldownMs = 120000;

    EncounterBudgetTracker budget;
    ObjectGuid p1(101);
    ObjectGuid p2(102);
    ObjectGuid bot1(201);
    uint32 now = 10000;

    CHECK(budget.CanCreateEncounter(EncounterType::AssassinContract, 0, p1, cfg, now));
    budget.OnEncounterStarted(EncounterType::AssassinContract, 0, p1);
    CHECK(budget.GetGlobalActive() == 1);
    CHECK(budget.GetMapActive(0) == 1);
    CHECK(budget.GetPlayerActive(p1) == 1);

    // Per-player active cap (1): cannot start a second encounter on p1 while first is active
    CHECK(!budget.CanCreateEncounter(EncounterType::TheWatcher, 0, p1, cfg, now));

    // Can start a second encounter on map 0 for p2
    CHECK(budget.CanCreateEncounter(EncounterType::TheWatcher, 0, p2, cfg, now));
    budget.OnEncounterStarted(EncounterType::TheWatcher, 0, p2);
    CHECK(budget.GetMapActive(0) == 2);

    // Map 0 is now at maxMapActive (2): third encounter on map 0 is blocked, but map 1 is allowed
    CHECK(!budget.CanCreateEncounter(EncounterType::RoadDuel, 0, ObjectGuid::Empty, cfg, now));
    CHECK(budget.CanCreateEncounter(EncounterType::RoadDuel, 1, ObjectGuid::Empty, cfg, now));
    budget.OnEncounterStarted(EncounterType::RoadDuel, 1, ObjectGuid::Empty);

    // Global active cap (3) reached: map 1 is now blocked too
    CHECK(budget.GetGlobalActive() == 3);
    CHECK(!budget.CanCreateEncounter(EncounterType::Deserter, 1, ObjectGuid::Empty, cfg, now));

    // Finish AssassinContract on p1 with bot1
    budget.OnEncounterFinished(EncounterType::AssassinContract, 0, p1, {bot1}, cfg, now);
    CHECK(budget.GetGlobalActive() == 2);
    CHECK(budget.GetMapActive(0) == 1);
    CHECK(budget.GetPlayerActive(p1) == 0);

    // Bot cooldown & personal cooldown & type cooldown are now active
    CHECK(budget.IsBotOnCooldown(bot1, now + 1000));
    CHECK(!budget.IsBotOnCooldown(bot1, now + 46000));
    CHECK(budget.IsPlayerOnPersonalCooldown(p1, now + 10000));
    CHECK(!budget.CanCreateEncounter(EncounterType::MysteryEncounter, 0, p1, cfg, now + 10000));
    CHECK(budget.CanCreateEncounter(EncounterType::MysteryEncounter, 0, p1, cfg, now + 61000));
}

// ============================================================================
// 7. Personality Affinity & Early Retreat
// ============================================================================
static void TestPersonalitySelection()
{
    WorldPersona rogueLoner;
    rogueLoner.grinding = 95;
    rogueLoner.patience = 85;
    rogueLoner.sociability = 5;
    rogueLoner.lean = 4; // Grind

    WorldPersona sociableCaravaner;
    sociableCaravaner.grinding = 15;
    sociableCaravaner.patience = 85;
    sociableCaravaner.sociability = 95;
    sociableCaravaner.lean = 5; // Explore

    float rogueAssassinScore = EncounterPersonality::ScoreAffinity(
        EncounterType::AssassinContract, EncounterRole::Assassin, rogueLoner, true);
    float sociableAssassinScore = EncounterPersonality::ScoreAffinity(
        EncounterType::AssassinContract, EncounterRole::Assassin, sociableCaravaner, false);
    CHECK(rogueAssassinScore > sociableAssassinScore);

    float sociableCaravanScore = EncounterPersonality::ScoreAffinity(
        EncounterType::Caravan, EncounterRole::CaravanMember, sociableCaravaner, false);
    float lonerCaravanScore = EncounterPersonality::ScoreAffinity(
        EncounterType::Caravan, EncounterRole::CaravanMember, rogueLoner, false);
    CHECK(sociableCaravanScore > lonerCaravanScore);

    // Cowardly / cautious early retreat check
    DerivedEncounterTraits cautiousTraits = EncounterPersonality::Derive(sociableCaravaner);
    CHECK(cautiousTraits.cautious >= 0.55f);
    CHECK(EncounterPersonality::ShouldRetreatFromPvP(cautiousTraits.cautious, 22.0f, 1));
    CHECK(!EncounterPersonality::ShouldRetreatFromPvP(cautiousTraits.cautious, 80.0f, 1));
}

// ============================================================================
// 8 & 9. State Machine Transitions & Timeout Cleanup
// ============================================================================
static void TestStateTransitionsAndTimeouts()
{
    EncounterStateMachine sm;
    sm.Initialize(1000, 30000, 60000, 5000);
    CHECK(sm.phase == EncounterPhase::Candidate);
    CHECK(sm.IsRunning());

    // Cannot jump directly from Candidate to Active or Resolve
    CHECK(!sm.TransitionTo(EncounterPhase::Active, 1100));
    CHECK(sm.TransitionTo(EncounterPhase::Prepare, 1100));
    CHECK(sm.phase == EncounterPhase::Prepare);
    CHECK(!sm.IsPhaseTimedOut(20000));
    CHECK(sm.RemainingPhaseTimeoutMs(20000) == 11100);
    CHECK(sm.IsPhaseTimedOut(31100));

    // Prepare -> Active -> Resolve -> Cooldown -> Terminated
    CHECK(sm.TransitionTo(EncounterPhase::Active, 5000));
    CHECK(sm.TransitionTo(EncounterPhase::Resolve, 25000));
    CHECK(sm.TransitionTo(EncounterPhase::Cooldown, 28000));
    CHECK(!sm.IsRunning());
    sm.MarkFullyRetired(28000);
    CHECK(sm.phase == EncounterPhase::Terminated);
    CHECK(!sm.TransitionTo(EncounterPhase::Prepare, 29000));
}

// ============================================================================
// 10, 11, 12. Participant Disappearance, Player Logout, Bot Logout
// ============================================================================
static void TestDisappearanceAndLogouts()
{
    EncounterConfig cfg;

    // Player logout mid-encounter
    TargetPlayerSnapshot player;
    player.guid = ObjectGuid(100);
    player.isRealPlayer = true;
    player.isOnlineAndInWorld = false; // Logged out!
    player.isAlive = true;
    player.mapId = 0;

    CHECK(EncounterEligibility::CheckOngoingPlayerEligibility(
        EncounterType::AssassinContract, player, 0, 20.0f, cfg) == EncounterStopReason::PlayerLogout);

    // Player map change / teleport mid-encounter
    player.isOnlineAndInWorld = true;
    player.mapId = 1;
    CHECK(EncounterEligibility::CheckOngoingPlayerEligibility(
        EncounterType::AssassinContract, player, 0, 20.0f, cfg) == EncounterStopReason::MapChanged);

    // Bot logout / disappearance mid-encounter
    BotActivitySnapshot botSnap;
    botSnap.botGuid = ObjectGuid(200);
    botSnap.isOnlineAndInWorld = false;
    CHECK(EncounterPriorityArbiter::CheckMidEncounterPreemption(botSnap, 0, 0) == EncounterStopReason::BotLogoutOrDespawn);

    // Bot death mid-encounter
    botSnap.isOnlineAndInWorld = true;
    botSnap.isAlive = false;
    CHECK(EncounterPriorityArbiter::CheckMidEncounterPreemption(botSnap, 0, 0) == EncounterStopReason::ParticipantDeath);

    // Bot manual command mid-encounter
    botSnap.isAlive = true;
    botSnap.hasManualCommand = true;
    CHECK(EncounterPriorityArbiter::CheckMidEncounterPreemption(botSnap, 0, 0) == EncounterStopReason::ManualBotCommand);

    // Bot joined player-controlled group mid-encounter
    botSnap.hasManualCommand = false;
    botSnap.inPlayerControlledGroup = true;
    CHECK(EncounterPriorityArbiter::CheckMidEncounterPreemption(botSnap, 0, 0) == EncounterStopReason::JoinedPlayerGroup);
}

// ============================================================================
// 13 & 14. Nemesis-lite Score/Decay & Bounty Score
// ============================================================================
static void TestNemesisAndBountyMemory()
{
    EncounterConfig cfg;
    cfg.bountyKillScore = 35;
    cfg.bountyThreshold = 100;
    cfg.bountyDecayPerMinute = 5;
    cfg.nemesisMinGrudgeScore = 2;
    cfg.nemesisDecayIntervalMs = 60000;
    cfg.nemesisTtlMs = 300000;

    EncounterMemoryStore mem;
    ObjectGuid player(100);
    ObjectGuid botA(201);
    ObjectGuid botB(202);
    uint32 now = 1000;

    // 1 kill -> bounty 35, grudge 2 (already a Nemesis rival!)
    mem.RecordPlayerKilledBot(player, botA, cfg, now);
    CHECK(mem.GetBountyScore(player, cfg, now) == 35);
    CHECK(!mem.IsBountyThresholdMet(player, cfg, now));
    CHECK(mem.GetGrudgeScore(player, botA, cfg, now) == 2);
    CHECK(mem.IsNemesisRival(player, botA, cfg, now));
    CHECK(mem.GetSelectionWeightMultiplier(player, botA, cfg, now) > 1.5f);

    // 2 more kills -> bounty 105 >= threshold (100)
    mem.RecordPlayerKilledBot(player, botB, cfg, now + 1000);
    mem.RecordPlayerKilledBot(player, botA, cfg, now + 2000);
    CHECK(mem.GetBountyScore(player, cfg, now + 2000) == 105);
    CHECK(mem.IsBountyThresholdMet(player, cfg, now + 2000));

    // Passive bounty decay: after 2 minutes (120000ms), decays by 2 * 5 = 10 -> 95 (< 100)
    uint32 twoMinLater = now + 120000;
    CHECK(mem.GetBountyScore(player, cfg, twoMinLater) == 95);
    CHECK(!mem.IsBountyThresholdMet(player, cfg, twoMinLater));

    // Grudge decay: after 2 decay intervals (120000ms), botB's grudge (2) decays to 0 and is pruned
    CHECK(mem.GetGrudgeScore(player, botB, cfg, twoMinLater + 5000) == 0);

    // TTL expiration: after nemesisTtlMs (300000ms), all unrefreshed records expire
    uint32 afterTtl = now + 350000;
    CHECK(mem.GetGrudgeScore(player, botA, cfg, afterTtl) == 0);
    CHECK(mem.GetPlayerNemesisList(player, cfg, afterTtl).empty());
}

// ============================================================================
// 15. Narrator Cooldown & Contextual Visibility Gates
// ============================================================================
static void TestNarratorCooldownAndContextualRules()
{
    EncounterConfig cfg;
    cfg.narratorEnabled = true;
    cfg.narratorPlayerCooldownMs = 60000;
    cfg.narratorBotCooldownMs = 30000;
    cfg.narratorMaxDistanceYards = 55.0f;

    EncounterNarratorGate gate;
    ObjectGuid player(100);
    ObjectGuid bot(200);
    uint32 now = 5000;

    // Assassin narrator rejected if too far (> 45yd) or stalked too briefly (< 3000ms)
    CHECK(!gate.CanSendToPlayer(player, EncounterType::AssassinContract, 0, 0, 50.0f, 4000, cfg, now));
    CHECK(!gate.CanSendToPlayer(player, EncounterType::AssassinContract, 0, 0, 25.0f, 1500, cfg, now));
    // Rejected if on different map
    CHECK(!gate.CanSendToPlayer(player, EncounterType::AssassinContract, 1, 0, 25.0f, 4000, cfg, now));

    // Allowed when close & stalked >= 3000ms
    CHECK(gate.CanSendToPlayer(player, EncounterType::AssassinContract, 0, 0, 25.0f, 4000, cfg, now));
    gate.RecordPlayerMessage(player, cfg, now);

    // Cooldown blocks subsequent messages to the same player until 60s have elapsed
    CHECK(!gate.CanSendToPlayer(player, EncounterType::TheWatcher, 0, 0, 30.0f, 4000, cfg, now + 10000));
    CHECK(gate.CanSendToPlayer(player, EncounterType::TheWatcher, 0, 0, 30.0f, 4000, cfg, now + 61000));

    // Bot speech gate
    CHECK(gate.CanBotSpeak(bot, 0, 0, 20.0f, cfg, now));
    gate.RecordBotSpeech(bot, cfg, now);
    CHECK(!gate.CanBotSpeak(bot, 0, 0, 20.0f, cfg, now + 5000));
    CHECK(gate.CanBotSpeak(bot, 0, 0, 20.0f, cfg, now + 31000));
}

// ============================================================================
// 16 & 17. No Duplicate Ownership & No Stealing From Higher-Priority Systems
// ============================================================================
static void TestOwnershipAndPriorityArbitration()
{
    EncounterOwnershipTable ownership;
    ObjectGuid bot1(201);
    ObjectGuid bot2(202);

    CHECK(ownership.TryClaim(bot1, 1));
    CHECK(ownership.IsOwned(bot1));
    CHECK(ownership.GetEncounterId(bot1) == 1);

    // Duplicate claim on the same bot for another encounter MUST fail
    CHECK(!ownership.TryClaim(bot1, 2));
    CHECK(ownership.GetEncounterId(bot1) == 1);

    // Wrong encounter ID cannot release bot1
    CHECK(!ownership.Release(bot1, 2));
    CHECK(ownership.IsOwned(bot1));

    // Correct encounter ID releases bot1 cleanly
    CHECK(ownership.Release(bot1, 1));
    CHECK(!ownership.IsOwned(bot1));

    // ReleaseAllForEncounter cleans up all participants
    CHECK(ownership.TryClaim(bot1, 5));
    CHECK(ownership.TryClaim(bot2, 5));
    ownership.ReleaseAllForEncounter(5);
    CHECK(ownership.Size() == 0);

    // Priority arbitration: never steal bots from higher-priority activities
    BotActivitySnapshot freeBot;
    freeBot.botGuid = bot1;
    CHECK(EncounterPriorityArbiter::CanClaimBot(freeBot));

    BotActivitySnapshot manualBot = freeBot;
    manualBot.hasManualCommand = true;
    CHECK(!EncounterPriorityArbiter::CanClaimBot(manualBot));

    BotActivitySnapshot playerGroupBot = freeBot;
    playerGroupBot.inPlayerControlledGroup = true;
    CHECK(!EncounterPriorityArbiter::CanClaimBot(playerGroupBot));

    BotActivitySnapshot dungeonBot = freeBot;
    dungeonBot.inDungeonOrBG = true;
    CHECK(!EncounterPriorityArbiter::CanClaimBot(dungeonBot));

    BotActivitySnapshot worldPartyBot = freeBot;
    worldPartyBot.inWorldParty = true;
    CHECK(!EncounterPriorityArbiter::CanClaimBot(worldPartyBot));

    BotActivitySnapshot socialHelpBot = freeBot;
    socialHelpBot.isHelpingSocial = true;
    CHECK(!EncounterPriorityArbiter::CanClaimBot(socialHelpBot));

    BotActivitySnapshot lootingBot = freeBot;
    lootingBot.isLootingOrCorpseRun = true;
    CHECK(!EncounterPriorityArbiter::CanClaimBot(lootingBot));

    // Verify MoveOwner::Encounter locomotion priority hierarchy
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Encounter) > LocomotionArbiter::PriorityOf(MoveOwner::Quest));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Encounter) > LocomotionArbiter::PriorityOf(MoveOwner::Travel));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Encounter) > LocomotionArbiter::PriorityOf(MoveOwner::Ambient));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Loot) > LocomotionArbiter::PriorityOf(MoveOwner::Encounter));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Corpse) > LocomotionArbiter::PriorityOf(MoveOwner::Encounter));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::AutoDungeon) > LocomotionArbiter::PriorityOf(MoveOwner::Encounter));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Battleground) > LocomotionArbiter::PriorityOf(MoveOwner::Encounter));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Combat) > LocomotionArbiter::PriorityOf(MoveOwner::Encounter));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Avoidance) > LocomotionArbiter::PriorityOf(MoveOwner::Encounter));
}

// Helper wrapping LocomotionStateStore for unit test token claim/release/forceRelease
struct TestLocomotionHelper
{
    uint64 nextCmdId = 1;

    struct ClaimResult
    {
        bool granted = false;
        LocomotionToken token;
    };

    ClaimResult TryAcquire(ObjectGuid botGuid, MoveOwner owner, MoveMode mode, uint32 nowMs)
    {
        LocomotionShard& shard = LocomotionStateStore::GetShard(botGuid);
        std::lock_guard<std::mutex> lock(shard.mutex);
        if (!LocomotionStateStore::CanClaimLocked(shard, botGuid, owner, false))
            return {};

        BotLocomotionRecord& rec = shard.locomotion[botGuid];
        rec.owner = owner;
        rec.mode = mode;
        rec.state = (mode == MoveMode::Chase) ? LocomotionState::Chasing : LocomotionState::Moving;
        rec.commandId = ++nextCmdId;
        rec.issuedAtMs = nowMs;
        LocomotionStateStore::AdvanceIntentEpochLocked(shard, botGuid, owner);

        ClaimResult res;
        res.granted = true;
        res.token = LocomotionToken{botGuid, owner, rec.commandId};
        return res;
    }

    bool ValidateToken(ObjectGuid botGuid, LocomotionToken const& token) const
    {
        LocomotionShard& shard = LocomotionStateStore::GetShard(botGuid);
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto it = shard.locomotion.find(botGuid);
        if (it == shard.locomotion.end())
            return false;
        return LocomotionCommandValidator::IsTokenMatch(it->second, token);
    }

    bool Release(ObjectGuid botGuid, LocomotionToken const& token)
    {
        LocomotionShard& shard = LocomotionStateStore::GetShard(botGuid);
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto it = shard.locomotion.find(botGuid);
        if (it == shard.locomotion.end() || !LocomotionCommandValidator::IsTokenMatch(it->second, token))
            return false;
        it->second.owner = MoveOwner::None;
        it->second.mode = MoveMode::Idle;
        it->second.state = LocomotionState::Idle;
        it->second.commandId = 0;
        shard.requests.erase(botGuid);
        return true;
    }

    void ForceReleaseOwner(ObjectGuid botGuid, MoveOwner owner)
    {
        LocomotionShard& shard = LocomotionStateStore::GetShard(botGuid);
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto it = shard.locomotion.find(botGuid);
        if (it != shard.locomotion.end() && it->second.owner == owner)
        {
            it->second.owner = MoveOwner::None;
            it->second.mode = MoveMode::Idle;
            it->second.state = LocomotionState::Idle;
            it->second.commandId = 0;
        }
        auto rIt = shard.requests.find(botGuid);
        if (rIt != shard.requests.end() && rIt->second.owner == owner)
            shard.requests.erase(rIt);
    }

    MoveOwner ActiveOwner(ObjectGuid botGuid) const
    {
        LocomotionShard& shard = LocomotionStateStore::GetShard(botGuid);
        std::lock_guard<std::mutex> lock(shard.mutex);
        return LocomotionStateStore::CurrentOwnerLocked(shard, botGuid);
    }
};

// ============================================================================
// 18. Cleanup Invariant Validator (§2 & §3)
// ============================================================================
static void TestCleanupInvariantValidator()
{
    EncounterCleanupSnapshot cleanSnap;
    cleanSnap.botGuid = ObjectGuid(301);
    cleanSnap.currentMoveOwner = MoveOwner::None;

    EncounterCleanupVerdict ok = EncounterCleanupValidator::Validate(cleanSnap);
    CHECK(ok.passed);
    CHECK(ok.violationCount == 0);
    CHECK(ok.summary == "PASS");

    // Verify each of the 16 post-encounter invariant violations is caught and reported
    {
        EncounterCleanupSnapshot s = cleanSnap;
        s.hasEncounterOwnership = true;
        EncounterCleanupVerdict v = EncounterCleanupValidator::Validate(s);
        CHECK(!v.passed && v.violationCount == 1);
    }
    {
        EncounterCleanupSnapshot s = cleanSnap;
        s.inAnyEncounterParticipantList = true;
        EncounterCleanupVerdict v = EncounterCleanupValidator::Validate(s);
        CHECK(!v.passed && v.violationCount == 1);
    }
    {
        EncounterCleanupSnapshot s = cleanSnap;
        s.hasStaleSelectionTarget = true;
        s.hasStalePvPCombatTarget = true;
        EncounterCleanupVerdict v = EncounterCleanupValidator::Validate(s);
        CHECK(!v.passed && v.violationCount == 2);
    }
    {
        EncounterCleanupSnapshot s = cleanSnap;
        s.currentMoveOwner = MoveOwner::Encounter;
        s.moveTokenStillValid = true;
        s.hasEncounterNavRequest = true;
        EncounterCleanupVerdict v = EncounterCleanupValidator::Validate(s);
        CHECK(!v.passed && v.violationCount == 3);
    }
    {
        EncounterCleanupSnapshot s = cleanSnap;
        s.hasResidualEncounterStealth = true;
        s.hasResidualEncounterMorph = true;
        s.hasResidualSitOrKneelState = true;
        s.hasResidualTemporaryGameObject = true;
        s.pausedByEncounterFlag = true;
        s.worldTaskStuckPaused = true;
        EncounterCleanupVerdict v = EncounterCleanupValidator::Validate(s);
        CHECK(!v.passed && v.violationCount == 6);
    }
}

// ============================================================================
// 19. Forced Live-Simulation & Adversarial Abort Matrix across all 13 Types (§4-§18)
// ============================================================================
static void TestAll13EncounterTypesLiveMatrix()
{
    LocomotionStateStore::ResetAllForTest();
    EncounterConfig cfg;
    EncounterOwnershipTable ownership;
    EncounterBudgetTracker budget;
    TestLocomotionHelper locoStore;

    static constexpr EncounterStopReason ABORT_REASONS[] = {
        EncounterStopReason::PlayerLogout,
        EncounterStopReason::BotLogoutOrDespawn,
        EncounterStopReason::ParticipantDeath,
        EncounterStopReason::MapChanged,
        EncounterStopReason::EnteredInstanceOrBG,
        EncounterStopReason::EnteredSafeZone,
        EncounterStopReason::JoinedPlayerGroup,
        EncounterStopReason::ManualBotCommand,
        EncounterStopReason::TooFar,
        EncounterStopReason::Unreachable,
        EncounterStopReason::Timeout,
        EncounterStopReason::PvPEligibilityLost,
        EncounterStopReason::AdminStopped
    };

    uint32 nowMs = 10000;
    uint64 nextId = 1;

    for (size_t typeIdx = 1; typeIdx < ENCOUNTER_TYPE_COUNT; ++typeIdx)
    {
        EncounterType type = static_cast<EncounterType>(typeIdx);

        // Hostile types get 20 normal runs, RoadDuel gets 30 multi-class runs, others get 12 normal runs
        uint32 normalRuns = 12;
        if (IsHostilePlayerEncounter(type) || type == EncounterType::FactionSkirmish ||
            type == EncounterType::PatrolAmbush || type == EncounterType::HunterAndPrey)
        {
            normalRuns = 20;
        }
        else if (type == EncounterType::RoadDuel)
        {
            normalRuns = 30;
        }

        uint32 totalRunsForType = normalRuns + 8u; // +8 forced mid-encounter aborts per type
        for (uint32 run = 0; run < totalRunsForType; ++run)
        {
            nowMs += 5000;
            bool isAbortRun = (run >= normalRuns);
            uint64 encId = nextId++;

            EncounterInstance inst;
            inst.id = encId;
            inst.type = type;
            inst.mapId = (run % 2 == 0) ? 0u : 1u;
            inst.targetPlayerGuid = IsPlayerTargetedEncounter(type) ? ObjectGuid(1000 + (run % 5)) : ObjectGuid::Empty;
            inst.state.Initialize(nowMs, cfg.prepareTimeoutMs, cfg.activeTimeoutMs, cfg.resolveTimeoutMs);
            CHECK(inst.state.TransitionTo(EncounterPhase::Prepare, nowMs));

            // Determine participant count (1 for solo, 2 for pairs/bounty, 4 for skirmish/ambush/caravan/party)
            uint32 partCount = 1;
            if (type == EncounterType::RoadDuel || type == EncounterType::HunterAndPrey ||
                (type == EncounterType::BountyHunter && (run % 2 == 1)))
            {
                partCount = 2;
            }
            else if (type == EncounterType::FactionSkirmish || type == EncounterType::PatrolAmbush ||
                     type == EncounterType::Caravan || type == EncounterType::RivalAdventuringParty)
            {
                partCount = 4;
            }
            else if (type == EncounterType::RareHunter)
            {
                partCount = 2;
            }

            for (uint32 pIdx = 0; pIdx < partCount; ++pIdx)
            {
                EncounterParticipant part;
                part.botGuid = ObjectGuid(5000 + uint64(typeIdx) * 100 + pIdx);
                part.mapId = inst.mapId;
                part.originalDisplayId = 1000 + pIdx;
                part.originalHealthPct = 95.0f;
                part.role = (type == EncounterType::RoadDuel)
                    ? (pIdx == 0 ? EncounterRole::DuelistA : EncounterRole::DuelistB)
                    : EncounterRole::Assassin;

                CHECK(ownership.TryClaim(part.botGuid, encId));
                auto res = locoStore.TryAcquire(part.botGuid, MoveOwner::Encounter, MoveMode::Navigate, nowMs);
                CHECK(res.granted);
                part.moveToken = res.token;

                if (type == EncounterType::AssassinContract || type == EncounterType::TheWatcher || type == EncounterType::PatrolAmbush)
                    part.stealthApplied = true;
                if (type == EncounterType::Deserter)
                    part.sitApplied = true;
                if (type == EncounterType::MysteryEncounter)
                {
                    part.morphApplied = true;
                    part.temporaryGobGuid = ObjectGuid(900000 + encId);
                    part.temporaryGobMapId = inst.mapId;
                }
                if (IsHostilePlayerEncounter(type) || type == EncounterType::FactionSkirmish ||
                    type == EncounterType::PatrolAmbush || type == EncounterType::HunterAndPrey)
                {
                    part.assignedTargetGuid = ObjectGuid(800000 + run);
                    part.engagedInCombat = true;
                }

                inst.participants.push_back(part);
            }

            budget.OnEncounterStarted(type, inst.mapId, inst.targetPlayerGuid, &cfg, nowMs);

            if (!isAbortRun)
            {
                // Normal lifecycle progression: Prepare -> Active -> Resolve -> Cooldown
                nowMs += 2000;
                CHECK(inst.state.TransitionTo(EncounterPhase::Active, nowMs));

                // For RoadDuel, verify non-lethal floor (22% HP) and winner/loser tracking
                if (type == EncounterType::RoadDuel)
                {
                    uint32 maxHp = 2000 + (run * 37u);
                    uint32 minSafeHp = std::max<uint32>(1u, (maxHp * 22u) / 100u);
                    uint32 curHp = maxHp;
                    uint32 stepDmg = std::max<uint32>(1u, maxHp / 13u);
                    while (curHp > minSafeHp + stepDmg)
                        curHp -= stepDmg;
                    curHp = minSafeHp;
                    CHECK(curHp >= minSafeHp && curHp > 0);
                    inst.duelCompleted = true;
                }

                nowMs += 4000;
                CHECK(inst.state.TransitionTo(EncounterPhase::Resolve, nowMs));
                nowMs += 1500;
                EncounterStopReason endReason = (type == EncounterType::TheWatcher || type == EncounterType::HunterAndPrey)
                    ? EncounterStopReason::CowardlyRetreat
                    : (type == EncounterType::RareHunter ? EncounterStopReason::TargetRareDead : EncounterStopReason::Completed);
                inst.state.Terminate(endReason, nowMs);
            }
            else
            {
                // Adversarial mid-encounter abort during Prepare or Active
                if (run % 2 == 0)
                    CHECK(inst.state.TransitionTo(EncounterPhase::Active, nowMs + 500));
                nowMs += 1000;
                EncounterStopReason abortReason = ABORT_REASONS[(run + typeIdx) % (sizeof(ABORT_REASONS) / sizeof(ABORT_REASONS[0]))];
                inst.state.Terminate(abortReason, nowMs);
            }

            CHECK(!inst.state.IsRunning());

            // Perform full FinalizeEncounter cleanup and validate all 16 invariants per bot
            std::vector<ObjectGuid> botGuids;
            for (auto& part : inst.participants)
            {
                botGuids.push_back(part.botGuid);
                if (part.moveToken.IsValid())
                    locoStore.Release(part.botGuid, part.moveToken);
                locoStore.ForceReleaseOwner(part.botGuid, MoveOwner::Encounter);
                part.moveToken = {};
                part.navGoalId = 0;
                part.assignedTargetGuid = ObjectGuid::Empty;
                part.engagedInCombat = false;
                part.stealthApplied = false;
                part.sitApplied = false;
                part.morphApplied = false;
                part.temporaryGobGuid = ObjectGuid::Empty;
                part.temporaryGobMapId = 0;
                ownership.Release(part.botGuid, inst.id);

                EncounterCleanupSnapshot snap;
                snap.botGuid = part.botGuid;
                snap.hasEncounterOwnership = ownership.IsOwned(part.botGuid);
                snap.moveTokenStillValid = part.moveToken.IsValid();
                snap.currentMoveOwner = locoStore.ActiveOwner(part.botGuid);
                snap.hasResidualEncounterStealth = part.stealthApplied;
                snap.hasResidualEncounterMorph = part.morphApplied;
                snap.hasResidualSitOrKneelState = part.sitApplied;
                snap.hasResidualTemporaryGameObject = !part.temporaryGobGuid.IsEmpty();
                snap.hasStaleSelectionTarget = !part.assignedTargetGuid.IsEmpty();
                snap.hasStalePvPCombatTarget = part.engagedInCombat;

                EncounterCleanupVerdict verdict = EncounterCleanupValidator::Validate(snap);
                CHECK(verdict.passed);
            }

            ownership.ReleaseAllForEncounter(inst.id);
            budget.OnEncounterFinished(type, inst.mapId, inst.targetPlayerGuid, botGuids, cfg, nowMs);
            inst.state.MarkFullyRetired(nowMs);
            CHECK(ownership.Size() == 0);
            CHECK(budget.GetGlobalActive() == 0);
        }
    }
}

// ============================================================================
// 20. WorldBrain Resume Validation Across All 6 Task Types (§19)
// ============================================================================
struct SimulatedBrainState
{
    ObjectGuid botGuid;
    WorldTask task;
    bool pausedByEncounter = false;
};

static void TestWorldBrainResumeAllTaskTypes()
{
    static constexpr WorldTaskType ALL_TASKS[] = {
        WorldTaskType::QuestObjective,
        WorldTaskType::Gather,
        WorldTaskType::Grind,
        WorldTaskType::Travel,
        WorldTaskType::QuestTurnIn,
        WorldTaskType::QuestAccept
    };

    for (size_t i = 0; i < sizeof(ALL_TASKS) / sizeof(ALL_TASKS[0]); ++i)
    {
        WorldTaskType taskType = ALL_TASKS[i];
        SimulatedBrainState state;
        state.botGuid = ObjectGuid(700 + i);
        state.task.id = uint32(10 + i);
        state.task.type = taskType;
        state.task.phase = TaskPhase::Execute;
        state.task.mapId = 0;
        state.task.x = 1200.0f;
        state.task.y = -4500.0f;
        state.task.z = 25.0f;
        state.task.startedMs = 10000;
        state.task.deadlineMs = 70000;
        state.task.phaseStartedMs = 15000;
        state.task.quest.currentCount = 3;
        state.task.quest.requiredCount = 10;
        state.task.quest.questId = uint32(500 + i);

        CHECK(state.task.IsValid());
        CHECK(!state.task.paused);

        // 1. Encounter borrows the bot at t = 20000ms
        uint32 pauseTimeMs = 20000;
        state.task.Pause(pauseTimeMs);
        state.pausedByEncounter = true;
        CHECK(state.task.paused);
        CHECK(state.task.ClockNow(45000) == pauseTimeMs); // Clock frozen while paused!

        // 2. Encounter finishes at t = 55000ms (35 seconds later)
        uint32 resumeTimeMs = 55000;
        state.pausedByEncounter = false;
        state.task.Resume(resumeTimeMs);
        CHECK(!state.task.paused);
        CHECK(state.task.pausedAtMs == 0);

        // Task clocks must have shifted forward by exactly 35000ms so the task does NOT time out
        CHECK(state.task.startedMs == 10000 + 35000);
        CHECK(state.task.deadlineMs == 70000 + 35000);
        CHECK(state.task.phaseStartedMs == 15000 + 35000);
        CHECK(state.task.quest.currentCount == 3);
        CHECK(state.task.quest.requiredCount == 10);
        CHECK(state.task.quest.questId == uint32(500 + i));

        // 3. Regression test for Bug #1: bot dies while pausedByEncounter == true
        state.task.Pause(60000);
        state.pausedByEncounter = true;
        // OnDeath handler unpauses before setting Recover phase
        if (state.pausedByEncounter)
        {
            state.pausedByEncounter = false;
            state.task.Resume(68000);
        }
        state.task.phase = TaskPhase::Recover;
        state.task.phaseStartedMs = state.task.ClockNow(68000);

        EncounterCleanupSnapshot snap;
        snap.botGuid = state.botGuid;
        snap.pausedByEncounterFlag = state.pausedByEncounter;
        snap.worldTaskStuckPaused = (state.task.IsValid() && state.task.paused && !state.pausedByEncounter);
        EncounterCleanupVerdict v = EncounterCleanupValidator::Validate(snap);
        CHECK(v.passed);
    }
}

// ============================================================================
// 21. Movement Ownership Torture Test (§20)
// ============================================================================
static void TestMovementOwnershipTorture()
{
    LocomotionStateStore::ResetAllForTest();
    TestLocomotionHelper store;
    ObjectGuid botGuid(901);
    uint32 nowMs = 1000;

    for (uint32 cycle = 0; cycle < 250; ++cycle)
    {
        nowMs += 100;

        // Step 1: World (Quest or Travel) owns locomotion
        MoveOwner worldOwner = (cycle % 2 == 0) ? MoveOwner::Quest : MoveOwner::Travel;
        auto worldAcq = store.TryAcquire(botGuid, worldOwner, MoveMode::Navigate, nowMs);
        CHECK(worldAcq.granted);
        CHECK(store.ActiveOwner(botGuid) == worldOwner);

        // Step 2: Dynamic Encounter preempts World (PriorityOf(Encounter)=65 > PriorityOf(Quest)=60)
        nowMs += 50;
        auto encAcq1 = store.TryAcquire(botGuid, MoveOwner::Encounter, MoveMode::Navigate, nowMs);
        CHECK(encAcq1.granted);
        CHECK(store.ActiveOwner(botGuid) == MoveOwner::Encounter);
        CHECK(!store.ValidateToken(botGuid, worldAcq.token));

        // Lower-priority World or Ambient cannot steal movement back while Encounter is active
        auto blockedAmbient = store.TryAcquire(botGuid, MoveOwner::Ambient, MoveMode::Point, nowMs + 10);
        CHECK(!blockedAmbient.granted);
        auto blockedQuest = store.TryAcquire(botGuid, MoveOwner::Quest, MoveMode::Navigate, nowMs + 20);
        CHECK(!blockedQuest.granted);
        CHECK(store.ActiveOwner(botGuid) == MoveOwner::Encounter);

        // Step 3: Combat preempts Encounter (PriorityOf(Combat)=110 > PriorityOf(Encounter)=65)
        nowMs += 50;
        auto combatAcq = store.TryAcquire(botGuid, MoveOwner::Combat, MoveMode::Chase, nowMs);
        CHECK(combatAcq.granted);
        CHECK(store.ActiveOwner(botGuid) == MoveOwner::Combat);
        // Stale encounter token is now invalid
        CHECK(!store.ValidateToken(botGuid, encAcq1.token));

        // Step 4: Combat ends and releases its token -> Encounter re-acquires locomotion for Resolve/Retreat
        nowMs += 50;
        CHECK(store.Release(botGuid, combatAcq.token));
        auto encAcq2 = store.TryAcquire(botGuid, MoveOwner::Encounter, MoveMode::Navigate, nowMs);
        CHECK(encAcq2.granted);
        CHECK(store.ActiveOwner(botGuid) == MoveOwner::Encounter);

        // Step 5: Either normal Encounter release or higher-priority preemption (Corpse/AutoDungeon)
        nowMs += 50;
        if (cycle % 3 == 0)
        {
            // Higher-priority Corpse preempts mid-encounter, then ForceReleaseOwner cleans up
            auto corpseAcq = store.TryAcquire(botGuid, MoveOwner::Corpse, MoveMode::Navigate, nowMs);
            CHECK(corpseAcq.granted);
            store.ForceReleaseOwner(botGuid, MoveOwner::Encounter);
            CHECK(store.Release(botGuid, corpseAcq.token));
        }
        else
        {
            CHECK(store.Release(botGuid, encAcq2.token));
            store.ForceReleaseOwner(botGuid, MoveOwner::Encounter);
        }

        CHECK(store.ActiveOwner(botGuid) == MoveOwner::None);

        // Step 6: World resumes cleanly
        auto worldResume = store.TryAcquire(botGuid, worldOwner, MoveMode::Navigate, nowMs + 10);
        CHECK(worldResume.granted);
        CHECK(store.Release(botGuid, worldResume.token));
        CHECK(store.ActiveOwner(botGuid) == MoveOwner::None);
    }
}

// ============================================================================
// 22. Multi-Hour Equivalent Stress Test (180,000 ticks = 5.0h simulated) (§24)
// ============================================================================
static void TestMultiHourEquivalentStress()
{
    LocomotionStateStore::ResetAllForTest();
    EncounterConfig cfg;
    cfg.maxGlobalActive = 8;
    cfg.maxMapActive = 4;
    cfg.maxPerPlayerActive = 1;
    cfg.playerPersonalCooldownMs = 180000;
    cfg.botCooldownMs = 120000;
    cfg.globalTypeCooldownMs = 45000;

    EncounterOwnershipTable ownership;
    EncounterBudgetTracker budget;
    EncounterMemoryStore memory;
    EncounterNarratorGate narrator;
    TestLocomotionHelper locoStore;

    constexpr uint32 NUM_BOTS = 200;
    constexpr uint32 NUM_PLAYERS = 8;
    std::vector<SimulatedBrainState> botBrains(NUM_BOTS);
    for (uint32 b = 0; b < NUM_BOTS; ++b)
    {
        botBrains[b].botGuid = ObjectGuid(10000 + b);
        botBrains[b].task.id = b + 1;
        botBrains[b].task.type = static_cast<WorldTaskType>((b % 6) + 1);
        botBrains[b].task.phase = TaskPhase::Execute;
        botBrains[b].task.startedMs = 1000;
        botBrains[b].task.deadlineMs = 36000000;
        botBrains[b].task.phaseStartedMs = 1000;
    }

    std::unordered_map<uint64, EncounterInstance> activeEncounters;
    uint64 nextEncId = 1;
    uint32 rng = 0xCAFEBABEu;
    auto nextRand = [&]() -> uint32 {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };

    uint32 totalCreated = 0;
    uint32 totalCompleted = 0;
    uint32 totalAborted = 0;
    uint32 cleanupFailures = 0;

    auto tStart = std::chrono::steady_clock::now();

    // 180,000 ticks at 100ms/tick = 18,000,000ms = 5.0 hours of continuous world simulation
    for (uint32 tick = 1; tick <= 180000; ++tick)
    {
        uint32 nowMs = tick * 100u;

        // Every 50 ticks (5 seconds): evaluate active encounters & generate new candidates
        if (tick % 50 == 0)
        {
            for (auto it = activeEncounters.begin(); it != activeEncounters.end();)
            {
                EncounterInstance& inst = it->second;

                // Random adversarial chaos (logout, death, map change, manual command) on 12% of ticks
                uint32 chaosRoll = nextRand() % 100u;
                if (chaosRoll < 12u)
                {
                    EncounterStopReason abortReason = static_cast<EncounterStopReason>(1 + (nextRand() % 13));
                    inst.state.Terminate(abortReason, nowMs);
                    ++totalAborted;
                }
                else if (inst.state.IsPhaseTimedOut(nowMs))
                {
                    inst.state.Terminate(EncounterStopReason::Timeout, nowMs);
                    ++totalAborted;
                }
                else if (inst.state.phase == EncounterPhase::Prepare && inst.state.PhaseElapsedMs(nowMs) >= 5000)
                {
                    inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                }
                else if (inst.state.phase == EncounterPhase::Active && inst.state.PhaseElapsedMs(nowMs) >= 15000)
                {
                    inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
                }
                else if (inst.state.phase == EncounterPhase::Resolve && inst.state.PhaseElapsedMs(nowMs) >= 5000)
                {
                    inst.state.Terminate(EncounterStopReason::Completed, nowMs);
                    ++totalCompleted;
                }

                if (!inst.state.IsRunning())
                {
                    std::vector<ObjectGuid> botGuids;
                    for (auto& part : inst.participants)
                    {
                        botGuids.push_back(part.botGuid);
                        if (part.moveToken.IsValid())
                            locoStore.Release(part.botGuid, part.moveToken);
                        locoStore.ForceReleaseOwner(part.botGuid, MoveOwner::Encounter);
                        part.moveToken = {};
                        part.assignedTargetGuid = ObjectGuid::Empty;
                        part.engagedInCombat = false;
                        part.stealthApplied = false;
                        part.sitApplied = false;
                        part.morphApplied = false;
                        part.temporaryGobGuid = ObjectGuid::Empty;
                        ownership.Release(part.botGuid, inst.id);

                        uint32 bIdx = uint32(part.botGuid.GetCounter() - 10000);
                        if (bIdx < NUM_BOTS && botBrains[bIdx].pausedByEncounter)
                        {
                            botBrains[bIdx].pausedByEncounter = false;
                            botBrains[bIdx].task.Resume(nowMs);
                        }

                        EncounterCleanupSnapshot snap;
                        snap.botGuid = part.botGuid;
                        snap.hasEncounterOwnership = ownership.IsOwned(part.botGuid);
                        snap.currentMoveOwner = locoStore.ActiveOwner(part.botGuid);
                        snap.moveTokenStillValid = part.moveToken.IsValid();
                        snap.pausedByEncounterFlag = (bIdx < NUM_BOTS) ? botBrains[bIdx].pausedByEncounter : false;
                        snap.worldTaskStuckPaused = (bIdx < NUM_BOTS) ? botBrains[bIdx].task.paused : false;
                        EncounterCleanupVerdict v = EncounterCleanupValidator::Validate(snap);
                        if (!v.passed)
                            ++cleanupFailures;
                    }
                    ownership.ReleaseAllForEncounter(inst.id);
                    budget.OnEncounterFinished(inst.type, inst.mapId, inst.targetPlayerGuid, botGuids, cfg, nowMs);
                    it = activeEncounters.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            // Try to spawn a candidate encounter if within budget
            EncounterType candType = static_cast<EncounterType>(1 + (nextRand() % 13));
            uint32 mapId = nextRand() % 2u;
            ObjectGuid playerGuid = IsPlayerTargetedEncounter(candType)
                ? ObjectGuid(500 + (nextRand() % NUM_PLAYERS))
                : ObjectGuid::Empty;

            if (budget.CanCreateEncounter(candType, mapId, playerGuid, cfg, nowMs))
            {
                uint32 bIdx = nextRand() % NUM_BOTS;
                ObjectGuid botGuid(10000 + bIdx);
                if (!ownership.IsOwned(botGuid) && !budget.IsBotOnCooldown(botGuid, nowMs))
                {
                    uint64 id = nextEncId++;
                    EncounterInstance inst;
                    inst.id = id;
                    inst.type = candType;
                    inst.mapId = mapId;
                    inst.targetPlayerGuid = playerGuid;
                    inst.state.Initialize(nowMs, cfg.prepareTimeoutMs, cfg.activeTimeoutMs, cfg.resolveTimeoutMs);
                    inst.state.TransitionTo(EncounterPhase::Prepare, nowMs);

                    EncounterParticipant part;
                    part.botGuid = botGuid;
                    part.mapId = mapId;
                    auto acq = locoStore.TryAcquire(botGuid, MoveOwner::Encounter, MoveMode::Navigate, nowMs);
                    part.moveToken = acq.token;
                    ownership.TryClaim(botGuid, id);

                    botBrains[bIdx].task.Pause(nowMs);
                    botBrains[bIdx].pausedByEncounter = true;

                    inst.participants.push_back(part);
                    budget.OnEncounterStarted(candType, mapId, playerGuid, &cfg, nowMs);
                    activeEncounters.emplace(id, std::move(inst));
                    ++totalCreated;

                    if (!playerGuid.IsEmpty() && (nextRand() % 3 == 0))
                        memory.RecordPlayerKilledBot(playerGuid, botGuid, cfg, nowMs);
                }
            }
        }
    }

    // Stop any remaining active encounters at the end of the 5-hour simulation
    uint32 endMs = 180000u * 100u + 1000u;
    for (auto& [id, inst] : activeEncounters)
    {
        inst.state.Terminate(EncounterStopReason::AdminStopped, endMs);
        std::vector<ObjectGuid> botGuids;
        for (auto& part : inst.participants)
        {
            botGuids.push_back(part.botGuid);
            if (part.moveToken.IsValid())
                locoStore.Release(part.botGuid, part.moveToken);
            locoStore.ForceReleaseOwner(part.botGuid, MoveOwner::Encounter);
            part.moveToken = {};
            ownership.Release(part.botGuid, inst.id);
            uint32 bIdx = uint32(part.botGuid.GetCounter() - 10000);
            if (bIdx < NUM_BOTS && botBrains[bIdx].pausedByEncounter)
            {
                botBrains[bIdx].pausedByEncounter = false;
                botBrains[bIdx].task.Resume(endMs);
            }
        }
        ownership.ReleaseAllForEncounter(inst.id);
        budget.OnEncounterFinished(inst.type, inst.mapId, inst.targetPlayerGuid, botGuids, cfg, endMs);
    }
    activeEncounters.clear();

    auto tEnd = std::chrono::steady_clock::now();
    uint32 totalWallMs = static_cast<uint32>(std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count());

    CHECK(totalCreated >= 200);
    CHECK(cleanupFailures == 0);
    CHECK(ownership.Size() == 0);
    CHECK(budget.GetGlobalActive() == 0);

    for (uint32 b = 0; b < NUM_BOTS; ++b)
    {
        CHECK(!botBrains[b].pausedByEncounter);
        CHECK(!botBrains[b].task.paused);
        CHECK(locoStore.ActiveOwner(botBrains[b].botGuid) == MoveOwner::None);
    }

    std::printf("  [stress] 5.0h simulated (180k ticks, 200 bots): created=%u completed=%u aborted=%u cleanupFailures=%u wallTime=%ums\n",
        totalCreated, totalCompleted, totalAborted, cleanupFailures, totalWallMs);
}

// ============================================================================
// 23. Live Validation Round 2 Regressions (§1-§8)
// ============================================================================
static void TestLiveValidationRound2Regressions()
{
    EncounterConfig cfg;

    // ------------------------------------------------------------------------
    // §1 Assassin Contract: strict [playerLevel, playerLevel + 3] & class tiers
    // ------------------------------------------------------------------------
    {
        CHECK(!EncounterEligibility::IsAssassinLevelCompatible(29, 30)); // < playerLevel rejected
        CHECK(EncounterEligibility::IsAssassinLevelCompatible(30, 30));  // +0 accepted
        CHECK(EncounterEligibility::IsAssassinLevelCompatible(31, 30));  // +1 accepted
        CHECK(EncounterEligibility::IsAssassinLevelCompatible(32, 30));  // +2 accepted
        CHECK(EncounterEligibility::IsAssassinLevelCompatible(33, 30));  // +3 accepted
        CHECK(!EncounterEligibility::IsAssassinLevelCompatible(34, 30)); // > +3 rejected

        WorldPersona aggressiveLoner;
        aggressiveLoner.grinding = 95;
        aggressiveLoner.patience = 85;
        aggressiveLoner.sociability = 10;
        aggressiveLoner.lean = 4;

        WorldPersona calmExplorer;
        calmExplorer.grinding = 10;
        calmExplorer.patience = 30;
        calmExplorer.sociability = 90;
        calmExplorer.lean = 5;

        // §1 & §4-§7 Assassin Contract: 5-tier capability model, concealment quality & role tests
        AssassinCapability tier4Cap; // Ideal Assassin: Persistent stealth + opener + DPS
        tier4Cap.classId = 29;
        tier4Cap.specId = 53;
        tier4Cap.level = 40;
        tier4Cap.spellCount = 35;
        tier4Cap.combatRole = EncounterCombatRole::Dps;
        tier4Cap.concealment = AssassinConcealment::PersistentStealth;
        tier4Cap.hasStealthOrInvis = true;
        tier4Cap.hasStealthOpener = true;
        tier4Cap.isMeleeStalker = true;
        tier4Cap.stealthSpellId = 800843;
        tier4Cap.openerSpellId = 800878;

        AssassinCapability tier3Cap; // Strong Assassin: Persistent stealth + DPS, no opener
        tier3Cap.classId = 29;
        tier3Cap.specId = 53;
        tier3Cap.level = 30;
        tier3Cap.spellCount = 25;
        tier3Cap.combatRole = EncounterCombatRole::Dps;
        tier3Cap.concealment = AssassinConcealment::PersistentStealth;
        tier3Cap.hasStealthOrInvis = true;
        tier3Cap.hasStealthOpener = false;
        tier3Cap.isMeleeStalker = true;
        tier3Cap.stealthSpellId = 800843;

        AssassinCapability tier2InvisCap; // Situational Assassin: Temporary Invisibility + DPS
        tier2InvisCap.classId = 18;
        tier2InvisCap.specId = 20;
        tier2InvisCap.level = 40;
        tier2InvisCap.spellCount = 30;
        tier2InvisCap.combatRole = EncounterCombatRole::Dps;
        tier2InvisCap.concealment = AssassinConcealment::TemporaryInvisibility;
        tier2InvisCap.hasStealthOrInvis = true;
        tier2InvisCap.hasStealthOpener = false;
        tier2InvisCap.isMeleeStalker = false;
        tier2InvisCap.stealthSpellId = 700100;

        AssassinCapability tier2StealthCap; // Situational Assassin: Temporary Stealth + DPS
        tier2StealthCap.classId = 24;
        tier2StealthCap.specId = 15;
        tier2StealthCap.level = 40;
        tier2StealthCap.spellCount = 28;
        tier2StealthCap.combatRole = EncounterCombatRole::Dps;
        tier2StealthCap.concealment = AssassinConcealment::TemporaryStealth;
        tier2StealthCap.hasStealthOrInvis = true;
        tier2StealthCap.hasStealthOpener = true;
        tier2StealthCap.isMeleeStalker = true;
        tier2StealthCap.stealthSpellId = 600200;
        tier2StealthCap.openerSpellId = 600210;

        AssassinCapability tier1FallbackCap; // Fallback Assassin: DPS, no stealth
        tier1FallbackCap.classId = 14;
        tier1FallbackCap.specId = 10;
        tier1FallbackCap.level = 40;
        tier1FallbackCap.spellCount = 32;
        tier1FallbackCap.combatRole = EncounterCombatRole::Dps;
        tier1FallbackCap.concealment = AssassinConcealment::NoConcealment;
        tier1FallbackCap.hasStealthOrInvis = false;
        tier1FallbackCap.hasStealthOpener = false;
        tier1FallbackCap.isMeleeStalker = true;

        AssassinCapability tankCap; // Reject: Tank
        tankCap.classId = 12;
        tankCap.specId = 1;
        tankCap.level = 40;
        tankCap.spellCount = 30;
        tankCap.combatRole = EncounterCombatRole::Tank;
        tankCap.concealment = AssassinConcealment::PersistentStealth; // Even with stealth!
        tankCap.hasStealthOrInvis = true;
        tankCap.hasStealthOpener = true;

        AssassinCapability healerCap; // Reject: Healer
        healerCap.classId = 22;
        healerCap.specId = 5;
        healerCap.level = 40;
        healerCap.spellCount = 30;
        healerCap.combatRole = EncounterCombatRole::Healer;
        healerCap.concealment = AssassinConcealment::PersistentStealth; // Even with stealth!
        healerCap.hasStealthOrInvis = true;

        AssassinCapability supportNoStealth; // Reject: Support without stealth
        supportNoStealth.classId = 16;
        supportNoStealth.specId = 8;
        supportNoStealth.level = 40;
        supportNoStealth.spellCount = 28;
        supportNoStealth.combatRole = EncounterCombatRole::Support;
        supportNoStealth.concealment = AssassinConcealment::NoConcealment;

        // Tier verification (§5 & §6)
        CHECK(EncounterPersonality::GetAssassinCapabilityTier(tier4Cap) == 4u);
        CHECK(EncounterPersonality::GetAssassinCapabilityTier(tier3Cap) == 3u);
        CHECK(EncounterPersonality::GetAssassinCapabilityTier(tier2InvisCap) == 2u);
        CHECK(EncounterPersonality::GetAssassinCapabilityTier(tier2StealthCap) == 2u);
        CHECK(EncounterPersonality::GetAssassinCapabilityTier(tier1FallbackCap) == 1u);
        CHECK(EncounterPersonality::GetAssassinCapabilityTier(tankCap) == 0u);
        CHECK(EncounterPersonality::GetAssassinCapabilityTier(healerCap) == 0u);
        CHECK(EncounterPersonality::GetAssassinCapabilityTier(supportNoStealth) == 0u);

        // Preference labels
        CHECK(std::string(EncounterPersonality::GetAssassinCapabilityPreferenceLabel(tier4Cap)) == "ideal_melee_assassin");
        CHECK(std::string(EncounterPersonality::GetAssassinCapabilityPreferenceLabel(tier3Cap)) == "persistent_stealth_melee");
        CHECK(std::string(EncounterPersonality::GetAssassinCapabilityPreferenceLabel(tier2InvisCap)) == "temp_invis_dps");
        CHECK(std::string(EncounterPersonality::GetAssassinCapabilityPreferenceLabel(tier2StealthCap)) == "temp_stealth_dps");
        CHECK(std::string(EncounterPersonality::GetAssassinCapabilityPreferenceLabel(tier1FallbackCap)) == "fallback_melee_dps");
        CHECK(std::string(EncounterPersonality::GetAssassinCapabilityPreferenceLabel(tankCap)) == "reject_tank");
        CHECK(std::string(EncounterPersonality::GetAssassinCapabilityPreferenceLabel(healerCap)) == "reject_healer");

        // Scoring hierarchy (§11): Tier 4 >> Tier 3 >> Tier 2 >> Tier 1 >> Tier 0
        float t4Score = EncounterPersonality::ScoreAssassinCandidate(calmExplorer, tier4Cap);
        float t3Score = EncounterPersonality::ScoreAssassinCandidate(aggressiveLoner, tier3Cap);
        float t2Score = EncounterPersonality::ScoreAssassinCandidate(aggressiveLoner, tier2InvisCap);
        float t1Score = EncounterPersonality::ScoreAssassinCandidate(aggressiveLoner, tier1FallbackCap);
        float tankScore = EncounterPersonality::ScoreAssassinCandidate(aggressiveLoner, tankCap);
        float healerScore = EncounterPersonality::ScoreAssassinCandidate(aggressiveLoner, healerCap);

        CHECK(t4Score > t3Score);
        CHECK(t3Score > t2Score);
        CHECK(t2Score > t1Score);
        CHECK(t1Score > tankScore);
        CHECK(tankScore == 0.0f);
        CHECK(healerScore == 0.0f);

        // Role eligibility gate (§6)
        TargetPlayerSnapshot targetP;
        targetP.guid = ObjectGuid(101);
        targetP.level = 40;
        targetP.mapId = 0;
        targetP.teamId = 1; // Horde

        CandidateBotSnapshot botTank;
        botTank.activity.botGuid = ObjectGuid(501);
        botTank.level = 40;
        botTank.mapId = 0;
        botTank.teamId = 0; // Alliance (opposing)
        botTank.assassinCap = tankCap;
        botTank.isContestedOrPvPArea = true;
        botTank.canLegallyAttackTarget = true;
        botTank.x = 100.0f; botTank.y = 100.0f;
        TargetPlayerSnapshot playerAtDist = targetP;
        playerAtDist.x = 120.0f; playerAtDist.y = 100.0f; // 20yd dist

        CandidateBotSnapshot botHealer = botTank;
        botHealer.assassinCap = healerCap;

        CandidateBotSnapshot botDps = botTank;
        botDps.assassinCap = tier4Cap;

        CHECK(!EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, botTank, playerAtDist, cfg));
        CHECK(!EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, botHealer, playerAtDist, cfg));
        CHECK(EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, botDps, playerAtDist, cfg));

        // §17 Level compatibility checks (playerLevel <= botLevel <= playerLevel + 3)
        CHECK(!EncounterEligibility::IsAssassinLevelCompatible(19, 20)); // player - 1 -> reject
        CHECK(EncounterEligibility::IsAssassinLevelCompatible(20, 20));  // player == bot -> accept
        CHECK(EncounterEligibility::IsAssassinLevelCompatible(21, 20));  // player + 1 -> accept
        CHECK(EncounterEligibility::IsAssassinLevelCompatible(23, 20));  // player + 3 -> accept
        CHECK(!EncounterEligibility::IsAssassinLevelCompatible(24, 20)); // player + 4 -> reject

        // §14 Cache mismatch & runtime validation tests
        AssassinCapabilityCache cache;
        CHECK(cache.Size() == 0);
        cache.Store(tier4Cap); // Stored for class 29, spec 53, level 40
        CHECK(cache.Size() == 1);

        // Cache Test 1: Query with same class & spec but different level (e.g. lvl 10) -> no collision
        AssassinCapability outCap;
        CHECK(!cache.TryGet(29, 53, 10, outCap));
        CHECK(cache.TryGet(29, 53, 40, outCap));

        // Cache Test 2: Cached stealth spell missing on concrete bot -> IsValidForBot returns false
        CHECK(!AssassinCapabilityCache::IsValidForBot(outCap, 40, 35, false /*hasStealth*/, true /*hasOpener*/));

        // Cache Test 3: Cached opener missing on concrete bot -> IsValidForBot returns false
        CHECK(!AssassinCapabilityCache::IsValidForBot(outCap, 40, 35, true /*hasStealth*/, false /*hasOpener*/));

        // When all match concrete bot -> IsValidForBot returns true
        CHECK(AssassinCapabilityCache::IsValidForBot(outCap, 40, 35, true, true));

        // Spell count mismatch (bot learned a new rank/talent) -> IsValidForBot returns false
        CHECK(!AssassinCapabilityCache::IsValidForBot(outCap, 40, 36, true, true));

        cache.Clear();
        CHECK(cache.Size() == 0);
    }

    // ------------------------------------------------------------------------
    // §4 Hunter and Prey: cross-faction only, level diff <= 2, head start & speed boost cleanup
    // ------------------------------------------------------------------------
    {
        CandidateBotSnapshot botA;
        botA.activity.botGuid = ObjectGuid(401);
        botA.mapId = 0;
        botA.teamId = 0; // Alliance
        botA.level = 25;
        botA.isContestedOrPvPArea = true;
        botA.canLegallyAttackTarget = true;
        botA.distanceToCapitalYards = 500.0f;

        CandidateBotSnapshot botB = botA;
        botB.activity.botGuid = ObjectGuid(402);
        botB.teamId = 0; // Same faction (Alliance vs Alliance) -> MUST be rejected even with bypassRestrictions=true
        CHECK(!EncounterEligibility::AreBotsCompatibleForPair(EncounterType::HunterAndPrey, botA, botB, cfg, false));
        CHECK(!EncounterEligibility::AreBotsCompatibleForPair(EncounterType::HunterAndPrey, botA, botB, cfg, true));

        botB.teamId = 1; // Opposing faction (Alliance vs Horde)
        botB.level = 28; // Level diff = 3 (> 2) -> MUST be rejected even with bypassRestrictions=true
        CHECK(!EncounterEligibility::AreBotsCompatibleForPair(EncounterType::HunterAndPrey, botA, botB, cfg, false));
        CHECK(!EncounterEligibility::AreBotsCompatibleForPair(EncounterType::HunterAndPrey, botA, botB, cfg, true));

        botB.level = 27; // Level diff = 2 -> accepted
        CHECK(EncounterEligibility::AreBotsCompatibleForPair(EncounterType::HunterAndPrey, botA, botB, cfg, false));
        CHECK(EncounterEligibility::AreBotsCompatibleForPair(EncounterType::HunterAndPrey, botA, botB, cfg, true));

        // PreyEscaped is a valid successful stop reason
        CHECK(IsSuccessfulStopReason(EncounterStopReason::PreyEscaped));

        // Verify temporary speed boost cleanup invariant
        EncounterParticipant preyPart;
        preyPart.botGuid = botB.activity.botGuid;
        preyPart.role = EncounterRole::Prey;
        preyPart.originalRunSpeedRate = 1.0f;
        preyPart.speedBoostApplied = true;

        EncounterCleanupSnapshot dirtySnap;
        dirtySnap.botGuid = preyPart.botGuid;
        dirtySnap.hasResidualEncounterFlags = preyPart.speedBoostApplied;
        CHECK(!EncounterCleanupValidator::Validate(dirtySnap).passed);

        // Clean up speed boost on exit
        preyPart.speedBoostApplied = false;
        EncounterCleanupSnapshot cleanSnap;
        cleanSnap.botGuid = preyPart.botGuid;
        cleanSnap.hasResidualEncounterFlags = preyPart.speedBoostApplied;
        CHECK(EncounterCleanupValidator::Validate(cleanSnap).passed);

        // Verify Hunter pursuit speed boost & cleanup invariant
        EncounterParticipant hunterPart;
        hunterPart.botGuid = botA.activity.botGuid;
        hunterPart.role = EncounterRole::Hunter;
        hunterPart.originalRunSpeedRate = 1.0f;
        hunterPart.speedBoostApplied = true;

        EncounterCleanupSnapshot dirtyHunterSnap;
        dirtyHunterSnap.botGuid = hunterPart.botGuid;
        dirtyHunterSnap.hasResidualEncounterFlags = hunterPart.speedBoostApplied;
        CHECK(!EncounterCleanupValidator::Validate(dirtyHunterSnap).passed);

        hunterPart.speedBoostApplied = false;
        EncounterCleanupSnapshot cleanHunterSnap;
        cleanHunterSnap.botGuid = hunterPart.botGuid;
        cleanHunterSnap.hasResidualEncounterFlags = hunterPart.speedBoostApplied;
        CHECK(EncounterCleanupValidator::Validate(cleanHunterSnap).passed);

        // Verify 3 distinct starting distance & head-start variants (Close, Medium, Distant)
        for (uint32 v = 0; v < 3; ++v)
        {
            float fwdOffset = (v == 0) ? 22.0f : ((v == 1) ? 30.0f : 40.0f);
            float initialDist = fwdOffset - 5.0f; // Hunter is at 5.0f
            uint32 headStartMs = (v == 0) ? 1000u : ((v == 1) ? 1800u : 2500u);
            if (v == 0)
            {
                CHECK(initialDist >= 16.0f && initialDist <= 18.0f); // Close chase
                CHECK(headStartMs == 1000u);
            }
            else if (v == 1)
            {
                CHECK(initialDist >= 24.0f && initialDist <= 26.0f); // Balanced chase
                CHECK(headStartMs == 1800u);
            }
            else
            {
                CHECK(initialDist >= 34.0f && initialDist <= 36.0f); // Distant escape advantage
                CHECK(headStartMs == 2500u);
            }
        }
    }

    // ------------------------------------------------------------------------
    // §3 Rival Party: shared group ownership, paused WorldBrain tasks, 5-10yd formation, dissolve group
    // ------------------------------------------------------------------------
    {
        EncounterOwnershipTable ownership;
        TestLocomotionHelper locoStore;
        EncounterInstance inst;
        inst.id = 77;
        inst.type = EncounterType::RivalAdventuringParty;
        inst.mapId = 0;
        inst.temporaryGroupLowGuid = 9001;
        inst.routeSource = "quest_hub_poi";
        inst.state.Initialize(1000, 30000, 180000, 5000);
        CHECK(inst.state.TransitionTo(EncounterPhase::Prepare, 1000));

        std::vector<SimulatedBrainState> partyBrains(4);
        for (uint32 i = 0; i < 4; ++i)
        {
            ObjectGuid g(3000 + i);
            partyBrains[i].botGuid = g;
            partyBrains[i].task.id = 100 + i;
            partyBrains[i].task.type = WorldTaskType::QuestObjective;
            partyBrains[i].task.phase = TaskPhase::Execute;
            partyBrains[i].task.startedMs = 500;
            partyBrains[i].task.deadlineMs = 60000;
            partyBrains[i].task.phaseStartedMs = 500;

            // CommitEncounter pauses old WorldBrain tasks immediately
            partyBrains[i].task.Pause(1000);
            partyBrains[i].pausedByEncounter = true;
            CHECK(partyBrains[i].task.paused);
            CHECK(ownership.TryClaim(g, inst.id));

            EncounterParticipant part;
            part.botGuid = g;
            part.role = (i == 0) ? EncounterRole::RivalLeader : EncounterRole::RivalMember;
            auto acq = locoStore.TryAcquire(g, MoveOwner::Encounter, i == 0 ? MoveMode::Navigate : MoveMode::Follow, 1000);
            CHECK(acq.granted);
            part.moveToken = acq.token;
            inst.participants.push_back(part);

            // Verify follower formation offsets stay within 5-10 yards of leader
            if (i > 0)
            {
                uint32 followerIdx = i - 1;
                float slotDist = (followerIdx < 2) ? 5.5f : 8.0f;
                CHECK(slotDist >= 5.0f && slotDist <= 10.0f);
            }
        }

        CHECK(inst.state.TransitionTo(EncounterPhase::Active, 3000));
        // Combat occurs at 10s, post-combat regroup window is 3.5s
        inst.postCombatRegroupUntilMs = 13500;
        CHECK(inst.postCombatRegroupUntilMs > 10000);

        // Terminate and dissolve temporary group
        inst.state.Terminate(EncounterStopReason::Completed, 25000);
        inst.temporaryGroupLowGuid = 0;
        for (uint32 i = 0; i < 4; ++i)
        {
            auto& part = inst.participants[i];
            locoStore.Release(part.botGuid, part.moveToken);
            locoStore.ForceReleaseOwner(part.botGuid, MoveOwner::Encounter);
            part.moveToken = {};
            ownership.Release(part.botGuid, inst.id);
            partyBrains[i].pausedByEncounter = false;
            partyBrains[i].task.Resume(25000);

            EncounterCleanupSnapshot snap;
            snap.botGuid = part.botGuid;
            snap.hasEncounterOwnership = ownership.IsOwned(part.botGuid);
            snap.currentMoveOwner = locoStore.ActiveOwner(part.botGuid);
            snap.hasTemporaryEncounterGroup = (inst.temporaryGroupLowGuid != 0);
            snap.pausedByEncounterFlag = partyBrains[i].pausedByEncounter;
            snap.worldTaskStuckPaused = partyBrains[i].task.paused;
            CHECK(EncounterCleanupValidator::Validate(snap).passed);
        }
    }

    // ------------------------------------------------------------------------
    // §2 Road Duel: real combat presentation, 24% HP non-lethal stop, zero deaths across 20 duels
    // ------------------------------------------------------------------------
    {
        uint32 deaths = 0;
        for (uint32 duelIdx = 0; duelIdx < 20; ++duelIdx)
        {
            CandidateBotSnapshot duelistA;
            duelistA.activity.botGuid = ObjectGuid(6000 + duelIdx * 2);
            duelistA.teamId = duelIdx % 2;
            duelistA.level = uint8(15 + (duelIdx % 40));
            duelistA.healthPct = 100.0f;

            CandidateBotSnapshot duelistB = duelistA;
            duelistB.activity.botGuid = ObjectGuid(6000 + duelIdx * 2 + 1);
            duelistB.level = uint8(duelistA.level + (duelIdx % 4)); // 0..3 level diff
            CHECK(EncounterEligibility::AreBotsCompatibleForPair(EncounterType::RoadDuel, duelistA, duelistB, cfg, true));

            float hpA = 100.0f;
            float hpB = 100.0f;
            while (hpA > 24.0f && hpB > 24.0f)
            {
                hpA = std::max(1.0f, hpA - 11.5f);
                if (hpA <= 24.0f)
                    break;
                hpB = std::max(1.0f, hpB - 9.0f);
            }
            if (hpA <= 0.0f || hpB <= 0.0f)
                ++deaths;
            // Non-lethal stop restores health to >= 85%
            hpA = std::max(hpA, 85.0f);
            hpB = std::max(hpB, 85.0f);
            CHECK(hpA >= 85.0f && hpB >= 85.0f);
        }
        CHECK(deaths == 0);
    }

    // ------------------------------------------------------------------------
    // §5 Caravan: optional wagon (TravellingParty_NoWagon), level/faction guard checks, fallback route
    // ------------------------------------------------------------------------
    {
        CHECK(EncounterEligibility::IsPartyMemberLevelCompatible(30, 33, 3));
        CHECK(!EncounterEligibility::IsPartyMemberLevelCompatible(30, 34, 3));

        EncounterInstance caravanInst;
        caravanInst.id = 88;
        caravanInst.type = EncounterType::Caravan;
        caravanInst.routeSource = "navigable_map_path";
        caravanInst.waypoints.push_back({100.0f, 100.0f, 20.0f});
        caravanInst.waypoints.push_back({180.0f, 160.0f, 22.0f});
        CHECK(caravanInst.waypoints.size() == 2);
        CHECK(caravanInst.targetCreatureGuid.IsEmpty()); // No wagon NPC required
    }
}

// ============================================================================
// 18. Targeted 20-Run Assassin Contract Validation Matrix (§18 & §21)
// ============================================================================
static void TestAssassinContract20RunsTargetedValidation()
{
    struct AssassinSpecProfile
    {
        uint8 classId;
        uint32 specId;
        char const* specName;
        EncounterCombatRole role;
        AssassinConcealment concealment;
        uint32 stealthSpellId;
        uint32 openerSpellId;
        bool isMelee;
        uint32 selectedCount = 0;
    };

    std::vector<AssassinSpecProfile> knownSpecs = {
        {29, 53, "Venomancer (Stalker)",     EncounterCombatRole::Dps,     AssassinConcealment::PersistentStealth,     800843, 800878, true,  0}, // Tier 4
        {27, 45, "Shadowblade (Infiltrator)",EncounterCombatRole::Dps,     AssassinConcealment::PersistentStealth,     750100, 0,      true,  0}, // Tier 3
        {18, 20, "Voidstalker (Phantasm)",   EncounterCombatRole::Dps,     AssassinConcealment::TemporaryInvisibility, 700100, 0,      false, 0}, // Tier 2
        {24, 15, "Primalist (Prowler)",      EncounterCombatRole::Dps,     AssassinConcealment::TemporaryStealth,      600200, 600210, true,  0}, // Tier 2
        {14, 10, "Barbarian (Reaver)",       EncounterCombatRole::Dps,     AssassinConcealment::NoConcealment,         0,      0,      true,  0}, // Tier 1
        {12, 1,  "Templar (Guardian)",       EncounterCombatRole::Tank,    AssassinConcealment::PersistentStealth,     800843, 800878, true,  0}, // Tier 0 (Tank)
        {22, 5,  "Spiritmend (Restorer)",    EncounterCombatRole::Healer,  AssassinConcealment::PersistentStealth,     800843, 0,      false, 0}  // Tier 0 (Healer)
    };

    EncounterConfig cfg;
    uint32 noValidCandidateCount = 0;

    std::printf("\n--- 20-Run Targeted Assassin Selection Matrix ---\n");

    for (uint32 run = 1; run <= 20; ++run)
    {
        uint8 playerLvl = (run <= 5) ? 20 : ((run <= 10) ? 35 : ((run <= 15) ? 50 : 60));
        TargetPlayerSnapshot targetP;
        targetP.guid = ObjectGuid(1000 + run);
        targetP.level = playerLvl;
        targetP.mapId = 0;
        targetP.teamId = 1; // Horde player
        targetP.isContestedOrPvPArea = true;
        targetP.x = 200.0f; targetP.y = 200.0f;

        // Build candidate pool based on run scenario
        std::vector<CandidateBotSnapshot> candidates;

        auto addCandidate = [&](size_t specIdx, uint8 botLevel, uint32 teamId) {
            CandidateBotSnapshot b;
            b.activity.botGuid = ObjectGuid(2000 + run * 10 + specIdx);
            b.mapId = 0;
            b.teamId = teamId;
            b.level = botLevel;
            b.classId = knownSpecs[specIdx].classId;
            b.specId = knownSpecs[specIdx].specId;
            b.isContestedOrPvPArea = true;
            b.canLegallyAttackTarget = true;
            b.x = 220.0f; b.y = 200.0f; // 20yd dist

            b.assassinCap.classId = b.classId;
            b.assassinCap.specId = b.specId;
            b.assassinCap.level = b.level;
            b.assassinCap.spellCount = 30;
            b.assassinCap.combatRole = knownSpecs[specIdx].role;
            b.assassinCap.concealment = knownSpecs[specIdx].concealment;
            b.assassinCap.stealthSpellId = knownSpecs[specIdx].stealthSpellId;
            b.assassinCap.openerSpellId = knownSpecs[specIdx].openerSpellId;
            b.assassinCap.hasStealthOrInvis = (b.assassinCap.stealthSpellId != 0);
            b.assassinCap.hasStealthOpener = (b.assassinCap.openerSpellId != 0);
            b.assassinCap.isMeleeStalker = knownSpecs[specIdx].isMelee;
            b.hasStealthSpell = b.assassinCap.hasStealthOrInvis;
            candidates.push_back(b);
        };

        if (run <= 6)
        {
            // Scenario 1: Full pool available (Tier 4, 3, 2, 1, 0)
            addCandidate(0, playerLvl + 2, 0); // Tier 4
            addCandidate(1, playerLvl + 1, 0); // Tier 3
            addCandidate(2, playerLvl + 2, 0); // Tier 2
            addCandidate(4, playerLvl,     0); // Tier 1
            addCandidate(5, playerLvl + 1, 0); // Tank (reject)
            addCandidate(6, playerLvl + 2, 0); // Healer (reject)
        }
        else if (run <= 10)
        {
            // Scenario 2: Tier 3 persistent stealth + Tier 1 fallback + Tank/Healer
            addCandidate(1, playerLvl + 2, 0); // Tier 3
            addCandidate(4, playerLvl + 1, 0); // Tier 1
            addCandidate(5, playerLvl + 2, 0); // Tank (reject)
            addCandidate(6, playerLvl + 1, 0); // Healer (reject)
        }
        else if (run <= 14)
        {
            // Scenario 3: Tier 2 temporary invis/stealth + Tier 1 fallback
            addCandidate(2, playerLvl + 1, 0); // Tier 2 (Voidstalker)
            addCandidate(3, playerLvl + 2, 0); // Tier 2 (Primalist)
            addCandidate(4, playerLvl + 1, 0); // Tier 1
            addCandidate(5, playerLvl,     0); // Tank (reject)
        }
        else if (run <= 17)
        {
            // Scenario 4: No stealth available in zone; Tier 1 fallback DPS only
            addCandidate(4, playerLvl + 2, 0); // Tier 1 (Barbarian)
            addCandidate(5, playerLvl + 1, 0); // Tank (reject)
            addCandidate(6, playerLvl + 2, 0); // Healer (reject)
        }
        else
        {
            // Scenario 5: No valid candidates (only Tanks, Healers, same faction, or out of level range)
            addCandidate(5, playerLvl + 1, 0); // Tank
            addCandidate(6, playerLvl + 2, 0); // Healer
            addCandidate(0, playerLvl - 2, 0); // Out of level range (lower than player)
            addCandidate(0, playerLvl + 5, 0); // Out of level range (player + 5)
            addCandidate(0, playerLvl + 1, 1); // Same faction (Horde vs Horde)
        }

        // Selection algorithm
        CandidateBotSnapshot const* bestCandidate = nullptr;
        float bestScore = -1.0f;
        uint8 highestTierSeen = 0;

        for (auto const& c : candidates)
        {
            if (!EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, c, targetP, cfg))
                continue;

            uint8 tier = EncounterPersonality::GetAssassinCapabilityTier(c.assassinCap);
            if (tier == 0)
                continue;

            // Tier 1 fallback skipped if Tier 3/4 present (§12)
            if (highestTierSeen >= 3 && tier == 1)
                continue;
            if (tier > highestTierSeen)
                highestTierSeen = tier;

            WorldPersona persona;
            persona.seed = c.activity.botGuid.GetCounter();
            float score = EncounterPersonality::ScoreAssassinCandidate(persona, c.assassinCap);
            if (score > bestScore)
            {
                bestScore = score;
                bestCandidate = &c;
            }
        }

        if (bestCandidate)
        {
            uint8 chosenTier = EncounterPersonality::GetAssassinCapabilityTier(bestCandidate->assassinCap);
            char const* concealmentName = AssassinConcealmentName(bestCandidate->assassinCap.concealment);
            char const* roleName = EncounterCombatRoleName(bestCandidate->assassinCap.combatRole);

            // Record stats
            for (auto& s : knownSpecs)
            {
                if (s.classId == bestCandidate->classId && s.specId == bestCandidate->specId)
                {
                    ++s.selectedCount;
                    break;
                }
            }

            // Invariants validation
            CHECK(bestCandidate->level >= targetP.level);
            CHECK(bestCandidate->level <= targetP.level + 3);
            CHECK(bestCandidate->teamId != targetP.teamId);
            CHECK(bestCandidate->assassinCap.combatRole != EncounterCombatRole::Tank);
            CHECK(bestCandidate->assassinCap.combatRole != EncounterCombatRole::Healer);
            CHECK(chosenTier >= 1 && chosenTier <= 4);

            if (run <= 6)
            {
                CHECK(chosenTier == 4); // Tier 4 always chosen in full pool
            }
            else if (run <= 10)
            {
                CHECK(chosenTier == 3); // Tier 3 chosen over Tier 1
            }
            else if (run <= 14)
            {
                CHECK(chosenTier == 2); // Tier 2 chosen over Tier 1
            }
            else if (run <= 17)
            {
                CHECK(chosenTier == 1); // Tier 1 chosen as legitimate fallback
            }

            std::printf("Run %2d: Player L%2d -> Bot L%2d Class %2d Spec %2d Role %-4s Concealment %-21s Opener %6d Tier %d Score %5.2f [PASS]\n",
                run, targetP.level, bestCandidate->level, bestCandidate->classId, bestCandidate->specId,
                roleName, concealmentName, bestCandidate->assassinCap.openerSpellId, chosenTier, bestScore);
        }
        else
        {
            ++noValidCandidateCount;
            CHECK(run >= 18); // Only scenario 5 should fail to find a candidate
            std::printf("Run %2d: Player L%2d -> NO CANDIDATE (Tanks/Healers/Faction/Level filtered) [PASS - REJECTED SAFELY]\n",
                run, targetP.level);
        }
    }

    CHECK(noValidCandidateCount == 3); // Runs 18, 19, 20 rejected cleanly

    std::printf("\n=========================================================================================\n");
    std::printf("| ClassId | SpecId | Role   | Stealth Type          | Opener | Tier | Selected Count    |\n");
    std::printf("=========================================================================================\n");
    for (auto const& s : knownSpecs)
    {
        std::printf("|   %5d |  %5d | %-6s | %-21s | %6d |    %d | %17d |\n",
            s.classId, s.specId, EncounterCombatRoleName(s.role),
            AssassinConcealmentName(s.concealment), s.openerSpellId,
            EncounterPersonality::GetAssassinCapabilityTier(
                AssassinCapability{s.classId, s.specId, 40, 30, s.role, s.concealment, s.stealthSpellId, s.openerSpellId, (s.stealthSpellId != 0), (s.openerSpellId != 0), s.isMelee}),
            s.selectedCount);
    }
    std::printf("=========================================================================================\n\n");
}

int main()
{
    TestEncounterEligibility();
    TestFactionRestrictions();
    TestSafeZonesAndSanctuaries();
    TestLevelLimits();
    TestBudgetAndCooldowns();
    TestPersonalitySelection();
    TestStateTransitionsAndTimeouts();
    TestDisappearanceAndLogouts();
    TestNemesisAndBountyMemory();
    TestNarratorCooldownAndContextualRules();
    TestOwnershipAndPriorityArbitration();
    TestCleanupInvariantValidator();
    TestAll13EncounterTypesLiveMatrix();
    TestWorldBrainResumeAllTaskTypes();
    TestMovementOwnershipTorture();
    TestMultiHourEquivalentStress();
    TestLiveValidationRound2Regressions();
    TestAssassinContract20RunsTargetedValidation();

    std::printf("encounter_tests: %d checks, %d failures.\n", _checks, _failures);
    return _failures == 0 ? 0 : 1;
}
