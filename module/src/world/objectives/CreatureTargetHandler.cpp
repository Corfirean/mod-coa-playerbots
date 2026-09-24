#include "CreatureTargetHandler.h"
#include "BotAI.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "ObjectiveCommon.h"
#include "Player.h"
#include "WorldExecutor.h"
#include "WorldReservations.h"
#include <algorithm>
#include <cmath>

using namespace WorldBrainInternal;

namespace
{
    // How close to the area the bot must be before it starts looking at mobs on the way in:
    // real players start pulling the first kobold they see, not the one at the camp's centre.
    constexpr float EARLY_SCAN_SLACK = 60.0f;

    // With corpses around, the mobs are coming back: wait near them this long before wandering.
    constexpr uint32 RESPAWN_WAIT_MS = 12000;

    // Grace for the combat engine to pick up a fresh Attack() before the brain second-guesses it.
    constexpr uint32 ENGAGE_GRACE_MS = 1500;

    // An engagement that never turned into a fight is retried this often before the target is
    // given up on.
    constexpr uint32 REENGAGE_LIMIT = 2;

    // Unreachable / refused targets per objective before the whole area is written off.
    constexpr uint32 TARGET_FAILURE_LIMIT = 5;
}

float CreatureTargetHandler::EngageRange(ObjectiveContext const& ctx) const
{
    return ctx.cfg.engageRange;
}

Creature* CreatureTargetHandler::CurrentTarget(ObjectiveContext const& ctx) const
{
    if (ctx.task.quest.targetGuid.IsEmpty() || ctx.task.quest.targetGuid.IsGameObject())
        return nullptr;
    return ObjectAccessor::GetCreature(*ctx.bot, ctx.task.quest.targetGuid);
}

Creature* CreatureTargetHandler::Scan(ObjectiveContext& ctx, uint32& corpses)
{
    std::unordered_set<uint32> wanted;
    std::unordered_map<uint32, uint32> served;
    ObjectiveCommon::WantedTargets(ctx, false, wanted, served);
    return ObjectiveCommon::FindCreature(ctx, wanted, served, HostileOnly(ctx), WantDead(ctx), corpses);
}

void CreatureTargetHandler::StartApproach(ObjectiveContext& ctx, Creature* target)
{
    ctx.task.quest.targetGuid = target->GetGUID();
    ctx.task.quest.progressMark = ObjectiveCommon::TaskProgress(ctx.bot, ctx.task);
    ctx.task.wandering = false;
    LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' TargetSelected '{}' ({}) for quest {} objective {} at {:.0f} yd.",
        ctx.bot->GetName(), target->GetName(), target->GetGUID().ToString(), ctx.task.quest.questId,
        uint32(ctx.task.quest.objectiveIndex), ctx.bot->GetDistance(target));
    SetPhase(ctx.bot, ctx.state, TaskPhase::Approach, "target reserved");
}

ObjectiveResult CreatureTargetHandler::AfterExecute(ObjectiveContext& ctx)
{
    Creature* target = CurrentTarget(ctx);
    if (target && !target->IsAlive() && target->hasLootRecipient() && !target->isTappedBy(ctx.bot))
    {
        // Somebody else's kill: no credit was ever possible, so it is not a failed attempt of the
        // bot's -- just a target gone.
        ObjectiveCommon::BeginSearch(ctx, "someone else got the kill");
        return ObjectiveResult::Running;
    }
    if (!target || !target->IsAlive())
    {
        // The kill happened (Verify reads the counter to see whether it paid).
        ctx.task.waitUntilMs = ctx.now + RollRange(ctx.state, 0x100d, ctx.cfg.reactionMinMs, ctx.cfg.reactionMaxMs);
        SetPhase(ctx.bot, ctx.state, TaskPhase::Loot, "target down");
        return ObjectiveResult::Running;
    }

    if (PhaseElapsed(ctx.state) < ENGAGE_GRACE_MS)
        return ObjectiveResult::Running;

    // Alive, and the bot is not fighting it: the fight never happened or ended without a kill.
    if (target->IsInEvadeMode() || !ctx.bot->IsValidAttackTarget(target))
        ObjectiveCommon::ForgetTarget(ctx, FailureReason::Evaded);
    else if (target->hasLootRecipient() && !target->isTappedBy(ctx.bot))
        ObjectiveCommon::ForgetTarget(ctx, FailureReason::TargetTaken);
    else if (ctx.task.quest.retryCount < REENGAGE_LIMIT)
    {
        ++ctx.task.quest.retryCount;
        SetPhase(ctx.bot, ctx.state, TaskPhase::Approach, "re-engaging");
        return ObjectiveResult::Running;
    }
    else
        ObjectiveCommon::ForgetTarget(ctx, FailureReason::Unreachable);

    ObjectiveCommon::BeginSearch(ctx, "fight ended without a kill");
    return ObjectiveResult::Running;
}

ObjectiveResult CreatureTargetHandler::Update(ObjectiveContext& ctx)
{
    WorldTask& task = ctx.task;

    switch (task.phase)
    {
        case TaskPhase::Planning:
            SetPhase(ctx.bot, ctx.state, TaskPhase::TravelToArea, "planned");
            return ObjectiveResult::Running;

        case TaskPhase::Recover:
            // Back from the dead. Twice killed in the same place means the place is too much.
            if (task.quest.deaths >= 2)
            {
                task.quest.deaths = 0;
                return ObjectiveCommon::FailArea(ctx, FailureReason::Died);
            }
            SetPhase(ctx.bot, ctx.state, TaskPhase::TravelToArea, "recovering");
            return ObjectiveResult::Running;

        case TaskPhase::TravelToArea:
        {
            float dist = std::hypot(ctx.bot->GetPositionX() - task.x, ctx.bot->GetPositionY() - task.y);
            if (ctx.bot->GetMapId() == task.mapId && dist <= task.areaRadius + EARLY_SCAN_SLACK && ctx.now >= task.nextScanMs)
            {
                task.nextScanMs = ctx.now + RollRange(ctx.state, 0x5ca9, ctx.cfg.scanMinMs, ctx.cfg.scanMaxMs);
                uint32 corpses = 0;
                if (Creature* target = Scan(ctx, corpses))
                {
                    WorldExecutor::Dismount(ctx.bot, ctx.state);
                    StartApproach(ctx, target);
                    return ObjectiveResult::Running;
                }
            }
            return ObjectiveCommon::TravelToArea(ctx);
        }

        case TaskPhase::Search:
        {
            if (!ObjectiveCommon::InArea(ctx, 45.0f))
            {
                SetPhase(ctx.bot, ctx.state, TaskPhase::TravelToArea, "drifted out of the area");
                return ObjectiveResult::Running;
            }

            if (ctx.now >= task.nextScanMs)
            {
                task.nextScanMs = ctx.now + RollRange(ctx.state, 0x5ca9, ctx.cfg.scanMinMs, ctx.cfg.scanMaxMs);
                uint32 corpses = 0;
                if (Creature* target = Scan(ctx, corpses))
                {
                    StartApproach(ctx, target);
                    return ObjectiveResult::Running;
                }
                task.corpsesSeen = corpses;
            }

            bool respawnComing = task.corpsesSeen > 0;
            if (PhaseElapsed(ctx.state) > ObjectiveCommon::SearchBudgetMs(ctx.state, ctx.cfg))
                return ObjectiveCommon::FailArea(ctx, FailureReason::NoTargets);

            // Fresh corpses mean the camp respawns soon: hang around instead of roaming off.
            if (!respawnComing || PhaseElapsed(ctx.state) > RESPAWN_WAIT_MS)
                ObjectiveCommon::Wander(ctx);
            return ObjectiveResult::Running;
        }

        case TaskPhase::Approach:
        {
            Creature* target = CurrentTarget(ctx);
            bool valid = target && target->IsAlive() != WantDead(ctx);
            if (valid && HostileOnly(ctx))
                valid = ctx.bot->IsValidAttackTarget(target) && !(target->hasLootRecipient() && !target->isTappedBy(ctx.bot));
            // The claim is re-confirmed every step of the approach: it may have lapsed while the
            // bot fought an add or rested, and someone else may hold it now.
            if (valid && !WorldReservations::TryReserve(ReservationKind::Creature, target->GetGUID().GetRawValue(),
                    ctx.bot->GetGUID(), ctx.cfg.targetReserveMs))
            {
                Count(ctx.state.metrics, &WorldMetrics::reservationConflicts);
                valid = false;
            }
            if (!valid)
            {
                ObjectiveCommon::BeginSearch(ctx, "target lost");
                return ObjectiveResult::Running;
            }

            if (PhaseElapsed(ctx.state) > ctx.cfg.approachTimeoutMs)
            {
                ObjectiveCommon::ForgetTarget(ctx, FailureReason::Timeout);
                if (task.quest.retryCount >= TARGET_FAILURE_LIMIT)
                    return ObjectiveCommon::FailArea(ctx, FailureReason::Unreachable);
                ObjectiveCommon::BeginSearch(ctx, "approach took too long");
                return ObjectiveResult::Running;
            }

            NavStatus status = ObjectiveCommon::Approach(ctx, target, EngageRange(ctx));
            if (status == NavStatus::Stuck)
            {
                Count(ctx.state.metrics, &WorldMetrics::movementStuck);
                ObjectiveCommon::ForgetTarget(ctx, FailureReason::Unreachable);
                if (task.quest.retryCount >= TARGET_FAILURE_LIMIT)
                    return ObjectiveCommon::FailArea(ctx, FailureReason::Unreachable);
                ObjectiveCommon::BeginSearch(ctx, "target unreachable");
                return ObjectiveResult::Running;
            }
            if (status != NavStatus::Arrived)
                return ObjectiveResult::Running;

            SetPhase(ctx.bot, ctx.state, TaskPhase::Execute, "in range");
            [[fallthrough]];
        }

        case TaskPhase::Execute:
        {
            Creature* target = CurrentTarget(ctx);
            if (!target || target->IsAlive() == WantDead(ctx))
            {
                ObjectiveCommon::BeginSearch(ctx, "target lost before acting");
                return ObjectiveResult::Running;
            }
            // Off the mount through the brain, so its remount cooldown knows (the combat engine
            // would dismount on engage anyway).
            WorldExecutor::Dismount(ctx.bot, ctx.state);
            return Execute(ctx, target);
        }

        case TaskPhase::Combat:
            return AfterExecute(ctx);

        case TaskPhase::Loot:
        {
            // The combat engine queued the corpse and TryProcessPendingLoot runs ahead of the
            // brain every tick, so by now looting is normally done. A corpse still holding loot
            // for the bot is handed back to that queue rather than walked away from.
            Creature* corpse = CurrentTarget(ctx);
            if (corpse && !corpse->IsAlive() && !corpse->loot.isLooted() && corpse->GetLootRecipient() == ctx.bot &&
                ctx.bot->GetDistance(corpse) < 30.0f && PhaseElapsed(ctx.state) < ctx.cfg.lootTimeoutMs)
            {
                BotAI::EnqueuePendingLoot(ctx.bot, corpse->GetGUID());
                return ObjectiveResult::Running;
            }
            if (ctx.now < task.waitUntilMs || ctx.bot->IsNonMeleeSpellCast(false))
                return ObjectiveResult::Running;
            SetPhase(ctx.bot, ctx.state, TaskPhase::Verify, "looted");
            [[fallthrough]];
        }

        case TaskPhase::Verify:
        {
            ObjectiveResult result = ObjectiveCommon::VerifyAttempt(ctx, ObjectiveCommon::DryAttemptLimit(ctx.def));
            if (result == ObjectiveResult::Failed && OnDryLimit(ctx))
                result = ObjectiveResult::Running;
            if (result == ObjectiveResult::Running)
                ObjectiveCommon::BeginSearch(ctx, "next target");
            return result;
        }

        default:
            return ObjectiveResult::Failed;
    }
}
