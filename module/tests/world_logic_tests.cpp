/*
 * mod-coa-playerbots -- standalone tests for the open-world layer's pure logic:
 * FailureMemory, ReservationTable, PopulationGrid, SpawnClustering, WorldUtility.
 *
 * These are the pieces that decide "don't retry that", "that mob is taken", "that camp is
 * crowded", "these spawns are one place" and "this is worth doing" -- exactly the kind of logic
 * that is easy to get subtly wrong and impossible to observe directly on a live server.
 *
 * Build and run (no core checkout needed):
 *   g++ -std=c++20 -O1 -Wall -Wextra -I module/tests/stub -I module/src/world \
 *       module/tests/world_logic_tests.cpp -o /tmp/world_logic_tests && /tmp/world_logic_tests
 */

#include "FailureMemory.h"
#include "PopulationGrid.h"
#include "ReservationTable.h"
#include "SpawnClustering.h"
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

int main()
{
    TestFailureMemory();
    TestReservations();
    TestPopulationGrid();
    TestClustering();
    TestUtility();

    std::printf("%d checks, %d failures\n", _checks, _failures);
    return _failures ? 1 : 0;
}
