/*
 * mod-coa-playerbots
 *
 * How BotMovement::Navigate judges "is this bot actually getting anywhere", and what it does when
 * it isn't. Pure (distances and time in, a decision out) so it is unit-tested standalone.
 *
 * Progress is measured on two axes, because WoW geometry is not flat. A bot climbing a staircase,
 * a ramp, a tower or a mine shaft toward a goal that sits nearly straight above or below it barely
 * changes its ground distance while doing exactly the right thing; judged on ground distance alone
 * it looked stuck after nine seconds and got repathed and detoured off the stairs. Closing either
 * the ground distance or the height difference by its threshold counts as progress.
 *
 * Route-aware extension: A legitimate navmesh corridor or switchback ramp around a ridge or wall
 * often increases or holds flat Euclidean distance to the final destination while steadily closing
 * distance to the active leg waypoint (legX, legY, legZ). UpdateRoute() credits progress toward the
 * active leg waypoint for up to maxDetourLegMs (unless the bot is oscillating across the same grid
 * cells), preventing false Repath/Detour triggers on valid switchbacks.
 *
 * The recovery ladder -- repath, detour one side, detour the other side, give up -- climbs one stage
 * per stall. Small accidental progress (a repath that moves the bot three yards before it jams
 * again) resets the stall timer but not the ladder: the stage only drops back to zero once the
 * goal is materially closer than it was when the first stall happened. Otherwise a bot could loop
 * on "repath" forever and never reach the detours or the final Stuck its caller escalates on.
 */

#ifndef COA_PLAYERBOTS_BOT_NAV_PROGRESS_H
#define COA_PLAYERBOTS_BOT_NAV_PROGRESS_H

#include "Define.h"
#include <algorithm>
#include <cmath>

struct NavProgressRules
{
    float progressYards = 2.5f;       // ground distance closed that counts as progress
    float climbYards = 1.5f;          // height difference closed that counts as progress
    uint32 stallMs = 9000;            // no progress for this long is a stall
    float recoveredYards = 20.0f;     // this much closer than at the first stall: the ladder resets
    float recoveredClimbYards = 8.0f; // ...or this much less height to go
    uint32 maxDetourLegMs = 27000;    // max duration leg progress can defer goal stall on a switchback
    float cellQuantumYards = 6.0f;    // grid cell size for repeated-area oscillation detection
};

enum class NavRecovery : uint8
{
    None,
    Repath,
    DetourLeft,
    DetourRight,
    GiveUp,
};

struct NavProgress
{
    float best2d = 0.0f;              // closest ground distance reached so far to final goal
    float bestDz = 0.0f;              // smallest height difference reached so far to final goal
    float bestLeg2d = 0.0f;           // closest ground distance reached so far to current leg/waypoint
    float bestLegDz = 0.0f;           // smallest height difference reached so far to current leg/waypoint
    uint32 lastProgressAt = 0;
    uint32 lastGoalProgressAt = 0;    // last time final-goal distance improved
    uint8 stage = 0;                  // recovery stages used since the ladder last reset (0-4)
    float stall2d = 0.0f;             // distances when the ladder started climbing
    float stallDz = 0.0f;
    uint32 legSwitchbacks = 0;        // count of switchback/corridor updates that deferred false stalls
    bool oscillatingArea = false;     // true when repeatedly revisiting recent cells without net goal progress
    int32 recentCellX[4] = {0, 0, 0, 0};
    int32 recentCellY[4] = {0, 0, 0, 0};
    uint8 cellCount = 0;
    uint8 cellHead = 0;
    uint8 cellRevisits = 0;

    // A fresh goal.
    void Start(float dist2d, float distZ, uint32 now)
    {
        *this = NavProgress();
        best2d = dist2d;
        bestDz = distZ;
        bestLeg2d = dist2d;
        bestLegDz = distZ;
        lastProgressAt = now;
        lastGoalProgressAt = now;
    }

    // Reset leg-specific progress baseline when a new corridor waypoint or detour leg is committed.
    void StartLeg(float legDist2d, float legDistZ, uint32 now)
    {
        bestLeg2d = legDist2d;
        bestLegDz = legDistZ;
        if (lastGoalProgressAt == 0)
            lastGoalProgressAt = now;
    }

    // Same goal, new baseline: the goal moved (a walking mob), or the bot was busy with something
    // else and is somewhere new. The ladder stage is kept -- being interrupted is not recovering --
    // but "materially closer" is measured from here on.
    void Rebase(float dist2d, float distZ, uint32 now)
    {
        best2d = dist2d;
        bestDz = distZ;
        bestLeg2d = dist2d;
        bestLegDz = distZ;
        lastProgressAt = now;
        lastGoalProgressAt = now;
        cellCount = 0;
        cellHead = 0;
        cellRevisits = 0;
        oscillatingArea = false;
        if (stage)
        {
            stall2d = dist2d;
            stallDz = distZ;
        }
    }

    // Time that should not count toward a stall (the movement slot was held by someone else, rooted, or casting).
    void Hold(uint32 now)
    {
        uint32 delta = (now > lastProgressAt) ? (now - lastProgressAt) : 0;
        lastProgressAt = now;
        if (lastGoalProgressAt > 0)
            lastGoalProgressAt += delta;
    }

    void RecordCell(float bx, float by, float quantum)
    {
        if (quantum <= 0.0f)
            return;
        int32 cx = static_cast<int32>(std::floor(bx / quantum));
        int32 cy = static_cast<int32>(std::floor(by / quantum));

        if (cellCount > 0)
        {
            uint8 lastIdx = (cellHead + 3u) % 4u;
            if (recentCellX[lastIdx] == cx && recentCellY[lastIdx] == cy)
                return;

            for (uint8 i = 0; i < cellCount; ++i)
            {
                uint8 idx = (cellHead + 4u - 1u - i) % 4u;
                if (recentCellX[idx] == cx && recentCellY[idx] == cy)
                {
                    if (cellRevisits < 255)
                        ++cellRevisits;
                    if (cellRevisits >= 3)
                        oscillatingArea = true;
                    break;
                }
            }
        }

        recentCellX[cellHead] = cx;
        recentCellY[cellHead] = cy;
        cellHead = (cellHead + 1u) % 4u;
        if (cellCount < 4u)
            ++cellCount;
    }

    NavRecovery UpdateRoute(float goalDist2d, float goalDistZ, float legDist2d, float legDistZ,
                            float bx, float by, uint32 now, NavProgressRules const& rules)
    {
        if (lastGoalProgressAt == 0)
            lastGoalProgressAt = lastProgressAt;

        bool closer = goalDist2d < best2d - rules.progressYards;
        bool climbed = goalDistZ < bestDz - rules.climbYards;
        if (closer || climbed)
        {
            best2d = std::min(best2d, goalDist2d);
            bestDz = std::min(bestDz, goalDistZ);
            bestLeg2d = std::min(bestLeg2d, legDist2d);
            bestLegDz = std::min(bestLegDz, legDistZ);
            lastProgressAt = now;
            lastGoalProgressAt = now;
            cellRevisits = 0;
            oscillatingArea = false;
            if (stage && (stall2d - goalDist2d >= rules.recoveredYards || stallDz - goalDistZ >= rules.recoveredClimbYards))
                stage = 0;
            return NavRecovery::None;
        }

        RecordCell(bx, by, rules.cellQuantumYards);

        // Route-aware leg progress: walking a switchback or around a ridge increases or holds flat
        // Euclidean distance to the final goal while steadily closing distance to the current leg waypoint.
        bool legCloser = legDist2d < bestLeg2d - rules.progressYards;
        bool legClimbed = legDistZ < bestLegDz - rules.climbYards;
        if ((legCloser || legClimbed) && !oscillatingArea && (now - lastGoalProgressAt <= rules.maxDetourLegMs))
        {
            bestLeg2d = std::min(bestLeg2d, legDist2d);
            bestLegDz = std::min(bestLegDz, legDistZ);
            lastProgressAt = now;
            ++legSwitchbacks;
            return NavRecovery::None;
        }

        if (now - lastProgressAt <= rules.stallMs)
            return NavRecovery::None;

        lastProgressAt = now;
        if (stage == 0)
        {
            stall2d = best2d;
            stallDz = bestDz;
        }
        stage = uint8(std::min<uint32>(stage + 1u, 4u));
        switch (stage)
        {
            case 1:  return NavRecovery::Repath;
            case 2:  return NavRecovery::DetourLeft;
            case 3:  return NavRecovery::DetourRight;
            default: return NavRecovery::GiveUp;
        }
    }

    NavRecovery Update(float dist2d, float distZ, uint32 now, NavProgressRules const& rules)
    {
        return UpdateRoute(dist2d, distZ, dist2d, distZ, 0.0f, 0.0f, now, rules);
    }
};

inline char const* NavRecoveryStageName(uint8 stage)
{
    switch (stage)
    {
        case 0:  return "none";
        case 1:  return "repathed";
        case 2:  return "detoured left";
        case 3:  return "detoured right";
        default: return "gave up";
    }
}

#endif // COA_PLAYERBOTS_BOT_NAV_PROGRESS_H
