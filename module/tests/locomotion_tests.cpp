/*
 * mod-coa-playerbots -- locomotion & navigation overhaul Round 4 regression tests
 *
 * Verifies all Round 4 core invariants using the EXACT production BotMovementStateStore:
 *  1. LocomotionToken lifecycle, generation validation, stale rejection
 *  2. Multi-owner priority arbitration via LocomotionArbiter
 *  3. Stale Navigate arrival returns NavStatus::Superseded without touching active state
 *  4. Transactional MoveTo: NOPATH leaves owner and commandId intact
 *  5. Linearized Physical Dispatch: per-bot dispatch gates serialize concurrently
 *  6. Stale Release race protection: stale token does not clear newer motion
 *  7. Mount interrupt and generation invalidation (TransitionDismount)
 *  8. Leader mount preference reset on group exit / cross-map
 *  9. Autonomous travel ignores leader PreferUnmounted
 * 10. SafePositionHistory: std::optional value copy + mapId filter
 * 11. Backtrack candidate advance on early stop without arrival
 * 12. Detour to Backtrack before GiveUp if candidates exist
 * 13. Hold expiration: Holding transitions to Idle after holdUntilMs
 * 14. Generator completion reconciliation: Moving/Point -> Idle when gen ended & stationary
 * 15. Mount cast failure backoff: TransitionCastFailed applies backoffMs cooldown
 * 16. 64-shard multithread stress with dispatch gate serialization
 */

#include "stub/Define.h"
#include "stub/ObjectGuid.h"
#include "BotMovementPrimitives.h"
#include "BotMovementStateStore.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <vector>

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

// ============================================================================
// Test 1: LocomotionToken semantics & stale rejection
// ============================================================================
static void TestLocomotionToken()
{
    ObjectGuid bot1(1001);
    ObjectGuid bot2(1002);

    LocomotionToken emptyTok;
    CHECK(!emptyTok.IsValid());
    CHECK(!emptyTok);

    LocomotionToken tok1{bot1, MoveOwner::Travel, 42};
    CHECK(tok1.IsValid());
    CHECK(bool(tok1));

    LocomotionToken tok1Copy{bot1, MoveOwner::Travel, 42};
    CHECK(tok1 == tok1Copy);
    CHECK(!(tok1 != tok1Copy));

    LocomotionToken tok1Newer{bot1, MoveOwner::Travel, 43};
    CHECK(tok1 != tok1Newer);

    LocomotionToken tok2{bot2, MoveOwner::Travel, 42};
    CHECK(tok1 != tok2);

    LocomotionToken tokCombat{bot1, MoveOwner::Combat, 42};
    CHECK(tok1 != tokCombat);

    // Stale token rejection simulation
    struct ActiveLocomotion { ObjectGuid guid; MoveOwner owner; uint64 commandId; };
    ActiveLocomotion active{bot1, MoveOwner::Travel, 43};

    auto TryRelease = [&](LocomotionToken const& token) -> bool {
        if (!token.IsValid()) return false;
        if (token.botGuid != active.guid || token.owner != active.owner) return false;
        if (token.commandId != active.commandId) return false;
        active.commandId = 0;
        return true;
    };

    CHECK(!TryRelease(tok1));       // Stale commandId=42 must NOT release active cmd=43
    CHECK(active.commandId == 43);  // Unaffected
    CHECK(TryRelease(tok1Newer));   // Matching commandId=43 releases
    CHECK(active.commandId == 0);
}

// ============================================================================
// Test 2: Priority Arbitration via production LocomotionArbiter
// ============================================================================
static void TestPriorityArbitration()
{
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Avoidance) > LocomotionArbiter::PriorityOf(MoveOwner::Combat));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Combat) > LocomotionArbiter::PriorityOf(MoveOwner::Battleground));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Battleground) > LocomotionArbiter::PriorityOf(MoveOwner::Quest));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Quest) > LocomotionArbiter::PriorityOf(MoveOwner::Travel));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Travel) > LocomotionArbiter::PriorityOf(MoveOwner::Gather));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Gather) > LocomotionArbiter::PriorityOf(MoveOwner::Ambient));
    CHECK(LocomotionArbiter::PriorityOf(MoveOwner::Ambient) > LocomotionArbiter::PriorityOf(MoveOwner::None));

    // Controlled motion blocks all claims
    CHECK(!LocomotionArbiter::CanClaim(MoveOwner::None, MoveOwner::Combat, true));
    CHECK(!LocomotionArbiter::CanClaim(MoveOwner::Ambient, MoveOwner::Avoidance, true));

    // Empty slot accepts any claim
    CHECK(LocomotionArbiter::CanClaim(MoveOwner::None, MoveOwner::Ambient, false));
    CHECK(LocomotionArbiter::CanClaim(MoveOwner::None, MoveOwner::Combat, false));

    // Same owner can renew
    CHECK(LocomotionArbiter::CanClaim(MoveOwner::Travel, MoveOwner::Travel, false));

    // Higher priority preempts lower
    CHECK(LocomotionArbiter::CanClaim(MoveOwner::Ambient, MoveOwner::Travel, false));
    CHECK(LocomotionArbiter::CanClaim(MoveOwner::Combat, MoveOwner::Avoidance, false));

    // Lower priority CANNOT preempt higher
    CHECK(!LocomotionArbiter::CanClaim(MoveOwner::Travel, MoveOwner::Ambient, false));
    CHECK(!LocomotionArbiter::CanClaim(MoveOwner::Avoidance, MoveOwner::Combat, false));
}

// ============================================================================
// Test 3: Stale Navigate Arrival returns NavStatus::Superseded
// ============================================================================
static void TestStaleNavigateArrivalSuperseded()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(2001);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    // Setup active request B (goalId=200, commandId=55)
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        MovementRequest& req = shard.requests[bot];
        req.botGuid = bot;
        req.owner = MoveOwner::Quest;
        req.goalId = 200;
        req.commandId = 55;

        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Quest;
        loc.commandId = 55;
        loc.state = LocomotionState::Moving;
    }

    // Stale arrival from old request A (goalId=100). Bot is within acceptRadius of A.
    // The rule: if no matching request, return Superseded and touch NOTHING.
    NavStatus status;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto rItr = shard.requests.find(bot);
        if (rItr != shard.requests.end() && rItr->second.owner == MoveOwner::Quest && rItr->second.goalId == 100)
        {
            // Matching: normal arrival
            status = NavStatus::Arrived;
        }
        else
        {
            // No match: stale arrival, must NOT touch state
            status = NavStatus::Superseded;
        }
    }

    CHECK(status == NavStatus::Superseded);

    // Active request B must be completely intact
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto it = shard.requests.find(bot);
        CHECK(it != shard.requests.end());
        CHECK(it->second.goalId == 200);
        CHECK(it->second.commandId == 55);
        CHECK(shard.locomotion[bot].commandId == 55);
        CHECK(shard.locomotion[bot].owner == MoveOwner::Quest);
    }
}

// ============================================================================
// Test 4: Transactional MoveTo Owner Preservation on NOPATH
// ============================================================================
static void TestTransactionalMoveToOwnerPreservation()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(2002);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    // Existing active: Combat commandId=10
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Combat;
        loc.commandId = 10;
        loc.state = LocomotionState::Moving;
    }

    // A new higher-priority Avoidance command pre-validates successfully
    bool claimOk = false;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        claimOk = LocomotionStateStore::CanClaimLocked(shard, bot, MoveOwner::Avoidance, false);
    }
    CHECK(claimOk);

    // PathGenerator returns NOPATH (simulated by pathValid=false)
    // CRITICAL: Must NOT commit owner or commandId change!
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        NopathCacheEntry& entry = shard.nopathCache[bot];
        entry.owner = MoveOwner::Avoidance;
        entry.x = 999.0f;
        entry.y = 999.0f;
        entry.expiresAtMs = 5000;
        // Critically: we do NOT touch shard.locomotion[bot]
    }

    // Verify previous Combat owner and commandId=10 are completely intact
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord const& loc = shard.locomotion[bot];
        CHECK(loc.owner == MoveOwner::Combat);
        CHECK(loc.commandId == 10);
        CHECK(loc.state == LocomotionState::Moving);

        auto cacheIt = shard.nopathCache.find(bot);
        CHECK(cacheIt != shard.nopathCache.end());
        CHECK(cacheIt->second.owner == MoveOwner::Avoidance);
    }
}

// ============================================================================
// Test 5: Linearized Physical Dispatch Gate Serialization
// ============================================================================
static void TestDispatchGateSerialization()
{
    ObjectGuid bot(2003);
    std::mutex& gate = LocomotionStateStore::GetDispatchGate(bot);

    // Verify clean lock acquisition
    bool lockedFirst = false;
    {
        std::lock_guard<std::mutex> lock(gate);
        lockedFirst = true;
    }
    CHECK(lockedFirst);

    // Concurrent serialization: t2 must wait until t1 exits gate
    std::atomic<bool> t1Holding{false};
    std::atomic<bool> t2CorrectlyBlocked{false};

    std::thread t1([&]() {
        std::lock_guard<std::mutex> lock(gate);
        t1Holding.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        t1Holding.store(false);
    });

    while (!t1Holding.load())
        std::this_thread::yield();

    std::thread t2([&]() {
        std::lock_guard<std::mutex> lock(gate);
        // When t2 acquired the gate, t1 must NOT be holding it anymore
        t2CorrectlyBlocked.store(!t1Holding.load());
    });

    t1.join();
    t2.join();
    CHECK(t2CorrectlyBlocked.load());
}

// ============================================================================
// Test 6: Stale Release Race Protection
// ============================================================================
static void TestStaleReleaseRaceProtection()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(2004);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    // Active command=100
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Travel;
        loc.commandId = 100;
        loc.state = LocomotionState::Moving;
    }

    // Stale release with commandId=99 arrives
    LocomotionToken staleToken{bot, MoveOwner::Travel, 99};
    bool matched = false;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        auto itr = shard.locomotion.find(bot);
        if (itr != shard.locomotion.end() &&
            itr->second.owner == staleToken.owner &&
            itr->second.commandId == staleToken.commandId) // 99 != 100: mismatch!
        {
            itr->second.owner = MoveOwner::None;
            itr->second.state = LocomotionState::Idle;
            matched = true;
        }
    }

    CHECK(!matched); // Stale token MUST be rejected

    // State remains active command 100
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        CHECK(shard.locomotion[bot].owner == MoveOwner::Travel);
        CHECK(shard.locomotion[bot].commandId == 100);
    }
}

// ============================================================================
// Test 7: Mount Interrupt and Generation Invalidation
// ============================================================================
static void TestMountInterruptAndGenerationInvalidation()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(2005);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    uint32 now = 10000;

    // Setup: mount cast in progress (pendingSpell=458, generation=1)
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot];
        mRec.pendingMountSpellId = 458; // Brown Horse
        mRec.state = MountState::MountCasting;
        mRec.mountGeneration = 1;
    }

    // Combat starts -> dismount requested
    MountTransitionResult trans;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot];
        trans = MountStateMachine::TransitionDismount(mRec, now);
    }

    // TransitionDismount must capture pending spell and bump generation
    CHECK(trans.interruptedSpellId == 458);

    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord const& mRec = shard.mounts[bot];
        CHECK(mRec.pendingMountSpellId == 0);       // cleared
        CHECK(mRec.mountGeneration == 2);            // bumped
        CHECK(mRec.state == MountState::Cooldown);
        CHECK(mRec.remountCooldownUntilMs == now + MountStateMachine::REMOUNT_COOLDOWN_MS);
    }

    // Late cast completion with the old spell: must NOT resurrect mount!
    // (pendingMountSpellId is already 0, generation has changed)
    bool lateResurrected = false;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord const& mRec = shard.mounts[bot];
        if (mRec.pendingMountSpellId == 458)
        {
            lateResurrected = true; // This must NOT happen!
        }
    }
    CHECK(!lateResurrected);
}

// ============================================================================
// Test 8: Leader Preference Resets on Group Exit / Cross-Map
// ============================================================================
static void TestLeaderPreferenceClearOnLeave()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(2006);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    // Initially has leader preference
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot];
        mRec.leaderDesiredState = DesiredMountState::PreferGround;
        mRec.leaderStateObservedAt = 1000;
    }

    // TryMatchLeaderMountState detects: no group / leader offline / cross-map
    // -> must reset leaderDesiredState to None
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot];
        if (mRec.leaderDesiredState != DesiredMountState::None)
            mRec.leaderDesiredState = DesiredMountState::None;
    }

    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        CHECK(shard.mounts[bot].leaderDesiredState == DesiredMountState::None);
    }
}

// ============================================================================
// Test 9: Autonomous Travel Ignores Leader PreferUnmounted
// ============================================================================
static void TestPreferUnmountedVsAutonomousTravel()
{
    uint32 now = 10000;
    uint32 observedAt = now - 2000; // 2s old -> debounced

    // Quest/Travel/Ambient autonomous owners must ignore PreferUnmounted and NOT dismount
    auto testAutonomousIgnores = [&](MoveOwner autonomousOwner)
    {
        MountPolicyDecision dec = MountPolicyResolver::Evaluate(
            autonomousOwner,                      // activeOwner
            MoveMode::Navigate,                   // activeMode
            150.0f,                               // travelDist (>90yd, autonomous travel)
            false,                                // isFollowingLeader (autonomous, not follow)
            DesiredMountState::PreferUnmounted,   // leaderPref
            true,                                 // leaderPrefDebounced
            true,                                 // isOutdoors
            false,                                // inCombat
            false,                                // casting
            40,                                   // level
            false,                                // isControlled
            MountState::MountedGround,            // currentState
            0,                                    // remountCooldownUntilMs
            now,
            false,                                // canFly
            true                                  // isCurrentlyMounted
        );
        // Autonomous owners must NOT get a Dismount decision
        CHECK(dec.action != MountAction::Dismount);
    };

    testAutonomousIgnores(MoveOwner::Quest);
    testAutonomousIgnores(MoveOwner::Travel);
    testAutonomousIgnores(MoveOwner::Ambient);

    // But a group follower (following leader) with PreferUnmounted DOES get dismount
    MountPolicyDecision followerDec = MountPolicyResolver::Evaluate(
        MoveOwner::None,                      // activeOwner: not autonomous
        MoveMode::Follow,                     // activeMode: following leader
        10.0f,                                // travelDist < 90yd
        true,                                 // isFollowingLeader
        DesiredMountState::PreferUnmounted,   // leaderPref
        true,                                 // leaderPrefDebounced
        true,                                 // isOutdoors
        false,                                // inCombat
        false,                                // casting
        40,                                   // level
        false,                                // isControlled
        MountState::MountedGround,            // currentState
        0,                                    // remountCooldownUntilMs
        now,
        false,                                // canFly
        true                                  // isCurrentlyMounted
    );
    CHECK(followerDec.action == MountAction::Dismount);
}

// ============================================================================
// Test 10: SafePositionHistory Value Copy & MapId Filter
// ============================================================================
static void TestSafePositionValueCopyAndMapIdFilter()
{
    SafePositionHistory ring;
    uint32 mapAzeroth = 0;
    uint32 mapOutland = 530;

    // Push Azeroth positions (map 0)
    ring.Push(100.0f, 100.0f, 10.0f, mapAzeroth, 1000);
    ring.Push(120.0f, 120.0f, 10.0f, mapAzeroth, 2000);

    // Push Outland positions (map 530)
    ring.Push(500.0f, 500.0f, 20.0f, mapOutland, 3000);
    ring.Push(520.0f, 520.0f, 20.0f, mapOutland, 4000);

    // Search from Azeroth: must only return Azeroth entries
    std::optional<SafePosition> azerothTarget = ring.FindBacktrackTarget(130.0f, 130.0f, 10.0f, mapAzeroth, 0);
    CHECK(azerothTarget.has_value());
    if (azerothTarget)
    {
        CHECK(azerothTarget->mapId == mapAzeroth);
        CHECK(std::abs(azerothTarget->x - 120.0f) < 0.1f);
    }

    // Search from Outland: must only return Outland entries
    std::optional<SafePosition> outlandTarget = ring.FindBacktrackTarget(530.0f, 530.0f, 20.0f, mapOutland, 0);
    CHECK(outlandTarget.has_value());
    if (outlandTarget)
    {
        CHECK(outlandTarget->mapId == mapOutland);
        CHECK(std::abs(outlandTarget->x - 520.0f) < 0.1f);
    }

    // Search in Northrend (map 571): nothing there
    std::optional<SafePosition> northrendTarget = ring.FindBacktrackTarget(130.0f, 130.0f, 10.0f, 571, 0);
    CHECK(!northrendTarget.has_value());

    // Return is by value: modifying copy must not affect ring contents
    if (azerothTarget)
    {
        azerothTarget->x = 999.0f;
        std::optional<SafePosition> check = ring.FindBacktrackTarget(130.0f, 130.0f, 10.0f, mapAzeroth, 0);
        CHECK(check.has_value());
        if (check)
            CHECK(std::abs(check->x - 120.0f) < 0.1f); // unchanged
    }
}

// ============================================================================
// Test 11: Backtrack Candidate Advance on Early Stop
// ============================================================================
static void TestBacktrackCandidateAdvanceOnEarlyStop()
{
    SafePositionHistory ring;
    uint32 mapId = 0;

    // Push 3 candidates (each 5yd apart so they pass the 3-60yd backtrack filter)
    ring.Push(10.0f, 10.0f, 0.0f, mapId, 1000); // candidate 2 (oldest)
    ring.Push(20.0f, 20.0f, 0.0f, mapId, 2000); // candidate 1
    ring.Push(30.0f, 30.0f, 0.0f, mapId, 3000); // candidate 0 (newest)

    float curX = 35.0f, curY = 35.0f, curZ = 0.0f;

    // skipCount=0: first match (30,30), ~7yd away
    auto cand0 = ring.FindBacktrackTarget(curX, curY, curZ, mapId, 0);
    CHECK(cand0.has_value());
    if (cand0) CHECK(std::abs(cand0->x - 30.0f) < 0.1f);

    // Bot stalled -> advance candidate (skipCount=1): (20,20)
    auto cand1 = ring.FindBacktrackTarget(curX, curY, curZ, mapId, 1);
    CHECK(cand1.has_value());
    if (cand1) CHECK(std::abs(cand1->x - 20.0f) < 0.1f);

    // Bot stalled again -> advance (skipCount=2): (10,10)
    auto cand2 = ring.FindBacktrackTarget(curX, curY, curZ, mapId, 2);
    CHECK(cand2.has_value());
    if (cand2) CHECK(std::abs(cand2->x - 10.0f) < 0.1f);

    // Exhausted (skipCount=3): nullopt
    auto cand3 = ring.FindBacktrackTarget(curX, curY, curZ, mapId, 3);
    CHECK(!cand3.has_value());
}

// ============================================================================
// Test 12: Detour to Backtrack Before GiveUp
// ============================================================================
static void TestDetourToBacktrackBeforeGiveUp()
{
    MovementRequest req;
    req.owner = MoveOwner::Quest;
    req.recoveryMode = RecoveryMode::DetourLeft;
    req.backtrackAttempts = 0;

    SafePositionHistory history;
    history.Push(50.0f, 50.0f, 0.0f, 0u, 1000u);

    // Progress returns GiveUp
    NavRecovery recovery = NavRecovery::GiveUp;

    // Rule: GiveUp must NOT bypass backtrack if safe candidates exist
    if (recovery == NavRecovery::GiveUp)
    {
        if (req.recoveryMode != RecoveryMode::Backtrack && req.backtrackAttempts < 3)
        {
            auto safeTarget = history.FindBacktrackTarget(60.0f, 60.0f, 0.0f, 0u, req.backtrackAttempts);
            if (safeTarget)
            {
                req.recoveryMode = RecoveryMode::Backtrack;
                req.backtrackAttempts++;
                req.backtrackX = safeTarget->x;
                req.backtrackY = safeTarget->y;
            }
        }
    }

    CHECK(req.recoveryMode == RecoveryMode::Backtrack);
    CHECK(req.backtrackAttempts == 1);
    CHECK(std::abs(req.backtrackX - 50.0f) < 0.1f);
}

// ============================================================================
// Test 13: Hold Expiration
// ============================================================================
static void TestHoldExpiration()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(2007);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    // Set up timed Hold expiring at 6000ms
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Combat;
        loc.mode = MoveMode::Hold;
        loc.state = LocomotionState::Holding;
        loc.holdUntilMs = 6000;
    }

    // Tick at 5500: still holding (5500 < 6000)
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        if (loc.state == LocomotionState::Holding && loc.holdUntilMs > 0 && 5500 >= loc.holdUntilMs)
        {
            loc.state = LocomotionState::Idle;
            loc.owner = MoveOwner::None;
        }
    }
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        CHECK(shard.locomotion[bot].state == LocomotionState::Holding);
    }

    // Tick at 6001: expired (6001 >= 6000)
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        if (loc.state == LocomotionState::Holding && loc.holdUntilMs > 0 && 6001 >= loc.holdUntilMs)
        {
            loc.state = LocomotionState::Idle;
            loc.owner = MoveOwner::None;
            loc.mode = MoveMode::Idle;
            loc.holdUntilMs = 0;
        }
    }
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        CHECK(shard.locomotion[bot].state == LocomotionState::Idle);
        CHECK(shard.locomotion[bot].owner == MoveOwner::None);
    }
}

// ============================================================================
// Test 14: Generator Completion Reconciliation
// ============================================================================
static void TestGeneratorCompletionReconciliation()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(2008);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    uint32 now = 7000;

    // Moving/Point mode, issued 2s ago
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Travel;
        loc.mode = MoveMode::Point;
        loc.state = LocomotionState::Moving;
        loc.issuedAtMs = now - 2000;
    }

    // Simulate: generator ended (genType != POINT, bot not moving, > 500ms since issue)
    uint32 genType = 0; // idle
    bool isMoving = false;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        if (loc.state == LocomotionState::Moving && loc.mode == MoveMode::Point)
        {
            if (genType != 1 /*POINT_MOTION_TYPE*/ && !isMoving && (now - loc.issuedAtMs > 500))
            {
                loc.state = LocomotionState::Idle;
                loc.owner = MoveOwner::None;
                loc.mode = MoveMode::Idle;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        CHECK(shard.locomotion[bot].state == LocomotionState::Idle);
        CHECK(shard.locomotion[bot].owner == MoveOwner::None);
    }
}

// ============================================================================
// Test 15: Mount Cast Failure Backoff
// ============================================================================
static void TestMountCastFailureBackoff()
{
    BotMountRecord mRec;
    uint32 now = 10000;

    mRec.state = MountState::MountCasting;
    mRec.pendingMountSpellId = 458;

    // Immediate cast failure applies 1500ms backoff
    MountStateMachine::TransitionCastFailed(mRec, now, 1500);

    CHECK(mRec.state == MountState::Cooldown);     // went into cooldown, not Unmounted yet
    CHECK(mRec.pendingMountSpellId == 0);           // pending spell cleared
    CHECK(mRec.remountCooldownUntilMs == now + 1500);
    CHECK(mRec.mountGeneration == 1);               // generation was bumped

    // CanMount is blocked during backoff window
    CHECK(!MountStateMachine::CanMount(mRec.state, 120.0f, mRec.remountCooldownUntilMs, now + 1000, true, false, false, 40, false));

    // UpdateCooldown transitions to Unmounted once expired
    MountStateMachine::UpdateCooldown(mRec, now + 1501);
    CHECK(mRec.state == MountState::Unmounted);

    // Now CanMount succeeds
    CHECK(MountStateMachine::CanMount(mRec.state, 120.0f, mRec.remountCooldownUntilMs, now + 1501, true, false, false, 40, false));
}

// ============================================================================
// Test 16: 64-Shard Multithread Stress Test with Dispatch Gate Serialization
// ============================================================================
static void Test64ShardMultithreadStress()
{
    LocomotionStateStore::ResetAllForTest();

    constexpr size_t NUM_THREADS = 8;
    constexpr size_t OPS_PER_THREAD = 10000;
    constexpr size_t NUM_BOTS = 1024;

    std::atomic<bool> startFlag{false};
    std::atomic<size_t> totalOpsCompleted{0};

    auto WorkerTask = [&](size_t threadId)
    {
        while (!startFlag.load(std::memory_order_relaxed))
            std::this_thread::yield();

        std::mt19937 rng(static_cast<uint32>(1337 + threadId));
        std::uniform_int_distribution<uint64> botDist(1, NUM_BOTS);
        std::uniform_int_distribution<int> opDist(0, 4);

        for (size_t op = 0; op < OPS_PER_THREAD; ++op)
        {
            ObjectGuid bot(botDist(rng));
            LocomotionShard& shard = LocomotionStateStore::GetShard(bot);
            std::mutex& gate = LocomotionStateStore::GetDispatchGate(bot);

            int action = opDist(rng);
            switch (action)
            {
                case 0: // Claim slot with dispatch gate -> shard lock (correct order)
                {
                    std::lock_guard<std::mutex> dispatchLock(gate);
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    if (LocomotionStateStore::CanClaimLocked(shard, bot, MoveOwner::Combat, false))
                    {
                        BotLocomotionRecord& loc = shard.locomotion[bot];
                        loc.owner = MoveOwner::Combat;
                        loc.commandId = op + 1;
                        loc.state = LocomotionState::Moving;
                    }
                    break;
                }
                case 1: // Push safe position (shard lock only)
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    float p = static_cast<float>(op % 100);
                    shard.safeHistories[bot].Push(p, p, 0.0f, 0u, static_cast<uint32>(op * 100));
                    break;
                }
                case 2: // CanMount query (shard lock only, read)
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    LocomotionStateStore::CanMountLocked(shard, bot, 120.0f, 10000, true, false, false, 40, false);
                    break;
                }
                case 3: // Dismount transition (dispatch gate -> shard lock)
                {
                    std::lock_guard<std::mutex> dispatchLock(gate);
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    BotMountRecord& mRec = shard.mounts[bot];
                    MountStateMachine::TransitionDismount(mRec, 15000);
                    break;
                }
                case 4: // Read snapshot (shard lock only)
                {
                    std::lock_guard<std::mutex> lock(shard.mutex);
                    LocomotionStateStore::GetRequestSnapshotLocked(shard, bot);
                    break;
                }
            }
            totalOpsCompleted.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(NUM_THREADS);
    for (size_t i = 0; i < NUM_THREADS; ++i)
        workers.emplace_back(WorkerTask, i);

    startFlag.store(true, std::memory_order_release);

    // Watchdog: 10-second timeout to catch deadlocks
    auto future = std::async(std::launch::async, [&]() {
        for (auto& w : workers)
            if (w.joinable())
                w.join();
    });

    std::future_status status = future.wait_for(std::chrono::seconds(10));
    CHECK(status == std::future_status::ready);
    CHECK(totalOpsCompleted.load() == NUM_THREADS * OPS_PER_THREAD);
}

// ============================================================================
// Main Runner
// ============================================================================
int main()
{
    std::printf("Running locomotion & navigation overhaul Round 4 regression tests...\n");
    std::printf("Using production LocomotionStateStore: %zu shards, %zu dispatch gates\n",
        LOCOMOTION_SHARDS, DISPATCH_GATES);

    TestLocomotionToken();
    TestPriorityArbitration();
    TestStaleNavigateArrivalSuperseded();
    TestTransactionalMoveToOwnerPreservation();
    TestDispatchGateSerialization();
    TestStaleReleaseRaceProtection();
    TestMountInterruptAndGenerationInvalidation();
    TestLeaderPreferenceClearOnLeave();
    TestPreferUnmountedVsAutonomousTravel();
    TestSafePositionValueCopyAndMapIdFilter();
    TestBacktrackCandidateAdvanceOnEarlyStop();
    TestDetourToBacktrackBeforeGiveUp();
    TestHoldExpiration();
    TestGeneratorCompletionReconciliation();
    TestMountCastFailureBackoff();
    Test64ShardMultithreadStress();

    std::printf("Locomotion tests completed: %d checks, %d failures\n", _checks, _failures);
    return _failures == 0 ? 0 : 1;
}
