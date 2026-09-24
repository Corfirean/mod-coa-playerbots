/*
 * mod-coa-playerbots
 *
 * Temporary bot-only parties: two to five bots of about the same level, near each other, with the
 * same kill quest open, team up for one objective area or a few minutes, then go their own ways --
 * the "several bots walking together for a while" a live world should show, and a faster kill
 * quest for all of them (group kill credit is shared).
 *
 * How it fits the rest of the layer:
 *  - the bot that forms the party (it just started a kill/collect objective task) is the leader
 *    and keeps its own WorldBrain running while grouped (BotAI asks IsLeader);
 *  - members are ordinary grouped bots: their brains suspend as for any group, they follow the
 *    leader and fight what it fights through the existing group behaviour;
 *  - the party ends when the leader's task ends, when its time is up (5-20 minutes by default),
 *    when the leader is gone, dead too long or busy elsewhere, or when nobody else is left; a
 *    member that falls far behind or stays dead too long just leaves.
 *
 * The group is a real Group (Create/AddMember, no invite packets, so nobody is teleported). The
 * core persists every group to the database; a temporary party must never outlive a restart, so
 * its rows are deleted a few seconds after it forms and the group lives in memory only.
 *
 * Off by default (CoaBots.WorldBrain.TemporaryParties) until it has been watched on a live
 * server, and never used in cluster mode, where the core's local Disband() does nothing.
 */

#ifndef COA_PLAYERBOTS_WORLD_PARTIES_H
#define COA_PLAYERBOTS_WORLD_PARTIES_H

#include "ObjectGuid.h"
#include "WorldBrainState.h"
#include <string>

class Player;

namespace WorldParties
{
    // Tries to gather a party around `leader` for the objective task it has just started.
    void TryForm(Player* leader, BrainState& state);

    // The leader of a temporary party keeps its own brain while grouped.
    bool IsLeader(ObjectGuid bot);

    // Leader or member of a temporary party.
    bool IsInParty(ObjectGuid bot);

    // The leader's task ended or was dropped: the party breaks up at the next check.
    void OnLeaderTaskEnded(ObjectGuid leader);

    // Upkeep: expiry, members lost, the leader moving on. Called from WorldBrain::GlobalUpdate.
    void Update(uint32 diff);

    // A bot logging out. Only marks it; the next Update handles the group, since the player
    // object is on its way out.
    void Forget(ObjectGuid bot);

    // One line for `.botcmd brain`, empty when the bot is not in a temporary party.
    std::string Describe(ObjectGuid bot);

    uint32 Count();
}

#endif // COA_PLAYERBOTS_WORLD_PARTIES_H
