#include "WorldParties.h"
#include "BotAI.h"
#include "CellImpl.h"
#include "DatabaseEnv.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectiveCommon.h"
#include "Player.h"
#include "QuestKnowledgeBase.h"
#include "ScriptMgr.h"
#include "SocialRules.h"
#include "TC9Sidecar.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

using namespace WorldBrainInternal;

namespace
{
    struct WorldParty
    {
        ObjectGuid::LowType groupLow = 0;
        ObjectGuid leader;
        std::vector<ObjectGuid> members;   // besides the leader
        uint32 questId = 0;
        uint32 formedMs = 0;
        uint32 expiresMs = 0;
        uint32 unpersistAtMs = 0;
        bool unpersisted = false;
        bool leaderOnTask = true;
        uint32 leaderDeadSinceMs = 0;
        std::unordered_map<ObjectGuid, uint32> memberDeadSinceMs;
    };

    std::unordered_map<uint32, WorldParty> _parties;       // by id
    std::unordered_map<ObjectGuid, uint32> _partyOf;       // leader and members -> party id
    uint32 _nextPartyId = 1;
    uint32 _updateTimer = 0;

    constexpr uint32 UPDATE_INTERVAL_MS = 2000;
    // The group's database rows are deleted right after forming (enough with the default single
    // character-database worker, which runs statements in order) and again this long after: by
    // then the core's own asynchronous inserts have certainly run, however many workers there are.
    constexpr uint32 UNPERSIST_DELAY_MS = 10000;
    constexpr uint32 LEADER_DEAD_GRACE_MS = 45000;
    constexpr uint32 MEMBER_DEAD_GRACE_MS = 90000;
    constexpr float MEMBER_MAX_DISTANCE = 200.0f;
    // After a party a bot keeps to itself for a while.
    constexpr uint32 PARTY_COOLDOWN_MIN_MS = 5 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 PARTY_COOLDOWN_MAX_MS = 12 * MINUTE * IN_MILLISECONDS;
    // A leader tries to form a party at most this often.
    constexpr uint32 ATTEMPT_MIN_MS = 3 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 ATTEMPT_MAX_MS = 8 * MINUTE * IN_MILLISECONDS;

    Group* GroupOf(WorldParty const& party)
    {
        return sGroupMgr->GetGroupByGUID(party.groupLow);
    }

    // Keeps the core from bringing the party back after a restart: without its rows, the group
    // exists in memory only.
    void DeleteGroupRows(ObjectGuid::LowType groupLow)
    {
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP);
        stmt->SetData(0, groupLow);
        trans->Append(stmt);
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER_ALL);
        stmt->SetData(0, groupLow);
        trans->Append(stmt);
        CharacterDatabase.CommitTransaction(trans);
    }

    void CoolDown(ObjectGuid guid, uint32 now)
    {
        if (BrainState* state = FindState(guid))
            state->nextPartyAttemptMs = now + RollRange(*state, 0x9a74, PARTY_COOLDOWN_MIN_MS, PARTY_COOLDOWN_MAX_MS);
    }

    void Unregister(uint32 id)
    {
        auto itr = _parties.find(id);
        if (itr == _parties.end())
            return;
        _partyOf.erase(itr->second.leader);
        for (ObjectGuid const& member : itr->second.members)
            _partyOf.erase(member);
        _parties.erase(itr);
    }

    void EndParty(uint32 id, PartyEnd why)
    {
        auto itr = _parties.find(id);
        if (itr == _parties.end())
            return;
        WorldParty& party = itr->second;
        uint32 now = NowMs();

        CoolDown(party.leader, now);
        for (ObjectGuid const& member : party.members)
            CoolDown(member, now);
        if (BrainState* leaderState = FindState(party.leader))
        {
            Count(leaderState->metrics, &WorldMetrics::partiesEnded);
            NoteEvent(*leaderState, Acore::StringFormat("party broke up ({})", SocialRules::PartyEndName(why)));
        }

        LOG_DEBUG("module.coa-playerbots.world", "Temporary party {} (leader {}, quest {}) ends after {}s: {}.", id,
            party.leader.ToString(), party.questId, (now - party.formedMs) / IN_MILLISECONDS, SocialRules::PartyEndName(why));

        Group* group = GroupOf(party);
        Unregister(id);
        if (group)
            group->Disband();
    }

    // Removes one member; the core disbands a group left with a single player on its own.
    // Returns false when that happened (the party is over).
    bool DropMember(uint32 id, WorldParty& party, ObjectGuid member, uint32 now)
    {
        CoolDown(member, now);
        party.members.erase(std::remove(party.members.begin(), party.members.end(), member), party.members.end());
        party.memberDeadSinceMs.erase(member);
        _partyOf.erase(member);
        LOG_DEBUG("module.coa-playerbots.world", "Temporary party {}: member {} leaves.", id, member.ToString());

        if (Group* group = GroupOf(party))
            if (group->IsMember(member))
                group->RemoveMember(member, GROUP_REMOVEMETHOD_LEAVE);

        // The core disbanded a group that was down to the leader alone.
        if (!GroupOf(party))
        {
            CoolDown(party.leader, now);
            if (BrainState* leaderState = FindState(party.leader))
            {
                Count(leaderState->metrics, &WorldMetrics::partiesEnded);
                NoteEvent(*leaderState, Acore::StringFormat("party broke up ({})", SocialRules::PartyEndName(PartyEnd::Empty)));
            }
            LOG_DEBUG("module.coa-playerbots.world", "Temporary party {} ends: {}.", id, SocialRules::PartyEndName(PartyEnd::Empty));
            Unregister(id);
            return false;
        }
        return true;
    }

    uint32 NowOrDeadSince(uint32& since, bool alive, uint32 now)
    {
        if (alive)
        {
            since = 0;
            return 0;
        }
        if (!since)
            since = now;
        return now - since;
    }

    void CheckParty(uint32 id, uint32 now)
    {
        auto itr = _parties.find(id);
        if (itr == _parties.end())
            return;
        WorldParty& party = itr->second;

        Group* group = GroupOf(party);
        if (!group)
        {
            Unregister(id);
            return;
        }

        if (!party.unpersisted && now >= party.unpersistAtMs)
        {
            DeleteGroupRows(party.groupLow);
            party.unpersisted = true;
        }

        Player* leader = ObjectAccessor::FindPlayer(party.leader);
        bool leaderHere = leader && leader->IsInWorld() && leader->GetGroup() == group;

        std::vector<ObjectGuid> members = party.members;
        for (ObjectGuid const& guid : members)
        {
            Player* member = ObjectAccessor::FindPlayer(guid);
            PartyMemberStatus status;
            status.present = member && member->IsInWorld() && member->GetGroup() == group;
            status.sameMap = status.present && leaderHere && member->GetMapId() == leader->GetMapId();
            status.alive = status.present && member->IsAlive();
            status.deadForMs = NowOrDeadSince(party.memberDeadSinceMs[guid], !status.present || status.alive, now);
            status.distanceToLeader = status.sameMap ? member->GetDistance(leader) : 0.0f;
            if (leaderHere && SocialRules::ShouldDropMember(status, MEMBER_MAX_DISTANCE, MEMBER_DEAD_GRACE_MS))
                if (!DropMember(id, party, guid, now))
                    return;
        }

        PartyStatus status;
        status.now = now;
        status.expiresMs = party.expiresMs;
        status.leaderPresent = leaderHere;
        status.leaderOnTask = party.leaderOnTask;
        BrainState* leaderState = FindState(party.leader);
        status.leaderFree = leaderHere && leaderState && !leaderState->suspended && !(leader->GetMap() && leader->GetMap()->Instanceable());
        status.leaderDeadForMs = NowOrDeadSince(party.leaderDeadSinceMs, !leaderHere || leader->IsAlive(), now);
        status.members = uint32(party.members.size());

        PartyEnd end = SocialRules::ShouldEnd(status, LEADER_DEAD_GRACE_MS);
        if (end != PartyEnd::No)
            EndParty(id, end);
    }

    bool OnWorkThatNeedsHands(QuestKnowledge const* info, uint8 objectiveIndex, ObjectiveDef const** out)
    {
        if (!info || objectiveIndex >= info->objectives.size())
            return false;
        ObjectiveDef const& def = info->objectives[objectiveIndex];
        // Kill and collect-from-kills objectives: the work a party shares (group kill credit).
        if (ActionOf(def.type) != ObjectiveAction::KillAndLoot)
            return false;
        *out = &def;
        return true;
    }
}

namespace WorldParties
{
    void TryForm(Player* leader, BrainState& state)
    {
        WorldBrainConfig const& cfg = WorldBrainSettings::Get();
        if (!cfg.temporaryParties || sToCloud9Sidecar->ClusterModeEnabled())
            return;

        uint32 now = NowMs();
        if (now < state.nextPartyAttemptMs)
            return;
        state.nextPartyAttemptMs = now + RollRange(state, 0x9a71, ATTEMPT_MIN_MS, ATTEMPT_MAX_MS);

        WorldTask const& task = state.task;
        ObjectiveDef const* def = nullptr;
        if (task.type != WorldTaskType::QuestObjective ||
            !OnWorkThatNeedsHands(QuestKB::Get(task.quest.questId), task.quest.objectiveIndex, &def))
            return;
        if (leader->GetGroup() || leader->GetGroupInvite() || IsInParty(leader->GetGUID()) || leader->IsInCombat())
            return;
        if (!leader->GetMap() || leader->GetMap()->Instanceable())
            return;
        // Sociable bots look for company; loners mostly don't.
        if (WorldBrainInternal::Roll(state, 0x9a72) % 100 >= uint32(state.persona.sociability) / 2)
            return;

        std::vector<Player*> players;
        Acore::AnyPlayerInObjectRangeCheck check(leader, cfg.partyRadius, true, true);
        Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(leader, players, check);
        Cell::VisitObjects(leader, searcher, cfg.partyRadius);

        PartyRules rules;
        rules.radius = cfg.partyRadius;
        bool haveHealer = BotAI::GetRole(leader->GetGUID()) == BotRole::Healer;
        bool haveTank = BotAI::GetRole(leader->GetGUID()) == BotRole::Tank;

        struct Pick
        {
            Player* player;
            BrainState* state;
            BotRole role;
            float score;
        };
        std::vector<Pick> picks;
        for (Player* other : players)
        {
            if (other == leader)
                continue;
            if (other->GetGroup() || other->GetGroupInvite() || other->IsInCombat() || IsInParty(other->GetGUID()))
                continue;
            if (other->GetTeamId() != leader->GetTeamId())
                continue;
            // Only bots have a brain: real players are never pulled into a party.
            BrainState* otherState = FindState(other->GetGUID());
            if (!otherState || otherState->suspended || otherState->opportunityActive || otherState->breakUntilMs > now ||
                otherState->nextPartyAttemptMs > now)
                continue;
            // Busy with its own task's hands-on part, or away from its task altogether (an errand,
            // a detour, helping someone: the task is paused).
            if (otherState->task.IsValid() && (otherState->task.paused || otherState->task.phase == TaskPhase::Execute ||
                otherState->task.phase == TaskPhase::Combat || otherState->task.phase == TaskPhase::Loot))
                continue;

            PartyCandidate candidate;
            candidate.leaderLevel = leader->GetLevel();
            candidate.level = other->GetLevel();
            candidate.distance = leader->GetDistance(other);
            candidate.sharesQuest = other->GetQuestStatus(task.quest.questId) == QUEST_STATUS_INCOMPLETE &&
                !ObjectiveCommon::IsDone(other, task.quest.questId, *def);
            BotRole role = BotAI::GetRole(other->GetGUID());
            candidate.fillsMissingRole = (role == BotRole::Healer && !haveHealer) || (role == BotRole::Tank && !haveTank);
            candidate.sociability = otherState->persona.sociability;
            float score = SocialRules::PartyCandidateScore(candidate, rules);
            if (score < 0.0f)
                continue;
            // The other bot has a say too.
            if (WorldBrainInternal::Roll(*otherState, 0x9a73) % 100 >= 40u + uint32(otherState->persona.sociability) / 2)
                continue;
            picks.push_back(Pick{ other, otherState, role, score });
        }
        if (picks.empty())
            return;

        std::sort(picks.begin(), picks.end(), [](Pick const& a, Pick const& b) { return a.score > b.score; });
        size_t size = RollRange(state, 0x9a75, 2, std::max<uint32>(2, cfg.partyMaxSize));
        if (picks.size() > size - 1)
            picks.resize(size - 1);

        // Whatever the server's own scripts say about grouping (challenge modes and the like) holds
        // for bots too.
        std::vector<Pick> allowed;
        for (Pick const& pick : picks)
        {
            std::string name = pick.player->GetName();
            if (sScriptMgr->OnPlayerCanGroupInvite(leader, name))
                allowed.push_back(pick);
        }
        if (allowed.empty())
            return;

        Group* group = new Group();
        if (!group->Create(leader))
        {
            delete group;
            return;
        }
        sGroupMgr->AddGroup(group);

        uint32 id = _nextPartyId++;
        WorldParty party;
        party.groupLow = group->GetGUID().GetCounter();
        party.leader = leader->GetGUID();
        party.questId = task.quest.questId;
        party.formedMs = now;
        party.expiresMs = now + SocialRules::PartyLifetimeMs(WorldBrainInternal::Roll(state, 0x9a76), cfg.partyMinMs, cfg.partyMaxMs);
        party.unpersistAtMs = now + UNPERSIST_DELAY_MS;

        std::string names;
        for (Pick const& pick : allowed)
        {
            if (!sScriptMgr->OnPlayerCanGroupAccept(pick.player, group) || !group->AddMember(pick.player))
                continue;
            party.members.push_back(pick.player->GetGUID());
            NoteEvent(*pick.state, Acore::StringFormat("joined {}'s party for quest {}", leader->GetName(), task.quest.questId));
            names += names.empty() ? pick.player->GetName() : ", " + pick.player->GetName();
        }

        if (party.members.empty())
        {
            DeleteGroupRows(party.groupLow);
            group->Disband();
            return;
        }
        group->BroadcastGroupUpdate();
        DeleteGroupRows(party.groupLow);

        _partyOf[party.leader] = id;
        for (ObjectGuid const& member : party.members)
            _partyOf[member] = id;
        _parties.emplace(id, std::move(party));

        Count(state.metrics, &WorldMetrics::partiesFormed);
        NoteEvent(state, Acore::StringFormat("formed a party with {} for quest {}", names, task.quest.questId));
        LOG_DEBUG("module.coa-playerbots.world", "Bot '{}' formed temporary party {} with {} for quest {}.", leader->GetName(), id,
            names, task.quest.questId);
    }

    bool IsLeader(ObjectGuid bot)
    {
        auto itr = _partyOf.find(bot);
        if (itr == _partyOf.end())
            return false;
        auto party = _parties.find(itr->second);
        return party != _parties.end() && party->second.leader == bot;
    }

    bool IsInParty(ObjectGuid bot)
    {
        return _partyOf.count(bot) != 0;
    }

    void OnLeaderTaskEnded(ObjectGuid leader)
    {
        auto itr = _partyOf.find(leader);
        if (itr == _partyOf.end())
            return;
        auto party = _parties.find(itr->second);
        if (party != _parties.end() && party->second.leader == leader)
            party->second.leaderOnTask = false;
    }

    void Update(uint32 diff)
    {
        if (_parties.empty())
            return;
        _updateTimer += diff;
        if (_updateTimer < UPDATE_INTERVAL_MS)
            return;
        _updateTimer = 0;

        uint32 now = NowMs();
        std::vector<uint32> ids;
        ids.reserve(_parties.size());
        for (auto const& [id, party] : _parties)
            ids.push_back(id);
        for (uint32 id : ids)
            CheckParty(id, now);
    }

    void Forget(ObjectGuid bot)
    {
        // Nothing to touch on the group here: the logging-out player is being torn down. The
        // next Update sees it gone and drops it (or ends the party, for the leader).
        OnLeaderTaskEnded(bot);
    }

    std::string Describe(ObjectGuid bot)
    {
        auto itr = _partyOf.find(bot);
        if (itr == _partyOf.end())
            return std::string();
        auto party = _parties.find(itr->second);
        if (party == _parties.end())
            return std::string();
        WorldParty const& p = party->second;
        uint32 now = NowMs();
        return Acore::StringFormat("Temporary party {}: {} of {} members, quest {}, {}s left{}.", itr->second,
            p.leader == bot ? "leader" : "member", p.members.size() + 1, p.questId,
            p.expiresMs > now ? (p.expiresMs - now) / IN_MILLISECONDS : 0, p.leaderOnTask ? "" : ", breaking up");
    }

    uint32 Count()
    {
        return uint32(_parties.size());
    }
}
