/*
 * mod-coa-playerbots
 *
 * When a quest is set aside, and the rare case where it is abandoned. Pure, unit-tested.
 *
 * Two very different kinds of "this isn't working":
 *
 *  - Transient: no live targets right now, a respawn delay, a path problem, a crowded camp, an
 *    interaction or cast that failed, a death. The world changes, so the quest is set aside for a
 *    while -- longer each time it fails again in a row -- and retried later while the bot does
 *    something else. A transient failure never abandons a quest: a pathing defect or a busy camp
 *    must not cost a bot a quest chain.
 *
 *  - Dead end: nothing this build can do will ever finish the quest from where the bot stands in
 *    it -- an open objective no handler executes, a quest that needs player kills or reputation or
 *    has no ender, a quest that has already failed, a quest-provided item that is gone. The planner
 *    never works on it. It is abandoned only when the log is so full that it blocks new work, one
 *    per cleanup pass, because taking up a slot is the only thing leaving it there costs.
 */

#ifndef COA_PLAYERBOTS_QUEST_POLICY_H
#define COA_PLAYERBOTS_QUEST_POLICY_H

#include "Define.h"
#include <algorithm>

namespace QuestPolicy
{
    // How long a quest is set aside after its `suspensions`-th transient failure in a row: the base
    // time, doubled per repeat, capped.
    inline uint32 SuspendMs(uint32 baseMs, uint32 suspensions, uint32 capMs)
    {
        uint64 ms = baseMs;
        for (uint32 i = 1; i < suspensions && ms < capMs; ++i)
            ms *= 2;
        return uint32(std::min<uint64>(ms, std::max(baseMs, capMs)));
    }

    // A dead-end quest leaves the log only when the log has no room for new work.
    inline bool ShouldAbandonDeadEnd(uint32 questsInLog, uint32 maxActiveQuests)
    {
        return questsInLog >= maxActiveQuests;
    }

    // A quest the bot can still finish: completable at all, and every objective still open is one
    // this build can execute. `openObjectives` / `openExecutable` count the objectives not done yet.
    inline bool Workable(bool completable, uint32 openObjectives, uint32 openExecutable)
    {
        return completable && openExecutable == openObjectives;
    }
}

#endif // COA_PLAYERBOTS_QUEST_POLICY_H
