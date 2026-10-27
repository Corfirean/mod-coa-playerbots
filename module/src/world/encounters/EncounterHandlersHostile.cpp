#include "EncounterHandlers.h"
#include "BotMovement.h"
#include "EncounterConfig.h"
#include "EncounterEligibility.h"
#include "EncounterNarrator.h"
#include "EncounterPersonality.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include <cmath>

namespace
{
    constexpr float PI_VAL = 3.14159265f;

    float Dist2D(WorldObject const* a, WorldObject const* b)
    {
        return std::hypot(a->GetPositionX() - b->GetPositionX(), a->GetPositionY() - b->GetPositionY());
    }

    float Dist2DPoint(WorldObject const* a, float x, float y)
    {
        return std::hypot(a->GetPositionX() - x, a->GetPositionY() - y);
    }

    void ComputeVectorOffsetPoint(Player const* bot, Player const* target, float angleOffsetRad, float distanceYards, float& outX, float& outY, float& outZ)
    {
        float dx = bot->GetPositionX() - target->GetPositionX();
        float dy = bot->GetPositionY() - target->GetPositionY();
        float baseAngle = (std::abs(dx) > 0.1f || std::abs(dy) > 0.1f)
            ? std::atan2(dy, dx)
            : (target->GetOrientation() + PI_VAL);
        float angle = baseAngle + angleOffsetRad;
        outX = target->GetPositionX() + std::cos(angle) * distanceYards;
        outY = target->GetPositionY() + std::sin(angle) * distanceYards;
        outZ = target->GetPositionZ();
        if (target->GetMap())
        {
            float groundZ = target->GetMap()->GetHeight(target->GetPhaseMask(), outX, outY, outZ + 10.0f);
            if (groundZ > INVALID_HEIGHT)
                outZ = groundZ;
        }
    }

    Player* FindNearestLivingEnemyParticipant(EncounterInstance& inst, Player* bot)
    {
        Player* best = nullptr;
        float bestDist = 99999.0f;
        for (auto const& p : inst.participants)
        {
            if (p.botGuid == bot->GetGUID() || p.retreated || p.teamId == bot->GetTeamId())
                continue;
            Player* enemy = ObjectAccessor::FindConnectedPlayer(p.botGuid);
            if (!enemy || !enemy->IsInWorld() || !enemy->IsAlive() || enemy->GetMapId() != bot->GetMapId())
                continue;
            if (!bot->IsValidAttackTarget(enemy))
                continue;
            float d = Dist2D(bot, enemy);
            if (d < bestDist)
            {
                bestDist = d;
                best = enemy;
            }
        }
        return best;
    }

    bool TryCowardlyRetreat(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        if (part.retreated)
            return true;
        if (!EncounterPersonality::ShouldRetreatFromPvP(part.cautiousness, bot->GetHealthPct(), 1))
            return false;

        part.retreated = true;
        bot->AttackStop();
        uint64 goalId = (inst.id << 8) | 99u;
        BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, part.originX, part.originY, part.originZ, 8.0f, &part.moveToken, false);
        return true;
    }
}

namespace EncounterHandlers
{
    // =========================================================================
    // 7.1 Assassin Contract (Humanized Stalking State Machine:
    //     0=Observe -> 1=MoveFlank -> 2=WaitDwell -> 3=Reposition -> 4=PreStrikeObserve -> 5=ApproachAttack)
    // =========================================================================
    void UpdateAssassinContract(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        Player* target = ObjectAccessor::FindConnectedPlayer(inst.targetPlayerGuid);
        if (!target || !target->IsInWorld())
        {
            inst.state.Terminate(EncounterStopReason::PlayerLogout, nowMs);
            return;
        }
        if (!target->IsAlive())
        {
            inst.state.Terminate(EncounterStopReason::ParticipantDeath, nowMs);
            return;
        }
        if (target->GetMapId() != bot->GetMapId())
        {
            inst.state.Terminate(EncounterStopReason::MapChanged, nowMs);
            return;
        }
        if (target->IsInFlight())
        {
            inst.state.Terminate(EncounterStopReason::PlayerInFlight, nowMs);
            return;
        }

        float dist = Dist2D(bot, target);

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            // Abort stalking if assassin is pulled into combat by a third-party mob or player
            if (bot->IsInCombat() && bot->GetVictim() != target)
            {
                inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
                return;
            }

            if (!bot->IsValidAttackTarget(target))
            {
                inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
                return;
            }

            if (dist <= 48.0f && part.stealthCapable)
                TryApplyStealth(bot, part);

            if (dist <= 44.0f)
            {
                uint32 delta = (part.lastActionMs > 0 && nowMs > part.lastActionMs) ? (nowMs - part.lastActionMs) : 500u;
                inst.stalkAccumulatedMs += std::min<uint32>(delta, 2000u);
            }
            part.lastActionMs = nowMs;

            // Contextual atmospheric message before attack once stalking in close range and in LOS
            if (!inst.introNarrated && dist <= 38.0f && inst.stalkAccumulatedMs >= 3200 && bot->IsWithinLOSInMap(target))
            {
                if (EncounterNarrator::TrySendPlayerAtmosphere(target, bot, EncounterType::AssassinContract, inst.stalkAccumulatedMs, uint32(inst.id)))
                    inst.introNarrated = true;
            }

            // Opportune strike moment: target is distracted in combat/sitting while close, or full stalking sequence completed
            bool opportuneMoment = (part.subState >= 5 && dist <= 26.0f) ||
                                   (dist <= 28.0f && inst.stalkAccumulatedMs >= 7500) ||
                                   (dist <= 22.0f && inst.stalkAccumulatedMs >= 2500 && (target->IsInCombat() || !target->IsStandState()));
            if (opportuneMoment)
            {
                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                EngagePvPOrCreature(bot, target);
                return;
            }

            // Outer approach if still far away (> 58 yards)
            if (dist > 58.0f)
            {
                if (part.navGoalId == 0 || Dist2DPoint(target, inst.centerX, inst.centerY) > 18.0f)
                {
                    inst.centerX = target->GetPositionX();
                    inst.centerY = target->GetPositionY();
                    inst.centerZ = target->GetPositionZ();
                    float flankAngle = ((inst.id % 2) == 0) ? 0.45f : -0.45f;
                    ComputeVectorOffsetPoint(bot, target, flankAngle, 34.0f, part.lastNavTargetX, part.lastNavTargetY, inst.destZ);
                    part.navGoalId = (inst.id << 8) + 1u + (++inst.subStage);
                }
                bool allowMount = (dist > 75.0f && !part.stealthApplied);
                NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                    part.lastNavTargetX, part.lastNavTargetY, inst.destZ, 6.0f, &part.moveToken, allowMount);
                if (nav == NavStatus::Stuck)
                    inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
                return;
            }

            if (part.subStateEnteredAtMs == 0)
                part.subStateEnteredAtMs = nowMs;
            uint32 stateElapsed = nowMs >= part.subStateEnteredAtMs ? (nowMs - part.subStateEnteredAtMs) : 0u;

            // Humanized cycle: 0=Observe(2.2s) -> 1=MoveFlank(up to 5s) -> 2=WaitDwell(3.0s) -> 3=Reposition(up to 4.5s) -> 4=PreStrikeObserve(1.8s) -> 5=Strike
            if (part.subState == 0 || part.subState == 2 || part.subState == 4)
            {
                uint32 dwellMs = (part.subState == 0) ? 2200u : ((part.subState == 2) ? 3000u : 1800u);
                BotMovement::Stop(bot, part.moveToken);
                bot->SetFacingToObject(target);
                if (stateElapsed >= dwellMs)
                {
                    ++part.subState;
                    part.subStateEnteredAtMs = nowMs;
                    part.navGoalId = 0;
                }
                return;
            }

            // Moving sub-states (1 = MoveFlank at ~26yd, 3 = Reposition at ~19yd, >=5 = FinalApproach)
            if (part.navGoalId == 0 || Dist2DPoint(target, inst.centerX, inst.centerY) > 16.0f)
            {
                inst.centerX = target->GetPositionX();
                inst.centerY = target->GetPositionY();
                inst.centerZ = target->GetPositionZ();
                float sign = ((inst.id + part.subState) % 2 == 0) ? 1.0f : -1.0f;
                float angleOff = (part.subState == 1) ? (0.65f * sign) : ((part.subState == 3) ? (-0.45f * sign) : (0.20f * sign));
                float desiredDist = (part.subState == 1) ? 26.0f : ((part.subState == 3) ? 19.0f : 14.0f);
                ComputeVectorOffsetPoint(bot, target, angleOff, desiredDist, part.lastNavTargetX, part.lastNavTargetY, inst.destZ);
                part.navGoalId = (inst.id << 8) + 10u + (++inst.subStage);
            }

            NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                part.lastNavTargetX, part.lastNavTargetY, inst.destZ, 4.5f, &part.moveToken, false);
            if (nav == NavStatus::Stuck)
            {
                inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
                return;
            }
            if (nav == NavStatus::Arrived || Dist2DPoint(bot, part.lastNavTargetX, part.lastNavTargetY) <= 5.0f || stateElapsed >= 4800u)
            {
                if (part.subState < 5)
                {
                    ++part.subState;
                    part.subStateEnteredAtMs = nowMs;
                    part.navGoalId = 0;
                }
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            if (TryCowardlyRetreat(inst, bot, part, nowMs))
            {
                inst.state.Terminate(EncounterStopReason::CowardlyRetreat, nowMs);
                return;
            }

            if (!bot->IsValidAttackTarget(target))
            {
                inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
                return;
            }

            if (!bot->IsInCombat() || bot->GetVictim() != target)
            {
                if (dist > 15.0f)
                {
                    if (part.navGoalId == 0 || Dist2DPoint(target, part.lastNavTargetX, part.lastNavTargetY) > 10.0f)
                    {
                        part.lastNavTargetX = target->GetPositionX();
                        part.lastNavTargetY = target->GetPositionY();
                        part.navGoalId = (inst.id << 8) + 50u + (++inst.subStage);
                    }
                    BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                        target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 3.5f, &part.moveToken, false);
                }
                EngagePvPOrCreature(bot, target);
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }

    // =========================================================================
    // 7.2 The Watcher (Distant Observer with Evasive Disengage on Player Charge)
    // =========================================================================
    void UpdateTheWatcher(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        Player* target = ObjectAccessor::FindConnectedPlayer(inst.targetPlayerGuid);
        if (!target || !target->IsInWorld() || !target->IsAlive())
        {
            inst.state.Terminate(EncounterStopReason::PlayerLogout, nowMs);
            return;
        }
        if (target->GetMapId() != bot->GetMapId())
        {
            inst.state.Terminate(EncounterStopReason::MapChanged, nowMs);
            return;
        }

        // If attacked during observation, disengage and end encounter cleanly
        if (bot->IsInCombat())
        {
            inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
            return;
        }

        float dist = Dist2D(bot, target);

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            if (dist <= 46.0f && dist >= 25.0f)
            {
                if (part.stealthCapable && (inst.id % 2 == 0))
                    TryApplyStealth(bot, part);
                part.subState = 0; // Start in stationary observation dwell
                part.subStateEnteredAtMs = nowMs;
                part.navGoalId = 0;
                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                return;
            }

            if (part.navGoalId == 0 || Dist2DPoint(target, inst.centerX, inst.centerY) > 16.0f)
            {
                inst.centerX = target->GetPositionX();
                inst.centerY = target->GetPositionY();
                ComputeVectorOffsetPoint(bot, target, 0.55f, 36.0f, part.lastNavTargetX, part.lastNavTargetY, inst.destZ);
                part.navGoalId = (inst.id << 8) + 1u + (++inst.subStage);
            }
            NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                part.lastNavTargetX, part.lastNavTargetY, inst.destZ, 6.0f, &part.moveToken, dist > 75.0f);
            if (nav == NavStatus::Stuck)
                inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            // Evasive disengage: if the player turns and charges toward the Watcher (< 24 yards),
            // immediately stealth (if capable), pick a retreat point directly away from the player, and leave!
            if (dist < 24.0f)
            {
                if (part.stealthCapable)
                    TryApplyStealth(bot, part);
                ComputeVectorOffsetPoint(bot, target, 0.0f, 52.0f, part.originX, part.originY, part.originZ);
                part.navGoalId = (inst.id << 8) | 90u;
                inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
                return;
            }

            if (dist <= 50.0f)
            {
                uint32 delta = (part.lastActionMs > 0 && nowMs > part.lastActionMs) ? (nowMs - part.lastActionMs) : 500u;
                inst.stalkAccumulatedMs += std::min<uint32>(delta, 2000u);
            }
            part.lastActionMs = nowMs;

            // Show rare atmospheric message (~60% of Watcher encounters) ONLY when genuinely close and in LOS
            if (!inst.introNarrated && dist <= 45.0f && inst.stalkAccumulatedMs >= 4000 &&
                (inst.id % 5 != 0) && bot->IsWithinLOSInMap(target))
            {
                if (EncounterNarrator::TrySendPlayerAtmosphere(target, bot, EncounterType::TheWatcher, inst.stalkAccumulatedMs, uint32(inst.id)))
                    inst.introNarrated = true;
            }

            if (part.subStateEnteredAtMs == 0)
                part.subStateEnteredAtMs = nowMs;
            uint32 stateElapsed = nowMs >= part.subStateEnteredAtMs ? (nowMs - part.subStateEnteredAtMs) : 0u;

            // Alternate between stationary observation (subState 0: 7s) and lateral vantage shift (subState 1: up to 5.5s)
            if (part.subState == 0 && dist <= 46.0f)
            {
                BotMovement::Stop(bot, part.moveToken);
                bot->SetFacingToObject(target);
                if (stateElapsed >= 7000u)
                {
                    part.subState = 1;
                    part.subStateEnteredAtMs = nowMs;
                    part.navGoalId = 0;
                }
            }
            else
            {
                if (part.navGoalId == 0 || Dist2DPoint(target, inst.centerX, inst.centerY) > 18.0f)
                {
                    inst.centerX = target->GetPositionX();
                    inst.centerY = target->GetPositionY();
                    float angleOffset = (inst.subStage % 2 == 0) ? 0.65f : -0.65f;
                    ComputeVectorOffsetPoint(bot, target, angleOffset, 36.0f, part.lastNavTargetX, part.lastNavTargetY, inst.destZ);
                    part.navGoalId = (inst.id << 8) + 10u + (++inst.subStage);
                }
                NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                    part.lastNavTargetX, part.lastNavTargetY, inst.destZ, 5.0f, &part.moveToken, false);
                if (nav == NavStatus::Arrived || Dist2DPoint(bot, part.lastNavTargetX, part.lastNavTargetY) <= 5.5f || stateElapsed >= 5500u)
                {
                    part.subState = 0;
                    part.subStateEnteredAtMs = nowMs;
                    part.navGoalId = 0;
                }
            }

            if (inst.state.PhaseElapsedMs(nowMs) >= 35000)
                inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            uint64 goalId = (inst.id << 8) | 90u;
            BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, part.originX, part.originY, part.originZ, 8.0f, &part.moveToken, false);
            if (inst.state.PhaseElapsedMs(nowMs) >= 5000 || Dist2D(bot, target) >= 52.0f)
                inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }

    // =========================================================================
    // 7.3 Faction Skirmish
    // =========================================================================
    void UpdateFactionSkirmish(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        if (part.retreated)
            return;

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            float d = Dist2DPoint(bot, inst.centerX, inst.centerY);
            if (d <= 24.0f)
            {
                part.arrivedAtStaging = true;
                if (Player* enemy = FindNearestLivingEnemyParticipant(inst, bot))
                {
                    if (Dist2D(bot, enemy) <= 32.0f)
                    {
                        inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                        EngagePvPOrCreature(bot, enemy);
                        return;
                    }
                }
            }
            else
            {
                uint64 goalId = (inst.id << 8) | 1u;
                NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                    inst.centerX, inst.centerY, inst.centerZ, 16.0f, &part.moveToken, d > 70.0f);
                if (nav == NavStatus::Stuck)
                    part.retreated = true;
            }

            if (inst.state.PhaseElapsedMs(nowMs) >= 22000)
                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            if (TryCowardlyRetreat(inst, bot, part, nowMs))
                return;

            Player* enemy = FindNearestLivingEnemyParticipant(inst, bot);
            if (!enemy)
            {
                // All opposing skirmishers defeated or retreated; survivors win and resume WorldBrain
                inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
                return;
            }

            if (!inst.introNarrated && Dist2D(bot, enemy) <= 28.0f)
            {
                EncounterNarrator::TryBotSpeak(bot, nullptr, EncounterType::FactionSkirmish, NarratorChannel::BotYell, uint32(inst.id));
                inst.introNarrated = true;
            }

            if (!bot->IsInCombat())
            {
                if (Dist2D(bot, enemy) > 18.0f)
                {
                    uint64 goalId = (inst.id << 8) + 10u + (nowMs / 2500u);
                    BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                        enemy->GetPositionX(), enemy->GetPositionY(), enemy->GetPositionZ(), 4.5f, &part.moveToken, false);
                }
                EngagePvPOrCreature(bot, enemy);
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }

    // =========================================================================
    // 7.4 Patrol Ambush
    // =========================================================================
    void UpdatePatrolAmbush(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        if (part.retreated)
            return;

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            if (part.role == EncounterRole::Ambusher)
            {
                float d = Dist2DPoint(bot, inst.destX, inst.destY);
                if (d <= 14.0f)
                {
                    part.arrivedAtStaging = true;
                    BotMovement::Stop(bot, part.moveToken);
                    if (part.stealthCapable)
                        TryApplyStealth(bot, part);

                    // Trigger ambush when any opposing patrol member enters 26 yards
                    if (Player* enemy = FindNearestLivingEnemyParticipant(inst, bot))
                    {
                        if (Dist2D(bot, enemy) <= 26.0f)
                        {
                            inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                            EngagePvPOrCreature(bot, enemy);
                        }
                    }
                }
                else
                {
                    uint64 goalId = (inst.id << 8) | 1u;
                    BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, inst.destX, inst.destY, inst.destZ, 9.0f, &part.moveToken, d > 65.0f);
                }
            }
            else
            {
                // Patrol group walks toward the ambush point (destX, destY)
                uint64 goalId = (inst.id << 8) | 2u;
                NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, inst.destX, inst.destY, inst.destZ, 10.0f, &part.moveToken, false);
                if (nav == NavStatus::Arrived || Dist2DPoint(bot, inst.destX, inst.destY) <= 18.0f)
                    inst.state.TransitionTo(EncounterPhase::Active, nowMs);
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            if (TryCowardlyRetreat(inst, bot, part, nowMs))
                return;

            Player* enemy = FindNearestLivingEnemyParticipant(inst, bot);
            if (!enemy)
            {
                inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
                return;
            }

            if (!inst.introNarrated && part.role == EncounterRole::Ambusher && Dist2D(bot, enemy) <= 26.0f)
            {
                EncounterNarrator::TryBotSpeak(bot, nullptr, EncounterType::PatrolAmbush, NarratorChannel::BotYell, uint32(inst.id));
                inst.introNarrated = true;
            }

            if (!bot->IsInCombat())
            {
                if (Dist2D(bot, enemy) > 16.0f)
                {
                    uint64 goalId = (inst.id << 8) + 20u + (nowMs / 2500u);
                    BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                        enemy->GetPositionX(), enemy->GetPositionY(), enemy->GetPositionZ(), 4.5f, &part.moveToken, false);
                }
                EngagePvPOrCreature(bot, enemy);
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }

    // =========================================================================
    // 7.5 Bounty Hunter & 7.6 Nemesis
    // =========================================================================
    static void UpdateDirectHostilePursuit(
        EncounterInstance& inst,
        Player* bot,
        EncounterParticipant& part,
        EncounterType speakType,
        uint32 nowMs)
    {
        Player* target = ObjectAccessor::FindConnectedPlayer(inst.targetPlayerGuid);
        if (!target || !target->IsInWorld() || !target->IsAlive())
        {
            inst.state.Terminate(EncounterStopReason::ParticipantDeath, nowMs);
            return;
        }

        float dist = Dist2D(bot, target);

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            if (dist <= 22.0f)
            {
                if (!bot->IsValidAttackTarget(target))
                {
                    inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
                    return;
                }

                if (bot->IsMounted())
                    BotMovement::RequestDismount(bot, MoveOwner::Encounter, DismountReason::Combat);

                if (!inst.introNarrated)
                {
                    EncounterNarrator::TryBotSpeak(bot, target, speakType, NarratorChannel::BotSay, uint32(inst.id));
                    inst.introNarrated = true;
                }

                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                EngagePvPOrCreature(bot, target);
                return;
            }

            uint64 goalId = (inst.id << 8) + 1u + (nowMs / 2500u);
            NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 16.0f, &part.moveToken, dist > 60.0f);
            if (nav == NavStatus::Stuck)
                inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            if (TryCowardlyRetreat(inst, bot, part, nowMs))
            {
                inst.state.Terminate(EncounterStopReason::CowardlyRetreat, nowMs);
                return;
            }

            if (!bot->IsValidAttackTarget(target))
            {
                inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
                return;
            }

            if (!bot->IsInCombat() || bot->GetVictim() != target)
            {
                if (dist > 16.0f)
                {
                    uint64 goalId = (inst.id << 8) + 10u + (nowMs / 2000u);
                    BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                        target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 4.0f, &part.moveToken, false);
                }
                EngagePvPOrCreature(bot, target);
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }

    void UpdateBountyHunter(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        UpdateDirectHostilePursuit(inst, bot, part, EncounterType::BountyHunter, nowMs);
    }

    void UpdateNemesis(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        UpdateDirectHostilePursuit(inst, bot, part, EncounterType::Nemesis, nowMs);
    }

    // =========================================================================
    // 7.10 Hunter and Prey
    // =========================================================================
    void UpdateHunterAndPrey(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        EncounterParticipant* hunterPart = inst.FindByRole(EncounterRole::Hunter);
        EncounterParticipant* preyPart = inst.FindByRole(EncounterRole::Prey);
        if (!hunterPart || !preyPart)
        {
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
            return;
        }

        Player* hunter = ObjectAccessor::FindConnectedPlayer(hunterPart->botGuid);
        Player* prey = ObjectAccessor::FindConnectedPlayer(preyPart->botGuid);
        if (!hunter || !prey || !hunter->IsInWorld() || !prey->IsInWorld() ||
            !hunter->IsAlive() || !prey->IsAlive() || hunter->GetMapId() != prey->GetMapId())
        {
            inst.state.Terminate(EncounterStopReason::ParticipantDeath, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            inst.state.TransitionTo(EncounterPhase::Active, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            float dist = Dist2D(hunter, prey);

            // Once Hunter closes the gap, normal PvP combat begins
            if (dist <= 15.0f || inst.subStage == 1)
            {
                inst.subStage = 1;
                Player* opponent = (bot == hunter) ? prey : hunter;
                if (!bot->IsValidAttackTarget(opponent))
                {
                    inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
                    return;
                }
                if (!bot->IsInCombat())
                    EngagePvPOrCreature(bot, opponent);
                return;
            }

            // If Prey survives the chase for 45 seconds without being caught, Prey escapes!
            if (inst.state.PhaseElapsedMs(nowMs) >= 45000 || dist >= 160.0f)
            {
                inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
                return;
            }

            if (part.role == EncounterRole::Hunter)
            {
                uint64 goalId = (inst.id << 8) + 10u + (nowMs / 2000u);
                BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                    prey->GetPositionX(), prey->GetPositionY(), prey->GetPositionZ(), 5.0f, &part.moveToken, false);
            }
            else
            {
                // Prey flees away from Hunter, shifting angle periodically
                float dx = prey->GetPositionX() - hunter->GetPositionX();
                float dy = prey->GetPositionY() - hunter->GetPositionY();
                float baseAngle = std::atan2(dy, dx);
                float weave = ((nowMs / 5000u) % 2 == 0) ? 0.35f : -0.35f;
                float fleeX = prey->GetPositionX() + std::cos(baseAngle + weave) * 42.0f;
                float fleeY = prey->GetPositionY() + std::sin(baseAngle + weave) * 42.0f;
                float fleeZ = prey->GetPositionZ();
                if (prey->GetMap())
                {
                    float gz = prey->GetMap()->GetHeight(prey->GetPhaseMask(), fleeX, fleeY, fleeZ + 10.0f);
                    if (gz > INVALID_HEIGHT)
                        fleeZ = gz;
                }

                uint64 goalId = (inst.id << 8) + 50u + (nowMs / 2500u);
                BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, fleeX, fleeY, fleeZ, 6.0f, &part.moveToken, false);
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }
}
