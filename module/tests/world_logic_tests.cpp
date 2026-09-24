/*
 * mod-coa-playerbots -- standalone tests for the open-world layer's pure logic:
 * FailureMemory, ReservationTable, PopulationGrid, SpawnClustering, WorldUtility, SocialRules, plus
 * the task lifecycle rules: task clocks across pauses (WorldTask), navigation progress and the
 * recovery ladder (BotNavProgress), and the quest set-aside / abandon policy (QuestPolicy).
 *
 * These are the pieces that decide "don't retry that", "that mob is taken", "that camp is
 * crowded", "these spawns are one place", "this is worth doing", "is this bot stuck" and "has this
 * task run out of time" -- exactly the kind of logic that is easy to get subtly wrong and
 * impossible to observe directly on a live server.
 *
 * Build and run (no core checkout needed):
 *   g++ -std=c++20 -O1 -Wall -Wextra -I module/tests/stub -I module/src/world -I module/src \
 *       module/tests/world_logic_tests.cpp -o /tmp/world_logic_tests && /tmp/world_logic_tests
 */

#include "BotNavProgress.h"
#include "FailureMemory.h"
#include "PopulationGrid.h"
#include "QuestPolicy.h"
#include "ReservationTable.h"
#include "SocialRules.h"
#include "SpawnClustering.h"
#include "WorldTask.h"
#include "WorldUtility.h"
#include <cstdio>
#include <set>
#include <string>

namespace
{
    int _failures = 0;
    int _checks = 0;

    void Check(bool condition, char const* what, int line)
    {
        ++_checks;
        if (!condition)
        {
            ++_failures;
            std::printf("FAIL (line %d): %s\n", line, what);
        }
    }
}

#define CHECK(cond) Check((cond), #cond, __LINE__)

static void TestFailureMemory()
{
    FailureMemory memory;
    memory.Remember(FailKind::Target, 42, 1000, 60000, 1);
    CHECK(memory.Has(FailKind::Target, 42, 1000));
    CHECK(memory.Has(FailKind::Target, 42, 60999));
    CHECK(!memory.Has(FailKind::Target, 42, 61000));       // expired exactly at ttl
    CHECK(!memory.Has(FailKind::Cluster, 42, 2000));       // kinds are separate
    CHECK(memory.RemainingMs(FailKind::Target, 42, 31000) == 30000);

    // A repeat failure while still remembered earns a strike and a longer memory.
    memory.Remember(FailKind::Target, 42, 2000, 60000, 1);
    CHECK(memory.Strikes(FailKind::Target, 42, 2000) == 2);
    CHECK(memory.Has(FailKind::Target, 42, 2000 + 119999));
    CHECK(!memory.Has(FailKind::Target, 42, 2000 + 120000));

    // After it expired, the next failure starts over at one strike.
    memory.Remember(FailKind::Target, 42, 500000, 60000, 1);
    CHECK(memory.Strikes(FailKind::Target, 42, 500000) == 1);

    // Strike multiplier is capped.
    for (int i = 0; i < 10; ++i)
        memory.Remember(FailKind::Spawn, 7, 1000, 1000, 2);
    CHECK(memory.RemainingMs(FailKind::Spawn, 7, 1000) == 1000 * FailureMemory::MAX_STRIKE_MULTIPLIER);

    // Prune drops only expired entries.
    memory.Remember(FailKind::Npc, 1, 0, 10, 0);
    memory.Remember(FailKind::Npc, 2, 0, 100000, 0);
    memory.Prune(50);
    CHECK(!memory.Has(FailKind::Npc, 1, 50));
    CHECK(memory.Has(FailKind::Npc, 2, 50));

    int live = 0;
    memory.ForEachLive(50, [&live](FailKind, uint64, uint32, uint8, uint8) { ++live; });
    CHECK(live == 3); // target 42, spawn 7, npc 2

    // Bounded growth: many distinct expired entries are pruned on write.
    FailureMemory big;
    for (uint64 i = 0; i < 1000; ++i)
        big.Remember(FailKind::Target, i, uint32(i * 100), 50, 0);
    CHECK(big.Size() < 200);
}

static void TestReservations()
{
    ReservationTable table;
    uint64 const botA = 1001;
    uint64 const botB = 1002;

    CHECK(table.TryReserve(ReservationKind::Creature, 555, botA, 0, 1000));
    CHECK(!table.TryReserve(ReservationKind::Creature, 555, botB, 10, 1000)); // taken
    CHECK(table.Conflicts() == 1);
    CHECK(table.IsHeldByOther(ReservationKind::Creature, 555, botB, 10));
    CHECK(!table.IsHeldByOther(ReservationKind::Creature, 555, botA, 10));    // own claim
    CHECK(table.TryReserve(ReservationKind::Creature, 555, botA, 20, 1000));   // refresh own
    CHECK(table.HolderOf(ReservationKind::Creature, 555, 500) == botA);

    // Expiry frees the target for someone else without any release.
    CHECK(table.TryReserve(ReservationKind::Creature, 555, botB, 1021, 1000));
    CHECK(table.HolderOf(ReservationKind::Creature, 555, 1021) == botB);

    // Releasing someone else's claim does nothing.
    table.Release(ReservationKind::Creature, 555, botA);
    CHECK(table.HolderOf(ReservationKind::Creature, 555, 1100) == botB);
    table.Release(ReservationKind::Creature, 555, botB);
    CHECK(table.HolderOf(ReservationKind::Creature, 555, 1100) == 0);

    // Kinds do not collide on the same key.
    CHECK(table.TryReserve(ReservationKind::Creature, 9, botA, 0, 1000));
    CHECK(table.TryReserve(ReservationKind::GameObject, 9, botB, 0, 1000));

    // Shared occupancy counts, excludes the asker, refreshes, expires.
    table.Join(ReservationKind::QuestCluster, 77, botA, 0, 1000);
    table.Join(ReservationKind::QuestCluster, 77, botB, 0, 1000);
    table.Join(ReservationKind::QuestCluster, 77, botA, 500, 1000);
    CHECK(table.Occupancy(ReservationKind::QuestCluster, 77, 600) == 2);
    CHECK(table.Occupancy(ReservationKind::QuestCluster, 77, 600, botA) == 1);
    CHECK(table.Occupancy(ReservationKind::QuestCluster, 77, 1200) == 1);  // botB lapsed
    table.Leave(ReservationKind::QuestCluster, 77, botA);
    CHECK(table.Occupancy(ReservationKind::QuestCluster, 77, 600) == 1);

    // ReleaseAll drops exclusive and shared claims of one owner only.
    table.TryReserve(ReservationKind::GatherNode, 3, botA, 0, 100000);
    table.Join(ReservationKind::QuestCluster, 88, botA, 0, 100000);
    table.Join(ReservationKind::QuestCluster, 88, botB, 0, 100000);
    table.ReleaseAll(botA);
    CHECK(table.HolderOf(ReservationKind::GatherNode, 3, 10) == 0);
    CHECK(table.HolderOf(ReservationKind::Creature, 9, 10) == 0);
    CHECK(table.HolderOf(ReservationKind::GameObject, 9, 10) == botB);
    CHECK(table.Occupancy(ReservationKind::QuestCluster, 88, 10) == 1);

    // Sweep empties expired state completely.
    table.ReleaseAll(botB);
    table.TryReserve(ReservationKind::Creature, 1, botA, 0, 10);
    table.Join(ReservationKind::QuestCluster, 2, botA, 0, 10);
    table.Sweep(1000);
    CHECK(table.ExclusiveCount() == 0);
    CHECK(table.SharedCount() == 0);

    // HeldBy lists exactly the live exclusive claims of a kind.
    table.TryReserve(ReservationKind::Creature, 11, botA, 0, 1000);
    table.TryReserve(ReservationKind::Creature, 12, botA, 0, 1000);
    table.TryReserve(ReservationKind::GameObject, 13, botA, 0, 1000);
    CHECK(table.HeldBy(botA, ReservationKind::Creature, 10).size() == 2);
}

static void TestPopulationGrid()
{
    PopulationGrid grid(100.0f);
    grid.UpdatePresence(1, 0, 50, 50, PopulationActivity::Questing);
    grid.UpdatePresence(2, 0, 60, 60, PopulationActivity::Gathering);
    grid.UpdatePresence(3, 1, 50, 50, PopulationActivity::Questing); // other map

    PopulationCell cell = grid.At(0, 10, 10);
    CHECK(cell.activeBots == 2);
    CHECK(cell.questingBots == 1);
    CHECK(cell.gatheringBots == 1);
    CHECK(grid.At(1, 10, 10).activeBots == 1);

    // Moving to another cell moves the count; staying is a no-op.
    grid.UpdatePresence(1, 0, 250, 50, PopulationActivity::Questing);
    grid.UpdatePresence(1, 0, 260, 55, PopulationActivity::Questing);
    CHECK(grid.At(0, 10, 10).activeBots == 1);
    CHECK(grid.At(0, 250, 50).activeBots == 1);

    // Incoming is separate from presence and replaced, not accumulated.
    grid.SetIncoming(2, 0, 250, 50);
    grid.SetIncoming(2, 0, 251, 51);
    CHECK(grid.At(0, 250, 50).incomingBots == 1);
    grid.SetIncoming(2, 0, 950, 950);
    CHECK(grid.At(0, 250, 50).incomingBots == 0);
    CHECK(grid.At(0, 950, 950).incomingBots == 1);

    // Around sums the 3x3 neighbourhood, including negative coordinates.
    grid.UpdatePresence(10, 5, -150, -150, PopulationActivity::Idle);
    grid.UpdatePresence(11, 5, -50, -50, PopulationActivity::Idle);
    grid.UpdatePresence(12, 5, 350, 350, PopulationActivity::Idle);
    CHECK(grid.Around(5, -100, -100, 1).activeBots == 2);
    CHECK(grid.Around(5, -100, -100, 0).activeBots == 0 || grid.Around(5, -100, -100, 0).activeBots == 1);

    // Removal clears presence and incoming; empty cells are dropped.
    grid.Remove(2);
    CHECK(grid.At(0, 950, 950).incomingBots == 0);
    grid.Remove(1);
    grid.Remove(3);
    grid.Remove(10);
    grid.Remove(11);
    grid.Remove(12);
    CHECK(grid.CellCount() == 0);
    CHECK(grid.BotCount() == 0);
}

static std::set<uint32> AsSet(std::vector<uint32> const& v)
{
    return std::set<uint32>(v.begin(), v.end());
}

static void TestClustering()
{
    // Two camps 300 yards apart, plus a lower mine level right under the second camp.
    std::vector<ClusterInputPoint> points =
    {
        { 0, 0, 10 }, { 20, 5, 10 }, { 35, 25, 11 }, { 10, 40, 9 },          // camp A: 0-3
        { 300, 0, 10 }, { 320, 10, 12 }, { 310, 30, 10 },                    // camp B: 4-6
        { 305, 5, -30 }, { 325, 15, -31 },                                   // mine level below B: 7-8
        { 1000, 1000, 0 },                                                   // lone spawn: 9
    };

    auto groups = SpawnClustering::Cluster(points, 45.0f, 14.0f, 120.0f);
    CHECK(groups.size() == 4);
    CHECK(AsSet(groups[0]) == std::set<uint32>({ 0, 1, 2, 3 }));
    CHECK(AsSet(groups[1]) == std::set<uint32>({ 4, 5, 6 }));
    CHECK(AsSet(groups[2]) == std::set<uint32>({ 7, 8 }));
    CHECK(AsSet(groups[3]) == std::set<uint32>({ 9 }));

    // Every point in exactly one group.
    size_t total = 0;
    for (auto const& g : groups)
        total += g.size();
    CHECK(total == points.size());

    // The anchor is a real member near the centroid, the radius covers every member.
    ClusterShape shape = SpawnClustering::Shape(points, groups[0]);
    CHECK(AsSet(groups[0]).count(shape.anchor) == 1);
    for (uint32 i : groups[0])
        CHECK(std::hypot(points[i].x - shape.cx, points[i].y - shape.cy) <= shape.radius + 0.01f);

    // A creature spawned along a 1000-yard river (a chain within link distance) is cut up.
    std::vector<ClusterInputPoint> river;
    for (int i = 0; i <= 50; ++i)
        river.push_back({ float(i * 20), 0.0f, 0.0f });
    auto pieces = SpawnClustering::Cluster(river, 45.0f, 14.0f, 120.0f);
    CHECK(pieces.size() >= 4);
    size_t riverTotal = 0;
    for (auto const& piece : pieces)
    {
        riverTotal += piece.size();
        ClusterShape s = SpawnClustering::Shape(river, piece);
        CHECK(s.radius <= 120.0f * 1.5f);
    }
    CHECK(riverTotal == river.size());

    // Empty input, single point.
    CHECK(SpawnClustering::Cluster({}, 45.0f, 14.0f, 120.0f).empty());
    CHECK(SpawnClustering::Cluster({ { 1, 2, 3 } }, 45.0f, 14.0f, 120.0f).size() == 1);
}

static void TestUtility()
{
    UtilityWeights w;

    // Travel cost grows with distance and faster past the far threshold.
    float near = WorldUtility::TravelCost(100, w.travelPer100Yards, w.farTravelYards, w.farTravelMultiplier);
    float mid = WorldUtility::TravelCost(400, w.travelPer100Yards, w.farTravelYards, w.farTravelMultiplier);
    float far = WorldUtility::TravelCost(1400, w.travelPer100Yards, w.farTravelYards, w.farTravelMultiplier);
    CHECK(near < mid && mid < far);
    CHECK(far - mid > (mid - near) * 2.0f);

    ObjectiveUtilityInput base;
    base.distance = 150;
    float baseline = WorldUtility::ScoreObjective(base, w);

    ObjectiveUtilityInput closer = base;
    closer.distance = 50;
    CHECK(WorldUtility::ScoreObjective(closer, w) > baseline);

    ObjectiveUtilityInput crowded = base;
    crowded.crowd = 10;
    CHECK(WorldUtility::ScoreObjective(crowded, w) < baseline);

    ObjectiveUtilityInput synergy = base;
    synergy.overlapCount = 2;
    CHECK(WorldUtility::ScoreObjective(synergy, w) > baseline);

    ObjectiveUtilityInput almost = base;
    almost.remainingFraction = 0.1f;
    CHECK(WorldUtility::ScoreObjective(almost, w) > baseline);

    ObjectiveUtilityInput failed = base;
    failed.failureStrikes = 2;
    CHECK(WorldUtility::ScoreObjective(failed, w) < baseline);

    // A camp with more spawns and fewer bots wins; failures push it down.
    ClusterWeights cw;
    ClusterScoreInput big;
    big.spawnCount = 12;
    big.distance = 200;
    ClusterScoreInput small = big;
    small.spawnCount = 3;
    CHECK(WorldUtility::ScoreCluster(big, cw) > WorldUtility::ScoreCluster(small, cw));
    ClusterScoreInput busy = big;
    busy.occupancy = 8;
    busy.crowd = 12;
    CHECK(WorldUtility::ScoreCluster(busy, cw) < WorldUtility::ScoreCluster(small, cw));

    // Crowding actually spreads bots: with equal camps, the empty one wins.
    ClusterScoreInput campA = big;
    ClusterScoreInput campB = big;
    campB.distance = 350; // a bit further...
    campA.occupancy = 6;  // ...but A is already full
    CHECK(WorldUtility::ScoreCluster(campB, cw) > WorldUtility::ScoreCluster(campA, cw));

    // Targets: a claimed or tagged mob loses to a free one further away.
    TargetWeights tw;
    TargetScoreInput freeFar;
    freeFar.distance = 40;
    TargetScoreInput reservedNear;
    reservedNear.distance = 5;
    reservedNear.reservedByOther = true;
    TargetScoreInput taggedNear = reservedNear;
    taggedNear.reservedByOther = false;
    taggedNear.tappedByOther = true;
    CHECK(WorldUtility::ScoreTarget(freeFar, tw) > WorldUtility::ScoreTarget(reservedNear, tw));
    CHECK(WorldUtility::ScoreTarget(freeFar, tw) > WorldUtility::ScoreTarget(taggedNear, tw));
    TargetScoreInput doubleDuty = freeFar;
    doubleDuty.objectivesServed = 2;
    CHECK(WorldUtility::ScoreTarget(doubleDuty, tw) > WorldUtility::ScoreTarget(freeFar, tw));

    // Jitter is deterministic per pair and spreads across bots.
    CHECK(WorldUtility::Jitter(1, 2) == WorldUtility::Jitter(1, 2));
    std::set<int> buckets;
    for (uint64 bot = 0; bot < 200; ++bot)
    {
        float j = WorldUtility::Jitter(bot, 12345);
        CHECK(j >= 0.0f && j < 1.0f);
        buckets.insert(int(j * 10));
    }
    CHECK(buckets.size() >= 8);
}

static void TestSocialRules()
{
    // A friendly player at 25% health, fighting a mob they tagged, 10 yd away: help.
    AssistInput in;
    in.sameSide = true;
    in.victimInCombat = true;
    in.victimHealthPct = 25.0f;
    in.attackerIsCreature = true;
    in.attackerTappedByVictim = true;
    in.attackerLevel = 12;
    in.botLevel = 12;
    in.distance = 10.0f;
    AssistRules rules;
    CHECK(SocialRules::ShouldAssist(in, rules));

    // Never take a kill: a mob nobody (or somebody else) has tagged is left alone.
    AssistInput untagged = in;
    untagged.attackerTappedByVictim = false;
    CHECK(!SocialRules::ShouldAssist(untagged, rules));

    // Never join PvP, never help the other faction.
    AssistInput pvp = in;
    pvp.attackerIsCreature = false;
    CHECK(!SocialRules::ShouldAssist(pvp, rules));
    AssistInput enemy = in;
    enemy.sameSide = false;
    CHECK(!SocialRules::ShouldAssist(enemy, rules));

    // Only when they are really in trouble, and close by.
    AssistInput healthy = in;
    healthy.victimHealthPct = 60.0f;
    CHECK(!SocialRules::ShouldAssist(healthy, rules));
    AssistInput atThreshold = in;
    atThreshold.victimHealthPct = rules.healthPct;
    CHECK(!SocialRules::ShouldAssist(atThreshold, rules));
    AssistInput far = in;
    far.distance = rules.radius + 1.0f;
    CHECK(!SocialRules::ShouldAssist(far, rules));
    AssistInput calm = in;
    calm.victimInCombat = false;
    CHECK(!SocialRules::ShouldAssist(calm, rules));

    // No suicide runs: too high a level, an elite the bot does not clearly out-level, a world boss.
    AssistInput tooHigh = in;
    tooHigh.attackerLevel = in.botLevel + rules.maxLevelAbove + 1;
    CHECK(!SocialRules::ShouldAssist(tooHigh, rules));
    AssistInput edge = in;
    edge.attackerLevel = in.botLevel + rules.maxLevelAbove;
    CHECK(SocialRules::ShouldAssist(edge, rules));
    AssistInput elite = in;
    elite.attackerElite = true;
    CHECK(!SocialRules::ShouldAssist(elite, rules));
    elite.botLevel = uint8(elite.attackerLevel + rules.eliteLevelMargin);
    CHECK(SocialRules::ShouldAssist(elite, rules));
    AssistInput boss = in;
    boss.attackerWorldBoss = true;
    boss.botLevel = 80;
    CHECK(!SocialRules::ShouldAssist(boss, rules));

    // Party candidates: must share the quest, be close and near the leader's level.
    PartyRules pr;
    PartyCandidate c;
    c.leaderLevel = 20;
    c.level = 20;
    c.distance = 10.0f;
    c.sharesQuest = true;
    CHECK(SocialRules::PartyCandidateScore(c, pr) > 0.0f);
    PartyCandidate noQuest = c;
    noQuest.sharesQuest = false;
    CHECK(SocialRules::PartyCandidateScore(noQuest, pr) < 0.0f);
    PartyCandidate tooFar = c;
    tooFar.distance = pr.radius + 1.0f;
    CHECK(SocialRules::PartyCandidateScore(tooFar, pr) < 0.0f);
    PartyCandidate gap = c;
    gap.level = uint8(c.leaderLevel + pr.maxLevelGap + 1);
    CHECK(SocialRules::PartyCandidateScore(gap, pr) < 0.0f);
    PartyCandidate below = c;
    below.level = uint8(c.leaderLevel - pr.maxLevelGap);
    CHECK(SocialRules::PartyCandidateScore(below, pr) > 0.0f);
    // Closer, same level and a missing role all rank higher.
    PartyCandidate farther = c;
    farther.distance = 50.0f;
    CHECK(SocialRules::PartyCandidateScore(c, pr) > SocialRules::PartyCandidateScore(farther, pr));
    PartyCandidate offLevel = c;
    offLevel.level = 22;
    CHECK(SocialRules::PartyCandidateScore(c, pr) > SocialRules::PartyCandidateScore(offLevel, pr));
    PartyCandidate healer = farther;
    healer.fillsMissingRole = true;
    CHECK(SocialRules::PartyCandidateScore(healer, pr) > SocialRules::PartyCandidateScore(farther, pr));

    // Lifetime stays in range, whatever the roll.
    for (uint32 roll : { 0u, 1u, 12345u, 0xFFFFFFFFu })
    {
        uint32 life = SocialRules::PartyLifetimeMs(roll, 300000, 1200000);
        CHECK(life >= 300000 && life <= 1200000);
    }
    CHECK(SocialRules::PartyLifetimeMs(99, 5000, 5000) == 5000);
    CHECK(SocialRules::PartyLifetimeMs(99, 5000, 1000) == 5000);

    // Ending: in priority order, and nothing while all is well.
    PartyStatus ps;
    ps.now = 1000;
    ps.expiresMs = 60000;
    ps.members = 2;
    CHECK(SocialRules::ShouldEnd(ps, 45000) == PartyEnd::No);
    PartyStatus expired = ps;
    expired.now = 60000;
    CHECK(SocialRules::ShouldEnd(expired, 45000) == PartyEnd::Expired);
    PartyStatus gone = ps;
    gone.leaderPresent = false;
    gone.members = 0;
    CHECK(SocialRules::ShouldEnd(gone, 45000) == PartyEnd::LeaderGone);
    PartyStatus empty = ps;
    empty.members = 0;
    CHECK(SocialRules::ShouldEnd(empty, 45000) == PartyEnd::Empty);
    PartyStatus done = ps;
    done.leaderOnTask = false;
    CHECK(SocialRules::ShouldEnd(done, 45000) == PartyEnd::TaskDone);
    PartyStatus busy = ps;
    busy.leaderFree = false;
    busy.leaderOnTask = false;
    CHECK(SocialRules::ShouldEnd(busy, 45000) == PartyEnd::LeaderBusy);
    PartyStatus dead = ps;
    dead.leaderDeadForMs = 30000;
    CHECK(SocialRules::ShouldEnd(dead, 45000) == PartyEnd::No);     // waiting for a resurrection
    dead.leaderDeadForMs = 45001;
    CHECK(SocialRules::ShouldEnd(dead, 45000) == PartyEnd::LeaderDead);

    // Members: dropped when gone, off the map, far behind, or dead too long -- not while dead briefly.
    PartyMemberStatus m;
    m.distanceToLeader = 20.0f;
    CHECK(!SocialRules::ShouldDropMember(m, 200.0f, 90000));
    PartyMemberStatus offline = m;
    offline.present = false;
    CHECK(SocialRules::ShouldDropMember(offline, 200.0f, 90000));
    PartyMemberStatus elsewhere = m;
    elsewhere.sameMap = false;
    CHECK(SocialRules::ShouldDropMember(elsewhere, 200.0f, 90000));
    PartyMemberStatus behind = m;
    behind.distanceToLeader = 250.0f;
    CHECK(SocialRules::ShouldDropMember(behind, 200.0f, 90000));
    PartyMemberStatus ghost = m;
    ghost.alive = false;
    ghost.distanceToLeader = 500.0f;                   // corpse-running: distance does not count
    ghost.deadForMs = 60000;
    CHECK(!SocialRules::ShouldDropMember(ghost, 200.0f, 90000));
    ghost.deadForMs = 90001;
    CHECK(SocialRules::ShouldDropMember(ghost, 200.0f, 90000));

    CHECK(std::string(SocialRules::PartyEndName(PartyEnd::TaskDone)) != "?");
}

// Regression: a quest task paused for an ambient errand kept ageing, so a 10-second Search that
// resumed after a 90-second vendor trip looked like 100 seconds of searching and failed at once.
static void TestTaskClocks()
{
    WorldTask task;
    task.type = WorldTaskType::QuestObjective;
    task.phase = TaskPhase::Search;
    task.startedMs = 1000;
    task.phaseStartedMs = 1000;
    task.deadlineMs = 1000 + 1800000;
    task.nextScanMs = 12000;       // armed: a scan was due 1 s after the pause started
    task.waitUntilMs = 0;          // never armed

    uint32 const pauseAt = 11000;
    task.Pause(pauseAt);
    CHECK(task.paused);
    CHECK(!task.CountsAsIncoming());
    task.Pause(pauseAt + 5000);    // a second pause request does not move the pause start
    CHECK(task.pausedAtMs == pauseAt);

    uint32 const resumeAt = pauseAt + 90000;
    CHECK(task.Resume(resumeAt) == 90000);
    CHECK(!task.paused);
    CHECK(resumeAt - task.phaseStartedMs == 10000);           // still 10 s into the search
    CHECK(task.deadlineMs - resumeAt == 1800000 - 10000);     // deadline budget untouched too
    CHECK(task.nextScanMs - resumeAt == 1000);                // the scan is still due in 1 s
    CHECK(task.waitUntilMs == 0);                             // an unarmed clock stays unarmed
    CHECK(task.startedMs == 1000 + 90000);

    // Resuming something that is not paused changes nothing.
    uint32 phaseBefore = task.phaseStartedMs;
    CHECK(task.Resume(resumeAt + 100) == 0);
    CHECK(task.phaseStartedMs == phaseBefore);

    // The task's own business away from the brain (a fight with an add) shifts the phase clock
    // only: the whole-task deadline is an anti-loop guard and keeps counting.
    uint32 deadlineBefore = task.deadlineMs;
    task.ShiftPhaseClock(40000);
    CHECK(task.phaseStartedMs == phaseBefore + 40000);
    CHECK(task.deadlineMs == deadlineBefore);

    // A phase entered during the pause (the bot died on its errand, the pause released a target)
    // starts at the frozen clock; after the resume it has used none of its budget -- in particular
    // it must not have started "in the future", which made the elapsed time underflow.
    WorldTask died;
    died.type = WorldTaskType::QuestObjective;
    died.phaseStartedMs = 500;
    died.deadlineMs = 500 + 1800000;
    died.Pause(2000);
    CHECK(died.ClockNow(50000) == 2000);
    died.phase = TaskPhase::Recover;
    died.phaseStartedMs = died.ClockNow(50000);        // what SetPhase does
    died.Resume(80000);
    CHECK(died.phaseStartedMs == 80000);
    CHECK(died.ClockNow(80000) == 80000);
    CHECK(died.deadlineMs - 80000 == 1800000 - 1500);

    // A gap in brain ticks while paused (a gathering detour's cast) is already covered by the
    // pause: it must not also be taken off the phase clock, or the phase gets its time back twice.
    WorldTask detour;
    detour.type = WorldTaskType::QuestObjective;
    detour.phase = TaskPhase::Search;
    detour.phaseStartedMs = 1000;
    detour.deadlineMs = 1000 + 1800000;
    detour.Pause(6000);                 // 5 s into the search
    detour.ShiftPhaseClock(8000);       // the brain did not tick for 8 s during the detour
    CHECK(detour.phaseStartedMs == 1000);
    detour.Resume(26000);               // a 20 s detour
    CHECK(26000 - detour.phaseStartedMs == 5000);

    // Incoming is exactly "travelling to the task's destination".
    WorldTask travel;
    travel.type = WorldTaskType::QuestObjective;
    travel.phase = TaskPhase::TravelToArea;
    CHECK(travel.CountsAsIncoming());
    travel.phase = TaskPhase::Search;
    CHECK(!travel.CountsAsIncoming());
    travel.phase = TaskPhase::Approach;
    CHECK(!travel.CountsAsIncoming());      // walking up to a mob inside the area is not incoming
    travel.phase = TaskPhase::TravelToArea;
    travel.Pause(5);
    CHECK(!travel.CountsAsIncoming());
    WorldTask none;
    none.phase = TaskPhase::TravelToArea;
    CHECK(!none.CountsAsIncoming());
}

// Regression: a bot that arrived in its objective area was counted both as present there and as
// still incoming, so 20 bots farming a camp looked like almost 40.
static void TestHeatmapIncomingLifecycle()
{
    PopulationGrid grid(100.0f);
    uint64 const bot = 7;
    float const ax = 1050.0f, ay = 2050.0f;   // area A

    WorldTask task;
    task.type = WorldTaskType::QuestObjective;
    task.phase = TaskPhase::TravelToArea;
    task.mapId = 0;
    task.x = ax;
    task.y = ay;

    // The same rule WorldExecutor::SyncIncoming applies after every executor step.
    auto sync = [&]()
    {
        if (task.CountsAsIncoming())
            grid.SetIncoming(bot, task.mapId, task.x, task.y);
        else
            grid.ClearIncoming(bot);
    };

    grid.UpdatePresence(bot, 0, 150.0f, 150.0f, PopulationActivity::Questing);   // far away
    sync();
    CHECK(grid.At(0, ax, ay).incomingBots == 1);
    CHECK(grid.StatusOf(bot).incoming);

    // Arrives: presence moves into A's cell, the task leaves TravelToArea.
    grid.UpdatePresence(bot, 0, ax + 3.0f, ay - 2.0f, PopulationActivity::Questing);
    task.phase = TaskPhase::Search;
    sync();
    PopulationCell cell = grid.At(0, ax, ay);
    CHECK(cell.activeBots == 1);
    CHECK(cell.incomingBots == 0);
    CHECK(cell.Crowd() == 1);
    CHECK(grid.Around(0, ax, ay, 1).Crowd() == 1);
    CHECK(grid.StatusOf(bot).present && !grid.StatusOf(bot).incoming);

    // Walking 15 yards to a mob inside the area does not make it incoming again.
    task.phase = TaskPhase::Approach;
    sync();
    CHECK(grid.At(0, ax, ay).incomingBots == 0);

    // Twenty bots working the camp count as twenty.
    for (uint64 other = 100; other < 119; ++other)
        grid.UpdatePresence(other, 0, ax + float(other % 7), ay + float(other % 5), PopulationActivity::Questing);
    CHECK(grid.Around(0, ax, ay, 1).Crowd() == 20);

    // Pausing mid-trip drops incoming; the task being released drops it too.
    task.phase = TaskPhase::TravelToArea;
    task.x = 5050.0f;
    sync();
    CHECK(grid.At(0, 5050.0f, ay).incomingBots == 1);
    task.Pause(1);
    sync();
    CHECK(grid.At(0, 5050.0f, ay).incomingBots == 0);
    grid.ClearIncoming(bot);                  // idempotent
    grid.Remove(bot);
    CHECK(!grid.StatusOf(bot).present && !grid.StatusOf(bot).incoming);
}

// Task switch / release / pause leave no stale claims behind.
static void TestReservationTaskLifecycle()
{
    ReservationTable table;
    uint64 const bot = 1, other = 2;
    uint64 const mob = 0xF130000000001234ull, area = 42;

    CHECK(table.TryReserve(ReservationKind::Creature, mob, bot, 0, 90000));
    table.Join(ReservationKind::QuestCluster, area, bot, 0, 120000);

    // Pause: the mob is let go, the area claim stays (the bot is coming back).
    table.Release(ReservationKind::Creature, mob, bot);
    CHECK(table.TryReserve(ReservationKind::Creature, mob, other, 10, 90000));
    CHECK(table.Occupancy(ReservationKind::QuestCluster, area, 10) == 1);
    table.Release(ReservationKind::Creature, mob, other);

    // Task end (switch, failure, suspension, death, logout): everything goes.
    CHECK(table.TryReserve(ReservationKind::Creature, mob, bot, 20, 90000));
    table.ReleaseAll(bot);
    CHECK(table.ExclusiveCount() == 0);
    CHECK(table.SharedCount() == 0);
    CHECK(table.HeldBy(bot, ReservationKind::Creature, 20).empty());
    CHECK(table.Occupancy(ReservationKind::QuestCluster, area, 20) == 0);
    CHECK(!table.IsHeldByOther(ReservationKind::Creature, mob, other, 20));
    table.ReleaseAll(bot);                    // releasing twice is harmless

    // Re-confirming a held target during a long approach keeps it; a lapsed one can be lost.
    CHECK(table.TryReserve(ReservationKind::Creature, mob, bot, 100, 90000));
    CHECK(table.TryReserve(ReservationKind::Creature, mob, bot, 80000, 90000));    // refreshed
    CHECK(!table.TryReserve(ReservationKind::Creature, mob, other, 150000, 90000)); // still ours
    CHECK(table.TryReserve(ReservationKind::Creature, mob, other, 170001, 90000));  // lapsed
    CHECK(!table.TryReserve(ReservationKind::Creature, mob, bot, 170002, 90000));   // someone else's now
}

// Regression: the stuck detector judged ground distance only (bots on stairs were "stuck"), and
// any 2.5 yards of progress reset the whole recovery ladder (a bot could repath forever).
static void TestNavProgress()
{
    NavProgressRules rules;
    NavProgress p;

    // Flat ground, steady progress: never a recovery.
    p.Start(100.0f, 0.0f, 0);
    for (uint32 t = 1; t <= 20; ++t)
        CHECK(p.Update(100.0f - float(t) * 4.0f, 0.0f, t * 1000, rules) == NavRecovery::None);
    CHECK(p.stage == 0);

    // A staircase: the goal is 8 yards away on the ground but 24 yards up. The bot circles the
    // stairwell -- ground distance wobbles around 8 -- and climbs steadily. No recovery.
    p.Start(8.0f, 24.0f, 0);
    for (uint32 t = 1; t <= 30; ++t)
    {
        float ground = 8.0f + float(t % 3);           // 8, 9, 10, 8, ...
        float height = std::max(0.0f, 24.0f - float(t) * 0.8f);
        CHECK(p.Update(ground, height, t * 1000, rules) == NavRecovery::None);
    }
    CHECK(p.stage == 0);

    // A real wall: nothing moves. The ladder climbs one stage per stall...
    p.Start(60.0f, 0.0f, 0);
    CHECK(p.Update(60.0f, 0.0f, 5000, rules) == NavRecovery::None);
    CHECK(p.Update(60.0f, 0.0f, 9001, rules) == NavRecovery::Repath);
    // ...a repath that wriggles 3 yards counts as progress (the stall timer restarts)...
    CHECK(p.Update(57.0f, 0.0f, 10000, rules) == NavRecovery::None);
    CHECK(p.stage == 1);
    // ...but does not reset the ladder: the next stall detours instead of repathing again.
    CHECK(p.Update(57.0f, 0.0f, 19001, rules) == NavRecovery::DetourLeft);
    CHECK(p.Update(54.0f, 0.0f, 20000, rules) == NavRecovery::None);        // another wriggle
    CHECK(p.Update(54.0f, 0.0f, 29001, rules) == NavRecovery::DetourRight);
    CHECK(p.Update(51.0f, 0.0f, 30000, rules) == NavRecovery::None);
    CHECK(p.Update(51.0f, 0.0f, 39001, rules) == NavRecovery::GiveUp);      // escalates to Stuck

    // A recovery that actually works -- 20+ yards closer than where the first stall happened --
    // resets the ladder, so a later, unrelated obstacle starts from a repath again.
    p.Start(80.0f, 0.0f, 0);
    CHECK(p.Update(80.0f, 0.0f, 9001, rules) == NavRecovery::Repath);
    CHECK(p.Update(70.0f, 0.0f, 10000, rules) == NavRecovery::None);
    CHECK(p.stage == 1);                                                     // 10 yd: not yet
    CHECK(p.Update(59.0f, 0.0f, 11000, rules) == NavRecovery::None);
    CHECK(p.stage == 0);                                                     // 21 yd: recovered
    CHECK(p.Update(59.0f, 0.0f, 20001, rules) == NavRecovery::Repath);

    // Climbing out of a mine counts as recovery too.
    p.Start(10.0f, 30.0f, 0);
    CHECK(p.Update(10.0f, 30.0f, 9001, rules) == NavRecovery::Repath);
    CHECK(p.Update(10.0f, 21.0f, 10000, rules) == NavRecovery::None);
    CHECK(p.stage == 0);

    // A new baseline (the mob walked, or the bot was busy elsewhere) keeps the stage and does
    // not stall immediately.
    p.Start(40.0f, 0.0f, 0);
    CHECK(p.Update(40.0f, 0.0f, 9001, rules) == NavRecovery::Repath);
    p.Rebase(55.0f, 0.0f, 30000);
    CHECK(p.stage == 1);
    CHECK(p.Update(55.0f, 0.0f, 35000, rules) == NavRecovery::None);
    CHECK(p.Update(55.0f, 0.0f, 39001, rules) == NavRecovery::DetourLeft);

    // Time spent blocked by a higher-priority owner never counts toward a stall.
    p.Start(40.0f, 0.0f, 0);
    p.Hold(8000);
    CHECK(p.Update(40.0f, 0.0f, 16000, rules) == NavRecovery::None);
    CHECK(p.Update(40.0f, 0.0f, 17001, rules) == NavRecovery::Repath);

    CHECK(std::string(NavRecoveryStageName(0)) == "none");
}

// Regression: an objective area's phase was the union of its members', so a phase-1 bot could be
// sent to a phase-2 anchor or wander point.
static void TestPhaseSeparatedClustering()
{
    std::vector<ClusterInputPoint> points =
    {
        { 0.0f, 0.0f, 0.0f, 1 },
        { 5.0f, 0.0f, 0.0f, 2 },    // right next to it, other phase
        { 10.0f, 3.0f, 0.0f, 1 },
        { 14.0f, -2.0f, 0.0f, 1 },
        { 7.0f, 6.0f, 0.0f, 2 },
    };
    auto groups = SpawnClustering::Cluster(points, 45.0f, 14.0f, 120.0f);
    CHECK(groups.size() == 2);
    for (auto const& group : groups)
    {
        uint32 mask = points[group.front()].phaseMask;
        for (uint32 i : group)
            CHECK(points[i].phaseMask == mask);
        // Anchor is one of the group's own members, so it shares the phase too.
        ClusterShape shape = SpawnClustering::Shape(points, group);
        CHECK(points[shape.anchor].phaseMask == mask);
    }
    CHECK(groups[0].size() == 3 && points[groups[0].front()].phaseMask == 1);

    // Same phase everywhere: one place, as before.
    for (ClusterInputPoint& point : points)
        point.phaseMask = 1;
    CHECK(SpawnClustering::Cluster(points, 45.0f, 14.0f, 120.0f).size() == 1);
}

// Regression: three transient failures (no targets, a path problem, a death) abandoned the quest.
static void TestQuestPolicy()
{
    uint32 const base = 600000, cap = 7200000;
    CHECK(QuestPolicy::SuspendMs(base, 1, cap) == 600000);
    CHECK(QuestPolicy::SuspendMs(base, 2, cap) == 1200000);
    CHECK(QuestPolicy::SuspendMs(base, 3, cap) == 2400000);
    CHECK(QuestPolicy::SuspendMs(base, 4, cap) == 4800000);
    CHECK(QuestPolicy::SuspendMs(base, 5, cap) == cap);
    CHECK(QuestPolicy::SuspendMs(base, 60, cap) == cap);       // no overflow, no abandon
    CHECK(QuestPolicy::SuspendMs(base, 0, cap) == base);
    CHECK(QuestPolicy::SuspendMs(base, 3, 1000) == base);      // a cap below the base is ignored

    // Dead ends only leave the log when it is full.
    CHECK(!QuestPolicy::ShouldAbandonDeadEnd(3, 15));
    CHECK(!QuestPolicy::ShouldAbandonDeadEnd(14, 15));
    CHECK(QuestPolicy::ShouldAbandonDeadEnd(15, 15));
    CHECK(QuestPolicy::ShouldAbandonDeadEnd(25, 15));

    // Workable: completable and every open objective executable.
    CHECK(QuestPolicy::Workable(true, 2, 2));
    CHECK(QuestPolicy::Workable(true, 0, 0));                   // all done / report quest
    CHECK(!QuestPolicy::Workable(true, 2, 1));                  // e.g. kill + use-object, no handler
    CHECK(!QuestPolicy::Workable(false, 1, 1));                 // player kills / no ender
}

int main()
{
    TestFailureMemory();
    TestReservations();
    TestPopulationGrid();
    TestClustering();
    TestUtility();
    TestSocialRules();
    TestTaskClocks();
    TestHeatmapIncomingLifecycle();
    TestReservationTaskLifecycle();
    TestNavProgress();
    TestPhaseSeparatedClustering();
    TestQuestPolicy();

    std::printf("%d checks, %d failures\n", _checks, _failures);
    return _failures ? 1 : 0;
}
