/*
 * mod-coa-playerbots
 *
 * Group tactical layer: shared interrupt assignments. See GroupDirector.h.
 */

#include "engine/GroupDirector.h"
#include "engine/CastGuard.h"
#include "engine/SpellPredicates.h"
#include "BotAI.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace BotAI
{
    namespace
    {
        // The group is looked at a few times a second, not every tick of every bot.
        constexpr uint32 SNAPSHOT_INTERVAL_MS = 250;
        // A snapshot nobody asked for this long is dropped (group left the fight, disbanded, changed map).
        constexpr uint32 SNAPSHOT_IDLE_DROP_MS = 30000;
        // Bounded scan: at most this many enemies are examined per snapshot.
        constexpr size_t MAX_ENEMIES = 16;
        // A player cannot answer a cast the instant it starts. Each bot waits its own, stable reaction time (base + a per-bot spread)
        // after the cast was first seen; the backup waits BACKUP_DELAY_MS more, so it only acts if the primary has not landed the kick.
        constexpr uint32 REACTION_BASE_MS = 250;
        constexpr uint32 REACTION_SPREAD_MS = 350;
        constexpr uint32 BACKUP_DELAY_MS = 600;
        // A kick issued with less than this left on the cast would land after the spell resolved.
        constexpr uint32 MIN_REMAINING_MS = 200;

        struct CastWatch
        {
            ObjectGuid enemy;
            uint32 spellId = 0;
            uint32 firstSeenMs = 0;
            uint32 finishMs = 0;
            ObjectGuid primary;
            ObjectGuid backup;
            bool primaryRefused = false; // the primary's kick was refused: the backup acts at once
            bool assigned = false;
        };

        struct Snapshot
        {
            uint32 builtMs = 0;
            uint32 lastUsedMs = 0;
            std::vector<CastWatch> watches;
        };

        // [group guid] -> snapshot. Bots are ticked from the world thread (see BotMgr); nothing here is touched from a map thread.
        std::unordered_map<ObjectGuid, Snapshot> s_snapshots;

        uint32 ReactionMs(ObjectGuid botGuid)
        {
            return REACTION_BASE_MS + uint32(botGuid.GetCounter() % REACTION_SPREAD_MS);
        }

        // Who is best placed to interrupt: damage dealers and supports first (their GCD costs the group least), then tanks, healers last.
        int RolePreference(Player* member)
        {
            switch (BotAI::GetRole(member->GetGUID()))
            {
                case BotRole::Dps: return 0;
                case BotRole::Support: return 1;
                case BotRole::Tank: return 2;
                case BotRole::Healer: return 3;
                default: return 4;
            }
        }

        void CollectEnemies(Group* group, std::vector<Unit*>& out)
        {
            auto add = [&](Unit* unit)
            {
                if (!unit || !unit->IsAlive() || unit->GetTypeId() != TYPEID_UNIT || out.size() >= MAX_ENEMIES)
                    return;
                if (std::find(out.begin(), out.end(), unit) == out.end())
                    out.push_back(unit);
            };
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || !member->IsAlive() || !member->IsInWorld())
                    continue;
                add(member->GetVictim());
                for (Unit* attacker : member->getAttackers())
                    add(attacker);
            }
        }

        void Assign(Group* group, CastWatch& watch, Unit* enemy)
        {
            struct Candidate { Player* bot; int preference; float distance; uint32 spellId; };
            std::vector<Candidate> candidates;
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || !member->IsBot() || !member->IsAlive() || !member->IsInWorld() || member->GetMap() != enemy->GetMap())
                    continue;
                if (CastGuard::IsCurrentlyCasting(member))
                    continue;
                uint32 spellId = SelectInterruptSpell(member, enemy);
                if (!spellId)
                    continue;
                candidates.push_back({ member, RolePreference(member), member->GetDistance(enemy), spellId });
            }
            std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b)
            {
                if (a.preference != b.preference)
                    return a.preference < b.preference;
                if (a.distance != b.distance)
                    return a.distance < b.distance;
                return a.bot->GetGUID() < b.bot->GetGUID(); // stable under equal conditions
            });
            watch.primary = candidates.size() > 0 ? candidates[0].bot->GetGUID() : ObjectGuid::Empty;
            watch.backup = candidates.size() > 1 ? candidates[1].bot->GetGUID() : ObjectGuid::Empty;
            watch.assigned = true;
        }

        Snapshot& Refresh(Group* group, uint32 now)
        {
            Snapshot& snap = s_snapshots[group->GetGUID()];
            snap.lastUsedMs = now;
            if (snap.builtMs != 0 && now - snap.builtMs < SNAPSHOT_INTERVAL_MS)
                return snap;
            snap.builtMs = now;

            // Sweep other groups' stale snapshots; cheap, only runs while some group is asking.
            for (auto itr = s_snapshots.begin(); itr != s_snapshots.end();)
            {
                if (itr->first != group->GetGUID() && now - itr->second.lastUsedMs > SNAPSHOT_IDLE_DROP_MS)
                    itr = s_snapshots.erase(itr);
                else
                    ++itr;
            }

            std::vector<Unit*> enemies;
            CollectEnemies(group, enemies);

            std::vector<CastWatch> fresh;
            for (Unit* enemy : enemies)
            {
                uint32 spellId = 0, finishMs = 0;
                if (!IsTargetCastingInterruptibleSpell(enemy, spellId, finishMs))
                    continue;
                if (!IsInterruptWorthwhile(sSpellMgr->GetSpellInfo(spellId)))
                    continue;

                auto known = std::find_if(snap.watches.begin(), snap.watches.end(),
                    [&](CastWatch const& w) { return w.enemy == enemy->GetGUID() && w.spellId == spellId; });
                if (known != snap.watches.end())
                {
                    known->finishMs = finishMs;
                    fresh.push_back(*known);
                    continue;
                }
                CastWatch watch;
                watch.enemy = enemy->GetGUID();
                watch.spellId = spellId;
                watch.firstSeenMs = now;
                watch.finishMs = finishMs;
                Assign(group, watch, enemy);
                fresh.push_back(watch);
            }
            snap.watches.swap(fresh);
            return snap;
        }
    }

    Unit* GroupDirector::GetInterruptTarget(Player* bot, uint32& outSpellId)
    {
        outSpellId = 0;
        if (!bot || !bot->IsAlive() || !bot->IsInWorld())
            return nullptr;
        Group* group = bot->GetGroup();
        if (!group)
            return nullptr;

        uint32 now = getMSTime();
        Snapshot& snap = Refresh(group, now);
        if (snap.watches.empty())
            return nullptr;

        for (CastWatch const& watch : snap.watches)
        {
            bool isPrimary = watch.primary == bot->GetGUID();
            bool isBackup = watch.backup == bot->GetGUID();
            if (!isPrimary && !isBackup)
                continue;

            uint32 due = watch.firstSeenMs + ReactionMs(bot->GetGUID());
            if (isBackup && !isPrimary && !watch.primaryRefused)
                due += BACKUP_DELAY_MS;
            if (now < due || watch.finishMs <= now + MIN_REMAINING_MS)
                continue;

            Unit* enemy = ObjectAccessor::GetUnit(*bot, watch.enemy);
            if (!enemy || !enemy->IsAlive())
                continue;
            uint32 spellId = SelectInterruptSpell(bot, enemy);
            if (!spellId)
                continue;
            outSpellId = spellId;
            return enemy;
        }
        return nullptr;
    }

    void GroupDirector::ReportInterruptAttempt(Player* bot, ObjectGuid enemy, bool accepted)
    {
        if (!bot || accepted)
            return;
        Group* group = bot->GetGroup();
        if (!group)
            return;
        auto itr = s_snapshots.find(group->GetGUID());
        if (itr == s_snapshots.end())
            return;
        for (CastWatch& watch : itr->second.watches)
            if (watch.enemy == enemy && watch.primary == bot->GetGUID())
                watch.primaryRefused = true;
    }

    void GroupDirector::ForgetBot(ObjectGuid botGuid)
    {
        for (auto& [groupGuid, snap] : s_snapshots)
            for (CastWatch& watch : snap.watches)
            {
                if (watch.primary == botGuid)
                    watch.primary = ObjectGuid::Empty;
                if (watch.backup == botGuid)
                    watch.backup = ObjectGuid::Empty;
            }
    }

    void GroupDirector::ForgetGroup(ObjectGuid groupGuid)
    {
        s_snapshots.erase(groupGuid);
    }

    uint32 GroupDirector::SnapshotCount()
    {
        return uint32(s_snapshots.size());
    }
}
