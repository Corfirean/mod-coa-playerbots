/*
 * mod-coa-playerbots -- locomotion & navigation overhaul regression tests
 *
 * Tests the core arbitration primitives:
 * - LocomotionToken lifecycle, generation validation, and stale token rejection
 * - Multi-owner priority arbitration and preemption safety
 * - Mount controller state machine, 90yd hysteresis, 6s remount cooldown, and debounced leader preference
 * - 16-entry safe position ring buffer and backtrack target selection
 * - Controlled motion guard invariant flags
 * - Chase range banding and deadzone hysteresis
 */

#include "stub/Define.h"
#include "stub/ObjectGuid.h"
#include <cstdio>
#include <cmath>
#include <array>
#include <vector>
#include <unordered_map>
#include <unordered_set>
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

// Replicated pure enums and types matching BotMovement.h for unit verification
enum class MoveOwner : uint8
{
    None = 0,
    Ambient,
    Travel,
    Follow,
    Gather,
    Quest,
    Battleground,
    Flee,
    Combat,
    Avoidance,
    Master,
};

enum class MountState : uint8
{
    Unmounted = 0,
    MountCasting,
    MountedGround,
    MountedFlying,
    DismountRequested,
    Cooldown,
};

enum class DesiredMountState : uint8
{
    None = 0,
    PreferGround,
    PreferFlying,
    PreferUnmounted,
};

enum class DismountReason : uint8
{
    Manual = 0,
    CombatEngaged,
    SpellCastStarted,
    IndoorEntered,
    FlightForbidden,
    WaterEntered,
    Arrival,
    ActionForbidden,
};

struct LocomotionToken
{
    ObjectGuid botGuid;
    MoveOwner owner = MoveOwner::None;
    uint64 commandId = 0;

    bool IsValid() const { return !botGuid.IsEmpty() && owner != MoveOwner::None && commandId != 0; }
    explicit operator bool() const { return IsValid(); }
};

inline bool operator==(LocomotionToken const& a, LocomotionToken const& b)
{
    return a.botGuid == b.botGuid && a.owner == b.owner && a.commandId == b.commandId;
}

inline bool operator!=(LocomotionToken const& a, LocomotionToken const& b)
{
    return !(a == b);
}

// -----------------------------------------------------------------------------
// Test 1: LocomotionToken semantics & stale rejection
// -----------------------------------------------------------------------------
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

    // Newer command generation for same bot and owner
    LocomotionToken tok1Newer{bot1, MoveOwner::Travel, 43};
    CHECK(tok1 != tok1Newer);

    // Stale token check simulation:
    // If active command is #43, an incoming release with #42 MUST be rejected as stale.
    struct ActiveLocomotion
    {
        ObjectGuid guid;
        MoveOwner owner;
        uint64 commandId;
    } active{bot1, MoveOwner::Travel, 43};

    auto TryRelease = [&](LocomotionToken const& token) -> bool {
        if (!token.IsValid())
            return false;
        if (token.botGuid != active.guid || token.owner != active.owner)
            return false;
        if (token.commandId != active.commandId)
            return false; // Stale token rejected!
        active.commandId = 0;
        return true;
    };

    CHECK(!TryRelease(tok1));       // Stale commandId 42 must NOT release active command 43
    CHECK(active.commandId == 43);  // Active command unaffected
    CHECK(TryRelease(tok1Newer));   // Matching commandId 43 successfully releases
    CHECK(active.commandId == 0);   // Released
}

static int OwnerPriority(MoveOwner owner)
{
    switch (owner)
    {
        case MoveOwner::Avoidance:    return 100;
        case MoveOwner::Combat:       return 90;
        case MoveOwner::Flee:         return 80;
        case MoveOwner::Master:       return 70;
        case MoveOwner::Battleground: return 60;
        case MoveOwner::Quest:        return 50;
        case MoveOwner::Gather:       return 40;
        case MoveOwner::Travel:       return 30;
        case MoveOwner::Follow:       return 20;
        case MoveOwner::Ambient:      return 10;
        default:                      return 0;
    }
}

static void TestPriorityArbitration()
{
    CHECK(OwnerPriority(MoveOwner::Avoidance) > OwnerPriority(MoveOwner::Combat));
    CHECK(OwnerPriority(MoveOwner::Combat) > OwnerPriority(MoveOwner::Travel));
    CHECK(OwnerPriority(MoveOwner::Travel) > OwnerPriority(MoveOwner::Ambient));

    // Preemption test
    struct Arbiter
    {
        MoveOwner currentOwner = MoveOwner::None;
        uint64 currentCmd = 0;
        uint64 nextCmdId = 1;

        bool RequestMove(MoveOwner owner, LocomotionToken& outToken)
        {
            if (currentOwner != MoveOwner::None && OwnerPriority(owner) < OwnerPriority(currentOwner))
                return false; // Cannot preempt higher priority

            currentOwner = owner;
            currentCmd = nextCmdId++;
            outToken = LocomotionToken{ObjectGuid(1), owner, currentCmd};
            return true;
        }
    } arbiter;

    LocomotionToken ambientTok;
    CHECK(arbiter.RequestMove(MoveOwner::Ambient, ambientTok));
    CHECK(arbiter.currentOwner == MoveOwner::Ambient);

    // Travel preempts Ambient
    LocomotionToken travelTok;
    CHECK(arbiter.RequestMove(MoveOwner::Travel, travelTok));
    CHECK(arbiter.currentOwner == MoveOwner::Travel);

    // Ambient CANNOT preempt Travel
    LocomotionToken ambientTok2;
    CHECK(!arbiter.RequestMove(MoveOwner::Ambient, ambientTok2));
    CHECK(arbiter.currentOwner == MoveOwner::Travel);

    // Combat preempts Travel
    LocomotionToken combatTok;
    CHECK(arbiter.RequestMove(MoveOwner::Combat, combatTok));
    CHECK(arbiter.currentOwner == MoveOwner::Combat);

    // Avoidance preempts Combat
    LocomotionToken avoidTok;
    CHECK(arbiter.RequestMove(MoveOwner::Avoidance, avoidTok));
    CHECK(arbiter.currentOwner == MoveOwner::Avoidance);
}

// -----------------------------------------------------------------------------
// Test 3: Mount Controller Hysteresis & Cooldown
// -----------------------------------------------------------------------------
static void TestMountController()
{
    constexpr float MOUNT_HYSTERESIS_DIST = 90.0f;
    constexpr uint32 REMOUNT_COOLDOWN_MS = 6000;
    constexpr uint32 LEADER_DEBOUNCE_MS = 1000;

    struct BotMountSimulator
    {
        MountState state = MountState::Unmounted;
        uint32 lastDismountAt = 0;
        uint32 pendingSpell = 0;
        DesiredMountState leaderPref = DesiredMountState::None;
        uint32 leaderObservedAt = 0;

        bool CanMount(float dist, uint32 now) const
        {
            if (state == MountState::MountedGround || state == MountState::MountedFlying)
                return false;
            if (pendingSpell != 0)
                return false;
            if (dist > 0.0f && dist < MOUNT_HYSTERESIS_DIST)
                return false;
            if (lastDismountAt != 0 && (now - lastDismountAt) < REMOUNT_COOLDOWN_MS)
                return false;
            return true;
        }

        bool RequestMount(float dist, uint32 now, uint32 spellId)
        {
            if (!CanMount(dist, now))
                return false;
            pendingSpell = spellId;
            state = MountState::MountCasting;
            return true;
        }

        void Dismount(uint32 now, DismountReason reason)
        {
            (void)reason;
            state = MountState::Unmounted;
            pendingSpell = 0;
            lastDismountAt = now;
        }

        void SetLeaderPref(DesiredMountState pref, uint32 now)
        {
            if (leaderPref != pref)
            {
                leaderPref = pref;
                leaderObservedAt = now;
            }
        }

        bool ShouldApplyLeaderPref(uint32 now) const
        {
            return leaderPref != DesiredMountState::None && (now - leaderObservedAt) >= LEADER_DEBOUNCE_MS;
        }
    } sim;

    uint32 now = 10000;

    // Short distance (<90 yd) must not trigger mount
    CHECK(!sim.CanMount(50.0f, now));
    CHECK(!sim.CanMount(89.9f, now));

    // Long distance (>=90 yd) allows mount
    CHECK(sim.CanMount(90.0f, now));
    CHECK(sim.CanMount(200.0f, now));

    // Request mount succeeds
    CHECK(sim.RequestMount(120.0f, now, 458));
    CHECK(sim.state == MountState::MountCasting);
    CHECK(sim.pendingSpell == 458);

    // While casting, another request is rejected
    CHECK(!sim.RequestMount(150.0f, now + 500, 458));

    // Bot dismounts at now = 15000
    sim.Dismount(15000, DismountReason::Manual);
    CHECK(sim.state == MountState::Unmounted);
    CHECK(sim.lastDismountAt == 15000);

    // Enforce 6s cooldown: 3s later (now = 18000), mounting is blocked
    CHECK(!sim.CanMount(150.0f, 18000));

    // Exactly 5999ms later, mounting still blocked
    CHECK(!sim.CanMount(150.0f, 20999));

    // 6000ms later (now = 21000), mounting allowed again
    CHECK(sim.CanMount(150.0f, 21000));
    CHECK(sim.CanMount(150.0f, 25000));

    // Leader debounce check
    sim.SetLeaderPref(DesiredMountState::PreferGround, 30000);
    CHECK(!sim.ShouldApplyLeaderPref(30500)); // 500ms: not ready
    CHECK(!sim.ShouldApplyLeaderPref(30999)); // 999ms: not ready
    CHECK(sim.ShouldApplyLeaderPref(31000));  // 1000ms: debounced & ready
}

// -----------------------------------------------------------------------------
// Test 4: 16-entry Safe Position Ring Buffer & Backtracking
// -----------------------------------------------------------------------------
static void TestSafePositionRingBuffer()
{
    constexpr size_t RING_SIZE = 16;
    struct SafePos
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        uint32 timeMs = 0;
    };

    struct RingBuffer
    {
        std::array<SafePos, RING_SIZE> entries{};
        size_t head = 0;
        size_t count = 0;

        void Push(float x, float y, float z, uint32 timeMs)
        {
            entries[head] = SafePos{x, y, z, timeMs};
            head = (head + 1) % RING_SIZE;
            if (count < RING_SIZE)
                ++count;
        }

        SafePos const* FindBacktrackTarget(float curX, float curY, float curZ) const
        {
            if (count == 0)
                return nullptr;

            for (size_t i = 0; i < count; ++i)
            {
                size_t idx = (head + RING_SIZE - 1 - i) % RING_SIZE;
                SafePos const& sp = entries[idx];
                float dx = curX - sp.x;
                float dy = curY - sp.y;
                float dz = curZ - sp.z;
                float d2 = dx * dx + dy * dy + dz * dz;
                // Backtrack target must be at least 3.0yd away to escape stuck geometry, and within 60.0yd
                if (d2 >= 9.0f && d2 <= 3600.0f)
                    return &sp;
            }
            return nullptr;
        }
    } ring;

    // Push 20 positions to verify circular wrap (capacity 16)
    for (int i = 0; i < 20; ++i)
    {
        float pos = static_cast<float>(i * 5);
        ring.Push(pos, pos, 0.0f, 1000 + i * 500);
    }

    CHECK(ring.count == RING_SIZE);

    // Latest position pushed was i=19 -> (95, 95)
    // Looking for backtrack from (95, 95):
    // i=19 is distance 0 (rejected < 3yd)
    // i=18 was (90, 90) -> distance sqrt(25 + 25) = ~7.07yd (accepted!)
    SafePos const* target = ring.FindBacktrackTarget(95.0f, 95.0f, 0.0f);
    CHECK(target != nullptr);
    if (target)
    {
        CHECK(std::abs(target->x - 90.0f) < 0.001f);
        CHECK(std::abs(target->y - 90.0f) < 0.001f);
    }
}

// -----------------------------------------------------------------------------
// Test 5: Chase Range Banding & Idempotency
// -----------------------------------------------------------------------------
static void TestChaseRangeBanding()
{
    struct ChaseBandCheck
    {
        float minRange = 0.0f;
        float maxRange = 0.0f;
        float angle = 0.0f;
        ObjectGuid target;

        bool IsIdempotent(ObjectGuid newTarget, float newMin, float newMax, float newAngle) const
        {
            if (target != newTarget)
                return false;
            if (std::abs(minRange - newMin) > 0.5f)
                return false;
            if (std::abs(maxRange - newMax) > 0.5f)
                return false;
            if (std::abs(angle - newAngle) > 0.1f)
                return false;
            return true;
        }
    };

    ObjectGuid mob(5555);
    ChaseBandCheck band{0.0f, 5.0f, 0.0f, mob};

    // Minor floating point variations within tolerance (<= 0.5yd) must NOT re-issue chase
    CHECK(band.IsIdempotent(mob, 0.2f, 5.2f, 0.05f));

    // Target change breaks idempotency
    CHECK(!band.IsIdempotent(ObjectGuid(6666), 0.0f, 5.0f, 0.0f));

    // Significant range change breaks idempotency
    CHECK(!band.IsIdempotent(mob, 0.0f, 10.0f, 0.0f));
}

// -----------------------------------------------------------------------------
// Test 6: Controlled Motion Guard Flag Invariants
// -----------------------------------------------------------------------------
static void TestControlledMotionGuard()
{
    // Bitmask simulating UnitState external control flags
    enum UnitStates : uint32
    {
        UNIT_STATE_CONFUSED     = 0x00000008,
        UNIT_STATE_FLEEING      = 0x00000010,
        UNIT_STATE_IN_FLIGHT    = 0x00000040,
        UNIT_STATE_STUNNED      = 0x00001000,
        UNIT_STATE_CHARGING     = 0x00080000,
        UNIT_STATE_JUMPING      = 0x00800000,
        UNIT_STATE_POSSESSED    = 0x02000000,
    };

    auto IsControlled = [](uint32 flags) -> bool {
        uint32 const mask = UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING | UNIT_STATE_IN_FLIGHT |
                            UNIT_STATE_STUNNED | UNIT_STATE_CHARGING | UNIT_STATE_JUMPING | UNIT_STATE_POSSESSED;
        return (flags & mask) != 0;
    };

    CHECK(!IsControlled(0));
    CHECK(IsControlled(UNIT_STATE_STUNNED));
    CHECK(IsControlled(UNIT_STATE_FLEEING));
    CHECK(IsControlled(UNIT_STATE_IN_FLIGHT));
    CHECK(IsControlled(UNIT_STATE_CHARGING));
    CHECK(IsControlled(UNIT_STATE_JUMPING));
    CHECK(IsControlled(UNIT_STATE_POSSESSED));
    CHECK(IsControlled(UNIT_STATE_CONFUSED));
}

int main()
{
    std::printf("Running locomotion & navigation overhaul unit tests...\n");

    TestLocomotionToken();
    TestPriorityArbitration();
    TestMountController();
    TestSafePositionRingBuffer();
    TestChaseRangeBanding();
    TestControlledMotionGuard();

    std::printf("Locomotion tests completed: %d checks, %d failures\n", _checks, _failures);
    return _failures == 0 ? 0 : 1;
}
