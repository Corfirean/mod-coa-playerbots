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

    void ComputeFlankingPoint(Player const* target, float angleOffsetRad, float distanceYards, float& outX, float& outY, float& outZ)
    {
        float angle = target->GetOrientation() + PI_VAL + angleOffsetRad;
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
    // 7.1 Assassin Contract
    // =========================================================================
    void UpdateAssassinContract(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
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
            if (dist <= 42.0f && part.stealthCapable)
                TryApplyStealth(bot, part);

            if (dist <= 40.0f)
            {
                uint32 delta = (part.lastActionMs > 0 && nowMs > part.lastActionMs) ? (nowMs - part.lastActionMs) : 500u;
                inst.stalkAccumulatedMs += std::min<uint32>(delta, 2000u);
            }
            part.lastActionMs = nowMs;

            // Contextual atmospheric message before attack once stalking in close range
            if (!inst.introNarrated && dist <= 38.0f && inst.stalkAccumulatedMs >= 3200)
            {
                if (EncounterNarrator::TrySendPlayerAtmosphere(target, bot, EncounterType::AssassinContract, inst.stalkAccumulatedMs, uint32(inst.id)))
                    inst.introNarrated = true;
            }

            // Opportune strike moment: stalked long enough in range, or target is distracted in combat/sitting
            bool opportuneMoment = (dist <= 28.0f && inst.stalkAccumulatedMs >= 6000) ||
                                   (dist <= 24.0f && (target->IsInCombat() || !target->IsStandState()));
            if (opportuneMoment)
            {
                if (!bot->IsValidAttackTarget(target))
                {
                    inst.state.Terminate(EncounterStopReason::PvPEligibilityLost, nowMs);
                    return;
                }
                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                EngagePvPOrCreature(bot, target);
                return;
            }

            // Approach from behind/flank rather than walking straight into the target's face
            float flankAngle = ((inst.id % 2) == 0) ? 0.55f : -0.55f;
            float fx = 0.0f, fy = 0.0f, fz = 0.0f;
            ComputeFlankingPoint(target, flankAngle, 22.0f, fx, fy, fz);

            uint64 goalId = (inst.id << 8) + 1u + (nowMs / 2500u);
            bool allowMount = (dist > 65.0f && !part.stealthApplied);
            NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, fx, fy, fz, 5.0f, &part.moveToken, allowMount);
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
                if (dist > 15.0f)
                {
                    uint64 goalId = (inst.id << 8) + 20u + (nowMs / 2000u);
                    BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
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
    // 7.2 The Watcher
    // =========================================================================
    void UpdateTheWatcher(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        Player* target = ObjectAccessor::FindConnectedPlayer(inst.targetPlayerGuid);
        if (!target || !target->IsInWorld() || !target->IsAlive())
        {
            inst.state.Terminate(EncounterStopReason::PlayerLogout, nowMs);
            return;
        }

        float dist = Dist2D(bot, target);

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            if (dist <= 48.0f)
            {
                if (part.stealthCapable && (inst.id % 2 == 0))
                    TryApplyStealth(bot, part);
                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                return;
            }

            float vx = 0.0f, vy = 0.0f, vz = 0.0f;
            ComputeFlankingPoint(target, 0.8f, 36.0f, vx, vy, vz);
            uint64 goalId = (inst.id << 8) + 1u + (nowMs / 3000u);
            NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, vx, vy, vz, 6.0f, &part.moveToken, dist > 75.0f);
            if (nav == NavStatus::Stuck)
                inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            // If the player detects and approaches the Watcher closely, stealth/retreat and finish
            if (dist < 15.0f)
            {
                if (part.stealthCapable)
                    TryApplyStealth(bot, part);
                inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
                return;
            }

            if (dist <= 52.0f)
            {
                uint32 delta = (part.lastActionMs > 0 && nowMs > part.lastActionMs) ? (nowMs - part.lastActionMs) : 500u;
                inst.stalkAccumulatedMs += std::min<uint32>(delta, 2000u);
            }
            part.lastActionMs = nowMs;

            // Show rare atmospheric message (~60% of Watcher encounters) when genuinely close
            if (!inst.introNarrated && dist <= 48.0f && inst.stalkAccumulatedMs >= 4000 && (inst.id % 5 != 0))
            {
                if (EncounterNarrator::TrySendPlayerAtmosphere(target, bot, EncounterType::TheWatcher, inst.stalkAccumulatedMs, uint32(inst.id)))
                    inst.introNarrated = true;
            }

            // Maintain 30..44 yard observation distance; occasionally reposition
            if (dist > 44.0f || (nowMs - inst.lastSubStageChangeMs >= 11000))
            {
                inst.lastSubStageChangeMs = nowMs;
                ++inst.subStage;
                float angleOffset = (inst.subStage % 2 == 0) ? 0.65f : -0.65f;
                float vx = 0.0f, vy = 0.0f, vz = 0.0f;
                ComputeFlankingPoint(target, angleOffset, 35.0f, vx, vy, vz);
                uint64 goalId = (inst.id << 8) + 10u + inst.subStage;
                BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, vx, vy, vz, 5.0f, &part.moveToken, false);
            }
            else if (dist >= 28.0f && dist <= 40.0f)
            {
                BotMovement::Stop(bot, MoveOwner::Encounter);
                bot->SetFacingToObject(target);
            }

            if (inst.state.PhaseElapsedMs(nowMs) >= 35000)
                inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            uint64 goalId = (inst.id << 8) | 90u;
            BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, part.originX, part.originY, part.originZ, 8.0f, &part.moveToken, false);
            if (inst.state.PhaseElapsedMs(nowMs) >= 5000 || Dist2D(bot, target) >= 55.0f)
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
                    BotMovement::Stop(bot, MoveOwner::Encounter);
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
