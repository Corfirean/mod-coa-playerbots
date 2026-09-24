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

struct NavProgressRules
{
    float progressYards = 2.5f;       // ground distance closed that counts as progress
    float climbYards = 1.5f;          // height difference closed that counts as progress
    uint32 stallMs = 9000;            // no progress for this long is a stall
    float recoveredYards = 20.0f;     // this much closer than at the first stall: the ladder resets
    float recoveredClimbYards = 8.0f; // ...or this much less height to go
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
    float best2d = 0.0f;              // closest ground distance reached so far
    float bestDz = 0.0f;              // smallest height difference reached so far
    uint32 lastProgressAt = 0;
    uint8 stage = 0;                  // recovery stages used since the ladder last reset (0-4)
    float stall2d = 0.0f;             // distances when the ladder started climbing
    float stallDz = 0.0f;

    // A fresh goal.
    void Start(float dist2d, float distZ, uint32 now)
    {
        *this = NavProgress();
        best2d = dist2d;
        bestDz = distZ;
        lastProgressAt = now;
    }

    // Same goal, new baseline: the goal moved (a walking mob), or the bot was busy with something
    // else and is somewhere new. The ladder stage is kept -- being interrupted is not recovering --
    // but "materially closer" is measured from here on.
    void Rebase(float dist2d, float distZ, uint32 now)
    {
        best2d = dist2d;
        bestDz = distZ;
        lastProgressAt = now;
        if (stage)
        {
            stall2d = dist2d;
            stallDz = distZ;
        }
    }

    // Time that should not count toward a stall (the movement slot was held by someone else).
    void Hold(uint32 now)
    {
        lastProgressAt = now;
    }

    NavRecovery Update(float dist2d, float distZ, uint32 now, NavProgressRules const& rules)
    {
        bool closer = dist2d < best2d - rules.progressYards;
        bool climbed = distZ < bestDz - rules.climbYards;
        if (closer || climbed)
        {
            best2d = std::min(best2d, dist2d);
            bestDz = std::min(bestDz, distZ);
            lastProgressAt = now;
            if (stage && (stall2d - dist2d >= rules.recoveredYards || stallDz - distZ >= rules.recoveredClimbYards))
                stage = 0;
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
