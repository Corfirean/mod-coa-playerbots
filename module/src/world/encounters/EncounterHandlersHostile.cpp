#include "EncounterHandlers.h"
#include "BotMovement.h"
#include "EncounterConfig.h"
#include "EncounterEligibility.h"
#include "EncounterNarrator.h"
#include "EncounterPersonality.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellMgr.h"
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

    bool EnsureCanAttack(EncounterInstance const& inst, Player* bot, Player* target)
    {
        if (inst.isForcedTest && target->IsGameMaster())
            return true;
        if (bot->IsValidAttackTarget(target))
            return true;
        if (inst.isForcedTest)
        {
            bot->SetPvP(true);
            target->SetPvP(true);
            if (target->IsGameMaster() || bot->IsValidAttackTarget(target))
                return true;
        }
        return false;
    }

    bool EnsureCanAttackBots(EncounterInstance const& inst, Player* botA, Player* botB)
    {
        if (!botA || !botB || botA->GetTeamId() == botB->GetTeamId())
            return false;
        if (botA->IsValidAttackTarget(botB))
            return true;
        if (inst.isForcedTest)
        {
            botA->SetPvP(true);
            botB->SetPvP(true);
            return botA->IsValidAttackTarget(botB);
        }
        return false;
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

            if (!EnsureCanAttack(inst, bot, target))
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
            if (!inst.introNarrated && dist <= 42.0f && (inst.stalkAccumulatedMs >= 1000 || inst.isForcedTest))
            {
                if (EncounterNarrator::TrySendPlayerAtmosphere(target, bot, EncounterType::AssassinContract, inst.stalkAccumulatedMs, uint32(inst.id)))
                    inst.introNarrated = true;
            }

            // Opportune strike moment: target is distracted in combat/sitting while close, or full stalking sequence completed, or forced test
            bool opportuneMoment = (inst.isForcedTest && dist <= 28.0f && inst.stalkAccumulatedMs >= 1500) ||
                                   (part.subState >= 5 && dist <= 26.0f) ||
                                   (dist <= 28.0f && inst.stalkAccumulatedMs >= 7500) ||
                                   (dist <= 22.0f && inst.stalkAccumulatedMs >= 2500 && (target->IsInCombat() || !target->IsStandState()));
            if (opportuneMoment)
            {
                part.assignedTargetGuid = target->GetGUID();
                part.engagedInCombat = true;
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

            if (!EnsureCanAttack(inst, bot, target))
            {
                inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
                return;
            }

            if (!bot->IsInCombat() || bot->GetVictim() != target)
            {
                if (dist > 3.0f)
                {
                    if (part.navGoalId == 0 || Dist2DPoint(target, part.lastNavTargetX, part.lastNavTargetY) > 5.0f)
                    {
                        part.lastNavTargetX = target->GetPositionX();
                        part.lastNavTargetY = target->GetPositionY();
                        part.navGoalId = (inst.id << 8) + 50u + (++inst.subStage);
                    }
                    BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                        target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), 2.5f, &part.moveToken, false);
                }
                else
                {
                    BotMovement::Stop(bot, part.moveToken);
                    bot->SetFacingToObject(target);
                    if (inst.isForcedTest && target->IsPlayer() && target->ToPlayer()->IsGameMaster())
                    {
                        if (nowMs - part.lastActionMs >= 1400)
                        {
                            part.lastActionMs = nowMs;
                            bot->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK1H);
                        }
                    }
                }
                if (part.openerSpellId != 0 && bot->HasSpell(part.openerSpellId) && !bot->IsInCombat())
                {
                    bot->CastSpell(target, part.openerSpellId, false);
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
                        part.assignedTargetGuid = enemy->GetGUID();
                        part.engagedInCombat = true;
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

            // Do not compete with BotAI combat movement once in combat; only close gap when out of combat
            if (!bot->IsInCombat())
            {
                part.assignedTargetGuid = enemy->GetGUID();
                part.engagedInCombat = true;
                if (Dist2D(bot, enemy) > 18.0f)
                {
                    if (part.navGoalId == 0 || Dist2DPoint(enemy, part.lastNavTargetX, part.lastNavTargetY) > 12.0f)
                    {
                        part.lastNavTargetX = enemy->GetPositionX();
                        part.lastNavTargetY = enemy->GetPositionY();
                        part.navGoalId = (inst.id << 8) + 10u + (++inst.subStage);
                    }
                    BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
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
    // 7.4 Patrol Ambush (Terrain-Validated Chokepoint & Lateral Ambusher Spread)
    // =========================================================================
    void UpdatePatrolAmbush(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        if (part.retreated)
            return;

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            if (part.role == EncounterRole::Ambusher)
            {
                // Compute a stable lateral ambush slot off the road center so ambushers don't stack in a pile
                if (part.lastNavTargetX == 0.0f && part.lastNavTargetY == 0.0f)
                {
                    size_t ambusherIdx = 0;
                    for (size_t i = 0; i < inst.participants.size(); ++i)
                    {
                        if (inst.participants[i].botGuid == bot->GetGUID())
                        {
                            ambusherIdx = i;
                            break;
                        }
                    }
                    float sideSign = (ambusherIdx % 2 == 0) ? 1.0f : -1.0f;
                    float lateralDist = 7.0f + 3.0f * float(ambusherIdx / 2);
                    float angle = float(ambusherIdx) * 1.1f + (sideSign * 1.57f);
                    part.lastNavTargetX = inst.destX + std::cos(angle) * lateralDist;
                    part.lastNavTargetY = inst.destY + std::sin(angle) * lateralDist;
                    part.navGoalId = (inst.id << 8) | 1u;
                }

                float ambushZ = inst.destZ;
                if (bot->GetMap())
                {
                    float gz = bot->GetMap()->GetHeight(bot->GetPhaseMask(), part.lastNavTargetX, part.lastNavTargetY, ambushZ + 10.0f);
                    if (gz > INVALID_HEIGHT)
                        ambushZ = gz;
                }

                float d = Dist2DPoint(bot, part.lastNavTargetX, part.lastNavTargetY);
                if (d <= 6.0f || part.arrivedAtStaging)
                {
                    if (!part.arrivedAtStaging)
                    {
                        part.arrivedAtStaging = true;
                        BotMovement::Stop(bot, part.moveToken);
                        if (part.stealthCapable)
                            TryApplyStealth(bot, part);
                    }

                    // Trigger ambush when any opposing patrol member enters ambush range
                    if (Player* enemy = FindNearestLivingEnemyParticipant(inst, bot))
                    {
                        if (Dist2D(bot, enemy) <= 24.0f)
                        {
                            part.assignedTargetGuid = enemy->GetGUID();
                            part.engagedInCombat = true;
                            inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                            EngagePvPOrCreature(bot, enemy);
                            return;
                        }
                    }
                }
                else
                {
                    NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                        part.lastNavTargetX, part.lastNavTargetY, ambushZ, 4.5f, &part.moveToken, d > 65.0f);
                    if (nav == NavStatus::Stuck)
                        part.retreated = true;
                }
            }
            else
            {
                // Patrol group walks toward the ambush point (destX, destY)
                uint64 goalId = (inst.id << 8) | 2u;
                NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, inst.destX, inst.destY, inst.destZ, 10.0f, &part.moveToken, false);
                if (nav == NavStatus::Arrived || Dist2DPoint(bot, inst.destX, inst.destY) <= 18.0f)
                    inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                else if (nav == NavStatus::Stuck)
                    part.retreated = true;
            }

            if (inst.state.PhaseElapsedMs(nowMs) >= 25000)
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
                part.assignedTargetGuid = enemy->GetGUID();
                part.engagedInCombat = true;
                if (Dist2D(bot, enemy) > 16.0f)
                {
                    if (part.navGoalId == 0 || Dist2DPoint(enemy, part.lastNavTargetX, part.lastNavTargetY) > 12.0f)
                    {
                        part.lastNavTargetX = enemy->GetPositionX();
                        part.lastNavTargetY = enemy->GetPositionY();
                        part.navGoalId = (inst.id << 8) + 20u + (++inst.subStage);
                    }
                    BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
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

        float dist = Dist2D(bot, target);

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            if (dist <= 22.0f)
            {
                if (!EnsureCanAttack(inst, bot, target))
                {
                    inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
                    return;
                }

                if (bot->IsMounted())
                    BotMovement::RequestDismount(bot, MoveOwner::Encounter, DismountReason::Combat);

                if (!inst.introNarrated)
                {
                    uint32 variantSeed = uint32(inst.id) + inst.subStage;
                    EncounterNarrator::TryBotSpeak(bot, target, speakType, NarratorChannel::BotSay, variantSeed);
                    inst.introNarrated = true;
                }

                part.assignedTargetGuid = target->GetGUID();
                part.engagedInCombat = true;
                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                EngagePvPOrCreature(bot, target);
                return;
            }

            if (part.navGoalId == 0 || Dist2DPoint(target, part.lastNavTargetX, part.lastNavTargetY) > 14.0f)
            {
                part.lastNavTargetX = target->GetPositionX();
                part.lastNavTargetY = target->GetPositionY();
                part.navGoalId = (inst.id << 8) + 1u + (++inst.subStage);
            }
            NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
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

            if (!EnsureCanAttack(inst, bot, target))
            {
                inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
                return;
            }

            if (!bot->IsInCombat() || bot->GetVictim() != target)
            {
                part.assignedTargetGuid = target->GetGUID();
                part.engagedInCombat = true;
                if (dist > 16.0f)
                {
                    if (part.navGoalId == 0 || Dist2DPoint(target, part.lastNavTargetX, part.lastNavTargetY) > 12.0f)
                    {
                        part.lastNavTargetX = target->GetPositionX();
                        part.lastNavTargetY = target->GetPositionY();
                        part.navGoalId = (inst.id << 8) + 10u + (++inst.subStage);
                    }
                    BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
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
    // 7.10 Hunter and Prey (Cross-Faction Only, Prey Head-Start, Chase & Escape)
    // =========================================================================
    void GivePreyHeadStart(EncounterInstance& inst, Player* hunter, Player* prey, EncounterParticipant& preyPart, uint32 nowMs)
    {
        if (!hunter || !prey)
            return;

        hunter->AttackStop();
        prey->AttackStop();

        if (prey->IsMounted())
            BotMovement::RequestDismount(prey, MoveOwner::Encounter, DismountReason::Manual);
        if (hunter->IsMounted())
            BotMovement::RequestDismount(hunter, MoveOwner::Encounter, DismountReason::Manual);

        // Use self mobility spell if known; otherwise apply temporary +15% run speed modifier for the chase
        uint32 mobilitySpell = 0;
        for (auto const& [spellId, playerSpell] : prey->GetSpellMap())
        {
            if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
                continue;
            SpellInfo const* sInfo = sSpellMgr->GetSpellInfo(spellId);
            if (!sInfo || sInfo->IsPassive() || !sInfo->IsPositive() || sInfo->NeedsExplicitUnitTarget())
                continue;
            for (auto const& eff : sInfo->GetEffects())
            {
                if (eff.IsAura(SPELL_AURA_MOD_INCREASE_SPEED) || eff.IsAura(SPELL_AURA_MOD_SPEED_ALWAYS))
                {
                    mobilitySpell = spellId;
                    break;
                }
            }
            if (mobilitySpell != 0)
                break;
        }

        if (mobilitySpell != 0)
        {
            prey->CastSpell(prey, mobilitySpell, true);
        }
        else if (!preyPart.speedBoostApplied)
        {
            preyPart.originalRunSpeedRate = prey->GetSpeedRate(MOVE_RUN);
            if (preyPart.originalRunSpeedRate < 0.1f)
                preyPart.originalRunSpeedRate = 1.0f;
            prey->SetSpeedRate(MOVE_RUN, preyPart.originalRunSpeedRate * 1.15f);
            preyPart.speedBoostApplied = true;
        }

        // Immediately start Prey's first escape leg away from Hunter
        float dx = prey->GetPositionX() - hunter->GetPositionX();
        float dy = prey->GetPositionY() - hunter->GetPositionY();
        float baseAngle = (std::abs(dx) > 0.1f || std::abs(dy) > 0.1f)
            ? std::atan2(dy, dx)
            : prey->GetOrientation();

        preyPart.lastActionMs = nowMs;
        preyPart.subState = 1;
        preyPart.lastNavTargetX = prey->GetPositionX() + std::cos(baseAngle) * 40.0f;
        preyPart.lastNavTargetY = prey->GetPositionY() + std::sin(baseAngle) * 40.0f;
        inst.destZ = prey->GetPositionZ();
        if (prey->GetMap())
        {
            float gz = prey->GetMap()->GetHeight(prey->GetPhaseMask(), preyPart.lastNavTargetX, preyPart.lastNavTargetY, inst.destZ + 10.0f);
            if (gz > INVALID_HEIGHT)
                inst.destZ = gz;
        }
        preyPart.navGoalId = (inst.id << 8) + 50u + preyPart.subState;
        BotMovement::Navigate(prey, MoveOwner::Encounter, preyPart.navGoalId,
            preyPart.lastNavTargetX, preyPart.lastNavTargetY, inst.destZ, 5.0f, &preyPart.moveToken, false);
    }

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

        // Strict cross-faction enforcement (§4): never allow same-faction Hunter & Prey
        if (hunter->GetTeamId() == prey->GetTeamId())
        {
            inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            hunterPart->subStateEnteredAtMs = nowMs;
            hunterPart->originX = hunter->GetPositionX();
            hunterPart->originY = hunter->GetPositionY();
            preyPart->subStateEnteredAtMs = nowMs;
            preyPart->originX = prey->GetPositionX();
            preyPart->originY = prey->GetPositionY();

            inst.subStage = 1; // 1 = GivePreyHeadStart (0..2.2s), 2 = Chase, 100 = Combat
            GivePreyHeadStart(inst, hunter, prey, *preyPart, nowMs);
            inst.state.TransitionTo(EncounterPhase::Active, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            uint32 activeElapsedMs = inst.state.PhaseElapsedMs(nowMs);
            float dist = Dist2D(hunter, prey);

            if (inst.subStage == 1 && activeElapsedMs >= 2200u)
                inst.subStage = 2;

            // Minimum visible chase requirement (§4): only allow combat lock after head-start + chase (>= 6.5s) and dist <= 10yd
            bool canLockCombat = (inst.subStage == 2 && activeElapsedMs >= 6500u && dist <= 10.0f);
            if (canLockCombat || inst.subStage == 100)
            {
                if (inst.subStage != 100)
                {
                    inst.subStage = 100;
                    if (preyPart->speedBoostApplied)
                    {
                        float restoreRate = preyPart->originalRunSpeedRate > 0.1f ? preyPart->originalRunSpeedRate : 1.0f;
                        prey->SetSpeedRate(MOVE_RUN, restoreRate);
                        preyPart->speedBoostApplied = false;
                    }
                    if (hunterPart->moveToken.IsValid())
                        BotMovement::Release(hunter, hunterPart->moveToken);
                    if (preyPart->moveToken.IsValid())
                        BotMovement::Release(prey, preyPart->moveToken);
                }

                Player* opponent = (bot == hunter) ? prey : hunter;
                if (!EnsureCanAttackBots(inst, bot, opponent))
                {
                    inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
                    return;
                }
                part.assignedTargetGuid = opponent->GetGUID();
                part.engagedInCombat = true;
                if (!bot->IsInCombat())
                    EngagePvPOrCreature(bot, opponent);
                return;
            }

            // Prevent early ranged/melee attacks before the chase lock threshold is reached
            if (bot->GetVictim() == prey || bot->GetVictim() == hunter)
                bot->AttackStop();

            // Prey escapes if chase lasts >= 28s or Prey opens distance >= 65 yards
            if (activeElapsedMs >= 28000u || dist >= 65.0f)
            {
                inst.state.Terminate(EncounterStopReason::PreyEscaped, nowMs);
                return;
            }

            if (part.role == EncounterRole::Hunter)
            {
                // During the 2.2s head-start window, Hunter spots Prey and prepares before pursuing
                if (inst.subStage == 1)
                {
                    bot->SetFacingToObject(prey);
                    if (!inst.introNarrated)
                    {
                        EncounterNarrator::TryBotSpeak(bot, nullptr, EncounterType::HunterAndPrey, NarratorChannel::BotYell, uint32(inst.id));
                        inst.introNarrated = true;
                    }
                    return;
                }

                if (part.navGoalId == 0 || Dist2DPoint(prey, part.lastNavTargetX, part.lastNavTargetY) > 10.0f)
                {
                    part.lastNavTargetX = prey->GetPositionX();
                    part.lastNavTargetY = prey->GetPositionY();
                    part.navGoalId = (inst.id << 8) + 10u + (++part.subState);
                }
                NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                    prey->GetPositionX(), prey->GetPositionY(), prey->GetPositionZ(), 4.5f, &part.moveToken, false);
                if (nav == NavStatus::Stuck)
                    inst.state.Terminate(EncounterStopReason::PreyEscaped, nowMs);
            }
            else
            {
                // Minimum meaningful movement check every 8 seconds: prevent running in circles around one point
                if (part.subStateEnteredAtMs == 0)
                    part.subStateEnteredAtMs = nowMs;
                if (nowMs - part.subStateEnteredAtMs >= 8000u)
                {
                    float netMoved = Dist2DPoint(prey, part.originX, part.originY);
                    if (netMoved < 6.0f)
                    {
                        inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
                        return;
                    }
                    part.originX = prey->GetPositionX();
                    part.originY = prey->GetPositionY();
                    part.subStateEnteredAtMs = nowMs;
                }

                // Compute a forward escape leg away from Hunter only when starting a leg or reaching current flee waypoint
                bool needNewLeg = (part.navGoalId == 0) ||
                                  (Dist2DPoint(prey, part.lastNavTargetX, part.lastNavTargetY) <= 8.0f) ||
                                  (nowMs - part.lastActionMs >= 6000u);
                if (needNewLeg)
                {
                    part.lastActionMs = nowMs;
                    float dx = prey->GetPositionX() - hunter->GetPositionX();
                    float dy = prey->GetPositionY() - hunter->GetPositionY();
                    float baseAngle = (std::abs(dx) > 0.1f || std::abs(dy) > 0.1f) ? std::atan2(dy, dx) : prey->GetOrientation();
                    float weave = ((++part.subState % 2) == 0) ? 0.25f : -0.25f;
                    part.lastNavTargetX = prey->GetPositionX() + std::cos(baseAngle + weave) * 40.0f;
                    part.lastNavTargetY = prey->GetPositionY() + std::sin(baseAngle + weave) * 40.0f;
                    inst.destZ = prey->GetPositionZ();
                    if (prey->GetMap())
                    {
                        float gz = prey->GetMap()->GetHeight(prey->GetPhaseMask(), part.lastNavTargetX, part.lastNavTargetY, inst.destZ + 10.0f);
                        if (gz > INVALID_HEIGHT)
                            inst.destZ = gz;
                    }
                    part.navGoalId = (inst.id << 8) + 50u + part.subState;
                }

                NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                    part.lastNavTargetX, part.lastNavTargetY, inst.destZ, 5.0f, &part.moveToken, false);
                if (nav == NavStatus::Stuck)
                    inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }
}
