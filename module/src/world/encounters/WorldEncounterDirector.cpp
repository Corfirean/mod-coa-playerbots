#include "WorldEncounterDirector.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "BotMovement.h"
#include "BotWorldPoi.h"
#include "CellImpl.h"
#include "Creature.h"
#include "DBCStores.h"
#include "EncounterBudget.h"
#include "EncounterCleanupDiagnostics.h"
#include "EncounterConfig.h"
#include "EncounterEligibility.h"
#include "EncounterHandlers.h"
#include "EncounterMemory.h"
#include "EncounterNarrator.h"
#include "EncounterPersonality.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "WorldBrainState.h"
#include "WorldExecutor.h"
#include "WorldParties.h"
#include "WorldSession.h"
#include "WorldSocial.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    EncounterOwnershipTable _ownership;
    EncounterBudgetTracker _budget;
    EncounterMemoryStore _memory;
    EncounterMetrics _metrics;
    bool _debugLogging = false;

    std::unordered_map<uint64, EncounterInstance> _encounters;
    std::unordered_map<ObjectGuid, WorldPersona> _knownPersonas;
    std::unordered_set<ObjectGuid> _knownSocialActive;
    std::unordered_map<ObjectGuid, uint32> _rareCreatureCooldownUntilMs;
    uint64 _nextEncounterId = 1;

    uint32 _evalAccumMs = 0;
    uint32 _combatTickAccumMs = 0;
    uint32 _rareScanAccumMs = 0;
    size_t _playerSampleCursor = 0;
    size_t _botSampleCursor = 0;
    uint32 _rngState = 0x9E3779B9u;

    uint32 NowMs()
    {
        return static_cast<uint32>(GameTime::GetGameTimeMS().count());
    }

    uint32 NextRand()
    {
        uint32 x = _rngState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        _rngState = x ? x : 0x9E3779B9u;
        return _rngState;
    }

    float Dist2D(float ax, float ay, float bx, float by)
    {
        return std::hypot(ax - bx, ay - by);
    }

    float Dist2D(WorldObject const* a, WorldObject const* b)
    {
        if (!a || !b)
            return 99999.0f;
        return a->GetDistance2d(b);
    }

    bool IsRealPlayer(Player const* p)
    {
        if (!p || !p->IsInWorld() || !p->GetSession())
            return false;
        return !sBotMgr->IsBotAccountId(p->GetSession()->GetAccountId());
    }

    bool IsBotPlayer(Player const* p)
    {
        if (!p || !p->IsInWorld() || !p->GetSession())
            return false;
        return sBotMgr->IsBotAccountId(p->GetSession()->GetAccountId());
    }

    bool IsSanctuaryArea(Player const* p)
    {
        if (!p)
            return true;
        if (p->HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_SANCTUARY))
            return true;
        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(p->GetAreaId()))
        {
            if (area->flags & AREA_FLAG_SANCTUARY)
                return true;
        }
        return false;
    }

    bool IsCapitalOrSafeZone(Player const* p)
    {
        if (!p)
            return true;
        if (BotAI::IsInCity(p) || p->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
            return true;
        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(p->GetAreaId()))
        {
            if (area->flags & (AREA_FLAG_CAPITAL | AREA_FLAG_SANCTUARY))
                return true;
        }
        return false;
    }

    bool IsContestedOrPvPZone(Player const* p)
    {
        if (!p || IsSanctuaryArea(p) || IsCapitalOrSafeZone(p))
            return false;
        if (p->HasByteFlag(UNIT_FIELD_BYTES_2, 1, UNIT_BYTE2_FLAG_FFA_PVP) ||
            p->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_IN_PVP))
            return true;
        if (AreaTableEntry const* area = sAreaTableStore.LookupEntry(p->GetAreaId()))
        {
            if (area->flags & AREA_FLAG_CONTESTED_AREA)
                return true;
            if (area->team == AREATEAM_NONE && !(area->flags & (AREA_FLAG_CAPITAL | AREA_FLAG_SANCTUARY)))
                return true;
        }
        return false;
    }

    float EstimateDistanceToCapital(Player const* p)
    {
        if (!p)
            return 0.0f;
        if (IsCapitalOrSafeZone(p))
            return 0.0f;
        return 500.0f;
    }

    bool IsPlayerControlledGroup(Player const* bot)
    {
        Group const* grp = bot ? bot->GetGroup() : nullptr;
        if (!grp)
            return false;
        if (WorldParties::IsInParty(bot->GetGUID()))
            return false;
        return true;
    }

    BotActivitySnapshot BuildBotActivitySnapshot(Player* bot)
    {
        BotActivitySnapshot snap;
        if (!bot)
            return snap;

        snap.botGuid = bot->GetGUID();
        snap.isOnlineAndInWorld = bot->IsInWorld();
        snap.isAlive = bot->IsAlive();
        snap.isInFlight = bot->IsInFlight();
        snap.inDungeonOrBG = (bot->GetMap() && bot->GetMap()->Instanceable()) || bot->InBattleground() || bot->InArena();
        snap.inBattlegroundQueueCritical = bot->InBattlegroundQueue();
        snap.inPlayerControlledGroup = IsPlayerControlledGroup(bot);
        snap.hasManualCommand = snap.inPlayerControlledGroup;
        snap.inWorldParty = (bot->GetGroup() && WorldParties::IsInParty(bot->GetGUID()));
        snap.isHelpingSocial = (_knownSocialActive.find(bot->GetGUID()) != _knownSocialActive.end());
        MoveOwner curOwner = BotMovement::CurrentOwner(bot);
        snap.isLootingOrCorpseRun = (curOwner == MoveOwner::Loot || curOwner == MoveOwner::Corpse ||
                                     curOwner == MoveOwner::AutoDungeon || curOwner == MoveOwner::Battleground);
        snap.isCastingGatherOrFish = bot->IsNonMeleeSpellCast(false);
        snap.alreadyInEncounter = _ownership.IsOwned(bot->GetGUID());
        return snap;
    }

    TargetPlayerSnapshot BuildTargetPlayerSnapshot(Player* player)
    {
        TargetPlayerSnapshot snap;
        if (!player)
            return snap;

        snap.guid = player->GetGUID();
        snap.isRealPlayer = IsRealPlayer(player);
        snap.isOnlineAndInWorld = player->IsInWorld();
        snap.isAlive = player->IsAlive();
        snap.isInFlight = player->IsInFlight();
        snap.isInDungeonOrBG = (player->GetMap() && player->GetMap()->Instanceable()) || player->InBattleground() || player->InArena();
        snap.isInSanctuary = IsSanctuaryArea(player);
        snap.isInCapitalOrSafeZone = IsCapitalOrSafeZone(player);
        snap.isContestedOrPvPArea = IsContestedOrPvPZone(player);
        snap.isPvPFlaggedOrAttackable = player->IsPvP() || player->IsFFAPvP() || snap.isContestedOrPvPArea;
        snap.isAFK = player->isAFK();
        snap.isInCombat = player->IsInCombat();
        snap.isGameMaster = player->IsGameMaster();
        snap.mapId = player->GetMapId();
        snap.zoneId = player->GetZoneId();
        snap.areaId = player->GetAreaId();
        snap.teamId = uint32(player->GetTeamId());
        snap.level = player->GetLevel();
        snap.partySize = player->GetGroup() ? player->GetGroup()->GetMembersCount() : 1u;
        snap.x = player->GetPositionX();
        snap.y = player->GetPositionY();
        snap.z = player->GetPositionZ();
        snap.distanceToCapitalYards = EstimateDistanceToCapital(player);
        return snap;
    }

    CandidateBotSnapshot BuildCandidateBotSnapshot(Player* bot, Unit const* optionalHostileTarget = nullptr)
    {
        CandidateBotSnapshot snap;
        if (!bot)
            return snap;

        snap.activity = BuildBotActivitySnapshot(bot);
        snap.mapId = bot->GetMapId();
        snap.zoneId = bot->GetZoneId();
        snap.areaId = bot->GetAreaId();
        snap.teamId = uint32(bot->GetTeamId());
        snap.level = bot->GetLevel();
        snap.classId = bot->getClass();
        snap.x = bot->GetPositionX();
        snap.y = bot->GetPositionY();
        snap.z = bot->GetPositionZ();
        snap.healthPct = bot->GetHealthPct();
        snap.distanceToCapitalYards = EstimateDistanceToCapital(bot);
        snap.isInSanctuary = IsSanctuaryArea(bot);
        snap.isInCapitalOrSafeZone = IsCapitalOrSafeZone(bot);
        snap.isContestedOrPvPArea = IsContestedOrPvPZone(bot);
        snap.canLegallyAttackTarget = optionalHostileTarget ? bot->IsValidAttackTarget(optionalHostileTarget) : true;
        snap.hasStealthSpell = (bot->getClass() == CLASS_ROGUE && bot->HasSpell(1784)) ||
                               (bot->getClass() == CLASS_DRUID && bot->HasSpell(5215));
        return snap;
    }

    WorldPersona GetPersonaForBot(ObjectGuid botGuid)
    {
        auto it = _knownPersonas.find(botGuid);
        if (it != _knownPersonas.end())
            return it->second;

        WorldPersona def;
        def.seed = uint32(botGuid.GetCounter());
        return def;
    }

    void GatherNearbyCandidateBots(WorldObject* centerObj, float radius, std::vector<Player*>& outBots, bool ignoreCooldown = false)
    {
        outBots.clear();
        if (!centerObj || !centerObj->IsInWorld())
            return;

        std::vector<Player*> players;
        Acore::AnyPlayerInObjectRangeCheck check(centerObj, radius, true);
        Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(centerObj, players, check);
        Cell::VisitObjects(centerObj, searcher, radius);

        _metrics.totalCandidatesVisited += players.size();
        for (Player* p : players)
        {
            if (!IsBotPlayer(p))
                continue;
            if (!ignoreCooldown && _budget.IsBotOnCooldown(p->GetGUID(), NowMs()))
                continue;
            outBots.push_back(p);
        }
    }

    bool IsRareAlreadyTargeted(ObjectGuid rareGuid, uint32 nowMs)
    {
        if (rareGuid.IsEmpty())
            return true;
        for (auto const& [id, inst] : _encounters)
        {
            if (inst.state.IsRunning() && inst.type == EncounterType::RareHunter && inst.targetCreatureGuid == rareGuid)
                return true;
        }
        auto it = _rareCreatureCooldownUntilMs.find(rareGuid);
        if (it != _rareCreatureCooldownUntilMs.end() && nowMs < it->second)
            return true;
        return false;
    }

    Creature* FindNearbyLiveRareCreature(WorldObject* centerObj, float radius)
    {
        if (!centerObj || !centerObj->IsInWorld())
            return nullptr;

        uint32 nowMs = NowMs();
        std::vector<Creature*> creatures;
        Acore::AnyUnitInObjectRangeCheck uCheck(centerObj, radius);
        Acore::CreatureListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(centerObj, creatures, uCheck);
        Cell::VisitObjects(centerObj, searcher, radius);

        Creature* best = nullptr;
        float bestDist = radius;
        for (Creature* c : creatures)
        {
            if (!c || !c->IsAlive() || !c->IsInWorld() || c->IsPet() || c->IsTotem() || c->IsCritter())
                continue;
            CreatureTemplate const* tpl = c->GetCreatureTemplate();
            if (!tpl || (tpl->rank != CREATURE_ELITE_RARE && tpl->rank != CREATURE_ELITE_RAREELITE))
                continue;
            if (IsRareAlreadyTargeted(c->GetGUID(), nowMs))
                continue;

            float d = Dist2D(centerObj->GetPositionX(), centerObj->GetPositionY(), c->GetPositionX(), c->GetPositionY());
            if (d < bestDist)
            {
                bestDist = d;
                best = c;
            }
        }
        return best;
    }

    EncounterParticipant MakeParticipant(Player* bot, EncounterRole role, uint32 nowMs)
    {
        EncounterParticipant p;
        p.botGuid = bot->GetGUID();
        p.role = role;
        p.teamId = uint32(bot->GetTeamId());
        p.originalFaction = bot->GetFaction();
        p.mapId = bot->GetMapId();
        p.originX = bot->GetPositionX();
        p.originY = bot->GetPositionY();
        p.originZ = bot->GetPositionZ();
        p.joinedAtMs = nowMs;
        p.lastActionMs = nowMs;
        p.subStateEnteredAtMs = nowMs;
        p.originalDisplayId = bot->GetDisplayId();
        p.originalHealthPct = bot->GetHealthPct();
        p.stealthCapable = (bot->getClass() == CLASS_ROGUE && bot->HasSpell(1784)) ||
                           (bot->getClass() == CLASS_DRUID && bot->HasSpell(5215));
        p.cautiousness = EncounterPersonality::Derive(GetPersonaForBot(bot->GetGUID())).cautious;
        return p;
    }

    void ResumeBrainStateForBot(Player* bot, ObjectGuid botGuid)
    {
        if (BrainState* bState = WorldBrainInternal::FindState(botGuid))
        {
            if (bState->pausedByEncounter)
            {
                bState->pausedByEncounter = false;
                if (bState->task.IsValid() && !bState->suspended)
                {
                    if (bot)
                    {
                        WorldExecutor::ResumeTask(bot, *bState);
                        if (bState->task.mapId == bot->GetMapId() &&
                            Dist2D(bot->GetPositionX(), bot->GetPositionY(), bState->task.x, bState->task.y) > 30.0f)
                        {
                            WorldBrainInternal::SetPhase(bot, *bState, TaskPhase::Recover, "resumed after encounter");
                        }
                    }
                    else
                    {
                        bState->task.Resume(NowMs());
                    }
                }
            }
        }
    }

    EncounterCleanupSnapshot BuildCleanupSnapshot(ObjectGuid botGuid, EncounterParticipant const* releasedPart = nullptr, uint64 closingEncounterId = 0)
    {
        EncounterCleanupSnapshot snap;
        snap.botGuid = botGuid;
        snap.hasEncounterOwnership = _ownership.IsOwned(botGuid);

        for (auto const& [id, inst] : _encounters)
        {
            if (id == closingEncounterId)
                continue;
            if (inst.FindParticipant(botGuid) != nullptr)
            {
                snap.inAnyEncounterParticipantList = true;
                break;
            }
        }

        if (releasedPart)
        {
            snap.moveTokenStillValid = releasedPart->moveToken.IsValid();
            snap.hasResidualEncounterStealth = releasedPart->stealthApplied;
            snap.hasResidualEncounterMorph = releasedPart->morphApplied;
            snap.hasResidualSitOrKneelState = releasedPart->sitApplied;
            snap.hasResidualTemporaryGameObject = !releasedPart->temporaryGobGuid.IsEmpty();
        }

        if (std::optional<MovementRequest> req = BotMovement::GetRequestSnapshot(botGuid))
        {
            if (req->goalId != 0 && req->owner == MoveOwner::Encounter)
                snap.hasEncounterNavRequest = true;
        }

        if (Player* bot = ObjectAccessor::FindConnectedPlayer(botGuid))
        {
            snap.currentMoveOwner = BotMovement::CurrentOwner(bot);
            if (releasedPart && releasedPart->originalDisplayId != 0 && bot->GetDisplayId() != releasedPart->originalDisplayId)
                snap.hasResidualEncounterMorph = true;
            if (releasedPart && releasedPart->originalFaction != 0 && bot->GetFaction() != releasedPart->originalFaction)
                snap.hasResidualFactionOverride = true;
            if (bot->getStandState() == UNIT_STAND_STATE_SIT || bot->getStandState() == UNIT_STAND_STATE_KNEEL)
            {
                if (releasedPart && releasedPart->sitApplied)
                    snap.hasResidualSitOrKneelState = true;
            }
        }

        if (BrainState const* bState = WorldBrainInternal::FindState(botGuid))
        {
            snap.pausedByEncounterFlag = bState->pausedByEncounter;
            if (bState->task.IsValid() && bState->task.paused &&
                !bState->suspended && !bState->pausedByEncounter && !bState->pausedBySocial && !bState->opportunityActive)
            {
                snap.worldTaskStuckPaused = true;
            }
        }

        return snap;
    }

    void FinalizeEncounter(EncounterInstance& inst, uint32 nowMs)
    {
        EncounterConfig const& cfg = EncounterSettings::Get();
        std::vector<ObjectGuid> botGuids;
        botGuids.reserve(inst.participants.size());

        for (auto& part : inst.participants)
        {
            botGuids.push_back(part.botGuid);
            Player* bot = ObjectAccessor::FindConnectedPlayer(part.botGuid);
            if (bot)
            {
                EncounterHandlers::RestoreParticipantState(bot, part);
                if (inst.type == EncounterType::RoadDuel)
                    bot->AttackStop();
            }
            else
            {
                // Bot logged out or despawned before finalization: clear offline state and any temporary GO
                BotMovement::ResetRequest(part.botGuid);
                part.moveToken = {};
                part.navGoalId = 0;
                part.assignedTargetGuid = ObjectGuid::Empty;
                part.engagedInCombat = false;
                part.stealthApplied = false;
                part.morphApplied = false;
                part.sitApplied = false;
                if (!part.temporaryGobGuid.IsEmpty())
                {
                    GameObject* gob = nullptr;
                    if (!inst.targetPlayerGuid.IsEmpty())
                    {
                        if (Player* tp = ObjectAccessor::FindConnectedPlayer(inst.targetPlayerGuid))
                            gob = ObjectAccessor::GetGameObject(*tp, part.temporaryGobGuid);
                    }
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
            _ownership.Release(part.botGuid, inst.id);
            ResumeBrainStateForBot(bot, part.botGuid);
        }

        _ownership.ReleaseAllForEncounter(inst.id);
        _budget.OnEncounterFinished(inst.type, inst.mapId, inst.targetPlayerGuid, botGuids, cfg, nowMs);
        if (inst.type == EncounterType::RareHunter && !inst.targetCreatureGuid.IsEmpty())
            _rareCreatureCooldownUntilMs[inst.targetCreatureGuid] = nowMs + cfg.rareScanIntervalMs * 2u;

        bool allCleanupPassed = true;
        std::string failureReasons;
        for (auto const& part : inst.participants)
        {
            EncounterCleanupSnapshot cSnap = BuildCleanupSnapshot(part.botGuid, &part, inst.id);
            EncounterCleanupVerdict verdict = EncounterCleanupValidator::Validate(cSnap);
            ++_metrics.cleanupChecks;
            if (verdict.passed)
            {
                ++_metrics.cleanupPasses;
            }
            else
            {
                ++_metrics.cleanupFailures;
                allCleanupPassed = false;
                if (!failureReasons.empty())
                    failureReasons += "; ";
                failureReasons += Acore::StringFormat("bot {} {}", part.botGuid.GetCounter(), verdict.summary);
            }
        }

        size_t typeIdx = static_cast<size_t>(inst.type);
        if (IsSuccessfulStopReason(inst.state.stopReason))
        {
            ++_metrics.totalCompleted;
            if (typeIdx < ENCOUNTER_TYPE_COUNT)
                ++_metrics.completedByType[typeIdx];
        }
        else if (inst.state.stopReason == EncounterStopReason::Unreachable ||
                 inst.state.stopReason == EncounterStopReason::Timeout)
        {
            ++_metrics.totalFailed;
            if (typeIdx < ENCOUNTER_TYPE_COUNT)
                ++_metrics.failedByType[typeIdx];
        }
        else
        {
            ++_metrics.totalCancelled;
            if (typeIdx < ENCOUNTER_TYPE_COUNT)
                ++_metrics.cancelledByType[typeIdx];
        }

        uint32 durationSec = (nowMs >= inst.state.createdAtMs) ? ((nowMs - inst.state.createdAtMs) / 1000u) : 0u;
        if (!allCleanupPassed)
        {
            LOG_ERROR("module.coa-playerbots.world",
                "Encounter #{} {} END reason={} duration={}s cleanup=FAIL details={}",
                inst.id,
                EncounterTypeName(inst.type),
                EncounterStopReasonName(inst.state.stopReason),
                durationSec,
                failureReasons);
        }
        else if (_debugLogging)
        {
            LOG_INFO("module.coa-playerbots.world",
                "Encounter #{} {} END reason={} duration={}s cleanup=PASS",
                inst.id,
                EncounterTypeName(inst.type),
                EncounterStopReasonName(inst.state.stopReason),
                durationSec);
        }

        inst.state.MarkFullyRetired(nowMs);
    }

    uint64 CommitEncounter(EncounterInstance&& inst, uint32 nowMs)
    {
        EncounterConfig const& cfg = EncounterSettings::Get();
        uint64 id = _nextEncounterId++;
        inst.id = id;

        uint32 actTimeout = (inst.type == EncounterType::Caravan || inst.type == EncounterType::RivalAdventuringParty)
            ? cfg.caravanActiveTimeoutMs
            : cfg.activeTimeoutMs;
        inst.state.Initialize(nowMs, cfg.prepareTimeoutMs, actTimeout, cfg.resolveTimeoutMs);
        inst.state.TransitionTo(EncounterPhase::Prepare, nowMs);

        for (auto const& p : inst.participants)
            _ownership.TryClaim(p.botGuid, id);

        _budget.OnEncounterStarted(inst.type, inst.mapId, inst.targetPlayerGuid, &cfg, nowMs);

        ++_metrics.totalCreated;
        size_t typeIdx = static_cast<size_t>(inst.type);
        if (typeIdx < ENCOUNTER_TYPE_COUNT)
            ++_metrics.createdByType[typeIdx];

        _encounters.emplace(id, std::move(inst));
        return id;
    }

    bool TryBuildPlayerEncounter(
        EncounterType type,
        Player* targetPlayer,
        std::vector<Player*> const& nearbyBots,
        bool bypassBudgetAndCooldown,
        uint32 nowMs,
        std::string& outReason)
    {
        EncounterConfig const& cfg = EncounterSettings::Get();
        if (!targetPlayer)
        {
            outReason = "No target player";
            return false;
        }

        TargetPlayerSnapshot playerSnap = BuildTargetPlayerSnapshot(targetPlayer);
        if (!bypassBudgetAndCooldown)
        {
            if (!_budget.CanCreateEncounter(type, playerSnap.mapId, playerSnap.guid, cfg, nowMs))
            {
                outReason = "Budget or cooldown active";
                return false;
            }
            if (!EncounterEligibility::IsPlayerEligibleForEncounter(type, playerSnap, cfg))
            {
                outReason = "Player not eligible in current zone/state";
                return false;
            }
        }
        else
        {
            if (!playerSnap.isOnlineAndInWorld || !playerSnap.isAlive)
            {
                outReason = "Player is offline or dead";
                return false;
            }
        }

        EncounterRole role = EncounterRole::Watcher;
        switch (type)
        {
            case EncounterType::AssassinContract: role = EncounterRole::Assassin; break;
            case EncounterType::TheWatcher:       role = EncounterRole::Watcher; break;
            case EncounterType::BountyHunter:     role = EncounterRole::BountyHunter; break;
            case EncounterType::Nemesis:          role = EncounterRole::NemesisRival; break;
            case EncounterType::MysteryEncounter: role = EncounterRole::MysteryStranger; break;
            default: break;
        }

        Player* bestBot = nullptr;
        float bestScore = -1.0f;
        Player* secondBestBot = nullptr;
        float secondBestScore = -1.0f;

        auto scoreCandidateForPlayer = [&](Player* bot, CandidateBotSnapshot const& botSnap) -> float
        {
            WorldPersona persona = GetPersonaForBot(bot->GetGUID());
            float score = 0.0f;
            if (type == EncounterType::AssassinContract)
            {
                score = EncounterPersonality::ScoreAssassinCandidate(persona, botSnap.classId, botSnap.hasStealthSpell);
            }
            else
            {
                score = EncounterPersonality::ScoreAffinity(type, role, persona, botSnap.hasStealthSpell);
            }
            score *= _memory.GetSelectionWeightMultiplier(playerSnap.guid, bot->GetGUID(), cfg, nowMs);
            return score;
        };

        for (Player* bot : nearbyBots)
        {
            if (bot == targetPlayer)
                continue;
            CandidateBotSnapshot botSnap = BuildCandidateBotSnapshot(
                bot, IsHostilePlayerEncounter(type) ? targetPlayer : nullptr);

            if (!bypassBudgetAndCooldown)
            {
                if (!EncounterEligibility::IsBotEligibleForPlayerEncounter(type, botSnap, playerSnap, cfg))
                    continue;
                if (type == EncounterType::Nemesis &&
                    !_memory.IsNemesisRival(playerSnap.guid, bot->GetGUID(), cfg, nowMs))
                {
                    continue;
                }
            }
            else
            {
                if (!EncounterEligibility::IsAvailableForEncounter(botSnap.activity))
                    continue;
                if (type == EncounterType::AssassinContract)
                {
                    // Strict Assassin Contract rules (§1): never relax level [playerLevel..playerLevel+3] or opposing faction
                    if (!EncounterEligibility::AreOpposingFactions(botSnap.teamId, playerSnap.teamId, true))
                        continue;
                    if (!EncounterEligibility::IsAssassinLevelCompatible(botSnap.level, playerSnap.level))
                        continue;
                }
                else if (IsHostilePlayerEncounter(type))
                {
                    if (!EncounterEligibility::AreOpposingFactions(botSnap.teamId, playerSnap.teamId, true))
                        continue;
                    if (EncounterEligibility::LevelDifference(botSnap.level, playerSnap.level) > cfg.maxLevelDiff)
                        continue;
                }
            }

            float score = scoreCandidateForPlayer(bot, botSnap);
            if (score > bestScore)
            {
                secondBestScore = bestScore;
                secondBestBot = bestBot;
                bestScore = score;
                bestBot = bot;
            }
            else if (score > secondBestScore)
            {
                secondBestScore = score;
                secondBestBot = bot;
            }
        }

        if (!bestBot && bypassBudgetAndCooldown)
        {
            for (Player* ob : sBotMgr->GetOnlineBots())
            {
                if (!ob || ob == targetPlayer || !ob->IsInWorld() || !ob->IsAlive() || (ob->GetMap() && ob->GetMap()->Instanceable()))
                    continue;
                CandidateBotSnapshot obSnap = BuildCandidateBotSnapshot(ob, IsHostilePlayerEncounter(type) ? targetPlayer : nullptr);
                if (!EncounterEligibility::IsAvailableForEncounter(obSnap.activity))
                    continue;

                if (type == EncounterType::AssassinContract)
                {
                    // Never relax Assassin level [playerLevel..playerLevel+3] or opposing faction filter (§1)
                    if (!EncounterEligibility::AreOpposingFactions(obSnap.teamId, playerSnap.teamId, true))
                        continue;
                    if (!EncounterEligibility::IsAssassinLevelCompatible(obSnap.level, playerSnap.level))
                        continue;
                }
                else if (IsHostilePlayerEncounter(type))
                {
                    if (!EncounterEligibility::AreOpposingFactions(obSnap.teamId, playerSnap.teamId, true))
                        continue;
                    if (EncounterEligibility::LevelDifference(obSnap.level, playerSnap.level) > cfg.maxLevelDiff)
                        continue;
                }

                float score = scoreCandidateForPlayer(ob, obSnap);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestBot = ob;
                }
            }
            if (bestBot)
            {
                float angle = targetPlayer->GetOrientation() + 3.14159f;
                float tx = targetPlayer->GetPositionX() + std::cos(angle) * 28.0f;
                float ty = targetPlayer->GetPositionY() + std::sin(angle) * 28.0f;
                float tz = targetPlayer->GetPositionZ();
                if (targetPlayer->GetMap())
                {
                    float gz = targetPlayer->GetMap()->GetHeight(targetPlayer->GetPhaseMask(), tx, ty, tz + 10.0f);
                    if (gz > INVALID_HEIGHT)
                        tz = gz;
                }
                bestBot->TeleportTo(targetPlayer->GetMapId(), tx, ty, tz, targetPlayer->GetOrientation());
                sBotMgr->QueueTeleportAck(bestBot->GetSession());
            }
        }

        if (!bestBot)
        {
            if (type == EncounterType::AssassinContract)
            {
                outReason = Acore::StringFormat(
                    "AssassinContract build failed: no available opposing-faction bot in level range [{}-{}]",
                    uint32(playerSnap.level), uint32(playerSnap.level) + 3u);
            }
            else
            {
                outReason = "No eligible bot found nearby";
            }
            return false;
        }

        if (type == EncounterType::BountyHunter)
            _memory.ConsumeBountyForAttempt(playerSnap.guid);
        if (IsHostilePlayerEncounter(type))
            _memory.RecordEncounterConflict(bestBot->GetGUID(), playerSnap.guid, cfg, nowMs);

        EncounterInstance inst;
        inst.type = type;
        inst.isForcedTest = bypassBudgetAndCooldown;
        inst.mapId = targetPlayer->GetMapId();
        inst.zoneId = targetPlayer->GetZoneId();
        inst.areaId = targetPlayer->GetAreaId();
        inst.centerX = targetPlayer->GetPositionX();
        inst.centerY = targetPlayer->GetPositionY();
        inst.centerZ = targetPlayer->GetPositionZ();
        inst.targetPlayerGuid = targetPlayer->GetGUID();
        if (type == EncounterType::Nemesis)
        {
            inst.subStage = _memory.GetGrudgeScore(playerSnap.guid, bestBot->GetGUID(), cfg, nowMs);
        }

        EncounterParticipant leadPart = MakeParticipant(bestBot, role, nowMs);
        if (IsHostilePlayerEncounter(type) && bypassBudgetAndCooldown)
        {
            bestBot->SetPvP(true);
            targetPlayer->SetPvP(true);
        }
        inst.participants.push_back(leadPart);

        // Bounty Hunter supports 1-2 coordinated hunters when a second eligible opposing bot is nearby
        if (type == EncounterType::BountyHunter && secondBestBot && secondBestBot != bestBot &&
            secondBestBot->GetTeamId() == bestBot->GetTeamId() &&
            Dist2D(bestBot->GetPositionX(), bestBot->GetPositionY(), secondBestBot->GetPositionX(), secondBestBot->GetPositionY()) <= 95.0f)
        {
            _memory.RecordEncounterConflict(secondBestBot->GetGUID(), playerSnap.guid, cfg, nowMs);
            EncounterParticipant secPart = MakeParticipant(secondBestBot, role, nowMs);
            if (bypassBudgetAndCooldown)
                secondBestBot->SetPvP(true);
            inst.participants.push_back(secPart);
        }

        int32 levelDelta = static_cast<int32>(bestBot->GetLevel()) - static_cast<int32>(playerSnap.level);
        char const* classPref = EncounterPersonality::GetAssassinClassPreferenceLabel(bestBot->getClass(), leadPart.stealthCapable);
        uint32 botCount = uint32(inst.participants.size());
        std::string leadName = bestBot->GetName();
        uint8 leadClass = bestBot->getClass();
        uint8 leadLevel = bestBot->GetLevel();
        uint32 leadTeam = uint32(bestBot->GetTeamId());

        uint64 id = CommitEncounter(std::move(inst), nowMs);

        if (_debugLogging || bypassBudgetAndCooldown)
        {
            LOG_INFO("module.coa-playerbots.world",
                "Encounter #{} START type={} targetPlayer={}({}) level={} leadBot={}({}) class={} level={} levelDelta={:+} faction={} score={:.2f} classPreference={} stealthCapable={}",
                id,
                EncounterTypeName(type),
                targetPlayer->GetName(),
                targetPlayer->GetGUID().GetCounter(),
                uint32(playerSnap.level),
                leadName,
                bestBot->GetGUID().GetCounter(),
                uint32(leadClass),
                uint32(leadLevel),
                levelDelta,
                leadTeam == TEAM_ALLIANCE ? "Alliance" : "Horde",
                bestScore,
                classPref,
                leadPart.stealthCapable ? "yes" : "no");
        }

        outReason = Acore::StringFormat(
            "Started {} (#{}) with {} bot(s) (lead: {} lvl={} delta={:+} class={} pref={})",
            EncounterTypeName(type), id, botCount, leadName, uint32(leadLevel), levelDelta, uint32(leadClass), classPref);
        return true;
    }

    bool TryBuildWorldEncounter(
        EncounterType type,
        Player* anchorUnit,
        std::vector<Player*> const& nearbyBots,
        bool bypassBudgetAndCooldown,
        uint32 nowMs,
        std::string& outReason)
    {
        EncounterConfig const& cfg = EncounterSettings::Get();
        if (!anchorUnit || nearbyBots.empty())
        {
            outReason = "No anchor or nearby bots";
            return false;
        }

        uint32 mapId = anchorUnit->GetMapId();
        ObjectGuid optionalPlayerGuid = IsRealPlayer(anchorUnit) ? anchorUnit->GetGUID() : ObjectGuid::Empty;

        if (!bypassBudgetAndCooldown && !_budget.CanCreateEncounter(type, mapId, optionalPlayerGuid, cfg, nowMs))
        {
            outReason = "Budget or cooldown active";
            return false;
        }

        // Collect free eligible bots near the anchor
        std::vector<Player*> freeBots;
        for (Player* b : nearbyBots)
        {
            CandidateBotSnapshot snap = BuildCandidateBotSnapshot(b);
            if (!bypassBudgetAndCooldown)
            {
                if (!EncounterEligibility::IsBotEligibleForWorldEncounter(
                        type, snap, mapId, anchorUnit->GetPositionX(), anchorUnit->GetPositionY(), anchorUnit->GetLevel(), cfg))
                    continue;
            }
            else
            {
                if (!b->IsInWorld() || !b->IsAlive() || (b->GetMap() && b->GetMap()->Instanceable()))
                    continue;
            }
            freeBots.push_back(b);
        }

        if (bypassBudgetAndCooldown && freeBots.size() < 2)
        {
            for (Player* ob : sBotMgr->GetOnlineBots())
            {
                if (!ob || !ob->IsInWorld() || !ob->IsAlive() || (ob->GetMap() && ob->GetMap()->Instanceable()))
                    continue;
                if (_ownership.IsOwned(ob->GetGUID()))
                    continue;
                if (std::find(freeBots.begin(), freeBots.end(), ob) == freeBots.end())
                    freeBots.push_back(ob);
                if (freeBots.size() >= 2)
                    break;
            }
            for (Player* fb : freeBots)
            {
                if (fb->GetMapId() != mapId || Dist2D(fb, anchorUnit) > 35.0f)
                {
                    fb->TeleportTo(mapId, anchorUnit->GetPositionX() + 6.0f, anchorUnit->GetPositionY() + 6.0f, anchorUnit->GetPositionZ(), 0.0f);
                    sBotMgr->QueueTeleportAck(fb->GetSession());
                }
            }
        }

        if (freeBots.empty())
        {
            outReason = "No free eligible bots nearby";
            return false;
        }

        // 7.9 Deserter (1 bot)
        if (type == EncounterType::Deserter)
        {
            Player* chosen = freeBots.front();
            EncounterInstance inst;
            inst.type = EncounterType::Deserter;
            inst.isForcedTest = bypassBudgetAndCooldown;
            inst.mapId = mapId;
            inst.zoneId = chosen->GetZoneId();
            inst.areaId = chosen->GetAreaId();
            inst.targetPlayerGuid = optionalPlayerGuid;
            inst.centerX = chosen->GetPositionX();
            inst.centerY = chosen->GetPositionY();
            inst.centerZ = chosen->GetPositionZ();
            float angle = float(NextRand() % 628) / 100.0f;
            inst.destX = chosen->GetPositionX() + std::cos(angle) * 18.0f;
            inst.destY = chosen->GetPositionY() + std::sin(angle) * 18.0f;
            inst.destZ = chosen->GetPositionZ();
            if (chosen->GetMap())
            {
                float gz = chosen->GetMap()->GetHeight(chosen->GetPhaseMask(), inst.destX, inst.destY, inst.destZ + 10.0f);
                if (gz > INVALID_HEIGHT)
                    inst.destZ = gz;
            }
            inst.participants.push_back(MakeParticipant(chosen, EncounterRole::Deserter, nowMs));
            uint64 id = CommitEncounter(std::move(inst), nowMs);
            outReason = Acore::StringFormat("Started deserter (#{}) with bot {}", id, chosen->GetName());
            return true;
        }

        // 7.7 Road Duel (2 friendly bots)
        if (type == EncounterType::RoadDuel)
        {
            for (size_t i = 0; i < freeBots.size(); ++i)
            {
                CandidateBotSnapshot snapA = BuildCandidateBotSnapshot(freeBots[i]);
                for (size_t j = i + 1; j < freeBots.size(); ++j)
                {
                    CandidateBotSnapshot snapB = BuildCandidateBotSnapshot(freeBots[j]);
                    if (!EncounterEligibility::AreBotsCompatibleForPair(EncounterType::RoadDuel, snapA, snapB, cfg, bypassBudgetAndCooldown))
                        continue;

                    EncounterInstance inst;
                    inst.type = EncounterType::RoadDuel;
                    inst.isForcedTest = bypassBudgetAndCooldown;
                    inst.mapId = mapId;
                    inst.zoneId = freeBots[i]->GetZoneId();
                    inst.areaId = freeBots[i]->GetAreaId();
                    inst.centerX = (freeBots[i]->GetPositionX() + freeBots[j]->GetPositionX()) * 0.5f;
                    inst.centerY = (freeBots[i]->GetPositionY() + freeBots[j]->GetPositionY()) * 0.5f;
                    inst.centerZ = freeBots[i]->GetPositionZ();
                    inst.participants.push_back(MakeParticipant(freeBots[i], EncounterRole::DuelistA, nowMs));
                    inst.participants.push_back(MakeParticipant(freeBots[j], EncounterRole::DuelistB, nowMs));
                    uint64 id = CommitEncounter(std::move(inst), nowMs);
                    outReason = Acore::StringFormat("Started road duel (#{}) between {} and {}",
                        id, freeBots[i]->GetName(), freeBots[j]->GetName());
                    return true;
                }
            }
            outReason = "Need 2 compatible friendly bots for Road Duel";
            return false;
        }

        // 7.10 Hunter and Prey (2 opposing bots)
        if (type == EncounterType::HunterAndPrey)
        {
            for (size_t i = 0; i < freeBots.size(); ++i)
            {
                for (size_t j = i + 1; j < freeBots.size(); ++j)
                {
                    CandidateBotSnapshot snapA = BuildCandidateBotSnapshot(freeBots[i], freeBots[j]);
                    CandidateBotSnapshot snapB = BuildCandidateBotSnapshot(freeBots[j], freeBots[i]);
                    if (!EncounterEligibility::AreBotsCompatibleForPair(EncounterType::HunterAndPrey, snapA, snapB, cfg, bypassBudgetAndCooldown))
                        continue;

                    EncounterInstance inst;
                    inst.type = EncounterType::HunterAndPrey;
                    inst.isForcedTest = bypassBudgetAndCooldown;
                    inst.mapId = mapId;
                    inst.zoneId = freeBots[i]->GetZoneId();
                    inst.areaId = freeBots[i]->GetAreaId();
                    inst.centerX = freeBots[i]->GetPositionX();
                    inst.centerY = freeBots[i]->GetPositionY();
                    inst.centerZ = freeBots[i]->GetPositionZ();
                    inst.participants.push_back(MakeParticipant(freeBots[i], EncounterRole::Hunter, nowMs));
                    inst.participants.push_back(MakeParticipant(freeBots[j], EncounterRole::Prey, nowMs));
                    uint64 id = CommitEncounter(std::move(inst), nowMs);
                    outReason = Acore::StringFormat("Started hunter_prey (#{}) between {} and {}",
                        id, freeBots[i]->GetName(), freeBots[j]->GetName());
                    return true;
                }
            }
            outReason = "Need 2 compatible opposing bots for Hunter and Prey";
            return false;
        }

        // 7.8 Caravan or 7.11 Rival Adventuring Party (3-6 / 3-5 same-faction bots)
        if (type == EncounterType::Caravan || type == EncounterType::RivalAdventuringParty)
        {
            uint32 minCount = (type == EncounterType::Caravan) ? cfg.caravanMinMembers : cfg.rivalPartyMinMembers;
            uint32 maxCount = (type == EncounterType::Caravan) ? cfg.caravanMaxMembers : cfg.rivalPartyMaxMembers;
            if (bypassBudgetAndCooldown)
                minCount = std::min<uint32>(minCount, 2u);

            std::vector<Player*> allianceBots;
            std::vector<Player*> hordeBots;
            for (Player* b : freeBots)
            {
                if (b->GetTeamId() == TEAM_ALLIANCE)
                    allianceBots.push_back(b);
                else
                    hordeBots.push_back(b);
            }

            std::vector<Player*> const& pool = (allianceBots.size() >= hordeBots.size()) ? allianceBots : hordeBots;
            if (pool.size() < minCount)
            {
                outReason = "Not enough same-faction bots nearby for party/caravan";
                return false;
            }

            Player* leader = pool.front();
            EncounterInstance inst;
            inst.type = type;
            inst.mapId = mapId;
            inst.zoneId = leader->GetZoneId();
            inst.areaId = leader->GetAreaId();
            inst.targetPlayerGuid = optionalPlayerGuid;
            inst.centerX = leader->GetPositionX();
            inst.centerY = leader->GetPositionY();
            inst.centerZ = leader->GetPositionZ();

            // Choose a destination hub or POI ~90..220 yards away
            float angle = float(NextRand() % 628) / 100.0f;
            inst.destX = leader->GetPositionX() + std::cos(angle) * 110.0f;
            inst.destY = leader->GetPositionY() + std::sin(angle) * 110.0f;
            inst.destZ = leader->GetPositionZ();
            std::vector<Poi const*> vendorPois;
            BotWorldPoi::Query(mapId, leader->GetPositionX(), leader->GetPositionY(), 450.0f, PoiKind::Vendor, vendorPois);
            for (Poi const* poi : vendorPois)
            {
                if (!poi)
                    continue;
                float d = Dist2D(leader->GetPositionX(), leader->GetPositionY(), poi->x, poi->y);
                if (d >= 70.0f && d <= 450.0f)
                {
                    inst.destX = poi->x;
                    inst.destY = poi->y;
                    inst.destZ = poi->z;
                    break;
                }
            }
            inst.waypoints.push_back({inst.destX, inst.destY, inst.destZ});

            EncounterRole leadRole = (type == EncounterType::Caravan) ? EncounterRole::CaravanLeader : EncounterRole::RivalLeader;
            EncounterRole memRole = (type == EncounterType::Caravan) ? EncounterRole::CaravanMember : EncounterRole::RivalMember;

            uint32 count = std::min<uint32>(uint32(pool.size()), maxCount);
            for (uint32 i = 0; i < count; ++i)
            {
                inst.participants.push_back(MakeParticipant(pool[i], i == 0 ? leadRole : memRole, nowMs));
                if (!optionalPlayerGuid.IsEmpty() && _memory.GetGrudgeScore(optionalPlayerGuid, pool[i]->GetGUID(), cfg, nowMs) > 0)
                    inst.subStage = 1;
            }

            uint64 id = CommitEncounter(std::move(inst), nowMs);
            outReason = Acore::StringFormat("Started {} (#{}) with {} bots", EncounterTypeName(type), id, count);
            return true;
        }

        // 7.3 Faction Skirmish or 7.4 Patrol Ambush (Alliance vs Horde)
        if (type == EncounterType::FactionSkirmish || type == EncounterType::PatrolAmbush)
        {
            uint32 minSide = bypassBudgetAndCooldown ? 1u : cfg.skirmishMinPerSide;
            uint32 maxSide = cfg.skirmishMaxPerSide;

            std::vector<Player*> allianceBots;
            std::vector<Player*> hordeBots;
            for (Player* b : freeBots)
            {
                if (b->GetTeamId() == TEAM_ALLIANCE)
                    allianceBots.push_back(b);
                else
                    hordeBots.push_back(b);
            }

            if (allianceBots.size() < minSide || hordeBots.size() < minSide)
            {
                outReason = "Not enough opposing faction bots nearby for skirmish/ambush";
                return false;
            }

            EncounterInstance inst;
            inst.type = type;
            inst.mapId = mapId;
            inst.zoneId = anchorUnit->GetZoneId();
            inst.areaId = anchorUnit->GetAreaId();
            inst.centerX = (allianceBots[0]->GetPositionX() + hordeBots[0]->GetPositionX()) * 0.5f;
            inst.centerY = (allianceBots[0]->GetPositionY() + hordeBots[0]->GetPositionY()) * 0.5f;
            inst.centerZ = allianceBots[0]->GetPositionZ();
            inst.destX = inst.centerX + 18.0f;
            inst.destY = inst.centerY + 18.0f;
            inst.destZ = inst.centerZ;
            if (Map* map = allianceBots[0]->GetMap())
            {
                float cz = map->GetHeight(allianceBots[0]->GetPhaseMask(), inst.centerX, inst.centerY, inst.centerZ + 12.0f);
                if (cz > INVALID_HEIGHT)
                    inst.centerZ = cz;
                float dz = map->GetHeight(allianceBots[0]->GetPhaseMask(), inst.destX, inst.destY, inst.destZ + 12.0f);
                if (dz > INVALID_HEIGHT)
                    inst.destZ = dz;
            }

            uint32 aCount = std::min<uint32>(uint32(allianceBots.size()), maxSide);
            uint32 hCount = std::min<uint32>(uint32(hordeBots.size()), maxSide);

            for (uint32 i = 0; i < aCount; ++i)
            {
                EncounterRole r = (type == EncounterType::PatrolAmbush)
                    ? (i == 0 ? EncounterRole::PatrolLeader : EncounterRole::PatrolMember)
                    : EncounterRole::SkirmisherAlliance;
                inst.participants.push_back(MakeParticipant(allianceBots[i], r, nowMs));
            }
            for (uint32 i = 0; i < hCount; ++i)
            {
                EncounterRole r = (type == EncounterType::PatrolAmbush)
                    ? EncounterRole::Ambusher
                    : EncounterRole::SkirmisherHorde;
                inst.participants.push_back(MakeParticipant(hordeBots[i], r, nowMs));
            }

            uint64 id = CommitEncounter(std::move(inst), nowMs);
            outReason = Acore::StringFormat("Started {} (#{}) ({}v{})", EncounterTypeName(type), id, aCount, hCount);
            return true;
        }

        // 7.12 Rare Hunter
        if (type == EncounterType::RareHunter)
        {
            Creature* rare = FindNearbyLiveRareCreature(anchorUnit, cfg.searchRadiusYards);
            if (!rare)
            {
                outReason = "No live rare creature within search radius";
                return false;
            }

            EncounterInstance inst;
            inst.type = EncounterType::RareHunter;
            inst.mapId = mapId;
            inst.zoneId = rare->GetZoneId();
            inst.areaId = rare->GetAreaId();
            inst.centerX = rare->GetPositionX();
            inst.centerY = rare->GetPositionY();
            inst.centerZ = rare->GetPositionZ();
            inst.targetCreatureGuid = rare->GetGUID();
            inst.targetCreatureEntry = rare->GetEntry();

            uint32 count = std::min<uint32>(uint32(freeBots.size()), cfg.rareHunterMaxBots);
            for (uint32 i = 0; i < count; ++i)
                inst.participants.push_back(MakeParticipant(freeBots[i], EncounterRole::RareHunter, nowMs));

            uint64 id = CommitEncounter(std::move(inst), nowMs);
            outReason = Acore::StringFormat("Started rare_hunter (#{}) targeting '{}' ({}) with {} bots",
                id, rare->GetName(), rare->GetEntry(), count);
            return true;
        }

        outReason = "Unsupported world encounter type";
        return false;
    }

    void EvaluateActiveEncounters(uint32 nowMs)
    {
        EncounterConfig const& cfg = EncounterSettings::Get();

        for (auto it = _encounters.begin(); it != _encounters.end();)
        {
            EncounterInstance& inst = it->second;

            // 1. Check phase timeout
            if (inst.state.IsRunning() && inst.state.IsPhaseTimedOut(nowMs))
            {
                EncounterStopReason timeoutReason = (inst.state.phase == EncounterPhase::Resolve)
                    ? EncounterStopReason::Completed
                    : EncounterStopReason::Timeout;
                inst.state.Terminate(timeoutReason, nowMs);
            }

            // 2. Check target player ongoing eligibility for player-targeted encounters
            if (inst.state.IsRunning() && IsPlayerTargetedEncounter(inst.type) && !inst.targetPlayerGuid.IsEmpty())
            {
                Player* targetPlayer = ObjectAccessor::FindConnectedPlayer(inst.targetPlayerGuid);
                TargetPlayerSnapshot pSnap = BuildTargetPlayerSnapshot(targetPlayer);

                float minDist = 0.0f;
                if (targetPlayer && !inst.participants.empty())
                {
                    if (Player* firstBot = ObjectAccessor::FindConnectedPlayer(inst.participants.front().botGuid))
                        minDist = Dist2D(firstBot->GetPositionX(), firstBot->GetPositionY(), pSnap.x, pSnap.y);
                }

                EncounterStopReason pReason = EncounterEligibility::CheckOngoingPlayerEligibility(
                    inst.type, pSnap, inst.mapId, minDist, cfg);
                if (pReason != EncounterStopReason::None)
                    inst.state.Terminate(pReason, nowMs);
            }

            // 3. Check participant preemption / disappearance
            if (inst.state.IsRunning())
            {
                size_t validCount = 0;
                for (auto& part : inst.participants)
                {
                    if (part.retreated)
                        continue;

                    Player* bot = ObjectAccessor::FindConnectedPlayer(part.botGuid);
                    BotActivitySnapshot bSnap = BuildBotActivitySnapshot(bot);
                    uint32 curMap = bot ? bot->GetMapId() : 0;

                    EncounterStopReason bReason = EncounterPriorityArbiter::CheckMidEncounterPreemption(
                        bSnap, inst.mapId, curMap);
                    if (bReason != EncounterStopReason::None)
                    {
                        if (inst.participants.size() <= 2)
                        {
                            inst.state.Terminate(bReason, nowMs);
                            break;
                        }
                        else
                        {
                            part.retreated = true;
                            if (bot)
                                EncounterHandlers::RestoreParticipantState(bot, part);
                            _ownership.Release(part.botGuid, inst.id);
                            ResumeBrainStateForBot(bot, part.botGuid);
                            continue;
                        }
                    }
                    ++validCount;
                }

                if (inst.state.IsRunning() && validCount == 0)
                    inst.state.Terminate(EncounterStopReason::BotLogoutOrDespawn, nowMs);
            }

            // 4. Finalize & erase completed/terminated instances
            if (!inst.state.IsRunning())
            {
                FinalizeEncounter(inst, nowMs);
                it = _encounters.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void EvaluateCandidateGeneration(uint32 nowMs, bool checkRareScan)
    {
        EncounterConfig const& cfg = EncounterSettings::Get();
        if (!cfg.enabled || _budget.GetGlobalActive() >= cfg.maxGlobalActive)
            return;

        std::vector<Player*> realPlayers;
        std::vector<Player*> openWorldBots;

        auto const& allPlayers = ObjectAccessor::GetPlayers();
        realPlayers.reserve(16);
        openWorldBots.reserve(64);

        for (auto const& [guid, p] : allPlayers)
        {
            if (!p || !p->IsInWorld() || !p->IsAlive())
                continue;
            if (p->GetMap() && p->GetMap()->Instanceable())
                continue;
            if (IsRealPlayer(p))
                realPlayers.push_back(p);
            else if (IsBotPlayer(p) && !_ownership.IsOwned(guid))
                openWorldBots.push_back(p);
        }

        // 1. Sample 1 real player per think cycle for personal / nearby encounters
        if (!realPlayers.empty())
        {
            Player* targetPlayer = realPlayers[_playerSampleCursor % realPlayers.size()];
            ++_playerSampleCursor;

            std::vector<Player*> nearbyBots;
            GatherNearbyCandidateBots(targetPlayer, cfg.searchRadiusYards, nearbyBots);

            if (!nearbyBots.empty())
            {
                std::string dummyMsg;

                // Priority 1: Bounty Hunter if player exceeded PvP notoriety threshold
                if (cfg.IsTypeEnabled(EncounterType::BountyHunter) &&
                    _memory.IsBountyThresholdMet(targetPlayer->GetGUID(), cfg, nowMs) &&
                    !_budget.IsPlayerOnBountyCooldown(targetPlayer->GetGUID(), nowMs))
                {
                    if (TryBuildPlayerEncounter(EncounterType::BountyHunter, targetPlayer, nearbyBots, false, nowMs, dummyMsg))
                        return;
                }

                // Priority 2: Rare Mystery Encounter roll
                if (cfg.IsTypeEnabled(EncounterType::MysteryEncounter) &&
                    (NextRand() % 1000u) < cfg.mysteryChancePermil)
                {
                    if (TryBuildPlayerEncounter(EncounterType::MysteryEncounter, targetPlayer, nearbyBots, false, nowMs, dummyMsg))
                        return;
                }

                // Priority 3: Nemesis encounter if a nearby bot holds a grudge (probabilistic 35% trigger)
                if (cfg.IsTypeEnabled(EncounterType::Nemesis) && ((NextRand() % 100u) < 35u))
                {
                    if (TryBuildPlayerEncounter(EncounterType::Nemesis, targetPlayer, nearbyBots, false, nowMs, dummyMsg))
                        return;
                }

                // Priority 4: Roll among Assassin, Watcher, or nearby ambient encounter
                uint32 roll = NextRand() % 100u;
                if (roll < 18u)
                {
                    TryBuildPlayerEncounter(EncounterType::AssassinContract, targetPlayer, nearbyBots, false, nowMs, dummyMsg);
                    return;
                }
                else if (roll < 32u)
                {
                    TryBuildPlayerEncounter(EncounterType::TheWatcher, targetPlayer, nearbyBots, false, nowMs, dummyMsg);
                    return;
                }
            }
        }

        // 2. Sample 1 open-world anchor (real player or bot) for world/ambient encounters
        Player* anchor = nullptr;
        if (!realPlayers.empty() && (NextRand() % 2 == 0))
            anchor = realPlayers[NextRand() % realPlayers.size()];
        else if (!openWorldBots.empty())
        {
            anchor = openWorldBots[_botSampleCursor % openWorldBots.size()];
            ++_botSampleCursor;
        }

        if (!anchor)
            return;

        std::vector<Player*> nearbyBots;
        GatherNearbyCandidateBots(anchor, cfg.searchRadiusYards, nearbyBots);
        if (nearbyBots.empty())
            return;

        std::string dummyMsg;
        if (checkRareScan && cfg.IsTypeEnabled(EncounterType::RareHunter))
        {
            if (TryBuildWorldEncounter(EncounterType::RareHunter, anchor, nearbyBots, false, nowMs, dummyMsg))
                return;
        }

        static constexpr EncounterType WORLD_POOL[] = {
            EncounterType::RoadDuel,
            EncounterType::Caravan,
            EncounterType::Deserter,
            EncounterType::RivalAdventuringParty,
            EncounterType::HunterAndPrey,
            EncounterType::FactionSkirmish,
            EncounterType::PatrolAmbush
        };

        EncounterType chosenType = WORLD_POOL[NextRand() % (sizeof(WORLD_POOL) / sizeof(WORLD_POOL[0]))];
        TryBuildWorldEncounter(chosenType, anchor, nearbyBots, false, nowMs, dummyMsg);
    }

    void DispatchEncounterHandler(EncounterInstance& inst, Player* bot, EncounterParticipant& part, uint32 nowMs)
    {
        switch (inst.type)
        {
            case EncounterType::AssassinContract:
                EncounterHandlers::UpdateAssassinContract(inst, bot, part, nowMs);
                break;
            case EncounterType::TheWatcher:
                EncounterHandlers::UpdateTheWatcher(inst, bot, part, nowMs);
                break;
            case EncounterType::FactionSkirmish:
                EncounterHandlers::UpdateFactionSkirmish(inst, bot, part, nowMs);
                break;
            case EncounterType::PatrolAmbush:
                EncounterHandlers::UpdatePatrolAmbush(inst, bot, part, nowMs);
                break;
            case EncounterType::BountyHunter:
                EncounterHandlers::UpdateBountyHunter(inst, bot, part, nowMs);
                break;
            case EncounterType::Nemesis:
                EncounterHandlers::UpdateNemesis(inst, bot, part, nowMs);
                break;
            case EncounterType::RoadDuel:
                EncounterHandlers::UpdateRoadDuel(inst, bot, part, nowMs);
                break;
            case EncounterType::Caravan:
                EncounterHandlers::UpdateCaravan(inst, bot, part, nowMs);
                break;
            case EncounterType::Deserter:
                EncounterHandlers::UpdateDeserter(inst, bot, part, nowMs);
                break;
            case EncounterType::HunterAndPrey:
                EncounterHandlers::UpdateHunterAndPrey(inst, bot, part, nowMs);
                break;
            case EncounterType::RivalAdventuringParty:
                EncounterHandlers::UpdateRivalParty(inst, bot, part, nowMs);
                break;
            case EncounterType::RareHunter:
                EncounterHandlers::UpdateRareHunter(inst, bot, part, nowMs);
                break;
            case EncounterType::MysteryEncounter:
                EncounterHandlers::UpdateMysteryEncounter(inst, bot, part, nowMs);
                break;
            default:
                inst.state.Terminate(EncounterStopReason::Completed, nowMs);
                break;
        }
    }

    void TickInCombatEncounterParticipants(uint32 nowMs)
    {
        uint32 activeTempGos = 0;
        for (auto it = _encounters.begin(); it != _encounters.end();)
        {
            EncounterInstance& inst = it->second;
            for (auto& part : inst.participants)
            {
                if (!part.temporaryGobGuid.IsEmpty())
                    ++activeTempGos;
                if (!inst.state.IsRunning() || part.retreated)
                    continue;
                Player* bot = ObjectAccessor::FindConnectedPlayer(part.botGuid);
                if (bot && bot->IsInWorld() && bot->IsAlive() && bot->IsInCombat())
                    DispatchEncounterHandler(inst, bot, part, nowMs);
            }

            if (!inst.state.IsRunning())
            {
                FinalizeEncounter(inst, nowMs);
                it = _encounters.erase(it);
            }
            else
            {
                ++it;
            }
        }
        _metrics.activeTemporaryGameObjects = activeTempGos;
    }
}

namespace WorldEncounterDirector
{
    void LoadConfig()
    {
        EncounterSettings::LoadFromConfig();
    }

    void Update(uint32 diff)
    {
        EncounterConfig const& cfg = EncounterSettings::Get();
        _evalAccumMs += diff;
        _combatTickAccumMs += diff;
        _rareScanAccumMs += diff;

        // Tick in-combat encounter participants every 500ms because UpdateSoloWorld (and thus
        // WorldBrain::Update -> UpdateBot) is bypassed while bot->IsInCombat() is true.
        if (_combatTickAccumMs >= 500u)
        {
            _combatTickAccumMs = 0;
            if (!_encounters.empty())
                TickInCombatEncounterParticipants(NowMs());
            else
                _metrics.activeTemporaryGameObjects = 0;
        }

        if (_evalAccumMs < cfg.thinkIntervalMs)
            return;

        auto t0 = std::chrono::steady_clock::now();
        uint32 nowMs = NowMs();
        if (_metrics.lastEvaluationMs != 0 && nowMs > _metrics.lastEvaluationMs)
        {
            uint32 dt = nowMs - _metrics.lastEvaluationMs;
            _metrics.avgEvalIntervalMs = (_metrics.avgEvalIntervalMs == 0)
                ? dt
                : (_metrics.avgEvalIntervalMs * 7u + dt) / 8u;
        }
        _metrics.lastEvaluationMs = nowMs;
        ++_metrics.directorEvaluations;
        _evalAccumMs = 0;

        bool checkRareScan = false;
        if (_rareScanAccumMs >= cfg.rareScanIntervalMs)
        {
            _rareScanAccumMs = 0;
            checkRareScan = true;
        }

        EvaluateActiveEncounters(nowMs);
        EvaluateCandidateGeneration(nowMs, checkRareScan);

        auto t1 = std::chrono::steady_clock::now();
        uint32 elapsedUs = static_cast<uint32>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        _metrics.lastUpdateDurationUs = elapsedUs;
        _metrics.avgUpdateDurationUs = (_metrics.avgUpdateDurationUs == 0)
            ? elapsedUs
            : (_metrics.avgUpdateDurationUs * 7u + elapsedUs) / 8u;
        if (elapsedUs > _metrics.peakUpdateDurationUs)
            _metrics.peakUpdateDurationUs = elapsedUs;
    }

    bool UpdateBot(Player* bot, BrainState& brainState, WorldPersona const& persona, uint32 diff)
    {
        (void)diff;
        if (!bot)
            return false;

        _knownPersonas[bot->GetGUID()] = persona;
        if (brainState.socialActive)
            _knownSocialActive.insert(bot->GetGUID());
        else
            _knownSocialActive.erase(bot->GetGUID());

        uint64 encId = _ownership.GetEncounterId(bot->GetGUID());
        if (encId == 0)
        {
            if (brainState.pausedByEncounter)
            {
                brainState.pausedByEncounter = false;
                if (brainState.task.IsValid())
                    WorldExecutor::ResumeTask(bot, brainState);
            }
            return false;
        }

        auto it = _encounters.find(encId);
        if (it == _encounters.end() || !it->second.state.IsRunning())
        {
            _ownership.Release(bot->GetGUID(), encId);
            if (brainState.pausedByEncounter)
            {
                brainState.pausedByEncounter = false;
                if (brainState.task.IsValid())
                    WorldExecutor::ResumeTask(bot, brainState);
            }
            return false;
        }

        EncounterInstance& inst = it->second;
        EncounterParticipant* part = inst.FindParticipant(bot->GetGUID());
        if (!part || part->retreated)
        {
            _ownership.Release(bot->GetGUID(), encId);
            if (brainState.pausedByEncounter)
            {
                brainState.pausedByEncounter = false;
                if (brainState.task.IsValid())
                    WorldExecutor::ResumeTask(bot, brainState);
            }
            return false;
        }

        // Pause the bot's underlying WorldBrain task while the encounter borrows the bot
        if (brainState.task.IsValid() && !brainState.pausedByEncounter)
        {
            WorldExecutor::PauseTask(bot, brainState, "dynamic encounter");
            brainState.pausedByEncounter = true;
        }

        uint32 nowMs = NowMs();
        DispatchEncounterHandler(inst, bot, *part, nowMs);

        if (!inst.state.IsRunning())
        {
            FinalizeEncounter(inst, nowMs);
            _encounters.erase(it);
            if (brainState.pausedByEncounter)
            {
                brainState.pausedByEncounter = false;
                if (brainState.task.IsValid())
                    WorldExecutor::ResumeTask(bot, brainState);
            }
            return false;
        }

        return true;
    }

    bool IsBotInEncounter(ObjectGuid botGuid)
    {
        return _ownership.IsOwned(botGuid);
    }

    void OnBotSuspended(Player* bot, char const* /*reason*/)
    {
        if (!bot)
            return;
        uint64 encId = _ownership.GetEncounterId(bot->GetGUID());
        if (encId == 0)
            return;

        auto it = _encounters.find(encId);
        if (it != _encounters.end())
        {
            uint32 nowMs = NowMs();
            it->second.state.Terminate(EncounterStopReason::JoinedPlayerGroup, nowMs);
            FinalizeEncounter(it->second, nowMs);
            _encounters.erase(it);
        }
        else
        {
            _ownership.Release(bot->GetGUID());
        }
    }

    void OnParticipantDeath(Unit* victim, Unit* /*killer*/)
    {
        if (!victim)
            return;

        ObjectGuid vGuid = victim->GetGUID();
        uint32 nowMs = NowMs();

        // Check if victim is an owned bot
        uint64 encId = _ownership.GetEncounterId(vGuid);
        if (encId != 0)
        {
            auto it = _encounters.find(encId);
            if (it != _encounters.end())
            {
                EncounterInstance& inst = it->second;
                if (inst.participants.size() <= 2)
                {
                    inst.state.Terminate(EncounterStopReason::ParticipantDeath, nowMs);
                    FinalizeEncounter(inst, nowMs);
                    _encounters.erase(it);
                }
                else if (EncounterParticipant* p = inst.FindParticipant(vGuid))
                {
                    p->retreated = true;
                    _ownership.Release(vGuid, encId);
                }
            }
            return;
        }

        // Check if victim is a target player or target rare creature
        for (auto it = _encounters.begin(); it != _encounters.end();)
        {
            EncounterInstance& inst = it->second;
            if (inst.targetPlayerGuid == vGuid || inst.targetCreatureGuid == vGuid)
            {
                EncounterStopReason r = (inst.targetCreatureGuid == vGuid)
                    ? EncounterStopReason::TargetRareDead
                    : EncounterStopReason::ParticipantDeath;
                inst.state.Terminate(r, nowMs);
                FinalizeEncounter(inst, nowMs);
                it = _encounters.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void OnPvPKill(ObjectGuid killerGuid, ObjectGuid victimGuid)
    {
        if (killerGuid.IsEmpty() || victimGuid.IsEmpty() || killerGuid == victimGuid)
            return;

        Player* killer = ObjectAccessor::FindConnectedPlayer(killerGuid);
        Player* victim = ObjectAccessor::FindConnectedPlayer(victimGuid);
        if (!killer || !victim)
            return;

        EncounterConfig const& cfg = EncounterSettings::Get();
        uint32 nowMs = NowMs();

        if (IsRealPlayer(killer) && IsBotPlayer(victim) && killer->GetTeamId() != victim->GetTeamId())
        {
            _memory.RecordPlayerKilledBot(killerGuid, victimGuid, cfg, nowMs);
        }
        else if (IsBotPlayer(killer) && IsRealPlayer(victim) && killer->GetTeamId() != victim->GetTeamId())
        {
            _memory.RecordBotKilledPlayer(killerGuid, victimGuid, cfg, nowMs);
        }

        OnParticipantDeath(victim, killer);
    }

    void OnPlayerLogout(ObjectGuid playerGuid)
    {
        if (playerGuid.IsEmpty())
            return;

        uint32 nowMs = NowMs();
        for (auto it = _encounters.begin(); it != _encounters.end();)
        {
            if (it->second.targetPlayerGuid == playerGuid)
            {
                it->second.state.Terminate(EncounterStopReason::PlayerLogout, nowMs);
                FinalizeEncounter(it->second, nowMs);
                it = _encounters.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void ForgetBot(ObjectGuid botGuid)
    {
        if (botGuid.IsEmpty())
            return;

        uint64 encId = _ownership.GetEncounterId(botGuid);
        if (encId != 0)
        {
            auto it = _encounters.find(encId);
            if (it != _encounters.end())
            {
                uint32 nowMs = NowMs();
                it->second.state.Terminate(EncounterStopReason::BotLogoutOrDespawn, nowMs);
                FinalizeEncounter(it->second, nowMs);
                _encounters.erase(it);
            }
            else
            {
                _ownership.Release(botGuid);
            }
        }

        _budget.ForgetBot(botGuid);
        _memory.ForgetBot(botGuid);
        EncounterNarrator::Gate().ForgetBot(botGuid);
        _knownPersonas.erase(botGuid);
        _knownSocialActive.erase(botGuid);
    }

    void ResetAll()
    {
        uint32 nowMs = NowMs();
        for (auto& [id, inst] : _encounters)
        {
            inst.state.Terminate(EncounterStopReason::AdminStopped, nowMs);
            FinalizeEncounter(inst, nowMs);
        }
        _encounters.clear();
        _ownership.Clear();
        _budget.Clear();
        _memory.Clear();
        _rareCreatureCooldownUntilMs.clear();
        EncounterNarrator::Gate().Clear();
        _knownSocialActive.clear();
    }

    std::string DescribeBot(ObjectGuid botGuid)
    {
        uint64 encId = _ownership.GetEncounterId(botGuid);
        Player* bot = ObjectAccessor::FindConnectedPlayer(botGuid);
        BrainState const* bState = WorldBrainInternal::FindState(botGuid);

        std::string prevTask = "none";
        bool brainPaused = false;
        if (bState)
        {
            brainPaused = bState->pausedByEncounter || bState->task.paused;
            if (bState->task.IsValid())
                prevTask = Acore::StringFormat("{}#{}(q={})", WorldTaskTypeName(bState->task.type), bState->task.id, bState->task.quest.questId);
        }

        if (encId == 0)
        {
            return Acore::StringFormat("encounter: none (brainPaused={} prevTask={})",
                brainPaused ? "yes" : "no", prevTask);
        }

        auto it = _encounters.find(encId);
        if (it == _encounters.end())
            return "encounter: none";

        EncounterInstance const& inst = it->second;
        EncounterParticipant const* part = inst.FindParticipant(botGuid);
        uint32 nowMs = NowMs();
        uint32 remSec = inst.state.RemainingPhaseTimeoutMs(nowMs) / 1000u;

        std::string targetStr = "none";
        float distToTarget = 0.0f;
        if (!inst.targetPlayerGuid.IsEmpty())
        {
            targetStr = Acore::StringFormat("player:{}", inst.targetPlayerGuid.GetCounter());
            if (bot)
            {
                if (Player const* tp = ObjectAccessor::FindConnectedPlayer(inst.targetPlayerGuid))
                    distToTarget = Dist2D(bot->GetPositionX(), bot->GetPositionY(), tp->GetPositionX(), tp->GetPositionY());
            }
        }
        else if (!inst.targetCreatureGuid.IsEmpty())
        {
            targetStr = Acore::StringFormat("rare:{}(entry={})", inst.targetCreatureGuid.GetCounter(), inst.targetCreatureEntry);
            if (bot)
                distToTarget = Dist2D(bot->GetPositionX(), bot->GetPositionY(), inst.centerX, inst.centerY);
        }
        else
        {
            targetStr = Acore::StringFormat("dest({:.0f},{:.0f})", inst.destX != 0.0f ? inst.destX : inst.centerX, inst.destY != 0.0f ? inst.destY : inst.centerY);
            if (bot)
                distToTarget = Dist2D(bot->GetPositionX(), bot->GetPositionY(), inst.destX != 0.0f ? inst.destX : inst.centerX, inst.destY != 0.0f ? inst.destY : inst.centerY);
        }

        char const* moveOwnerStr = bot ? MoveOwnerName(BotMovement::CurrentOwner(bot)) : "offline";
        std::string combatTargetStr = (bot && bot->GetVictim()) ? bot->GetVictim()->GetName() : "none";
        uint32 tempGoCount = (part && !part->temporaryGobGuid.IsEmpty()) ? 1u : 0u;

        return Acore::StringFormat(
            "encounter: #{} type={} phase={} bot={}({}) role={} target={} map={}/zone={} dist={:.1f}yd moveOwner={} combatTarget={} timeoutLeft={}s prevTask={} brainPaused={} narrator={} tempGroup=none tempGO={} stopReason={}",
            inst.id,
            EncounterTypeName(inst.type),
            EncounterPhaseName(inst.state.phase),
            bot ? bot->GetName() : "offline",
            botGuid.GetCounter(),
            part ? EncounterRoleName(part->role) : "unknown",
            targetStr,
            bot ? bot->GetMapId() : inst.mapId,
            bot ? bot->GetZoneId() : inst.zoneId,
            distToTarget,
            moveOwnerStr,
            combatTargetStr,
            remSec,
            prevTask,
            brainPaused ? "yes" : "no",
            (inst.introNarrated || (part && part->narratorSpoken)) ? "spoken" : "silent",
            tempGoCount,
            EncounterStopReasonName(inst.state.stopReason));
    }

    std::string DescribeStats()
    {
        EncounterConfig const& cfg = EncounterSettings::Get();
        std::string out = Acore::StringFormat(
            "DynamicEncounters: enabled={} debug={} active={}/{} evals={} avgEvalInterval={}ms updateUs(last/avg/peak)={}/{}/{}us candidatesVisited={} tempGOs={}\n"
            "Lifecycle: created={} completed={} cancelled={} failed={} cleanup(checks/pass/fail)={}/{}/{}\n",
            cfg.enabled ? "yes" : "no",
            _debugLogging ? "on" : "off",
            uint32(_encounters.size()),
            cfg.maxGlobalActive,
            _metrics.directorEvaluations,
            _metrics.avgEvalIntervalMs,
            _metrics.lastUpdateDurationUs,
            _metrics.avgUpdateDurationUs,
            _metrics.peakUpdateDurationUs,
            _metrics.totalCandidatesVisited,
            _metrics.activeTemporaryGameObjects,
            _metrics.totalCreated,
            _metrics.totalCompleted,
            _metrics.totalCancelled,
            _metrics.totalFailed,
            _metrics.cleanupChecks,
            _metrics.cleanupPasses,
            _metrics.cleanupFailures);

        out += "By type (created/completed/cancelled/failed):";
        for (size_t i = 1; i < ENCOUNTER_TYPE_COUNT; ++i)
        {
            EncounterType t = static_cast<EncounterType>(i);
            out += Acore::StringFormat(" {}={}/{}/{}/{}",
                EncounterTypeName(t),
                _metrics.createdByType[i],
                _metrics.completedByType[i],
                _metrics.cancelledByType[i],
                _metrics.failedByType[i]);
        }

        if (!_encounters.empty())
            out += "\n" + DescribeActiveList();
        return out;
    }

    std::string DescribeActiveList()
    {
        if (_encounters.empty())
            return "Active encounters: 0";

        uint32 nowMs = NowMs();
        std::string out = Acore::StringFormat("Active encounters ({}):", uint32(_encounters.size()));
        for (auto const& [id, inst] : _encounters)
        {
            out += Acore::StringFormat(
                "\n  #{} type={} phase={} map={}/zone={} targetPlayer={} bots={} left={}s narrator={} stopReason={}",
                id,
                EncounterTypeName(inst.type),
                EncounterPhaseName(inst.state.phase),
                inst.mapId,
                inst.zoneId,
                inst.targetPlayerGuid.GetCounter(),
                uint32(inst.participants.size()),
                inst.state.RemainingPhaseTimeoutMs(nowMs) / 1000u,
                inst.introNarrated ? "spoken" : "silent",
                EncounterStopReasonName(inst.state.stopReason));
            for (auto const& part : inst.participants)
            {
                out += Acore::StringFormat("\n    -> {}", DescribeBot(part.botGuid));
            }
        }
        return out;
    }

    bool ValidateEncounterCleanup(ObjectGuid botGuid, std::string& outSummary)
    {
        EncounterCleanupSnapshot snap = BuildCleanupSnapshot(botGuid);
        EncounterCleanupVerdict verdict = EncounterCleanupValidator::Validate(snap);
        outSummary = verdict.summary;
        return verdict.passed;
    }

    void SetDebugLogging(bool enabled)
    {
        _debugLogging = enabled;
    }

    bool IsDebugLogging()
    {
        return _debugLogging;
    }

    void ResetCooldowns(ObjectGuid optionalPlayerGuid)
    {
        if (optionalPlayerGuid.IsEmpty())
        {
            _budget.Clear();
            _rareCreatureCooldownUntilMs.clear();
            EncounterNarrator::Gate().Clear();
        }
        else
        {
            _budget.ForgetPlayer(optionalPlayerGuid);
            EncounterNarrator::Gate().ForgetPlayer(optionalPlayerGuid);
        }
    }

    std::string DescribeBountyAndNemesis(ObjectGuid playerGuid)
    {
        EncounterConfig const& cfg = EncounterSettings::Get();
        uint32 nowMs = NowMs();
        uint32 bounty = _memory.GetBountyScore(playerGuid, cfg, nowMs);
        auto nemList = _memory.GetPlayerNemesisList(playerGuid, cfg, nowMs);

        std::string out = Acore::StringFormat(
            "Player {} bounty={}/{} (bountyCooldown={} personalCooldown={}) nemesisRecords={}",
            playerGuid.GetCounter(),
            bounty,
            cfg.bountyThreshold,
            _budget.IsPlayerOnBountyCooldown(playerGuid, nowMs) ? "yes" : "no",
            _budget.IsPlayerOnPersonalCooldown(playerGuid, nowMs) ? "yes" : "no",
            uint32(nemList.size()));

        for (NemesisRecord const& rec : nemList)
        {
            out += Acore::StringFormat("\n  bot={} grudge={} playerKilledBot={} botKilledPlayer={}",
                rec.botGuid.GetCounter(),
                rec.grudgeScore,
                rec.timesPlayerKilledBot,
                rec.timesBotKilledPlayer);
        }
        return out;
    }

    bool ForceStartEncounter(EncounterType type, Player* targetOrNearPlayer, std::string& outMessage)
    {
        if (type == EncounterType::None || !targetOrNearPlayer)
        {
            outMessage = "Invalid encounter type or target player.";
            return false;
        }

        EncounterConfig const& cfg = EncounterSettings::Get();
        uint32 nowMs = NowMs();
        std::vector<Player*> nearbyBots;
        GatherNearbyCandidateBots(targetOrNearPlayer, std::max(cfg.searchRadiusYards, 350.0f), nearbyBots, /*ignoreCooldown=*/true);

        if (IsPlayerTargetedEncounter(type))
            return TryBuildPlayerEncounter(type, targetOrNearPlayer, nearbyBots, true, nowMs, outMessage);

        return TryBuildWorldEncounter(type, targetOrNearPlayer, nearbyBots, true, nowMs, outMessage);
    }

    bool StopEncounter(uint64 encounterId, std::string& outMessage)
    {
        auto it = _encounters.find(encounterId);
        if (it == _encounters.end())
        {
            outMessage = Acore::StringFormat("Encounter #{} not found.", encounterId);
            return false;
        }

        uint32 nowMs = NowMs();
        it->second.state.Terminate(EncounterStopReason::AdminStopped, nowMs);
        FinalizeEncounter(it->second, nowMs);
        _encounters.erase(it);
        outMessage = Acore::StringFormat("Encounter #{} stopped and participants released.", encounterId);
        return true;
    }

    uint32 StopAllEncounters()
    {
        uint32 count = uint32(_encounters.size());
        uint32 nowMs = NowMs();
        for (auto& [id, inst] : _encounters)
        {
            inst.state.Terminate(EncounterStopReason::AdminStopped, nowMs);
            FinalizeEncounter(inst, nowMs);
        }
        _encounters.clear();
        _ownership.Clear();
        return count;
    }

    EncounterMetrics GetMetrics()
    {
        return _metrics;
    }

    uint32 GetActiveEncounterCount()
    {
        return uint32(_encounters.size());
    }
}
