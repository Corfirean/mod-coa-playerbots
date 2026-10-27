#include "EncounterHandlers.h"
#include "BotMovement.h"
#include "CellImpl.h"
#include "Creature.h"
#include "EncounterConfig.h"
#include "EncounterNarrator.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include <cmath>

namespace
{
    constexpr uint32 SPELL_ROGUE_STEALTH = 1784;
    constexpr uint32 SPELL_DRUID_PROWL   = 5215;

    float Dist2D(WorldObject const* a, WorldObject const* b)
    {
        return std::hypot(a->GetPositionX() - b->GetPositionX(), a->GetPositionY() - b->GetPositionY());
    }

    float Dist2DPoint(WorldObject const* a, float x, float y)
    {
        return std::hypot(a->GetPositionX() - x, a->GetPositionY() - y);
    }

    Creature* FindNearbyHostileMobForParty(Player* leader, float radius)
    {
        if (!leader || !leader->IsInWorld())
            return nullptr;

        Creature* best = nullptr;
        float bestDist = radius;
        std::vector<Creature*> creatures;
        Acore::AnyUnfriendlyUnitInObjectRangeCheck uCheck(leader, leader, radius);
        Acore::CreatureListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(leader, creatures, uCheck);
        Cell::VisitObjects(leader, searcher, radius);

        for (Creature* c : creatures)
        {
            if (!c || !c->IsAlive() || c->IsCivilian() || c->IsCritter() || c->IsPet() || c->IsTotem())
                continue;
            if (!leader->IsValidAttackTarget(c))
                continue;
            int levelDiff = std::abs(int(c->GetLevel()) - int(leader->GetLevel()));
            if (levelDiff > 4)
                continue;
            float d = Dist2D(leader, c);
            if (d < bestDist)
            {
                bestDist = d;
                best = c;
            }
        }
        return best;
    }
}

namespace EncounterHandlers
{
    bool IsStealthCapableClass(uint8 classId)
    {
        return classId == CLASS_ROGUE || classId == CLASS_DRUID;
    }

    bool TryApplyStealth(Player* bot, EncounterParticipant& part)
    {
        if (!bot || !bot->IsAlive() || bot->IsInCombat())
            return false;

        if (bot->HasStealthAura() || bot->HasInvisibilityAura())
        {
            part.stealthApplied = true;
            return true;
        }

        if (bot->IsMounted())
            BotMovement::RequestDismount(bot, MoveOwner::Encounter, DismountReason::Manual);

        uint32 spellId = 0;
        if (bot->getClass() == CLASS_ROGUE && bot->HasSpell(SPELL_ROGUE_STEALTH))
            spellId = SPELL_ROGUE_STEALTH;
        else if (bot->getClass() == CLASS_DRUID && bot->HasSpell(SPELL_DRUID_PROWL))
            spellId = SPELL_DRUID_PROWL;
        else if (bot->getClass() == CLASS_ROGUE)
            spellId = SPELL_ROGUE_STEALTH;

        if (spellId != 0)
        {
            bot->CastSpell(bot, spellId, true);
            part.stealthApplied = true;
            return true;
        }
        return false;
    }

    void ClearStealthIfApplied(Player* bot, EncounterParticipant& part)
    {
        if (!bot || !part.stealthApplied)
            return;
        bot->RemoveAurasByType(SPELL_AURA_MOD_STEALTH);
        part.stealthApplied = false;
    }

    void RestoreParticipantState(Player* bot, EncounterParticipant& part)
    {
        if (!bot)
            return;

        if (part.moveToken.IsValid())
            BotMovement::Release(bot, part.moveToken);
        BotMovement::ForceReleaseOwner(bot, MoveOwner::Encounter);
        BotMovement::ResetRequest(bot->GetGUID());
        part.moveToken = {};

        ClearStealthIfApplied(bot, part);

        if (part.sitApplied)
        {
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            part.sitApplied = false;
        }

        if (part.morphApplied && part.originalDisplayId != 0)
        {
            bot->SetDisplayId(part.originalDisplayId);
            part.morphApplied = false;
        }

        if (!part.temporaryGobGuid.IsEmpty())
        {
            if (GameObject* gob = ObjectAccessor::GetGameObject(*bot, part.temporaryGobGuid))
            {
                gob->SetRespawnTime(0);
                gob->Delete();
            }
            part.temporaryGobGuid = ObjectGuid::Empty;
        }
    }

    void EngagePvPOrCreature(Player* bot, Unit* target)
    {
        if (!bot || !target || !bot->IsAlive() || !target->IsAlive())
            return;
        if (!bot->IsValidAttackTarget(target))
            return;

        if (bot->IsMounted())
            BotMovement::RequestDismount(bot, MoveOwner::Encounter, DismountReason::Combat);

        bot->SetSelection(target->GetGUID());
        bot->Attack(target, true);
    }

    // =========================================================================
    // 7.7 Road Duel
    // =========================================================================
    void UpdateRoadDuel(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        EncounterParticipant* otherPart = nullptr;
        for (auto& p : inst.participants)
        {
            if (p.botGuid != bot->GetGUID())
            {
                otherPart = &p;
                break;
            }
        }
        if (!otherPart)
        {
            inst.state.Terminate(EncounterStopReason::BotLogoutOrDespawn, nowMs);
            return;
        }

        Player* opponent = ObjectAccessor::FindConnectedPlayer(otherPart->botGuid);
        if (!opponent || !opponent->IsInWorld() || !opponent->IsAlive() || opponent->GetMapId() != bot->GetMapId())
        {
            inst.state.Terminate(EncounterStopReason::BotLogoutOrDespawn, nowMs);
            return;
        }

        // If either bot gets pulled into real external combat, abort friendly duel cleanly
        if (bot->IsInCombat() || opponent->IsInCombat())
        {
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            float dist = Dist2D(bot, opponent);
            if (dist > 5.0f)
            {
                uint64 goalId = (inst.id << 8) | 1u;
                BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, inst.centerX, inst.centerY, inst.centerZ, 3.5f, &part.moveToken, false);
                return;
            }

            if (bot->IsMounted())
                BotMovement::RequestDismount(bot, MoveOwner::Encounter, DismountReason::Manual);

            BotMovement::Stop(bot, part.moveToken);
            bot->SetFacingToObject(opponent);
            if (!part.arrivedAtStaging)
            {
                part.arrivedAtStaging = true;
                bot->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
            }

            if (part.arrivedAtStaging && otherPart->arrivedAtStaging)
                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            // Keep duelists within melee/duel range and exchanging non-lethal blows
            float dist = Dist2D(bot, opponent);
            if (dist > 5.5f)
            {
                uint64 goalId = (inst.id << 8) | 2u;
                BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                    opponent->GetPositionX(), opponent->GetPositionY(), opponent->GetPositionZ(), 3.5f, &part.moveToken, false);
                return;
            }

            BotMovement::Stop(bot, part.moveToken);
            bot->SetFacingToObject(opponent);

            if (nowMs - part.lastActionMs >= 1500)
            {
                part.lastActionMs = nowMs;
                bot->HandleEmoteCommand(((nowMs / 1500) % 2 == 0) ? EMOTE_ONESHOT_ATTACK1H : EMOTE_ONESHOT_PARRY_UNARMED);

                // Controlled non-lethal duel exchange: never reduce below 20% health
                uint32 oppMaxHp = opponent->GetMaxHealth();
                uint32 oppCurHp = opponent->GetHealth();
                uint32 minSafeHp = std::max<uint32>(1u, oppMaxHp / 5u);
                uint32 stepDmg = std::max<uint32>(1u, oppMaxHp / 14u);

                if (oppCurHp > minSafeHp + stepDmg)
                {
                    opponent->SetHealth(oppCurHp - stepDmg);
                }
                else
                {
                    opponent->SetHealth(minSafeHp);
                    inst.duelCompleted = true;
                    inst.duelWinnerGuid = bot->GetGUID();
                    inst.duelLoserGuid = opponent->GetGUID();
                    inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
                }
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            if (!part.emoteDone)
            {
                part.emoteDone = true;
                BotMovement::Stop(bot, part.moveToken);
                bot->SetFacingToObject(opponent);
                if (inst.duelWinnerGuid == bot->GetGUID())
                    bot->HandleEmoteCommand(EMOTE_ONESHOT_CHEER);
                else
                    bot->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);

                // Restore health after friendly duel so neither bot walks away at 20% HP
                if (bot->GetHealthPct() < 75.0f)
                    bot->SetHealth(uint32(bot->GetMaxHealth() * 0.80f));
            }

            if (inst.state.PhaseElapsedMs(nowMs) >= 3500)
                inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }

    // =========================================================================
    // 7.8 Caravan / Travelling Party
    // =========================================================================
    void UpdateCaravan(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        if (bot->IsInCombat())
            return;

        EncounterParticipant* leaderPart = inst.FindByRole(EncounterRole::CaravanLeader);
        Player* leader = leaderPart ? ObjectAccessor::FindConnectedPlayer(leaderPart->botGuid) : nullptr;
        if (!leader || !leader->IsInWorld() || !leader->IsAlive() || leader->GetMapId() != bot->GetMapId())
        {
            inst.state.Terminate(EncounterStopReason::BotLogoutOrDespawn, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            if (part.role == EncounterRole::CaravanLeader)
            {
                part.arrivedAtStaging = true;
                bool allReady = true;
                for (auto const& p : inst.participants)
                {
                    if (!p.arrivedAtStaging)
                        allReady = false;
                }
                if (allReady || inst.state.PhaseElapsedMs(nowMs) >= 15000)
                    inst.state.TransitionTo(EncounterPhase::Active, nowMs);
            }
            else
            {
                float d = Dist2D(bot, leader);
                if (d <= 18.0f)
                    part.arrivedAtStaging = true;
                else
                {
                    uint64 goalId = (inst.id << 8) | 1u;
                    BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                        leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), 12.0f, &part.moveToken, true);
                }
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            // Assist leader if leader is fighting
            if (leader->IsInCombat() && leader->GetVictim() && bot != leader)
            {
                EngagePvPOrCreature(bot, leader->GetVictim());
                return;
            }

            if (part.role == EncounterRole::CaravanLeader)
            {
                float wx = inst.destX;
                float wy = inst.destY;
                float wz = inst.destZ;
                if (inst.currentWaypointIdx < inst.waypoints.size())
                {
                    wx = inst.waypoints[inst.currentWaypointIdx].x;
                    wy = inst.waypoints[inst.currentWaypointIdx].y;
                    wz = inst.waypoints[inst.currentWaypointIdx].z;
                }

                uint64 goalId = (inst.id << 8) + 10u + uint64(inst.currentWaypointIdx);
                NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, wx, wy, wz, 14.0f, &part.moveToken, true);
                if (nav == NavStatus::Arrived || Dist2DPoint(bot, wx, wy) <= 15.0f)
                {
                    if (inst.currentWaypointIdx + 1 < inst.waypoints.size())
                        ++inst.currentWaypointIdx;
                    else
                        inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
                }
                else if (nav == NavStatus::Stuck)
                {
                    inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
                }
            }
            else
            {
                float d = Dist2D(bot, leader);
                if (d > 10.0f)
                {
                    uint64 goalId = (inst.id << 8) + 50u + (nowMs / 2500u);
                    BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                        leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), 7.0f, &part.moveToken, true);
                }
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }

    // =========================================================================
    // 7.9 Deserter
    // =========================================================================
    void UpdateDeserter(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        if (bot->IsInCombat())
            return;

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            uint64 goalId = (inst.id << 8) | 1u;
            NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, inst.destX, inst.destY, inst.destZ, 4.5f, &part.moveToken, false);
            if (nav == NavStatus::Arrived || Dist2DPoint(bot, inst.destX, inst.destY) <= 5.0f)
            {
                if (bot->IsMounted())
                    BotMovement::RequestDismount(bot, MoveOwner::Encounter, DismountReason::Manual);
                BotMovement::Stop(bot, part.moveToken);
                bot->SetStandState(UNIT_STAND_STATE_SIT);
                part.sitApplied = true;
                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
            }
            else if (nav == NavStatus::Stuck)
            {
                inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            if (!part.narratorSpoken && inst.state.PhaseElapsedMs(nowMs) >= 3000)
            {
                Player* listener = !inst.targetPlayerGuid.IsEmpty()
                    ? ObjectAccessor::FindConnectedPlayer(inst.targetPlayerGuid)
                    : nullptr;
                if (EncounterNarrator::TryBotSpeak(bot, listener, EncounterType::Deserter, NarratorChannel::BotSay, uint32(inst.id)))
                    part.narratorSpoken = true;
            }

            if (inst.state.PhaseElapsedMs(nowMs) >= 25000)
            {
                if (part.sitApplied)
                {
                    bot->SetStandState(UNIT_STAND_STATE_STAND);
                    part.sitApplied = false;
                }
                inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            uint64 goalId = (inst.id << 8) | 2u;
            NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, inst.centerX, inst.centerY, inst.centerZ, 6.0f, &part.moveToken, false);
            if (nav == NavStatus::Arrived || inst.state.PhaseElapsedMs(nowMs) >= 7000)
                inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }

    // =========================================================================
    // 7.11 Rival Adventuring Party
    // =========================================================================
    void UpdateRivalParty(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        EncounterParticipant* leaderPart = inst.FindByRole(EncounterRole::RivalLeader);
        Player* leader = leaderPart ? ObjectAccessor::FindConnectedPlayer(leaderPart->botGuid) : nullptr;
        if (!leader || !leader->IsInWorld() || !leader->IsAlive() || leader->GetMapId() != bot->GetMapId())
        {
            inst.state.Terminate(EncounterStopReason::BotLogoutOrDespawn, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            inst.state.TransitionTo(EncounterPhase::Active, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            if (bot->IsInCombat())
                return;

            // If leader is fighting a mob, members assist
            if (leader->IsInCombat() && leader->GetVictim() && bot != leader)
            {
                EngagePvPOrCreature(bot, leader->GetVictim());
                return;
            }

            if (part.role == EncounterRole::RivalLeader)
            {
                // Contextual Nemesis-lite / repeat encounter greeting if near target player
                if (!inst.introNarrated && !inst.targetPlayerGuid.IsEmpty() && inst.subStage > 0)
                {
                    if (Player* realPlayer = ObjectAccessor::FindConnectedPlayer(inst.targetPlayerGuid))
                    {
                        if (realPlayer->GetMapId() == bot->GetMapId() && Dist2D(bot, realPlayer) <= 32.0f)
                        {
                            if (EncounterNarrator::TryBotSpeak(bot, realPlayer, EncounterType::RivalAdventuringParty, NarratorChannel::BotSay, uint32(inst.id)))
                                inst.introNarrated = true;
                        }
                    }
                }

                // Periodically look for a level-appropriate hostile mob along the route
                if (nowMs - part.lastActionMs >= 4000)
                {
                    part.lastActionMs = nowMs;
                    if (Creature* mob = FindNearbyHostileMobForParty(bot, 24.0f))
                    {
                        EngagePvPOrCreature(bot, mob);
                        return;
                    }
                }

                uint64 goalId = (inst.id << 8) | 10u;
                NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, inst.destX, inst.destY, inst.destZ, 15.0f, &part.moveToken, true);
                if (nav == NavStatus::Arrived || Dist2DPoint(bot, inst.destX, inst.destY) <= 16.0f)
                    inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
                else if (nav == NavStatus::Stuck)
                    inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
            }
            else
            {
                float d = Dist2D(bot, leader);
                if (d > 9.0f)
                {
                    uint64 goalId = (inst.id << 8) + 30u + (nowMs / 2500u);
                    BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                        leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), 6.5f, &part.moveToken, true);
                }
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }

    // =========================================================================
    // 7.12 Rare Hunter
    // =========================================================================
    void UpdateRareHunter(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        Creature* rare = ObjectAccessor::GetCreature(*bot, inst.targetCreatureGuid);
        if (!rare || !rare->IsInWorld() || !rare->IsAlive())
        {
            inst.state.Terminate(EncounterStopReason::TargetRareDead, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            float dist = Dist2D(bot, rare);
            if (dist <= 22.0f && bot->IsWithinLOSInMap(rare))
            {
                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                EngagePvPOrCreature(bot, rare);
                return;
            }

            uint64 goalId = (inst.id << 8) | 1u;
            NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                rare->GetPositionX(), rare->GetPositionY(), rare->GetPositionZ(), 18.0f, &part.moveToken, true);
            if (nav == NavStatus::Stuck)
                inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            if (!bot->IsInCombat())
            {
                float dist = Dist2D(bot, rare);
                if (dist > 22.0f)
                {
                    uint64 goalId = (inst.id << 8) | 2u;
                    BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                        rare->GetPositionX(), rare->GetPositionY(), rare->GetPositionZ(), 15.0f, &part.moveToken, false);
                }
                else
                {
                    EngagePvPOrCreature(bot, rare);
                }
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Resolve)
        {
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }
}
