#include "EncounterHandlers.h"
#include "BotMovement.h"
#include "CellImpl.h"
#include "Creature.h"
#include "EncounterConfig.h"
#include "EncounterNarrator.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuras.h"
#include <cmath>

namespace
{
    constexpr uint32 SPELL_ROGUE_STEALTH = 1784;
    constexpr uint32 SPELL_DRUID_PROWL   = 5215;
    constexpr uint32 DUEL_ARBITER_GOB_ENTRY = 21680;
    constexpr uint32 ROAD_DUEL_SAFE_HP_PCT  = 24;

    float Dist2D(WorldObject const* a, WorldObject const* b)
    {
        return std::hypot(a->GetPositionX() - b->GetPositionX(), a->GetPositionY() - b->GetPositionY());
    }

    float Dist2DPoint(WorldObject const* a, float x, float y)
    {
        return std::hypot(a->GetPositionX() - x, a->GetPositionY() - y);
    }

    void DeleteTemporaryGobForPart(Player* optionalContextPlayer, EncounterParticipant& part)
    {
        if (part.temporaryGobGuid.IsEmpty())
            return;

        GameObject* gob = optionalContextPlayer ? ObjectAccessor::GetGameObject(*optionalContextPlayer, part.temporaryGobGuid) : nullptr;
        if (!gob && part.temporaryGobMapId != 0)
        {
            if (Map* gobMap = sMapMgr->FindMap(part.temporaryGobMapId, 0))
                gob = gobMap->GetGameObject(part.temporaryGobGuid);
        }
        if (gob)
        {
            gob->SetRespawnTime(0);
            gob->Delete();
        }
        part.temporaryGobGuid = ObjectGuid::Empty;
        part.temporaryGobMapId = 0;
    }

    void RemoveNegativeAurasFromDuelOpponent(Player* victim, Player* opponent)
    {
        if (!victim || !opponent)
            return;

        ObjectGuid oppGuid = opponent->GetGUID();
        ObjectGuid oppPetGuid = opponent->GetPetGUID();
        Player::AuraApplicationMap& auras = victim->GetAppliedAuras();
        for (auto it = auras.begin(); it != auras.end();)
        {
            Aura const* aura = it->second ? it->second->GetBase() : nullptr;
            ObjectGuid casterGuid = aura ? aura->GetCasterGUID() : ObjectGuid::Empty;
            if (it->second && !it->second->IsPositive() &&
                (casterGuid == oppGuid || (!oppPetGuid.IsEmpty() && casterGuid == oppPetGuid)))
            {
                victim->RemoveAura(it);
            }
            else
            {
                ++it;
            }
        }
    }

    bool HasExternalAttackerOutsideDuel(Player const* bot, Player const* duelOpponent)
    {
        if (!bot || !duelOpponent)
            return false;

        ObjectGuid oppGuid = duelOpponent->GetGUID();
        for (Unit const* att : bot->getAttackers())
        {
            if (!att || !att->IsAlive())
                continue;
            if (att->GetGUID() == oppGuid || att->GetCharmerOrOwnerGUID() == oppGuid)
                continue;
            return true;
        }
        return false;
    }

    void StopRoadDuelSafely(Player* botA, EncounterParticipant& partA, Player* botB, EncounterParticipant* partB)
    {
        if (botA)
        {
            uint32 safeHpA = std::max<uint32>(1u, (botA->GetMaxHealth() * ROAD_DUEL_SAFE_HP_PCT) / 100u);
            if (botA->IsAlive() && botA->GetHealth() < safeHpA)
                botA->SetHealth(safeHpA);
            botA->CombatStopWithPets(true);
            botA->InterruptNonMeleeSpells(false);
        }
        if (botB)
        {
            uint32 safeHpB = std::max<uint32>(1u, (botB->GetMaxHealth() * ROAD_DUEL_SAFE_HP_PCT) / 100u);
            if (botB->IsAlive() && botB->GetHealth() < safeHpB)
                botB->SetHealth(safeHpB);
            botB->CombatStopWithPets(true);
            botB->InterruptNonMeleeSpells(false);
        }

        if (botA && botA->duel && botA->duel->State != DUEL_STATE_COMPLETED)
            botA->DuelComplete(DUEL_INTERRUPTED);
        else if (botB && botB->duel && botB->duel->State != DUEL_STATE_COMPLETED)
            botB->DuelComplete(DUEL_INTERRUPTED);

        if (botA && botB)
        {
            RemoveNegativeAurasFromDuelOpponent(botA, botB);
            RemoveNegativeAurasFromDuelOpponent(botB, botA);
        }

        if (botA)
        {
            botA->SetGuidValue(PLAYER_DUEL_ARBITER, ObjectGuid::Empty);
            botA->SetUInt32Value(PLAYER_DUEL_TEAM, 0);
            botA->duel.reset(nullptr);
            botA->GetCombatManager().EndAllPvPCombat();
            if (botA->GetTarget() == partA.assignedTargetGuid)
                botA->SetSelection(ObjectGuid::Empty);
            if (botA->IsAlive())
            {
                float restorePct = std::max(partA.originalHealthPct, 85.0f);
                if (botA->GetHealthPct() < restorePct)
                {
                    uint32 targetHp = std::max<uint32>(1u, uint32(botA->GetMaxHealth() * (restorePct / 100.0f)));
                    botA->SetHealth(std::min<uint32>(botA->GetMaxHealth(), targetHp));
                }
            }
        }
        if (botB)
        {
            botB->SetGuidValue(PLAYER_DUEL_ARBITER, ObjectGuid::Empty);
            botB->SetUInt32Value(PLAYER_DUEL_TEAM, 0);
            botB->duel.reset(nullptr);
            botB->GetCombatManager().EndAllPvPCombat();
            if (partB && botB->GetTarget() == partB->assignedTargetGuid)
                botB->SetSelection(ObjectGuid::Empty);
            if (botB->IsAlive())
            {
                float origPct = partB ? partB->originalHealthPct : 85.0f;
                float restorePct = std::max(origPct, 85.0f);
                if (botB->GetHealthPct() < restorePct)
                {
                    uint32 targetHp = std::max<uint32>(1u, uint32(botB->GetMaxHealth() * (restorePct / 100.0f)));
                    botB->SetHealth(std::min<uint32>(botB->GetMaxHealth(), targetHp));
                }
            }
        }

        partA.assignedTargetGuid = ObjectGuid::Empty;
        partA.engagedInCombat = false;
        DeleteTemporaryGobForPart(botA, partA);

        if (partB)
        {
            partB->assignedTargetGuid = ObjectGuid::Empty;
            partB->engagedInCombat = false;
            DeleteTemporaryGobForPart(botB, *partB);
        }
    }

    void StartRoadDuelCombat(
        EncounterInstance& /*inst*/,
        Player* botA,
        EncounterParticipant& partA,
        Player* botB,
        EncounterParticipant& partB)
    {
        if (!botA || !botB)
            return;

        if (botA->IsMounted())
            BotMovement::RequestDismount(botA, MoveOwner::Encounter, DismountReason::Combat);
        if (botB->IsMounted())
            BotMovement::RequestDismount(botB, MoveOwner::Encounter, DismountReason::Combat);

        if (botA->GetHealthPct() < 85.0f)
            botA->SetFullHealth();
        if (botB->GetHealthPct() < 85.0f)
            botB->SetFullHealth();

        // Spawn Duel Arbiter Flag GameObject at midpoint if available and set up native core DuelInfo
        if (!botA->duel && !botB->duel)
        {
            ObjectGuid arbiterGuid = ObjectGuid::Empty;
            if (Map* map = botA->GetMap())
            {
                if (sObjectMgr->GetGameObjectTemplate(DUEL_ARBITER_GOB_ENTRY))
                {
                    GameObject* flagGob = new GameObject();
                    float midX = (botA->GetPositionX() + botB->GetPositionX()) * 0.5f;
                    float midY = (botA->GetPositionY() + botB->GetPositionY()) * 0.5f;
                    float midZ = (botA->GetPositionZ() + botB->GetPositionZ()) * 0.5f;
                    G3D::Quat rot = G3D::Quat::fromAxisAngleRotation(G3D::Vector3::unitZ(), botA->GetOrientation());
                    if (flagGob->Create(map->GenerateLowGuid<HighGuid::GameObject>(), DUEL_ARBITER_GOB_ENTRY,
                            map, botA->GetPhaseMask(), midX, midY, midZ, botA->GetOrientation(), rot, 0, GO_STATE_READY))
                    {
                        flagGob->SetUInt32Value(GAMEOBJECT_FACTION, botA->GetFaction());
                        flagGob->SetUInt32Value(GAMEOBJECT_LEVEL, botA->GetLevel() + 1);
                        flagGob->SetRespawnTime(60);
                        map->AddToMap(flagGob, true);
                        arbiterGuid = flagGob->GetGUID();
                        partA.temporaryGobGuid = arbiterGuid;
                        partA.temporaryGobMapId = botA->GetMapId();
                    }
                    else
                    {
                        delete flagGob;
                    }
                }
            }

            time_t nowSec = GameTime::GetGameTime().count();
            botA->duel = std::make_unique<DuelInfo>(botB, botA, false);
            botB->duel = std::make_unique<DuelInfo>(botA, botA, false);
            botA->duel->StartTime = nowSec;
            botB->duel->StartTime = nowSec;
            botA->duel->State = DUEL_STATE_IN_PROGRESS;
            botB->duel->State = DUEL_STATE_IN_PROGRESS;

            botA->SetGuidValue(PLAYER_DUEL_ARBITER, arbiterGuid);
            botB->SetGuidValue(PLAYER_DUEL_ARBITER, arbiterGuid);
            botA->SetUInt32Value(PLAYER_DUEL_TEAM, 1);
            botB->SetUInt32Value(PLAYER_DUEL_TEAM, 2);
        }

        partA.assignedTargetGuid = botB->GetGUID();
        partA.engagedInCombat = true;
        partB.assignedTargetGuid = botA->GetGUID();
        partB.engagedInCombat = true;

        botA->SetSelection(botB->GetGUID());
        botB->SetSelection(botA->GetGUID());
        botA->Attack(botB, true);
        botB->Attack(botA, true);
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

    // Computes distinct 5-10 yard formation slots around the party/caravan leader (left-rear, right-rear, left-flank, right-flank)
    // so members never stack on a single point (§3, §5).
    void ComputePartyFormationSlot(uint32 followerIdx, float& outDist, float& outAngleOffset)
    {
        switch (followerIdx)
        {
            case 0: outDist = 5.5f; outAngleOffset =  2.35f; break; // left-rear
            case 1: outDist = 5.5f; outAngleOffset = -2.35f; break; // right-rear
            case 2: outDist = 8.0f; outAngleOffset =  1.85f; break; // left-flank
            case 3: outDist = 8.0f; outAngleOffset = -1.85f; break; // right-flank
            default:
                outDist = 6.5f + float(followerIdx / 2u) * 2.0f;
                outAngleOffset = (followerIdx % 2u == 0) ? 2.15f : -2.15f;
                break;
        }
    }

    char const* GetFormationSlotLabel(uint32 followerIdx)
    {
        switch (followerIdx)
        {
            case 0: return "left_rear(5.5yd)";
            case 1: return "right_rear(5.5yd)";
            case 2: return "left_flank(8.0yd)";
            case 3: return "right_flank(8.0yd)";
            default: return "rear_guard(8.5yd)";
        }
    }

    Unit* FindSharedCombatTargetForParty(EncounterInstance const& inst, Player* leader)
    {
        if (!leader || !leader->IsInWorld())
            return nullptr;

        if (Unit* lv = leader->GetVictim())
        {
            if (lv->IsAlive() && leader->IsValidAttackTarget(lv))
                return lv;
        }

        for (Unit* att : leader->getAttackers())
        {
            if (att && att->IsAlive() && leader->IsValidAttackTarget(att))
                return att;
        }

        for (auto const& m : inst.participants)
        {
            if (m.retreated)
                continue;
            Player* member = ObjectAccessor::FindConnectedPlayer(m.botGuid);
            if (!member || !member->IsInWorld() || !member->IsAlive() || member->GetMapId() != leader->GetMapId())
                continue;
            if (Unit* mv = member->GetVictim())
            {
                if (mv->IsAlive() && leader->IsValidAttackTarget(mv))
                    return mv;
            }
            for (Unit* att : member->getAttackers())
            {
                if (att && att->IsAlive() && leader->IsValidAttackTarget(att))
                    return att;
            }
        }
        return nullptr;
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
        part.navGoalId = 0;

        // Clear any encounter combat/target/pet state so no stale PvP or target GUID remains
        if (!part.assignedTargetGuid.IsEmpty())
        {
            if (bot->GetTarget() == part.assignedTargetGuid)
                bot->SetSelection(ObjectGuid::Empty);
            if (bot->GetVictim() && bot->GetVictim()->GetGUID() == part.assignedTargetGuid)
                bot->AttackStop();
            part.assignedTargetGuid = ObjectGuid::Empty;
        }

        if (Pet* pet = bot->GetPet())
        {
            if (pet->IsAlive() && pet->GetVictim() &&
                (pet->GetVictim()->GetTypeId() == TYPEID_PLAYER ||
                 part.role == EncounterRole::DuelistA || part.role == EncounterRole::DuelistB))
            {
                pet->AttackStop();
            }
        }

        if (part.role == EncounterRole::DuelistA || part.role == EncounterRole::DuelistB || bot->duel)
        {
            Player* duelOpp = bot->duel ? bot->duel->Opponent : nullptr;
            StopRoadDuelSafely(bot, part, duelOpp, nullptr);
        }

        if (part.engagedInCombat)
        {
            bot->AttackStop();
            bot->GetCombatManager().EndAllPvPCombat();
            part.engagedInCombat = false;
        }

        ClearStealthIfApplied(bot, part);

        if (part.sitApplied || bot->getStandState() == UNIT_STAND_STATE_SIT || bot->getStandState() == UNIT_STAND_STATE_KNEEL)
        {
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            part.sitApplied = false;
        }

        if (part.morphApplied && part.originalDisplayId != 0)
        {
            bot->SetDisplayId(part.originalDisplayId);
            part.morphApplied = false;
        }

        if (part.originalFaction != 0 && bot->GetFaction() != part.originalFaction)
        {
            bot->SetFaction(part.originalFaction);
            part.originalFaction = 0;
        }

        if (part.speedBoostApplied)
        {
            float restoreRate = part.originalRunSpeedRate > 0.1f ? part.originalRunSpeedRate : 1.0f;
            bot->SetSpeedRate(MOVE_RUN, restoreRate);
            part.speedBoostApplied = false;
        }

        if (!part.temporaryGobGuid.IsEmpty())
        {
            GameObject* gob = ObjectAccessor::GetGameObject(*bot, part.temporaryGobGuid);
            if (!gob && part.temporaryGobMapId != 0)
            {
                if (Map* gobMap = sMapMgr->FindMap(part.temporaryGobMapId, 0))
                    gob = gobMap->GetGameObject(part.temporaryGobGuid);
            }
            if (gob)
            {
                gob->SetRespawnTime(0);
                gob->Delete();
            }
            part.temporaryGobGuid = ObjectGuid::Empty;
            part.temporaryGobMapId = 0;
        }
    }

    void EngagePvPOrCreature(Player* bot, Unit* target)
    {
        if (!bot || !target || !bot->IsAlive() || !target->IsAlive())
            return;
        if (!bot->IsValidAttackTarget(target))
        {
            if (target->IsPlayer() && target->ToPlayer()->IsGameMaster())
            {
                // allow attacking GameMaster in test encounters
            }
            else
                return;
        }

        if (bot->IsMounted())
            BotMovement::RequestDismount(bot, MoveOwner::Encounter, DismountReason::Combat);

        bot->SetSelection(target->GetGUID());
        bot->Attack(target, true);
    }

    // =========================================================================
    // 7.7 Road Duel (Real Combat Presentation with Native DuelInfo & 24% HP Safety Wrapper)
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
            StopRoadDuelSafely(bot, part, opponent, otherPart);
            inst.state.Terminate(EncounterStopReason::BotLogoutOrDespawn, nowMs);
            return;
        }

        // If a third-party mob or external player attacks either duelist, abort the friendly duel cleanly
        if (HasExternalAttackerOutsideDuel(bot, opponent) || HasExternalAttackerOutsideDuel(opponent, bot))
        {
            StopRoadDuelSafely(bot, part, opponent, otherPart);
            inst.state.Terminate(EncounterStopReason::Completed, nowMs);
            return;
        }

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            float dist = Dist2D(bot, opponent);
            if (dist > 7.5f)
            {
                if (part.navGoalId == 0)
                    part.navGoalId = (inst.id << 8) | 1u;
                NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                    opponent->GetPositionX(), opponent->GetPositionY(), opponent->GetPositionZ(), 4.5f, &part.moveToken, false);
                if (nav == NavStatus::Stuck)
                    inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
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

            if ((part.arrivedAtStaging && otherPart->arrivedAtStaging && inst.state.PhaseElapsedMs(nowMs) >= 1200) ||
                inst.state.PhaseElapsedMs(nowMs) >= 2400)
            {
                part.navGoalId = 0;
                otherPart->navGoalId = 0;
                BotMovement::Release(bot, part.moveToken);
                BotMovement::Release(opponent, otherPart->moveToken);
                part.moveToken = {};
                otherPart->moveToken = {};
                inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                StartRoadDuelCombat(inst, bot, part, opponent, *otherPart);
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            uint32 selfSafeHp = std::max<uint32>(1u, (bot->GetMaxHealth() * ROAD_DUEL_SAFE_HP_PCT) / 100u);
            uint32 oppSafeHp = std::max<uint32>(1u, (opponent->GetMaxHealth() * ROAD_DUEL_SAFE_HP_PCT) / 100u);
            bool coreDuelEnded = (!bot->duel || !opponent->duel ||
                                  bot->duel->State == DUEL_STATE_COMPLETED ||
                                  opponent->duel->State == DUEL_STATE_COMPLETED);

            // Non-lethal safety threshold (24% HP) or native core duel finish -> immediately stop all combat/pets/DoTs!
            if (coreDuelEnded || bot->GetHealth() <= selfSafeHp || opponent->GetHealth() <= oppSafeHp ||
                inst.state.PhaseElapsedMs(nowMs) >= 35000)
            {
                inst.duelCompleted = true;
                bool oppLost = (opponent->GetHealthPct() <= bot->GetHealthPct());
                inst.duelWinnerGuid = oppLost ? bot->GetGUID() : opponent->GetGUID();
                inst.duelLoserGuid = oppLost ? opponent->GetGUID() : bot->GetGUID();

                StopRoadDuelSafely(bot, part, opponent, otherPart);
                inst.state.TransitionTo(EncounterPhase::Resolve, nowMs);
                return;
            }

            // Ensure both duelists stay actively attacking each other with real melee & spells via BotAI
            if (!bot->IsInCombat() || bot->GetVictim() != opponent)
            {
                EngagePvPOrCreature(bot, opponent);
            }
            if (!opponent->IsInCombat() || opponent->GetVictim() != bot)
            {
                EngagePvPOrCreature(opponent, bot);
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
            }

            if (inst.state.PhaseElapsedMs(nowMs) >= 3500)
                inst.state.Terminate(EncounterStopReason::Completed, nowMs);
        }
    }

    // =========================================================================
    // 7.8 Caravan / Travelling Party (Optional Wagon, Escort Formation & Shared Defense)
    // =========================================================================
    void UpdateCaravan(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        EncounterParticipant* leaderPart = inst.FindByRole(EncounterRole::CaravanLeader);
        Player* leader = leaderPart ? ObjectAccessor::FindConnectedPlayer(leaderPart->botGuid) : nullptr;
        if (!leader || !leader->IsInWorld() || !leader->IsAlive() || leader->GetMapId() != bot->GetMapId())
        {
            inst.state.Terminate(EncounterStopReason::BotLogoutOrDespawn, nowMs);
            return;
        }

        if (bot->IsInCombat())
        {
            inst.postCombatRegroupUntilMs = nowMs + 3500u;
            return;
        }

        // Compute deterministic 5-10 yard formation slot around CaravanLeader
        uint32 followerIdx = 0;
        for (auto const& p : inst.participants)
        {
            if (p.botGuid == bot->GetGUID())
                break;
            if (p.role != EncounterRole::CaravanLeader)
                ++followerIdx;
        }
        float slotDist = 6.0f;
        float slotAngle = 2.4f;
        ComputePartyFormationSlot(followerIdx, slotDist, slotAngle);

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            if (part.role == EncounterRole::CaravanLeader)
            {
                part.arrivedAtStaging = true;
                bool allReady = true;
                for (auto const& p : inst.participants)
                {
                    if (!p.retreated && !p.arrivedAtStaging)
                        allReady = false;
                }
                if (allReady || inst.state.PhaseElapsedMs(nowMs) >= 10000)
                {
                    part.subStateEnteredAtMs = nowMs;
                    inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                }
            }
            else
            {
                float d = Dist2D(bot, leader);
                if (d <= 16.0f)
                {
                    part.arrivedAtStaging = true;
                    part.moveToken = BotMovement::Follow(bot, MoveOwner::Encounter, leader, slotDist, slotAngle);
                }
                else
                {
                    if (part.navGoalId == 0)
                        part.navGoalId = (inst.id << 8) | (1u + followerIdx);
                    NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                        leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), 8.0f, &part.moveToken, true);
                    if (nav == NavStatus::Stuck)
                        part.retreated = true;
                }
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            // Shared Caravan Defense: if leader or any guard is attacked, defend together
            if (Unit* sharedTarget = FindSharedCombatTargetForParty(inst, leader))
            {
                inst.postCombatRegroupUntilMs = nowMs + 3500u;
                part.assignedTargetGuid = sharedTarget->GetGUID();
                part.engagedInCombat = true;
                EngagePvPOrCreature(bot, sharedTarget);
                return;
            }

            // Post-combat regroup window (3.5s) before resuming the march
            if (nowMs < inst.postCombatRegroupUntilMs)
            {
                if (part.role == EncounterRole::CaravanLeader)
                    BotMovement::Stop(bot, part.moveToken);
                else
                    part.moveToken = BotMovement::Follow(bot, MoveOwner::Encounter, leader, slotDist, slotAngle);
                return;
            }

            if (part.role == EncounterRole::CaravanLeader)
            {
                // Pace control: check follower distances so caravan does not accordion-stretch across the zone
                float maxFollowerDist = 0.0f;
                for (auto& m : inst.participants)
                {
                    if (m.botGuid == bot->GetGUID() || m.retreated)
                        continue;
                    if (Player* member = ObjectAccessor::FindConnectedPlayer(m.botGuid))
                    {
                        if (member->GetMapId() == bot->GetMapId() && member->IsAlive())
                        {
                            float fd = Dist2D(bot, member);
                            if (fd > 130.0f)
                            {
                                m.retreated = true;
                                continue;
                            }
                            if (fd > maxFollowerDist)
                                maxFollowerDist = fd;
                        }
                    }
                }

                if (maxFollowerDist > 30.0f && (nowMs - part.subStateEnteredAtMs) < 5500)
                {
                    BotMovement::Stop(bot, part.moveToken);
                    return;
                }
                if (maxFollowerDist <= 18.0f)
                    part.subStateEnteredAtMs = nowMs;

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
                NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId, wx, wy, wz, 12.0f, &part.moveToken, false);
                if (nav == NavStatus::Arrived || Dist2DPoint(bot, wx, wy) <= 13.0f)
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
                if (d > 130.0f)
                {
                    part.retreated = true;
                    return;
                }
                if (d <= 40.0f)
                {
                    part.moveToken = BotMovement::Follow(bot, MoveOwner::Encounter, leader, slotDist, slotAngle);
                }
                else
                {
                    if (part.navGoalId == 0 || Dist2DPoint(leader, part.lastNavTargetX, part.lastNavTargetY) > 14.0f)
                    {
                        part.lastNavTargetX = leader->GetPositionX();
                        part.lastNavTargetY = leader->GetPositionY();
                        part.lastNavTargetZ = leader->GetPositionZ();
                        part.navGoalId = (inst.id << 8) + 50u + (++part.subState);
                    }
                    NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                        part.lastNavTargetX, part.lastNavTargetY, part.lastNavTargetZ, 8.0f, &part.moveToken, true);
                    if (nav == NavStatus::Stuck)
                        part.retreated = true;
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
    // 7.11 Rival Adventuring Party (Cohesive Group Ownership, 5-10yd Formation, Shared Combat & Regroup)
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

        uint32 followerIdx = 0;
        for (auto const& p : inst.participants)
        {
            if (p.botGuid == bot->GetGUID())
                break;
            if (p.role != EncounterRole::RivalLeader)
                ++followerIdx;
        }
        float slotDist = 5.5f;
        float slotAngle = 2.35f;
        ComputePartyFormationSlot(followerIdx, slotDist, slotAngle);

        if (inst.state.phase == EncounterPhase::Prepare)
        {
            if (part.role == EncounterRole::RivalLeader)
            {
                part.arrivedAtStaging = true;
                bool allReady = true;
                for (auto const& p : inst.participants)
                {
                    if (!p.retreated && !p.arrivedAtStaging)
                        allReady = false;
                }
                if (allReady || inst.state.PhaseElapsedMs(nowMs) >= 5500)
                {
                    part.subStateEnteredAtMs = nowMs;
                    inst.state.TransitionTo(EncounterPhase::Active, nowMs);
                }
            }
            else
            {
                float d = Dist2D(bot, leader);
                if (d <= 14.0f)
                {
                    part.arrivedAtStaging = true;
                    part.moveToken = BotMovement::Follow(bot, MoveOwner::Encounter, leader, slotDist, slotAngle);
                }
                else
                {
                    if (part.navGoalId == 0)
                        part.navGoalId = (inst.id << 8) | 1u;
                    NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                        leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), 8.0f, &part.moveToken, true);
                    if (nav == NavStatus::Stuck)
                        part.retreated = true;
                }
            }
            return;
        }

        if (inst.state.phase == EncounterPhase::Active)
        {
            if (bot->IsInCombat())
            {
                inst.postCombatRegroupUntilMs = nowMs + 3500u;
                part.engagedInCombat = true;
                return;
            }

            // Shared group combat: if leader or any party member is fighting, all members converge and assist
            if (Unit* sharedTarget = FindSharedCombatTargetForParty(inst, leader))
            {
                inst.postCombatRegroupUntilMs = nowMs + 3500u;
                part.assignedTargetGuid = sharedTarget->GetGUID();
                part.engagedInCombat = true;
                EngagePvPOrCreature(bot, sharedTarget);
                return;
            }

            // Post-combat regroup window (3.5s): pause leader travel while followers return to 5-10yd formation slots
            if (inst.postCombatRegroupUntilMs != 0 && nowMs < inst.postCombatRegroupUntilMs)
            {
                if (part.role == EncounterRole::RivalLeader)
                {
                    BotMovement::Stop(bot, part.moveToken);
                }
                else
                {
                    part.moveToken = BotMovement::Follow(bot, MoveOwner::Encounter, leader, slotDist, slotAngle);
                }
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

                // Periodically look for a level-appropriate hostile mob within 28 yards along the route
                if (nowMs - part.lastActionMs >= 2000)
                {
                    part.lastActionMs = nowMs;
                    if (Creature* mob = FindNearbyHostileMobForParty(bot, 28.0f))
                    {
                        inst.postCombatRegroupUntilMs = nowMs + 3500u;
                        part.assignedTargetGuid = mob->GetGUID();
                        part.engagedInCombat = true;
                        EngagePvPOrCreature(bot, mob);
                        for (auto& m : inst.participants)
                        {
                            if (m.botGuid == bot->GetGUID() || m.retreated)
                                continue;
                            if (Player* member = ObjectAccessor::FindConnectedPlayer(m.botGuid))
                            {
                                if (member->IsInWorld() && member->IsAlive() && member->GetMapId() == bot->GetMapId())
                                {
                                    m.assignedTargetGuid = mob->GetGUID();
                                    m.engagedInCombat = true;
                                    EngagePvPOrCreature(member, mob);
                                }
                            }
                        }
                        return;
                    }
                }

                // Pace control: keep party together within formation distance
                float maxFollowerDist = 0.0f;
                for (auto& m : inst.participants)
                {
                    if (m.botGuid == bot->GetGUID() || m.retreated)
                        continue;
                    if (Player* member = ObjectAccessor::FindConnectedPlayer(m.botGuid))
                    {
                        if (member->GetMapId() == bot->GetMapId() && member->IsAlive())
                        {
                            float fd = Dist2D(bot, member);
                            if (fd > 130.0f)
                            {
                                m.retreated = true;
                                continue;
                            }
                            if (fd > maxFollowerDist)
                                maxFollowerDist = fd;
                        }
                    }
                }

                if (maxFollowerDist > 28.0f && (nowMs - part.subStateEnteredAtMs) < 6000)
                {
                    BotMovement::Stop(bot, part.moveToken);
                    return;
                }
                if (maxFollowerDist <= 18.0f)
                    part.subStateEnteredAtMs = nowMs;

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
                if (d > 130.0f)
                {
                    part.retreated = true;
                    return;
                }
                if (d <= 38.0f)
                {
                    part.moveToken = BotMovement::Follow(bot, MoveOwner::Encounter, leader, slotDist, slotAngle);
                }
                else
                {
                    if (part.navGoalId == 0 || Dist2DPoint(leader, part.lastNavTargetX, part.lastNavTargetY) > 12.0f)
                    {
                        float leaderOri = leader->GetOrientation();
                        part.lastNavTargetX = leader->GetPositionX() + std::cos(leaderOri + slotAngle) * slotDist;
                        part.lastNavTargetY = leader->GetPositionY() + std::sin(leaderOri + slotAngle) * slotDist;
                        part.lastNavTargetZ = leader->GetPositionZ();
                        part.navGoalId = (inst.id << 8) + 30u + (++part.subState);
                    }
                    NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
                        part.lastNavTargetX, part.lastNavTargetY, part.lastNavTargetZ, 6.0f, &part.moveToken, true);
                    if (nav == NavStatus::Stuck)
                        part.retreated = true;
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
                part.assignedTargetGuid = rare->GetGUID();
                part.engagedInCombat = true;
                EngagePvPOrCreature(bot, rare);
                return;
            }

            if (part.navGoalId == 0)
                part.navGoalId = (inst.id << 8) | 1u;
            NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, part.navGoalId,
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
                    NavStatus nav = BotMovement::Navigate(bot, MoveOwner::Encounter, goalId,
                        rare->GetPositionX(), rare->GetPositionY(), rare->GetPositionZ(), 15.0f, &part.moveToken, false);
                    if (nav == NavStatus::Stuck)
                        inst.state.Terminate(EncounterStopReason::Unreachable, nowMs);
                }
                else
                {
                    part.assignedTargetGuid = rare->GetGUID();
                    part.engagedInCombat = true;
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
