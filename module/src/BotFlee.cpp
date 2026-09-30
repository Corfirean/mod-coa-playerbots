#include "BotFlee.h"
#include "engine/BotDebugLog.h"
#include "engine/CombatContext.h"
#include "engine/SpellPredicates.h"
#include "BotMovement.h"
#include "Player.h"
#include "Unit.h"
#include <cmath>
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
    constexpr float FLEE_SCAN_RADIUS = 20.0f;
    constexpr float FLEE_DISTANCE_YARDS = 40.0f;
    constexpr uint32 FLEE_MIN_DURATION_MS = 6000;
    constexpr uint32 FLEE_REPICK_INTERVAL_MS = 4000;
    constexpr int FLEE_ANGLE_BINS = 12; // 30 degrees each

    // Picks a retreat point away from the densest cluster of nearby hostiles: an angular
    // histogram around the bot (12 bins), each hostile voting into the bin matching its direction
    // from the bot with a weight inversely proportional to distance (closer threats count more),
    // then the bin with the least accumulated danger wins. Generalizes BotAvoidance.cpp's single
    // "away from the one hazard" vector to many threats at once instead of one, using the same
    // scan (SpellPredicates::GetNearbyEnemies) CombatContext already relies on for its own
    // nearby-hostile count, so this agrees with the rest of the engine about what "nearby" means.
    void PickFleeDestination(Player* bot, BotFleeState& state)
    {
        std::vector<Unit*> hostiles;
        BotAI::GetNearbyEnemies(bot, bot, FLEE_SCAN_RADIUS, hostiles);

        float danger[FLEE_ANGLE_BINS] = {};
        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();
        for (Unit* u : hostiles)
        {
            if (!u)
                continue;
            float dx = u->GetPositionX() - bx;
            float dy = u->GetPositionY() - by;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 0.1f)
                dist = 0.1f;
            float angle = std::atan2(dy, dx);
            int bin = int((angle + float(M_PI)) / (2.0f * float(M_PI)) * FLEE_ANGLE_BINS) % FLEE_ANGLE_BINS;
            if (bin < 0)
                bin += FLEE_ANGLE_BINS;
            danger[bin] += 1.0f / dist;
        }

        int bestBin = 0;
        for (int i = 1; i < FLEE_ANGLE_BINS; ++i)
        {
            if (danger[i] < danger[bestBin])
                bestBin = i;
        }

        float bestAngle = (float(bestBin) + 0.5f) / float(FLEE_ANGLE_BINS) * 2.0f * float(M_PI) - float(M_PI);
        state.fleeX = bx + std::cos(bestAngle) * FLEE_DISTANCE_YARDS;
        state.fleeY = by + std::sin(bestAngle) * FLEE_DISTANCE_YARDS;
        // Seed value only -- BotMovement::MoveTo re-grounds against the real terrain at
        // (fleeX, fleeY) before issuing the move, same as every other retreat point in this
        // module (see BotAvoidance.cpp's identical comment on its own escape points).
        state.fleeZ = bot->GetPositionZ();
    }
}

namespace BotFlee
{
    bool IsLethal(Player* bot, BotAI::CombatContext const& ctx, BotFleeState& state, uint32 nowMs)
    {
        if (!bot)
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
            BotMovement::Release(bot, MoveOwner::Avoidance);
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
