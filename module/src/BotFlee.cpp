#include "BotFlee.h"
#include "engine/BotDebugLog.h"
#include "engine/CombatContext.h"
#include "engine/SpellPredicates.h"
#include "BotMovement.h"
#include "DangerMap.h"
#include "training/TrainingArenaManager.h"
#include "Map.h"
#include "Player.h"
#include "Unit.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
    // Lethal trigger tuning. Stricter than CombatContext::worthDefensiveCooldown on purpose (see
    // BotFlee.h's header comment) -- this only fires once things are already dire.
    constexpr float LETHAL_HP_PCT = 30.0f;
    constexpr uint32 LETHAL_TTL_MS = 4000;
    constexpr uint32 LETHAL_CONFIRM_MS = 800;

    // Stalemate trigger tuning. 25s is well past the base StuckDetector's 9s window on purpose:
    // that detector resets on any attempted new cast/target even if it doesn't land, so it can
    // fire repeatedly through a fight that is genuinely progressing very slowly. This only cares
    // about actual target HP progress, so it can afford to wait longer before concluding the
    // fight is unwinnable.
    constexpr uint32 STALEMATE_MS = 25000;
    constexpr float STALEMATE_HP_EPSILON = 2.0f;
    constexpr float STALEMATE_SELF_SAFE_HP_PCT = 50.0f;

    // Retreat movement tuning.
    constexpr float FLEE_SCAN_RADIUS = 50.0f;
    constexpr float FLEE_DISTANCE_YARDS = 28.0f;
    constexpr uint32 FLEE_MIN_DURATION_MS = 6000;
    constexpr uint32 FLEE_REPICK_INTERVAL_MS = 4000;
    constexpr int FLEE_ANGLE_BINS = 12; // 30 degrees each

    // Evaluates 12 candidate retreat endpoints (plus midpoints) against all nearby hostiles within
    // 50 yards, static DangerMap cells, liquid/cliff height penalties, and reachability, picking the
    // safest reachable direction instead of running blindly between two mobs into a deeper pack.
    void PickFleeDestination(Player* bot, BotFleeState& state)
    {
        std::vector<Unit*> hostiles;
        BotAI::GetNearbyEnemies(bot, bot, FLEE_SCAN_RADIUS, hostiles);

        float const bx = bot->GetPositionX();
        float const by = bot->GetPositionY();
        float const bz = bot->GetPositionZ();
        Map* map = bot->GetMap();
        uint32 const mapId = bot->GetMapId();

        float bestScore = std::numeric_limits<float>::max();
        float bestX = bx;
        float bestY = by;
        float bestZ = bz;

        for (int i = 0; i < FLEE_ANGLE_BINS; ++i)
        {
            float const angle = (float(i) + 0.5f) / float(FLEE_ANGLE_BINS) * 2.0f * float(M_PI) - float(M_PI);
            float const cosA = std::cos(angle);
            float const sinA = std::sin(angle);
            float const cx = bx + cosA * FLEE_DISTANCE_YARDS;
            float const cy = by + sinA * FLEE_DISTANCE_YARDS;
            float const mx = bx + cosA * (FLEE_DISTANCE_YARDS * 0.5f);
            float const my = by + sinA * (FLEE_DISTANCE_YARDS * 0.5f);

            float cz = bz;
            float heightPenalty = 0.0f;
            if (map)
            {
                float const groundZ = map->GetHeight(bot->GetPhaseMask(), cx, cy, bz + 4.0f, true);
                if (groundZ > INVALID_HEIGHT)
                {
                    cz = groundZ;
                    if (std::fabs(cz - bz) > 8.0f)
                        heightPenalty += 5.0f;
                }
                else
                {
                    heightPenalty += 10.0f;
                }
                if (map->IsInWater(bot->GetPhaseMask(), cx, cy, cz, bot->GetCollisionHeight()))
                    heightPenalty += 8.0f;
            }

            uint8 const team = bot->GetTeamId() == TEAM_ALLIANCE ? 0 : 1;
            float score = heightPenalty + DangerMap::Risk(mapId, cx, cy, bot->GetLevel(), team) * 0.25f;
            for (Unit* u : hostiles)
            {
                if (!u || !u->IsAlive())
                    continue;
                float const ux = u->GetPositionX();
                float const uy = u->GetPositionY();
                float const distEnd = std::max(2.0f, std::hypot(cx - ux, cy - uy));
                float const distMid = std::max(2.0f, std::hypot(mx - ux, my - uy));
                score += (1.0f / distEnd) + (0.5f / distMid);
            }

            if (score < bestScore)
            {
                bestScore = score;
                bestX = cx;
                bestY = cy;
                bestZ = cz;
            }
        }

        state.fleeX = bestX;
        state.fleeY = bestY;
        state.fleeZ = bestZ;
    }
}

namespace BotFlee
{
    // Running away is an open-world response. In a dungeon, a raid, a battleground or an arena the group fights it out: a tank or a healer
    // that walked off mid-pull would drop the group's aggro and healing, and a boss that does not lose health for a while is a phase, not a
    // lost fight.
    bool FleeAllowedHere(Player const* bot)
    {
        if (!bot)
            return false;
        if (bot->InBattleground())
            return false;
        Map const* map = bot->GetMap();
        return !map || (!map->IsDungeon() && !map->IsRaid());
    }

    bool IsLethal(Player* bot, BotAI::CombatContext const& ctx, BotFleeState& state, uint32 nowMs)
    {
        if (!bot || !FleeAllowedHere(bot))
        {
            state.lethalFirstSeenMs = 0;
            return false;
        }

        bool lethalNow = ctx.botHpPct <= LETHAL_HP_PCT && ctx.botIncomingDps > 1.0f &&
            (float(bot->GetHealth()) / ctx.botIncomingDps) * 1000.0f <= float(LETHAL_TTL_MS);

        if (!lethalNow)
        {
            state.lethalFirstSeenMs = 0;
            return false;
        }

        if (state.lethalFirstSeenMs == 0)
        {
            state.lethalFirstSeenMs = nowMs;
            return false;
        }

        return nowMs - state.lethalFirstSeenMs >= LETHAL_CONFIRM_MS;
    }

    bool IsStalemate(Unit* target, BotAI::CombatContext const& ctx, BotFleeState& state, uint32 nowMs)
    {
        // A controlled training boss is deliberately immortal. Unchanged target HP
        // cannot be used as evidence that this timed encounter should be abandoned.
        if (ctx.bot && CoaTraining::Owns(ctx.bot->GetGUID()))
            return false;
        if (!FleeAllowedHere(ctx.bot))
        {
            state.stalemateTargetGuid.Clear();
            return false;
        }
        if (!target)
        {
            state.stalemateTargetGuid.Clear();
            return false;
        }

        ObjectGuid targetGuid = target->GetGUID();
        float targetHpPct = target->GetHealthPct();

        if (state.stalemateTargetGuid != targetGuid)
        {
            state.stalemateTargetGuid = targetGuid;
            state.stalemateLowestHpPct = targetHpPct;
            state.stalemateSinceMs = nowMs;
            return false;
        }

        if (targetHpPct <= state.stalemateLowestHpPct - STALEMATE_HP_EPSILON)
        {
            state.stalemateLowestHpPct = targetHpPct;
            state.stalemateSinceMs = nowMs;
            return false;
        }

        return ctx.botHpPct >= STALEMATE_SELF_SAFE_HP_PCT && nowMs - state.stalemateSinceMs >= STALEMATE_MS;
    }

    FleeReason Evaluate(Player* bot, Unit* target, BotAI::CombatContext const& ctx, BotFleeState& state, uint32 nowMs)
    {
        // Lethal checked first: a bot about to die takes priority over a merely-unwinnable fight.
        if (IsLethal(bot, ctx, state, nowMs))
            return FleeReason::Lethal;
        if (IsStalemate(target, ctx, state, nowMs))
            return FleeReason::Stalemate;
        return FleeReason::None;
    }

    bool Drive(Player* bot, BotFleeState& state, FleeReason reason, uint32 nowMs)
    {
        if (!bot)
            return false;

        // A retreat that began outside and carried the bot into an instance ends there.
        if (!FleeAllowedHere(bot))
        {
            state.fleeing = false;
            state.reason = FleeReason::None;
            return false;
        }

        if (!state.fleeing)
        {
            state.fleeing = true;
            state.reason = reason;
            state.fleeCommitUntilMs = nowMs + FLEE_MIN_DURATION_MS;
            state.nextDestinationPickMs = 0; // force an immediate pick below
            LOG_ERROR(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
                "FLEE: bot '{}' retreating from encounter (reason={}).",
                bot->GetName(), reason == FleeReason::Lethal ? "lethal" : "stalemate");
        }

        if (nowMs >= state.fleeCommitUntilMs && !bot->IsInCombat())
        {
            // Administrative release: bot disengaged safely, clear avoidance owner claim
            BotMovement::ForceReleaseOwner(bot, MoveOwner::Avoidance);
            LOG_DEBUG(BotAI::BotDebugLog::LoggerName(bot->GetGUID()),
                "FLEE: bot '{}' disengaged safely, resuming normal behavior.", bot->GetName());
            state.fleeing = false;
            state.reason = FleeReason::None;
            // Confirmed live: without this, re-engaging the same target right after a short local
            // retreat (exactly what a bot's own grind/quest logic tends to do -- the retreat
            // distance doesn't stop it walking straight back to the nearest hostile) inherited a
            // stalemateSinceMs timestamp that was already >25s old from before the retreat, so
            // IsStalemate fired again almost immediately instead of giving the new engagement a
            // fresh window -- a tight retreat/re-engage loop every ~6-16s that cost two fleet bots
            // most of their leveling time in one afternoon. Clearing the tracking here means
            // re-fighting the same target after escaping it starts a genuinely fresh clock.
            state.stalemateTargetGuid.Clear();
            state.stalemateLowestHpPct = 100.0f;
            state.stalemateSinceMs = 0;
            state.lethalFirstSeenMs = 0;
            return false;
        }

        if (nowMs >= state.nextDestinationPickMs)
        {
            PickFleeDestination(bot, state);
            state.nextDestinationPickMs = nowMs + FLEE_REPICK_INTERVAL_MS;
        }

        BotMovement::MoveTo(bot, MoveOwner::Avoidance, state.fleeX, state.fleeY, state.fleeZ);
        return true;
    }
}
