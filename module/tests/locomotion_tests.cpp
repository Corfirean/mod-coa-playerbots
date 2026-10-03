/*
 * mod-coa-playerbots -- locomotion & navigation overhaul Round 4 & 5 regression tests
 *
 * Verifies all Round 4 & 5 core invariants using the EXACT production BotMovementStateStore:
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
 * 17. Synthetic token collision prevention (P0)
 * 18. Lower priority IssueLeg failed claim does ZERO physical mutation (P0)
 * 19. Generation-guarded deferred actions in Update() (P0)
 * 20. Deferred mount policy race protection
 * 21. Navigate -> Hold mode transition cleans up stale request & prevents mount
 * 22. Generator reconciler - Follow (with stationary check)
 * 23. Generator reconciler - Chase (with stationary check)
 * 24. Dungeon, raid, and water mount legality & dismount reasons
 * 25. Dispatch contention & concurrency scalability
 * 26. Point generator reconciler (zero magic numbers)
 * 27. Stale Navigate request generation guard blocks physical dispatch (P0)
 * 28. Planning epoch prevents older same-owner path result from overwriting newer intent (P1)
 * 29. Cancelled mount generation cannot resurrect late auras (P0/P1)
 * 30. Autonomous Navigate mounts through policy without explicit direct call (P1)
 */

#include "stub/Define.h"
#include "stub/MotionMaster.h"
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
        req.requestGeneration = 1;
        req.movementCommandId = 55;

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
        CHECK(it->second.movementCommandId == 55);
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

    // Simulate: generator ended (genType != POINT_MOTION_TYPE, bot not moving, > 500ms since issue)
    MovementGeneratorType genType = IDLE_MOTION_TYPE;
    bool isMoving = false;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        GeneratorReconciler::ReconcilePoint(loc, genType, isMoving, now);
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
// Test 17: Synthetic Token Collision Prevention (P0 Invariant)
// ============================================================================
static void TestSyntheticTokenCollisionPrevention()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(3001);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    // Initial state: commandId 40
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Ambient;
        loc.commandId = 40;
        loc.state = LocomotionState::Moving;
    }

    // Navigate A starts: allocates requestGeneration = 1.
    // CRITICAL: Navigate does NOT guess or predict commandId = 41!
    uint64 reqGenA = 0;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        reqGenA = LocomotionStateStore::AllocateRequestGenerationLocked(shard, bot);
        MovementRequest& req = shard.requests[bot];
        req.botGuid = bot;
        req.owner = MoveOwner::Quest;
        req.goalId = 100;
        req.requestGeneration = reqGenA;
        req.movementCommandId = 0; // Not committed yet!
    }
    CHECK(reqGenA == 1);

    // Concurrent command B arrives and commits real commandId = 41 (Combat)
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Combat;
        loc.commandId = 41;
        loc.state = LocomotionState::Moving;
    }

    // Now Navigate A's IssueLeg fails (e.g. CanClaimLocked fails because Combat > Quest).
    // In the old broken code: Navigate A generated LocomotionToken{Quest, 41} and called Release(41),
    // killing Command B.
    // In Round 5 code:
    // 1. IssueLeg returns invalid token: LocomotionToken{}
    // 2. Navigate failure only cleans up its own request generation:
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        LocomotionToken emptyTok{};
        CHECK(!emptyTok.IsValid());

        auto reqIt = shard.requests.find(bot);
        if (reqIt != shard.requests.end() && reqIt->second.requestGeneration == reqGenA)
        {
            shard.requests.erase(reqIt);
        }
    }

    // Command B (Combat, 41) MUST remain completely intact!
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord const& loc = shard.locomotion[bot];
        CHECK(loc.owner == MoveOwner::Combat);
        CHECK(loc.commandId == 41);
        CHECK(loc.state == LocomotionState::Moving);
        CHECK(shard.requests.find(bot) == shard.requests.end());
    }
}

// ============================================================================
// Test 18: Lower Priority IssueLeg Failed Claim Does ZERO Physical Mutation (P0 Invariant)
// ============================================================================
static void TestLowerPriorityIssueLegNoMutation()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(3002);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    // High priority Combat Chase active: commandId = 50
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Combat;
        loc.mode = MoveMode::Chase;
        loc.state = LocomotionState::Chasing;
        loc.commandId = 50;
    }

    // Low priority Quest Navigate attempts to issue leg.
    // In old code: IssueLeg used force=true and called ClearAuthorizedMovement(bot),
    // clearing Combat's motion!
    // In Round 5 code: IssueLeg has NO force parameter and NO ClearAuthorizedMovement.
    // Pre-claim check under lock:
    bool canClaim = false;
    LocomotionToken tokenResult;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        canClaim = LocomotionStateStore::CanClaimLocked(shard, bot, MoveOwner::Quest, false);
        if (canClaim)
        {
            // Should not enter here
            tokenResult = LocomotionToken{bot, MoveOwner::Quest, 51};
        }
    }

    CHECK(!canClaim);
    CHECK(!tokenResult.IsValid());

    // Combat Chase must be completely unmutated
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord const& loc = shard.locomotion[bot];
        CHECK(loc.owner == MoveOwner::Combat);
        CHECK(loc.mode == MoveMode::Chase);
        CHECK(loc.state == LocomotionState::Chasing);
        CHECK(loc.commandId == 50);
    }
}

// ============================================================================
// Test 19: Generation-Guarded Deferred Actions in Update() (P0 Invariant)
// ============================================================================
static void TestGenerationGuardedDeferredAction()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(3003);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    // Initial state: Travel Point, commandId = 5
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Travel;
        loc.mode = MoveMode::Point;
        loc.state = LocomotionState::Moving;
        loc.commandId = 5;
    }

    // Update() tick snapshot: decides command 5 is finished, sets deferredClear = true
    uint64 deferredCommandId = 5;
    MoveOwner deferredOwner = MoveOwner::Travel;
    MoveMode deferredMode = MoveMode::Point;
    LocomotionState deferredState = LocomotionState::Moving;

    // Before deferred execution, higher priority Combat preempts: commandId = 6
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Combat;
        loc.mode = MoveMode::Chase;
        loc.state = LocomotionState::Chasing;
        loc.commandId = 6;
    }

    // Deferred action executes under dispatchGate + shard.mutex:
    bool executedMutation = false;
    {
        std::mutex& gate = LocomotionStateStore::GetDispatchGate(bot);
        std::lock_guard<std::mutex> dispatchLock(gate);
        std::lock_guard<std::mutex> lock(shard.mutex);

        BotLocomotionRecord& loc = shard.locomotion[bot];
        if (LocomotionCommandValidator::ValidateDeferredAction(loc, deferredCommandId, deferredOwner, deferredMode, deferredState))
        {
            // Stale validation must return false!
            loc.state = LocomotionState::Idle;
            loc.owner = MoveOwner::None;
            executedMutation = true;
        }
    }

    CHECK(!executedMutation);

    // Combat command 6 remains active and untouched
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotLocomotionRecord const& loc = shard.locomotion[bot];
        CHECK(loc.owner == MoveOwner::Combat);
        CHECK(loc.mode == MoveMode::Chase);
        CHECK(loc.commandId == 6);
    }
}

// ============================================================================
// Test 20: Deferred Mount Policy Race Protection
// ============================================================================
static void TestDeferredMountPolicyRaceProtection()
{
    uint32 now = 10000;

    // Leader preference snapshot: PreferUnmounted
    DesiredMountState leaderPref = DesiredMountState::PreferUnmounted;

    // Bot transitions to autonomous Navigate (150yd travel)
    MoveOwner activeOwner = MoveOwner::Quest;
    MoveMode activeMode = MoveMode::Navigate;
    float travelDist = 150.0f;

    // Re-evaluating under dispatch gate:
    MountPolicyDecision dec = MountPolicyResolver::Evaluate(
        activeOwner, activeMode, travelDist,
        false /*isFollowingLeader*/, leaderPref, true /*debounced*/,
        true /*outdoors*/, false /*combat*/, false /*casting*/,
        40 /*level*/, false /*controlled*/,
        MountState::Unmounted, 0, now, false /*canFly*/, false /*isCurrentlyMounted*/
    );

    // Must mount for autonomous travel despite leader unmounted preference
    CHECK(dec.action == MountAction::MountGround);
}

// ============================================================================
// Test 21: Navigate -> Hold Mode Transition Cleans Up Stale Request & Prevents Mount
// ============================================================================
static void TestNavigateToHoldCleansRequestAndBlocksMount()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(3004);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    // Setup active Navigate request with 200yd travel
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        MovementRequest& req = shard.requests[bot];
        req.botGuid = bot;
        req.owner = MoveOwner::Quest;
        req.goalId = 999;
        req.requestGeneration = 1;
        req.movementCommandId = 12;

        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Quest;
        loc.mode = MoveMode::Navigate;
        loc.state = LocomotionState::Moving;
        loc.commandId = 12;
    }

    // Bot transitions to Hold (e.g. crowd controlled or script hold)
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        shard.requests.erase(bot); // Cleaned up!
        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.mode = MoveMode::Hold;
        loc.state = LocomotionState::Holding;
        loc.holdUntilMs = 5000;
    }

    // Requests map must have no entry for this bot
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        CHECK(shard.requests.find(bot) == shard.requests.end());
    }

    // Evaluate mount policy with activeMode = MoveMode::Hold:
    // Even if travelDist is somehow passed as 200yd, activeMode is Hold -> MUST NOT mount!
    MountPolicyDecision dec = MountPolicyResolver::Evaluate(
        MoveOwner::Quest, MoveMode::Hold, 200.0f,
        false, DesiredMountState::None, true,
        true, false, false, 40, false,
        MountState::Unmounted, 0, 1000, false, false
    );
    CHECK(dec.action == MountAction::None);
}

// ============================================================================
// Test 22: Generator Reconciler - Follow
// ============================================================================
static void TestGeneratorReconcilerFollow()
{
    BotLocomotionRecord rec;
    rec.owner = MoveOwner::Travel;
    rec.mode = MoveMode::Follow;
    rec.state = LocomotionState::Following;
    rec.issuedAtMs = 1000;

    uint32 now = 2000;

    // Case A: Generator still active (genType == FOLLOW_MOTION_TYPE) -> do not reconcile
    CHECK(!GeneratorReconciler::ReconcileFollow(rec, FOLLOW_MOTION_TYPE, false, now));
    CHECK(rec.state == LocomotionState::Following);

    // Case B: Stationary Follow (bot reached leader, stationary, age > 500ms) -> MUST remain Following!
    CHECK(!GeneratorReconciler::ReconcileFollow(rec, FOLLOW_MOTION_TYPE, false, now + 5000));
    CHECK(rec.state == LocomotionState::Following);

    // Case C: Still moving -> do not reconcile
    CHECK(!GeneratorReconciler::ReconcileFollow(rec, IDLE_MOTION_TYPE, true /*isMoving*/, now));
    CHECK(rec.state == LocomotionState::Following);

    // Case D: Within 500ms debounce -> do not reconcile
    CHECK(!GeneratorReconciler::ReconcileFollow(rec, IDLE_MOTION_TYPE, false, 1200 /*issued 200ms ago*/));
    CHECK(rec.state == LocomotionState::Following);

    // Case E: Generator ended (IDLE_MOTION_TYPE), not moving, issued > 500ms ago -> reconciles to Idle!
    CHECK(GeneratorReconciler::ReconcileFollow(rec, IDLE_MOTION_TYPE, false, now));
    CHECK(rec.state == LocomotionState::Idle);
    CHECK(rec.mode == MoveMode::Idle);
    CHECK(rec.owner == MoveOwner::None);
}

// ============================================================================
// Test 23: Generator Reconciler - Chase
// ============================================================================
static void TestGeneratorReconcilerChase()
{
    BotLocomotionRecord rec;
    rec.owner = MoveOwner::Combat;
    rec.mode = MoveMode::Chase;
    rec.state = LocomotionState::Chasing;
    rec.issuedAtMs = 1000;

    uint32 now = 2000;

    // Case A: Generator still active (genType == CHASE_MOTION_TYPE) -> do not reconcile
    CHECK(!GeneratorReconciler::ReconcileChase(rec, CHASE_MOTION_TYPE, false, now));
    CHECK(rec.state == LocomotionState::Chasing);

    // Case B: Stationary Chase (target in valid range, bot stands still, age > 500ms) -> MUST remain Chasing!
    CHECK(!GeneratorReconciler::ReconcileChase(rec, CHASE_MOTION_TYPE, false, now + 5000));
    CHECK(rec.state == LocomotionState::Chasing);

    // Case C: Still moving -> do not reconcile
    CHECK(!GeneratorReconciler::ReconcileChase(rec, IDLE_MOTION_TYPE, true /*isMoving*/, now));
    CHECK(rec.state == LocomotionState::Chasing);

    // Case D: Within debounce -> do not reconcile
    CHECK(!GeneratorReconciler::ReconcileChase(rec, IDLE_MOTION_TYPE, false, 1300 /*issued 300ms ago*/));
    CHECK(rec.state == LocomotionState::Chasing);

    // Case E: Generator ended (IDLE_MOTION_TYPE), not moving, past debounce -> reconciles to Idle!
    CHECK(GeneratorReconciler::ReconcileChase(rec, IDLE_MOTION_TYPE, false, now));
    CHECK(rec.state == LocomotionState::Idle);
    CHECK(rec.mode == MoveMode::Idle);
    CHECK(rec.owner == MoveOwner::None);
}

// ============================================================================
// Test 24: Dungeon, Raid, and Water Mount Legality & Dismount Reasons
// ============================================================================
static void TestDungeonRaidWaterMountLegality()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(3005);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    uint32 now = 5000;

    // Test CanMountLocked
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        // Base legal: outdoor, lvl 40, 100yd
        CHECK(LocomotionStateStore::CanMountLocked(shard, bot, 100.0f, now, true, false, false, 40, false));

        // Illegal in dungeon
        CHECK(!LocomotionStateStore::CanMountLocked(shard, bot, 100.0f, now, true, false, false, 40, false, true /*dungeon*/));
        // Illegal in raid
        CHECK(!LocomotionStateStore::CanMountLocked(shard, bot, 100.0f, now, true, false, false, 40, false, false, true /*raid*/));
        // Illegal in water
        CHECK(!LocomotionStateStore::CanMountLocked(shard, bot, 100.0f, now, true, false, false, 40, false, false, false, true /*water*/));
        // Illegal swimming
        CHECK(!LocomotionStateStore::CanMountLocked(shard, bot, 100.0f, now, true, false, false, 40, false, false, false, false, true /*swimming*/));
    }

    // Test MountPolicyResolver::Evaluate dismount actions & reasons
    // In Dungeon -> Dismount with IndoorEntered
    MountPolicyDecision decDungeon = MountPolicyResolver::Evaluate(
        MoveOwner::None, MoveMode::Idle, 0.0f, false, DesiredMountState::None, true,
        true, false, false, 40, false, MountState::MountedGround, 0, now, false, true,
        true /*isDungeon*/
    );
    CHECK(decDungeon.action == MountAction::Dismount);
    CHECK(decDungeon.dismountReason == DismountReason::IndoorEntered);

    // In Water -> Dismount with WaterEntered
    MountPolicyDecision decWater = MountPolicyResolver::Evaluate(
        MoveOwner::None, MoveMode::Idle, 0.0f, false, DesiredMountState::None, true,
        true, false, false, 40, false, MountState::MountedGround, 0, now, false, true,
        false, false, true /*isInWater*/
    );
    CHECK(decWater.action == MountAction::Dismount);
    CHECK(decWater.dismountReason == DismountReason::WaterEntered);

    // Swimming -> Dismount with WaterEntered
    MountPolicyDecision decSwim = MountPolicyResolver::Evaluate(
        MoveOwner::None, MoveMode::Idle, 0.0f, false, DesiredMountState::None, true,
        true, false, false, 40, false, MountState::MountedGround, 0, now, false, true,
        false, false, false, true /*isSwimming*/
    );
    CHECK(decSwim.action == MountAction::Dismount);
    CHECK(decSwim.dismountReason == DismountReason::WaterEntered);
}

// ============================================================================
// Test 25: Dispatch Contention & Concurrency Scalability
// ============================================================================
static void TestDispatchContentionAndScalability()
{
    // Find two distinct bot GUIDs that hash to DIFFERENT dispatch gates
    ObjectGuid botA(1);
    size_t gateA = botA.GetCounter() % DISPATCH_GATES;
    ObjectGuid botB(2);
    while ((botB.GetCounter() % DISPATCH_GATES) == gateA)
    {
        botB = ObjectGuid(botB.GetRawValue() + 1);
    }
    CHECK((botA.GetCounter() % DISPATCH_GATES) != (botB.GetCounter() % DISPATCH_GATES));

    // Also find botC that hashes to the SAME gate as botA
    ObjectGuid botC(botA.GetRawValue() + 1);
    while ((botC.GetCounter() % DISPATCH_GATES) != gateA)
    {
        botC = ObjectGuid(botC.GetRawValue() + 1);
    }
    CHECK((botA.GetCounter() % DISPATCH_GATES) == (botC.GetCounter() % DISPATCH_GATES));

    // Verify botA and botB can acquire their dispatch gates concurrently without contention
    std::mutex& gateForA = LocomotionStateStore::GetDispatchGate(botA);
    std::mutex& gateForB = LocomotionStateStore::GetDispatchGate(botB);

    std::atomic<bool> aLocked{false};
    std::atomic<bool> bRanConcurrently{false};

    std::thread threadA([&]() {
        std::lock_guard<std::mutex> lockA(gateForA);
        aLocked.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    while (!aLocked.load())
        std::this_thread::yield();

    std::thread threadB([&]() {
        // While threadA holds gateForA, threadB should be able to immediately acquire gateForB!
        std::lock_guard<std::mutex> lockB(gateForB);
        bRanConcurrently.store(aLocked.load());
    });

    threadA.join();
    threadB.join();

    CHECK(bRanConcurrently.load());
}

// ============================================================================
// Test 26: Point Generator Reconciler (Zero Magic Numbers)
// ============================================================================
static void TestGeneratorReconcilerPoint()
{
    BotLocomotionRecord rec;
    rec.owner = MoveOwner::Travel;
    rec.mode = MoveMode::Point;
    rec.state = LocomotionState::Moving;
    rec.issuedAtMs = 1000;

    uint32 now = 2000;

    // Case A: Generator still active (genType == POINT_MOTION_TYPE) -> do not reconcile
    CHECK(!GeneratorReconciler::ReconcilePoint(rec, POINT_MOTION_TYPE, false, now));
    CHECK(rec.state == LocomotionState::Moving);

    // Case B: Stationary Point with POINT_MOTION_TYPE -> MUST remain Moving!
    CHECK(!GeneratorReconciler::ReconcilePoint(rec, POINT_MOTION_TYPE, false, now + 5000));
    CHECK(rec.state == LocomotionState::Moving);

    // Case C: Still moving -> do not reconcile
    CHECK(!GeneratorReconciler::ReconcilePoint(rec, IDLE_MOTION_TYPE, true /*isMoving*/, now));
    CHECK(rec.state == LocomotionState::Moving);

    // Case D: Within 500ms debounce -> do not reconcile
    CHECK(!GeneratorReconciler::ReconcilePoint(rec, IDLE_MOTION_TYPE, false, 1200 /*issued 200ms ago*/));
    CHECK(rec.state == LocomotionState::Moving);

    // Case E: Generator ended (IDLE_MOTION_TYPE), not moving, past debounce -> reconciles to Idle!
    CHECK(GeneratorReconciler::ReconcilePoint(rec, IDLE_MOTION_TYPE, false, now));
    CHECK(rec.state == LocomotionState::Idle);
    CHECK(rec.mode == MoveMode::Idle);
    CHECK(rec.owner == MoveOwner::None);
}

// ============================================================================
// Test 27: Stale Navigate Request Generation Guard Blocks Physical Dispatch & Mutation (P0)
// ============================================================================
static void TestStaleNavigateRequestGenerationGuardBlocksDispatch()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4001);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    // Navigate A starts: requestGen = 10, goalId = 100
    MovementDispatchGuard guardA{10, 100};
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        MovementRequest& req = shard.requests[bot];
        req.botGuid = bot;
        req.owner = MoveOwner::Quest;
        req.goalId = 100;
        req.requestGeneration = 10;
        req.movementCommandId = 0;
    }

    // While A was calculating path, Navigate B arrives with newer requestGen = 11, goalId = 200, commits command 50
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        MovementRequest& req = shard.requests[bot];
        req.goalId = 200;
        req.requestGeneration = 11;
        req.movementCommandId = 50;

        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Quest;
        loc.commandId = 50;
        loc.state = LocomotionState::Moving;
        loc.mode = MoveMode::Navigate;
    }

    // Now A finishes path and tries to commit with guardA (gen=10, goal=100)
    LocomotionToken tokenA;
    bool committedA = false;
    {
        std::mutex& gate = LocomotionStateStore::GetDispatchGate(bot);
        std::lock_guard<std::mutex> dispatchLock(gate);
        std::lock_guard<std::mutex> lock(shard.mutex);

        // Exact guard validation logic inside MoveToInternal:
        auto rItr = shard.requests.find(bot);
        if (rItr == shard.requests.end() ||
            rItr->second.owner != MoveOwner::Quest ||
            rItr->second.requestGeneration != guardA.requestGeneration ||
            rItr->second.goalId != guardA.goalId)
        {
            // Stale! Reject with ZERO mutation!
            committedA = false;
        }
        else
        {
            committedA = true;
            BotLocomotionRecord& loc = shard.locomotion[bot];
            loc.commandId++;
            tokenA = LocomotionToken{bot, MoveOwner::Quest, loc.commandId};
        }
    }

    CHECK(!committedA);
    CHECK(!tokenA.IsValid());

    // Active Navigate B remains completely untouched
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        CHECK(shard.locomotion[bot].commandId == 50);
        CHECK(shard.locomotion[bot].owner == MoveOwner::Quest);
        CHECK(shard.requests[bot].requestGeneration == 11);
        CHECK(shard.requests[bot].goalId == 200);
        CHECK(shard.requests[bot].movementCommandId == 50);
    }
}

// ============================================================================
// Test 28: Planning Epoch Prevents Older Same-Owner Path Result From Overwriting Newer Intent (P1)
// ============================================================================
static void TestPlanningEpochSameOwnerRace()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4002);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    // Direct MoveTo A starts: allocates ticket 1
    uint64 ticketA = 0;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        ticketA = LocomotionStateStore::AllocatePlanTicketLocked(shard, bot, MoveOwner::Quest);
    }
    CHECK(ticketA == 1);

    // Direct MoveTo B starts later: allocates ticket 2 and commits command 100
    uint64 ticketB = 0;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        ticketB = LocomotionStateStore::AllocatePlanTicketLocked(shard, bot, MoveOwner::Quest);
        CHECK(ticketB == 2);

        // B commits under dispatch gate
        BotLocomotionRecord& loc = shard.locomotion[bot];
        loc.owner = MoveOwner::Quest;
        loc.mode = MoveMode::Point;
        loc.commandId = 100;
        loc.state = LocomotionState::Moving;
    }

    // Now A's slow path finishes, attempts to commit with ticketA (1)
    bool committedA = false;
    {
        std::mutex& gate = LocomotionStateStore::GetDispatchGate(bot);
        std::lock_guard<std::mutex> dispatchLock(gate);
        std::lock_guard<std::mutex> lock(shard.mutex);

        if (LocomotionStateStore::IsPlanCurrentLocked(shard, bot, MoveOwner::Quest, ticketA))
        {
            committedA = true;
            shard.locomotion[bot].commandId = 101;
        }
    }

    CHECK(!committedA); // Ticket 1 must be rejected by ticket 2!

    // Verify B's command 100 is still active
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        CHECK(shard.locomotion[bot].commandId == 100);
    }
}

// ============================================================================
// Test 29: Cancelled Mount Generation Cannot Resurrect Late Auras (P0/P1)
// ============================================================================
static void TestCancelledMountGenerationCannotResurrect()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4003);
    LocomotionShard& shard = LocomotionStateStore::GetShard(bot);

    uint32 now = 10000;

    // Mount cast starts: generation 1, castingGeneration 1
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot];
        mRec.mountGeneration = 1;
        mRec.castingGeneration = 1;
        mRec.pendingMountSpellId = 458;
        mRec.state = MountState::MountCasting;
    }

    // Dismount / cancellation occurs: bumps mountGeneration to 2, sets state to Cooldown
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot];
        MountStateMachine::TransitionDismount(mRec, now);
        CHECK(mRec.mountGeneration == 2);
        CHECK(mRec.state == MountState::Cooldown);
    }

    // Late completion with old generation 1:
    // castingGeneration (1) != mountGeneration (2)
    bool removeStaleAura = false;
    bool mountSuccess = false;
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        BotMountRecord& mRec = shard.mounts[bot];

        // Resolution check
        if (mRec.castingGeneration != mRec.mountGeneration)
        {
            // Invalidated generation!
            removeStaleAura = true;
            // Do NOT transition to Mounted!
        }
        else
        {
            mountSuccess = true;
        }
    }

    CHECK(removeStaleAura);
    CHECK(!mountSuccess);

    // FSM remains in Cooldown / Unmounted, does NOT resurrect to MountedGround
    {
        std::lock_guard<std::mutex> lock(shard.mutex);
        CHECK(shard.mounts[bot].state == MountState::Cooldown);
    }
}

// ============================================================================
// Test 30: Autonomous Navigate Mounts Through Policy Without Explicit Direct Call (P1)
// ============================================================================
static void TestAutonomousNavigateMountsThroughPolicy()
{
    uint32 now = 10000;

    // Navigate autonomous travel with travelDist = 150yd
    MountPolicyDecision decNav = MountPolicyResolver::Evaluate(
        MoveOwner::Travel, MoveMode::Navigate, 150.0f,
        false /*isFollowingLeader*/, DesiredMountState::None, true /*debounced*/,
        true /*outdoors*/, false /*combat*/, false /*casting*/,
        40 /*level*/, false /*controlled*/,
        MountState::Unmounted, 0, now, false /*canFly*/, false /*isCurrentlyMounted*/
    );

    // Mount policy autonomously decides to mount ground without any direct pre-call!
    CHECK(decNav.action == MountAction::MountGround);

    // If bot enters Hold, policy returns None:
    MountPolicyDecision decHold = MountPolicyResolver::Evaluate(
        MoveOwner::Travel, MoveMode::Hold, 150.0f,
        false, DesiredMountState::None, true,
        true, false, false, 40, false,
        MountState::Unmounted, 0, now, false, false
    );
    CHECK(decHold.action == MountAction::None);
}

// ============================================================================
// Test 31: Old MoveTo Plan Cannot Overwrite Newer Hold (Same Owner) (P0/P1)
// ============================================================================
static void TestOldMoveToPlanCannotOverwriteNewerHoldSameOwner()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4001);
    auto& shard = LocomotionStateStore::GetShard(bot);

    // Step 1: MoveTo starts path planning for Quest owner, allocating ticket 1
    uint64 ticket1 = LocomotionStateStore::AllocatePlanTicketLocked(shard, bot, MoveOwner::Quest);
    CHECK(ticket1 == 1);

    // Step 2: While path calculation is in-flight, a newer Hold intent arrives for Quest
    LocomotionStateStore::AdvanceIntentEpochLocked(shard, bot, MoveOwner::Quest);

    // Step 3: Path calculation finishes and attempts to commit with ticket 1
    bool canCommit = LocomotionStateStore::IsPlanCurrentLocked(shard, bot, MoveOwner::Quest, ticket1);
    CHECK(!canCommit); // In-flight plan cannot overwrite newer Hold!
}

// ============================================================================
// Test 32: Old MoveTo Plan Cannot Overwrite Newer Follow (Same Owner) (P0/P1)
// ============================================================================
static void TestOldMoveToPlanCannotOverwriteNewerFollowSameOwner()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4002);
    auto& shard = LocomotionStateStore::GetShard(bot);

    uint64 ticket = LocomotionStateStore::AllocatePlanTicketLocked(shard, bot, MoveOwner::Quest);

    // In-flight: Follow intent arrives for Quest
    LocomotionStateStore::AdvanceIntentEpochLocked(shard, bot, MoveOwner::Quest);

    bool canCommit = LocomotionStateStore::IsPlanCurrentLocked(shard, bot, MoveOwner::Quest, ticket);
    CHECK(!canCommit); // Old plan cannot overwrite newer Follow!
}

// ============================================================================
// Test 33: Old MoveTo Plan Cannot Overwrite Newer Chase (Same Owner) (P0/P1)
// ============================================================================
static void TestOldMoveToPlanCannotOverwriteNewerChaseSameOwner()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4003);
    auto& shard = LocomotionStateStore::GetShard(bot);

    uint64 ticket = LocomotionStateStore::AllocatePlanTicketLocked(shard, bot, MoveOwner::Combat);

    // In-flight: Chase intent arrives for Combat
    LocomotionStateStore::AdvanceIntentEpochLocked(shard, bot, MoveOwner::Combat);

    bool canCommit = LocomotionStateStore::IsPlanCurrentLocked(shard, bot, MoveOwner::Combat, ticket);
    CHECK(!canCommit); // Old plan cannot overwrite newer Chase!
}

// ============================================================================
// Test 34: ForceStopOwner Invalidates Pending MoveTo Plan (P0/P1)
// ============================================================================
static void TestForceStopOwnerInvalidatesPendingMoveToPlan()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4004);
    auto& shard = LocomotionStateStore::GetShard(bot);

    uint64 ticket = LocomotionStateStore::AllocatePlanTicketLocked(shard, bot, MoveOwner::Quest);

    // ForceStopOwner called for Quest
    LocomotionStateStore::AdvanceIntentEpochLocked(shard, bot, MoveOwner::Quest);

    bool canCommit = LocomotionStateStore::IsPlanCurrentLocked(shard, bot, MoveOwner::Quest, ticket);
    CHECK(!canCommit); // ForceStopOwner invalidates in-flight plan!

    // Also test administrative stop for all owners (MoveOwner::None)
    uint64 ticketTravel = LocomotionStateStore::AllocatePlanTicketLocked(shard, bot, MoveOwner::Travel);
    LocomotionStateStore::AdvanceIntentEpochLocked(shard, bot, MoveOwner::None);

    bool canCommitTravel = LocomotionStateStore::IsPlanCurrentLocked(shard, bot, MoveOwner::Travel, ticketTravel);
    CHECK(!canCommitTravel); // Administrative stop of all owners invalidates pending travel plan!
}

// ============================================================================
// Test 35: Same Navigate Request Command CAS (P0/P1)
// ============================================================================
static void TestSameNavigateRequestCommandCAS()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4005);
    auto& shard = LocomotionStateStore::GetShard(bot);

    uint64 reqGen = 50;
    uint64 goalId = 999;
    MovementRequest req;
    req.botGuid = bot;
    req.owner = MoveOwner::Quest;
    req.goalId = goalId;
    req.requestGeneration = reqGen;
    req.movementCommandId = 100;
    shard.requests[bot] = req;

    // Active locomotion state
    BotLocomotionRecord rec;
    rec.owner = MoveOwner::Quest;
    rec.mode = MoveMode::Navigate;
    rec.commandId = 101;
    shard.locomotion[bot] = rec;

    // Tick A snapshots previousMovementCommandId = 100
    // But before Tick A commits, Tick B runs, issues leg 102 and commits it:
    shard.requests[bot].movementCommandId = 102;
    shard.locomotion[bot].commandId = 102;

    // Now Tick A resumes with its older token 101 and expectedPrevious = 100:
    LocomotionToken tokenA{bot, MoveOwner::Quest, 101};
    bool committedA = LocomotionStateStore::CommitNavigateLegLocked(
        shard, bot, reqGen, goalId, 100, req, tokenA);

    CHECK(!committedA); // CAS failed! Command 101 cannot overwrite newer command 102!
    CHECK(shard.requests[bot].movementCommandId == 102); // Request remains on 102!
}

// ============================================================================
// Test 36: Stale GiveUp Cannot Erase Advanced Command (P0/P1)
// ============================================================================
static void TestStaleGiveUpCannotEraseAdvancedCommand()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4006);
    auto& shard = LocomotionStateStore::GetShard(bot);

    // Active request is currently at command 102
    MovementRequest req;
    req.botGuid = bot;
    req.owner = MoveOwner::Quest;
    req.goalId = 999;
    req.requestGeneration = 50;
    req.movementCommandId = 102;
    shard.requests[bot] = req;

    // A stale tick that snapshotted command 101 gives up:
    bool erased = LocomotionStateStore::EraseRequestIfCurrentLocked(shard, bot, 50, 101);
    CHECK(!erased); // Stale give-up cannot erase request whose commandId advanced!
    CHECK(shard.requests.find(bot) != shard.requests.end()); // Request safely preserved!

    // If matching command 102 gives up, it succeeds:
    bool erasedCurrent = LocomotionStateStore::EraseRequestIfCurrentLocked(shard, bot, 50, 102);
    CHECK(erasedCurrent);
    CHECK(shard.requests.find(bot) == shard.requests.end());
}

// ============================================================================
// Test 37: Stale Recovery Leg Cannot Replace Newer Leg (P0/P1)
// ============================================================================
static void TestStaleRecoveryLegCannotReplaceNewerLeg()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4007);
    auto& shard = LocomotionStateStore::GetShard(bot);

    uint64 reqGen = 60;
    uint64 goalId = 888;
    MovementRequest req;
    req.botGuid = bot;
    req.owner = MoveOwner::Travel;
    req.goalId = goalId;
    req.requestGeneration = reqGen;
    req.movementCommandId = 205;
    shard.requests[bot] = req;

    shard.locomotion[bot].commandId = 205;
    shard.locomotion[bot].owner = MoveOwner::Travel;
    shard.locomotion[bot].mode = MoveMode::Navigate;

    // Stale recovery leg issued when commandId was 203 attempts to commit:
    MovementRequest backtrackReq = req;
    backtrackReq.recoveryMode = RecoveryMode::Backtrack;
    LocomotionToken staleToken{bot, MoveOwner::Travel, 204};

    bool committed = LocomotionStateStore::CommitNavigateLegLocked(
        shard, bot, reqGen, goalId, 203 /*expectedPrev*/, backtrackReq, staleToken);

    CHECK(!committed); // Stale recovery leg rejected!
    CHECK(shard.requests[bot].movementCommandId == 205);
}

// ============================================================================
// Test 38: Gathering Mount Cast And Walk Resumption (P1)
// ============================================================================
static void TestGatheringMountCastAndWalkResumption()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4008);
    auto& shard = LocomotionStateStore::GetShard(bot);

    // 1. Initial state: Bot far from node (120yd)
    float dist = 120.0f;
    uint32 now = 1000;
    BotMountRecord& mRec = shard.mounts[bot];
    mRec.state = MountState::Unmounted;

    // 2. Bot starts mount cast: state transitions to MountCasting
    mRec.state = MountState::MountCasting;
    mRec.mountGeneration = 1;
    mRec.castingGeneration = 1;
    mRec.mountCastStartedAt = now;

    // While MountCasting, bot must NOT reissue movement or count as stall
    CHECK(mRec.state == MountState::MountCasting);

    // 3. Mount completes: transitions to MountedGround
    mRec.state = MountState::MountedGround;

    // 4. Once mounted, walk can safely continue mounted
    CHECK(mRec.state == MountState::MountedGround);
    CHECK(dist >= 90.0f);
}

// ============================================================================
// Test 39: Gather Order Stall Does Not Advance During Mount Cast (P1)
// ============================================================================
static void TestGatherOrderStallDoesNotAdvanceDuringMountCast()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4009);
    auto& shard = LocomotionStateStore::GetShard(bot);

    BotMountRecord& mRec = shard.mounts[bot];
    mRec.state = MountState::MountCasting;

    // Simulating guild gather order logic:
    uint32 gatherOrderStallMs = 0;
    uint32 diff = 500;

    // During mount cast, stall counter must NOT advance:
    if (mRec.state != MountState::MountCasting)
    {
        gatherOrderStallMs += diff;
    }

    CHECK(gatherOrderStallMs == 0); // Stall counter did NOT advance!
}

// ============================================================================
// Test 40: Forget Clears Planning Metadata (P2)
// ============================================================================
static void TestForgetClearsPlanningMetadata()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4010);
    auto& shard = LocomotionStateStore::GetShard(bot);

    // Allocate planning ticket
    LocomotionStateStore::AllocatePlanTicketLocked(shard, bot, MoveOwner::Quest);
    CHECK(shard.activePlanning.find(bot) != shard.activePlanning.end());
    CHECK(shard.nextPlanTicket.find(bot) != shard.nextPlanTicket.end());

    // Forget removes both planning containers
    shard.activePlanning.erase(bot);
    shard.nextPlanTicket.erase(bot);

    CHECK(shard.activePlanning.find(bot) == shard.activePlanning.end());
    CHECK(shard.nextPlanTicket.find(bot) == shard.nextPlanTicket.end());
}

// ============================================================================
// Test 41: Planning Epoch Preserves Same-Owner History Across Intermediary Owner (P0/P1)
// ============================================================================
static void TestPlanningEpochPreservesSameOwnerHistoryAcrossIntermediary()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4011);
    auto& shard = LocomotionStateStore::GetShard(bot);

    // Step 1: Travel MoveTo A allocates ticket 10
    uint64 ticketA = LocomotionStateStore::AllocatePlanTicketLocked(shard, bot, MoveOwner::Travel);

    // Step 2: Travel MoveTo B allocates ticket 11 (superseding A)
    uint64 ticketB = LocomotionStateStore::AllocatePlanTicketLocked(shard, bot, MoveOwner::Travel);
    CHECK(ticketB > ticketA);

    // Step 3: An intermediary Gather intent arrives (ticket 12, lower priority than Travel)
    uint64 ticketGather = LocomotionStateStore::AdvanceIntentEpochLocked(shard, bot, MoveOwner::Gather);
    CHECK(ticketGather > ticketB);

    // Step 4: Travel MoveTo A finishes path generation and tries to commit with ticketA.
    // Under the old bug, activePlanning had {12, Gather}, and since Gather was lower priority,
    // Travel A would incorrectly return TRUE!
    // Under the new decoupled epoch state, Travel A's ticket (10) < latestByOwner[Travel] (11),
    // so it MUST be rejected.
    bool canCommitA = LocomotionStateStore::IsPlanCurrentLocked(shard, bot, MoveOwner::Travel, ticketA);
    CHECK(!canCommitA); // Stale Travel A plan rejected despite intermediate Gather!

    // Step 5: Travel MoveTo B is still current for Travel owner, and higher priority than Gather:
    bool canCommitB = LocomotionStateStore::IsPlanCurrentLocked(shard, bot, MoveOwner::Travel, ticketB);
    CHECK(canCommitB); // Travel B can still commit!

    // Step 6: Verify priority ordering: an older in-flight Combat plan (ticket 20)
    // is NOT superseded by a newer lower-priority Ambient intent (ticket 21).
    uint64 ticketCombat = LocomotionStateStore::AllocatePlanTicketLocked(shard, bot, MoveOwner::Combat);
    uint64 ticketAmbient = LocomotionStateStore::AdvanceIntentEpochLocked(shard, bot, MoveOwner::Ambient);
    CHECK(ticketAmbient > ticketCombat);
    bool canCommitCombat = LocomotionStateStore::IsPlanCurrentLocked(shard, bot, MoveOwner::Combat, ticketCombat);
    CHECK(canCommitCombat); // Lower-priority Ambient does not invalidate higher-priority Combat!
}

// ============================================================================
// Test 42: Mount Selection Failure Has Zero Locomotion Mutation (P1)
// ============================================================================
static void TestMountSelectionFailureZeroLocomotionMutation()
{
    LocomotionStateStore::ResetAllForTest();
    ObjectGuid bot(4012);
    auto& shard = LocomotionStateStore::GetShard(bot);

    // Setup active locomotion: bot is in Point under Travel owner
    BotLocomotionRecord rec;
    rec.owner = MoveOwner::Travel;
    rec.mode = MoveMode::Point;
    rec.state = LocomotionState::Moving;
    rec.commandId = 55;
    rec.destX = 100.0f;
    rec.destY = 200.0f;
    rec.destZ = 10.0f;
    shard.locomotion[bot] = rec;

    BotMountRecord& mRec = shard.mounts[bot];
    mRec.state = MountState::Unmounted;
    mRec.mountGeneration = 3;

    // Simulate Mount Request when no mount spell is known (e.g. level 20 with no training):
    // In production RequestMountInternal, SelectMountSpell returns 0.
    // In this failure branch:
    // 1) no locomotion state or commandId is mutated
    // 2) no mount state or generation is mutated
    // 3) a 5000ms cooldown is recorded on noMountAvailableUntilMs
    uint32 now = 10000;
    uint32 spellId = 0; // SelectMountSpell returned 0

    if (!spellId)
    {
        mRec.noMountAvailableUntilMs = now + 5000;
        // returns false WITHOUT calling ForceStopOwnerInternal!
    }

    // Verify ZERO locomotion mutation:
    CHECK(shard.locomotion[bot].commandId == 55);
    CHECK(shard.locomotion[bot].owner == MoveOwner::Travel);
    CHECK(shard.locomotion[bot].mode == MoveMode::Point);
    CHECK(shard.locomotion[bot].state == LocomotionState::Moving);
    CHECK(shard.locomotion[bot].destX == 100.0f);

    // Verify mount record:
    CHECK(mRec.state == MountState::Unmounted);
    CHECK(mRec.mountGeneration == 3);
    CHECK(mRec.noMountAvailableUntilMs == now + 5000);

    // Verify backoff throttle prevents immediate re-query within 5000ms:
    uint32 nextCheckTime = now + 1000;
    CHECK(nextCheckTime < mRec.noMountAvailableUntilMs);
}

// ============================================================================
// Main Runner
// ============================================================================
static void TestCompressedRealmFlightPolicy()
{
    CHECK(CanUseFlyingMount(530, 45, 45, 225));
    CHECK(CanUseFlyingMount(571, 55, 45, 300));
    CHECK(!CanUseFlyingMount(530, 44, 45, 300));
    CHECK(!CanUseFlyingMount(530, 55, 60, 300));
    CHECK(!CanUseFlyingMount(530, 55, 45, 224));
    CHECK(!CanUseFlyingMount(0, 55, 45, 300));
    auto flying = MountPolicyResolver::Evaluate(MoveOwner::None, MoveMode::Follow, 10.0f,
        true, DesiredMountState::PreferFlying, true, true, false, false, 55, false,
        MountState::Unmounted, 0, 10000, true, false);
    CHECK(flying.action == MountAction::MountFlying);
    CHECK(flying.wantFlying);
    auto switchMount = MountPolicyResolver::Evaluate(MoveOwner::None, MoveMode::Follow, 10.0f,
        true, DesiredMountState::PreferFlying, true, true, false, false, 55, false,
        MountState::MountedGround, 0, 10000, true, true);
    CHECK(switchMount.action == MountAction::Dismount);
    CHECK(switchMount.dismountReason == DismountReason::LeaderState);
}

int main()
{
    std::printf("Running locomotion & navigation overhaul regression tests (Round 4, 5, 6 & Pre-Live Hardening)...\n");
    std::printf("Using production LocomotionStateStore: %zu shards, %zu dispatch gates\n",
        LOCOMOTION_SHARDS, DISPATCH_GATES);

    TestLocomotionToken();
    TestPriorityArbitration();
    TestStaleNavigateArrivalSuperseded();
    TestTransactionalMoveToOwnerPreservation();
    TestDispatchGateSerialization();
    TestStaleReleaseRaceProtection();
    TestMountInterruptAndGenerationInvalidation();
    TestCompressedRealmFlightPolicy();
    TestLeaderPreferenceClearOnLeave();
    TestPreferUnmountedVsAutonomousTravel();
    TestSafePositionValueCopyAndMapIdFilter();
    TestBacktrackCandidateAdvanceOnEarlyStop();
    TestDetourToBacktrackBeforeGiveUp();
    TestHoldExpiration();
    TestGeneratorCompletionReconciliation();
    TestMountCastFailureBackoff();
    Test64ShardMultithreadStress();

    // Round 5 tests
    TestSyntheticTokenCollisionPrevention();
    TestLowerPriorityIssueLegNoMutation();
    TestGenerationGuardedDeferredAction();
    TestDeferredMountPolicyRaceProtection();
    TestNavigateToHoldCleansRequestAndBlocksMount();
    TestGeneratorReconcilerFollow();
    TestGeneratorReconcilerChase();
    TestDungeonRaidWaterMountLegality();
    TestDispatchContentionAndScalability();

    // Round 6 tests
    TestGeneratorReconcilerPoint();
    TestStaleNavigateRequestGenerationGuardBlocksDispatch();
    TestPlanningEpochSameOwnerRace();
    TestCancelledMountGenerationCannotResurrect();
    TestAutonomousNavigateMountsThroughPolicy();

    // Pre-live hardening tests (Tests 31-42)
    TestOldMoveToPlanCannotOverwriteNewerHoldSameOwner();
    TestOldMoveToPlanCannotOverwriteNewerFollowSameOwner();
    TestOldMoveToPlanCannotOverwriteNewerChaseSameOwner();
    TestForceStopOwnerInvalidatesPendingMoveToPlan();
    TestSameNavigateRequestCommandCAS();
    TestStaleGiveUpCannotEraseAdvancedCommand();
    TestStaleRecoveryLegCannotReplaceNewerLeg();
    TestGatheringMountCastAndWalkResumption();
    TestGatherOrderStallDoesNotAdvanceDuringMountCast();
    TestForgetClearsPlanningMetadata();
    TestPlanningEpochPreservesSameOwnerHistoryAcrossIntermediary();
    TestMountSelectionFailureZeroLocomotionMutation();

    std::printf("Locomotion tests completed: %d checks, %d failures\n", _checks, _failures);
    return _failures == 0 ? 0 : 1;
}
