#include "WorldEncounterDirector.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "BotMovement.h"
#include "BotWorldPoi.h"
#include "CellImpl.h"
#include "Creature.h"
#include "DBCStores.h"
#include "EncounterBudget.h"
#include "EncounterConfig.h"
#include "EncounterEligibility.h"
#include "EncounterHandlers.h"
#include "EncounterMemory.h"
#include "EncounterNarrator.h"
#include "EncounterPersonality.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "WorldBrainState.h"
#include "WorldParties.h"
#include "WorldSession.h"
#include "WorldSocial.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace
{
    EncounterOwnershipTable _ownership;
    EncounterBudgetTracker _budget;
    EncounterMemoryStore _memory;
    EncounterMetrics _metrics;

    std::unordered_map<uint64, EncounterInstance> _encounters;
    std::unordered_map<ObjectGuid, WorldPersona> _knownPersonas;
    uint64 _nextEncounterId = 1;

    uint32 _evalAccumMs = 0;
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
        if (p->IsFFAPvP() || p->IsPvP())
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
        if (WorldParties::IsWorldParty(grp))
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
        snap.hasManualCommand = (BotAI::GetManualCommand(bot) != BotManualCommand::None);
        snap.inPlayerControlledGroup = IsPlayerControlledGroup(bot);
        snap.inWorldParty = (bot->GetGroup() && WorldParties::IsWorldParty(bot->GetGroup()));
        snap.isHelpingSocial = WorldSocial::IsHelping(bot);
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

    void GatherNearbyCandidateBots(WorldObject* centerObj, float radius, std::vector<Player*>& outBots)
    {
        outBots.clear();
        if (!centerObj || !centerObj->IsInWorld())
            return;

        std::vector<Player*> players;
        Acore::AnyPlayerInObjectRangeCheck check(centerObj, radius, true);
        Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(centerObj, players, check);
        Cell::VisitWorldObjects(centerObj, searcher, radius);

        for (Player* p : players)
        {
            if (!IsBotPlayer(p))
                continue;
            if (_budget.IsBotOnCooldown(p->GetGUID(), NowMs()))
                continue;
            outBots.push_back(p);
        }
    }

    Creature* FindNearbyLiveRareCreature(WorldObject* centerObj, float radius)
    {
        if (!centerObj || !centerObj->IsInWorld())
            return nullptr;

        std::vector<Creature*> creatures;
        Acore::AnyUnitInObjectRangeCheck uCheck(centerObj, radius);
        Acore::CreatureListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(centerObj, creatures, uCheck);
        Cell::VisitGridObjects(centerObj, searcher, radius);

        Creature* best = nullptr;
        float bestDist = radius;
        for (Creature* c : creatures)
        {
            if (!c || !c->IsAlive() || !c->IsInWorld() || c->IsPet() || c->IsTotem() || c->IsCritter())
                continue;
            CreatureTemplate const* tpl = c->GetCreatureTemplate();
            if (!tpl || (tpl->rank != CREATURE_ELITE_RARE && tpl->rank != CREATURE_ELITE_RAREELITE))
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
        p.mapId = bot->GetMapId();
        p.originX = bot->GetPositionX();
        p.originY = bot->GetPositionY();
        p.originZ = bot->GetPositionZ();
        p.joinedAtMs = nowMs;
        p.lastActionMs = nowMs;
        p.originalDisplayId = bot->GetDisplayId();
        p.stealthCapable = EncounterHandlers::IsStealthCapableClass(bot->getClass());
        p.cautiousness = EncounterPersonality::Derive(GetPersonaForBot(bot->GetGUID())).cautiousness;
        return p;
    }

    void FinalizeEncounter(EncounterInstance& inst, uint32 nowMs)
    {
        EncounterConfig const& cfg = EncounterSettings::Get();
        std::vector<ObjectGuid> botGuids;
        botGuids.reserve(inst.participants.size());

        for (auto& part : inst.participants)
        {
            botGuids.push_back(part.botGuid);
            if (Player* bot = ObjectAccessor::FindConnectedPlayer(part.botGuid))
            {
                EncounterHandlers::RestoreParticipantState(bot, part);
                if (inst.type == EncounterType::RoadDuel)
                    bot->AttackStop();
            }
            _ownership.Release(part.botGuid, inst.id);
        }

        _ownership.ReleaseAllForEncounter(inst.id);
        _budget.OnEncounterFinished(inst.type, inst.mapId, inst.targetPlayerGuid, botGuids, cfg, nowMs);

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

        _budget.OnEncounterStarted(inst.type, inst.mapId, inst.targetPlayerGuid);

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
            if (!playerSnap.isOnlineAndInWorld || !playerSnap.isAlive || playerSnap.isInSanctuary)
            {
                outReason = "Player is offline, dead, or in sanctuary";
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

        for (Player* bot : nearbyBots)
        {
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
                if (!EncounterPriorityArbiter::CanClaimBot(botSnap.activity))
                    continue;
                if (IsHostilePlayerEncounter(type) && (botSnap.teamId == playerSnap.teamId || !botSnap.canLegallyAttackTarget))
                    continue;
            }

            WorldPersona persona = GetPersonaForBot(bot->GetGUID());
            bool stealthCap = EncounterHandlers::IsStealthCapableClass(bot->getClass());
            float score = EncounterPersonality::ScoreAffinity(type, role, persona, stealthCap);
            score *= _memory.GetSelectionWeightMultiplier(playerSnap.guid, bot->GetGUID(), cfg, nowMs);

            if (score > bestScore)
            {
                bestScore = score;
                bestBot = bot;
            }
        }

        if (!bestBot)
        {
            outReason = "No eligible bot found nearby";
            return false;
        }

        if (type == EncounterType::BountyHunter)
            _memory.ConsumeBountyForAttempt(playerSnap.guid);
        if (IsHostilePlayerEncounter(type))
            _memory.RecordEncounterConflict(bestBot->GetGUID(), playerSnap.guid, cfg, nowMs);

        EncounterInstance inst;
        inst.type = type;
        inst.mapId = targetPlayer->GetMapId();
        inst.zoneId = targetPlayer->GetZoneId();
        inst.areaId = targetPlayer->GetAreaId();
        inst.centerX = targetPlayer->GetPositionX();
        inst.centerY = targetPlayer->GetPositionY();
        inst.centerZ = targetPlayer->GetPositionZ();
        inst.targetPlayerGuid = targetPlayer->GetGUID();
        inst.participants.push_back(MakeParticipant(bestBot, role, nowMs));

        uint64 id = CommitEncounter(std::move(inst), nowMs);
        outReason = Acore::StringFormat("Started {} (#{}) with bot {}", EncounterTypeName(type), id, bestBot->GetName());
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
            else if (!EncounterPriorityArbiter::CanClaimBot(snap.activity))
            {
                continue;
            }
            freeBots.push_back(b);
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
                    if (!EncounterEligibility::AreBotsCompatibleForPair(EncounterType::RoadDuel, snapA, snapB, cfg))
                        continue;

                    EncounterInstance inst;
                    inst.type = EncounterType::RoadDuel;
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
                    if (!EncounterEligibility::AreBotsCompatibleForPair(EncounterType::HunterAndPrey, snapA, snapB, cfg))
                        continue;

                    EncounterInstance inst;
                    inst.type = EncounterType::HunterAndPrey;
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
            for (BotWorldPoi::Hub const& hub : BotWorldPoi::GetHubsForMap(mapId))
            {
                float d = Dist2D(leader->GetPositionX(), leader->GetPositionY(), hub.x, hub.y);
                if (d >= 70.0f && d <= 450.0f)
                {
                    inst.destX = hub.x;
                    inst.destY = hub.y;
                    inst.destZ = hub.z;
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

                // Priority 3: Nemesis encounter if a nearby bot holds a grudge
                if (cfg.IsTypeEnabled(EncounterType::Nemesis))
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
        _rareScanAccumMs += diff;

        if (_evalAccumMs < cfg.thinkIntervalMs)
            return;

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
    }

    bool UpdateBot(Player* bot, BrainState& brainState, WorldPersona const& persona, uint32 diff)
    {
        (void)diff;
        if (!bot)
            return false;

        _knownPersonas[bot->GetGUID()] = persona;

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
        switch (inst.type)
        {
            case EncounterType::AssassinContract:
                EncounterHandlers::UpdateAssassinContract(inst, bot, *part, nowMs);
                break;
            case EncounterType::TheWatcher:
                EncounterHandlers::UpdateTheWatcher(inst, bot, *part, nowMs);
                break;
            case EncounterType::FactionSkirmish:
                EncounterHandlers::UpdateFactionSkirmish(inst, bot, *part, nowMs);
                break;
            case EncounterType::PatrolAmbush:
                EncounterHandlers::UpdatePatrolAmbush(inst, bot, *part, nowMs);
                break;
            case EncounterType::BountyHunter:
                EncounterHandlers::UpdateBountyHunter(inst, bot, *part, nowMs);
                break;
            case EncounterType::Nemesis:
                EncounterHandlers::UpdateNemesis(inst, bot, *part, nowMs);
                break;
            case EncounterType::RoadDuel:
                EncounterHandlers::UpdateRoadDuel(inst, bot, *part, nowMs);
                break;
            case EncounterType::Caravan:
                EncounterHandlers::UpdateCaravan(inst, bot, *part, nowMs);
                break;
            case EncounterType::Deserter:
                EncounterHandlers::UpdateDeserter(inst, bot, *part, nowMs);
                break;
            case EncounterType::HunterAndPrey:
                EncounterHandlers::UpdateHunterAndPrey(inst, bot, *part, nowMs);
                break;
            case EncounterType::RivalAdventuringParty:
                EncounterHandlers::UpdateRivalParty(inst, bot, *part, nowMs);
                break;
            case EncounterType::RareHunter:
                EncounterHandlers::UpdateRareHunter(inst, bot, *part, nowMs);
                break;
            case EncounterType::MysteryEncounter:
                EncounterHandlers::UpdateMysteryEncounter(inst, bot, *part, nowMs);
                break;
            default:
                inst.state.Terminate(EncounterStopReason::Completed, nowMs);
                break;
        }

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
        EncounterNarrator::Gate().Clear();
    }

    std::string DescribeBot(ObjectGuid botGuid)
    {
        uint64 encId = _ownership.GetEncounterId(botGuid);
        if (encId == 0)
            return "encounter: none";

        auto it = _encounters.find(encId);
        if (it == _encounters.end())
            return "encounter: none";

        EncounterInstance const& inst = it->second;
        EncounterParticipant const* part = inst.FindParticipant(botGuid);
        uint32 nowMs = NowMs();
        uint32 remSec = inst.state.RemainingPhaseTimeoutMs(nowMs) / 1000u;

        std::string targetStr = "none";
        if (!inst.targetPlayerGuid.IsEmpty())
            targetStr = Acore::StringFormat("player:{}", inst.targetPlayerGuid.GetCounter());
        else if (!inst.targetCreatureGuid.IsEmpty())
            targetStr = Acore::StringFormat("rare:{}(entry={})", inst.targetCreatureGuid.GetCounter(), inst.targetCreatureEntry);
        else
            targetStr = Acore::StringFormat("area({:.0f},{:.0f})", inst.centerX, inst.centerY);

        return Acore::StringFormat("encounter: #{} type={} role={} phase={} target={} timeoutLeft={}s",
            inst.id,
            EncounterTypeName(inst.type),
            part ? EncounterRoleName(part->role) : "unknown",
            EncounterPhaseName(inst.state.phase),
            targetStr,
            remSec);
    }

    std::string DescribeStats()
    {
        EncounterConfig const& cfg = EncounterSettings::Get();
        std::string out = Acore::StringFormat(
            "DynamicEncounters: enabled={} active={}/{} evals={} avgEvalInterval={}ms created={} completed={} cancelled={} failed={}\n",
            cfg.enabled ? "yes" : "no",
            uint32(_encounters.size()),
            cfg.maxGlobalActive,
            _metrics.directorEvaluations,
            _metrics.avgEvalIntervalMs,
            _metrics.totalCreated,
            _metrics.totalCompleted,
            _metrics.totalCancelled,
            _metrics.totalFailed);

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
        {
            uint32 nowMs = NowMs();
            out += "\nActive instances:";
            for (auto const& [id, inst] : _encounters)
            {
                out += Acore::StringFormat("\n  #{} type={} phase={} map={} bots={} left={}s",
                    id,
                    EncounterTypeName(inst.type),
                    EncounterPhaseName(inst.state.phase),
                    inst.mapId,
                    uint32(inst.participants.size()),
                    inst.state.RemainingPhaseTimeoutMs(nowMs) / 1000u);
            }
        }
        return out;
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
        GatherNearbyCandidateBots(targetOrNearPlayer, std::max(cfg.searchRadiusYards, 350.0f), nearbyBots);

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
