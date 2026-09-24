/*
 * mod-coa-playerbots
 *
 * The pure decisions behind the social layer (WorldSocial, WorldParties): when stepping into
 * someone else's fight is right, which nearby bot makes a good temporary party member, and when a
 * temporary party has run its course. No engine types, so they are unit-tested in module/tests/;
 * the engine-facing code only reads the inputs off the world and acts on the answer.
 *
 * The one hard rule everywhere: helping never takes anything from the person helped. A bot only
 * joins a fight against a mob that is already tagged by the player it helps (or that player's
 * group), so the kill -- loot, quest credit, experience -- stays theirs whatever the bot adds.
 */

#ifndef COA_PLAYERBOTS_SOCIAL_RULES_H
#define COA_PLAYERBOTS_SOCIAL_RULES_H

#include "Define.h"
#include <cstdlib>

// Everything the "should I help?" decision looks at, read off the engine by WorldSocial.
struct AssistInput
{
    bool sameSide = false;             // same faction, not hostile to the bot
    bool victimInCombat = false;
    float victimHealthPct = 100.0f;
    bool attackerIsCreature = false;   // never a player or a player's pet: open-world PvP is not the bot's to join
    bool attackerTappedByVictim = false;
    bool attackerElite = false;
    bool attackerWorldBoss = false;
    uint8 attackerLevel = 1;
    uint8 botLevel = 1;
    float distance = 0.0f;
};

struct AssistRules
{
    float radius = 30.0f;
    float healthPct = 40.0f;        // only when the victim is really in trouble
    uint8 maxLevelAbove = 3;        // never take on a mob this far above the bot's level
    uint8 eliteLevelMargin = 5;     // an elite only when the bot out-levels it by this much
};

// A nearby bot being considered for a temporary party.
struct PartyCandidate
{
    uint8 leaderLevel = 1;
    uint8 level = 1;
    float distance = 0.0f;
    bool sharesQuest = false;       // has the leader's quest open, objective not done
    bool fillsMissingRole = false;  // a healer or tank the party does not have yet
    uint8 sociability = 50;
};

struct PartyRules
{
    float radius = 60.0f;
    uint8 maxLevelGap = 3;
};

// A temporary party's state, read off the engine each check.
struct PartyStatus
{
    uint32 now = 0;
    uint32 expiresMs = 0;
    bool leaderPresent = true;      // online and in the world
    bool leaderOnTask = true;       // still on the task the party formed for
    bool leaderFree = true;         // not suspended (manual command, instance, battleground)
    uint32 leaderDeadForMs = 0;     // 0 while alive
    uint32 members = 0;             // besides the leader
};

enum class PartyEnd : uint8
{
    No,
    Expired,
    LeaderGone,
    TaskDone,
    LeaderBusy,
    LeaderDead,
    Empty,
};

// One member's state, read off the engine each check.
struct PartyMemberStatus
{
    bool present = true;            // online and in the world
    bool sameMap = true;
    bool alive = true;
    uint32 deadForMs = 0;
    float distanceToLeader = 0.0f;
};

namespace SocialRules
{
    inline bool ShouldAssist(AssistInput const& in, AssistRules const& rules)
    {
        if (!in.sameSide || !in.victimInCombat || !in.attackerIsCreature)
            return false;
        // Somebody else's mob, or one nobody has tagged yet: stepping in would take the kill.
        if (!in.attackerTappedByVictim)
            return false;
        if (in.distance > rules.radius || in.victimHealthPct >= rules.healthPct)
            return false;
        if (in.attackerWorldBoss)
            return false;
        if (int(in.attackerLevel) > int(in.botLevel) + int(rules.maxLevelAbove))
            return false;
        if (in.attackerElite && int(in.botLevel) < int(in.attackerLevel) + int(rules.eliteLevelMargin))
            return false;
        return true;
    }

    // Higher is better; negative means "not a candidate at all".
    inline float PartyCandidateScore(PartyCandidate const& c, PartyRules const& rules)
    {
        if (!c.sharesQuest || c.distance > rules.radius)
            return -1.0f;
        int gap = std::abs(int(c.level) - int(c.leaderLevel));
        if (gap > int(rules.maxLevelGap))
            return -1.0f;
        float score = 100.0f;
        score -= float(gap) * 15.0f;
        score -= c.distance * (40.0f / (rules.radius > 1.0f ? rules.radius : 1.0f));
        score += float(c.sociability) * 0.2f;
        if (c.fillsMissingRole)
            score += 30.0f;
        return score;
    }

    // A party lasts somewhere in [minMs, maxMs], picked by a per-party roll.
    inline uint32 PartyLifetimeMs(uint32 roll, uint32 minMs, uint32 maxMs)
    {
        if (maxMs <= minMs)
            return minMs;
        return minMs + roll % (maxMs - minMs + 1);
    }

    inline PartyEnd ShouldEnd(PartyStatus const& s, uint32 leaderDeadGraceMs)
    {
        if (!s.leaderPresent)
            return PartyEnd::LeaderGone;
        if (s.members == 0)
            return PartyEnd::Empty;
        if (s.leaderDeadForMs > leaderDeadGraceMs)
            return PartyEnd::LeaderDead;
        if (!s.leaderFree)
            return PartyEnd::LeaderBusy;
        if (!s.leaderOnTask)
            return PartyEnd::TaskDone;
        if (s.now >= s.expiresMs)
            return PartyEnd::Expired;
        return PartyEnd::No;
    }

    // A member that fell far behind, left the map or stayed dead too long leaves the party; the
    // others carry on.
    inline bool ShouldDropMember(PartyMemberStatus const& m, float maxDistance, uint32 deadGraceMs)
    {
        if (!m.present || !m.sameMap)
            return true;
        if (!m.alive)
            return m.deadForMs > deadGraceMs;
        return m.distanceToLeader > maxDistance;
    }

    inline char const* PartyEndName(PartyEnd end)
    {
        switch (end)
        {
            case PartyEnd::No:         return "still going";
            case PartyEnd::Expired:    return "time is up";
            case PartyEnd::LeaderGone: return "leader gone";
            case PartyEnd::TaskDone:   return "the work here is done";
            case PartyEnd::LeaderBusy: return "leader busy elsewhere";
            case PartyEnd::LeaderDead: return "leader dead too long";
            case PartyEnd::Empty:      return "everyone else left";
        }
        return "?";
    }
}

#endif // COA_PLAYERBOTS_SOCIAL_RULES_H
