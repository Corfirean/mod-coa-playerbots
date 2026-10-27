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
#include "EncounterBudget.h"
#include "EncounterEligibility.h"
#include "EncounterInstance.h"
#include "EncounterMemory.h"
#include "EncounterNarrator.h"
#include "EncounterParticipant.h"
#include "EncounterPersonality.h"
#include "EncounterState.h"
#include "EncounterTypes.h"

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
    bot.level = 34; // diff = 4 -> allowed
    bot.x = 40.0f;
    bot.y = 0.0f;
    bot.isContestedOrPvPArea = true;
    bot.canLegallyAttackTarget = true;

    CHECK(EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, bot, player, cfg));

    bot.level = 35; // diff = 5 -> rejected
    CHECK(!EncounterEligibility::IsBotEligibleForPlayerEncounter(EncounterType::AssassinContract, bot, player, cfg));

    bot.level = 25; // diff = 5 -> rejected
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

    std::printf("encounter_tests: %d checks, %d failures.\n", _checks, _failures);
    return _failures == 0 ? 0 : 1;
}
