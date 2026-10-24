/*
 * mod-coa-playerbots
 *
 * Group tactical layer: shared interrupt assignments. See GroupDirector.h.
 *
 * The decisions themselves (who is preferred, when a bot may act, when a cast counts as a new one) are in engine/CombatDecisions.h and are
 * unit-tested there; this file gathers the facts from the game and keeps the per-group state.
 */

#include "engine/GroupDirector.h"
#include "engine/CastGuard.h"
#include "engine/CombatDecisions.h"
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
        // Bounded scans: at most this many enemies are examined per snapshot and this many group members are candidates for one cast.
        constexpr size_t MAX_ENEMIES = 16;
        constexpr size_t MAX_CANDIDATES = 40;

        struct CastWatch
        {
            ObjectGuid enemy;
            uint32 spellId = 0;
            uint32 firstSeenMs = 0;
            uint32 finishMs = 0;
            ObjectGuid primary;
            ObjectGuid backup;
            bool primaryUsable = false;      // result of the last validation of the primary (see Revalidate)
            bool primaryRefused = false;     // the primary's kick was refused: the backup acts as soon as its reaction time is over
            bool primaryUnconfirmed = false; // the primary's kick was accepted but the enemy kept casting: same
            uint32 primaryAcceptedMs = 0;    // when the primary's accepted kick was sent, until confirmed or declared failed
            ObjectGuid acceptedBy;           // who sent the accepted kick (primary or backup)
            uint32 nextBackupFillMs = 0;     // earliest time to look for a backup again after none was found
        };

        struct Snapshot
        {
            uint32 builtMs = 0;
            uint32 lastUsedMs = 0;
            std::vector<CastWatch> watches;
            // [bot] -> last time it asked for its assignment: a bot that stopped asking is not playing (not ticked, frozen, loading).
            std::unordered_map<ObjectGuid, uint32> lastPolledMs;
        };

        // [group guid] -> snapshot. Bots are ticked from the world thread (see BotMgr); nothing here is touched from a map thread.
        std::unordered_map<ObjectGuid, Snapshot> s_snapshots;
        GroupDirector::Stats s_stats;

        uint32 ReactionMs(ObjectGuid botGuid)
        {
            return Decisions::INTERRUPT_REACTION_BASE_MS + uint32(botGuid.GetCounter() % Decisions::INTERRUPT_REACTION_SPREAD_MS);
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

        // Can `member` deliver an interrupt on `enemy` right now? Alive and in the same map, still in the group, not in the middle of another cast,
        // not stunned/silenced/feared/confused, in line of sight, knows a kick that is off cooldown and in range, and has been playing (asking
        // for its assignment) recently. `spellId` receives the kick it would use.
        bool CanInterruptNow(Snapshot const& snap, Group* group, Player* member, Unit* enemy, uint32 watchSeenMs, uint32 now, uint32& spellId)
        {
            spellId = 0;
            if (!member || !member->IsAlive() || !member->IsInWorld() || member->GetMap() != enemy->GetMap() || member->GetGroup() != group)
                return false;
            if (CastGuard::IsCurrentlyCasting(member))
                return false;
            if (member->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING))
                return false;
            if (!member->IsWithinLOSInMap(enemy))
                return false;
            auto polled = snap.lastPolledMs.find(member->GetGUID());
            uint32 lastSign = polled != snap.lastPolledMs.end() ? std::max(polled->second, watchSeenMs) : watchSeenMs;
            if (now - lastSign > Decisions::INTERRUPT_BOT_STALE_MS)
                return false;
            spellId = SelectInterruptSpell(member, enemy);
            if (!spellId)
                return false;
            // Silence stops spells, not physical abilities: only a kick that is a spell is lost to it.
            SpellInfo const* kick = sSpellMgr->GetSpellInfo(spellId);
            if (kick && kick->PreventionType == SPELL_PREVENTION_TYPE_SILENCE && member->HasAuraType(SPELL_AURA_MOD_SILENCE))
            {
                spellId = 0;
                return false;
            }
            return true;
        }

        Player* FindMember(Group* group, ObjectGuid guid)
        {
            if (!guid)
                return nullptr;
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    if (member->GetGUID() == guid)
                        return member;
            return nullptr;
        }

        // Keeps a watch's assignment honest: the primary and the backup are checked against the current game state, a failed primary is
        // replaced by the backup, and an empty slot is filled from the group. `taken` lists bots that are primary on a cast handled earlier in
        // the same pass (a bot can only do one thing at a time).
        void Revalidate(Snapshot const& snap, Group* group, CastWatch& watch, Unit* enemy, uint32 now, std::vector<uint64>& taken)
        {
            uint32 spellId = 0;
            Player* primary = FindMember(group, watch.primary);
            bool primaryOk = primary && CanInterruptNow(snap, group, primary, enemy, watch.firstSeenMs, now, spellId);
            // A bot already primary on an earlier cast of this pass cannot be primary here as well.
            if (primaryOk && std::find(taken.begin(), taken.end(), watch.primary.GetRawValue()) != taken.end())
                primaryOk = false;
            Player* backup = FindMember(group, watch.backup);
            bool backupOk = backup && CanInterruptNow(snap, group, backup, enemy, watch.firstSeenMs, now, spellId);

            if (!primaryOk && backupOk)
            {
                // The primary cannot do it any more: the backup takes over, no reaction wait for a stage it never held.
                watch.primary = watch.backup;
                watch.backup = ObjectGuid::Empty;
                watch.primaryRefused = false;
                watch.primaryUnconfirmed = false;
                watch.primaryAcceptedMs = 0;
                primaryOk = true;
                backupOk = false;
                ++s_stats.reassigned;
            }

            // Fill empty slots from the members who can act. A missing backup is looked for again once a second only: with a single
            // interrupter in the group there never is one, and the scan is not free.
            if (!primaryOk || (!backupOk && now >= watch.nextBackupFillMs))
            {
                std::vector<uint64> excluded = taken;
                if (primaryOk)
                    excluded.push_back(watch.primary.GetRawValue());
                if (backupOk)
                    excluded.push_back(watch.backup.GetRawValue());
                Decisions::InterruptCandidate candidates[MAX_CANDIDATES];
                Player* bots[MAX_CANDIDATES];
                size_t n = 0;
                for (GroupReference* ref = group->GetFirstMember(); ref && n < MAX_CANDIDATES; ref = ref->next())
                {
                    Player* member = ref->GetSource();
                    if (!member || !member->IsBot())
                        continue;
                    uint32 kick = 0;
                    Decisions::InterruptCandidate& c = candidates[n];
                    c.id = member->GetGUID().GetRawValue();
                    c.preference = RolePreference(member);
                    c.distance = member->GetDistance(enemy);
                    c.usable = CanInterruptNow(snap, group, member, enemy, watch.firstSeenMs, now, kick);
                    bots[n++] = member;
                }
                // pair.primary is the best free bot, pair.backup the second best.
                Decisions::InterruptPair pair = Decisions::ChooseInterrupters(candidates, n, excluded.data(), excluded.size());
                int next = pair.primary, after = pair.backup;
                if (!primaryOk)
                {
                    if (next >= 0)
                    {
                        watch.primary = bots[next]->GetGUID();
                        watch.primaryRefused = watch.primaryUnconfirmed = false;
                        watch.primaryAcceptedMs = 0;
                        primaryOk = true;
                    }
                    else
                        watch.primary = ObjectGuid::Empty;
                    next = after;
                }
                if (!backupOk)
                {
                    watch.backup = (primaryOk && next >= 0) ? bots[next]->GetGUID() : ObjectGuid::Empty;
                    if (!watch.backup)
                        watch.nextBackupFillMs = now + 1000;
                }
            }
            watch.primaryUsable = primaryOk;
            if (primaryOk)
                taken.push_back(watch.primary.GetRawValue());
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

            struct Seen { Unit* enemy; uint32 spellId; uint32 finishMs; };
            std::vector<Seen> seen;
            for (Unit* enemy : enemies)
            {
                uint32 spellId = 0, finishMs = 0;
                if (!IsTargetCastingInterruptibleSpell(enemy, spellId, finishMs))
                    continue;
                if (!IsInterruptWorthwhile(sSpellMgr->GetSpellInfo(spellId)))
                    continue;
                seen.push_back({ enemy, spellId, finishMs });
            }
            // The cast that ends first is served first: it gets the first choice of interrupter.
            std::sort(seen.begin(), seen.end(), [](Seen const& a, Seen const& b) { return a.finishMs < b.finishMs; });

            std::vector<CastWatch> previous;
            previous.swap(snap.watches);
            std::vector<uint64> taken;
            for (Seen const& s : seen)
            {
                auto known = std::find_if(previous.begin(), previous.end(),
                    [&](CastWatch const& w) { return w.enemy == s.enemy->GetGUID() && w.spellId == s.spellId; });
                CastWatch watch;
                if (known != previous.end() && !Decisions::IsNewCastInstance(known->finishMs, now))
                {
                    watch = *known;
                    watch.finishMs = s.finishMs; // pushback moves the end of the same cast
                    previous.erase(known);
                    // An accepted kick that left the cast running did not take effect: hand it to the backup now.
                    if (watch.primaryAcceptedMs != 0 && now - watch.primaryAcceptedMs > Decisions::INTERRUPT_CONFIRM_MS)
                    {
                        watch.primaryAcceptedMs = 0;
                        watch.primaryUnconfirmed = true;
                        ++s_stats.unconfirmed;
                    }
                }
                else
                {
                    // First time this cast is seen (a repeat of the same spell after the earlier cast ended is a new cast: new reaction time).
                    watch.enemy = s.enemy->GetGUID();
                    watch.spellId = s.spellId;
                    watch.firstSeenMs = now;
                    watch.finishMs = s.finishMs;
                    ++s_stats.castsSeen;
                }
                Revalidate(snap, group, watch, s.enemy, now, taken);
                snap.watches.push_back(watch);
            }

            // Casts that are gone: stopped before their end after an accepted kick = interrupted; otherwise they finished or the enemy died.
            for (CastWatch const& gone : previous)
            {
                if (gone.acceptedBy && gone.finishMs > now + 100)
                    ++s_stats.confirmedStopped;
                else
                    ++s_stats.endedWithoutStop;
            }

            // Forget bots that stopped asking long ago so the map does not grow.
            for (auto itr = snap.lastPolledMs.begin(); itr != snap.lastPolledMs.end();)
            {
                if (now - itr->second > SNAPSHOT_IDLE_DROP_MS)
                    itr = snap.lastPolledMs.erase(itr);
                else
                    ++itr;
            }
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
        s_snapshots[group->GetGUID()].lastPolledMs[bot->GetGUID()] = now; // before Refresh: the asker counts as playing
        Snapshot& snap = Refresh(group, now);
        if (snap.watches.empty())
            return nullptr;

        for (CastWatch const& watch : snap.watches)
        {
            bool isPrimary = watch.primary == bot->GetGUID();
            bool isBackup = watch.backup == bot->GetGUID();
            if (!isPrimary && !isBackup)
                continue;

            Decisions::InterruptTiming timing;
            timing.now = now;
            timing.firstSeenMs = watch.firstSeenMs;
            timing.finishMs = watch.finishMs;
            timing.reactionMs = ReactionMs(bot->GetGUID());
            timing.isBackup = isBackup && !isPrimary;
            timing.primaryUsable = watch.primaryUsable;
            timing.primaryRefused = watch.primaryRefused;
            timing.primaryUnconfirmed = watch.primaryUnconfirmed;
            if (!Decisions::InterruptIsDue(timing))
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
        if (!bot)
            return;
        Group* group = bot->GetGroup();
        if (!group)
            return;
        auto itr = s_snapshots.find(group->GetGUID());
        if (itr == s_snapshots.end())
            return;
        uint32 now = getMSTime();
        ++s_stats.attempts;
        if (accepted)
            ++s_stats.accepted;
        else
            ++s_stats.refused;
        for (CastWatch& watch : itr->second.watches)
        {
            if (watch.enemy != enemy)
                continue;
            if (accepted)
            {
                // The game accepted the cast request; whether the enemy cast actually stopped is judged on later snapshots.
                if (watch.primary == bot->GetGUID() || watch.backup == bot->GetGUID())
                {
                    watch.acceptedBy = bot->GetGUID();
                    if (watch.primary == bot->GetGUID())
                        watch.primaryAcceptedMs = now;
                }
            }
            else if (watch.primary == bot->GetGUID())
                watch.primaryRefused = true;
        }
    }

    void GroupDirector::ForgetBot(ObjectGuid botGuid)
    {
        for (auto& [groupGuid, snap] : s_snapshots)
        {
            snap.lastPolledMs.erase(botGuid);
            for (CastWatch& watch : snap.watches)
            {
                // The empty slot is filled again on the next snapshot.
                if (watch.primary == botGuid)
                {
                    watch.primary = ObjectGuid::Empty;
                    watch.primaryUsable = false;
                }
                if (watch.backup == botGuid)
                    watch.backup = ObjectGuid::Empty;
                if (watch.acceptedBy == botGuid)
                    watch.acceptedBy = ObjectGuid::Empty;
            }
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

    GroupDirector::Stats const& GroupDirector::GetStats()
    {
        return s_stats;
    }
}
