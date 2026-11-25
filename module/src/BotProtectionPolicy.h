/*
 * mod-coa-playerbots
 *
 * Unified bot protection predicate (PB-002):
 * A bot that belongs to a guild, is in an online or persisted group/raid,
 * is on a real player's incoming friends list (character_social), or is
 * executing a player/guild order MUST NOT be repurposed, re-specced,
 * relocated for population balancing, or drafted as a free LFG/BG reserve.
 */

#ifndef COA_PLAYERBOTS_BOT_PROTECTION_POLICY_H
#define COA_PLAYERBOTS_BOT_PROTECTION_POLICY_H

namespace BotProtectionPolicy
{
    struct ProtectionFacts
    {
        bool inGuild = false;
        bool hasGuildInvite = false;
        bool inOnlineGroupOrRaid = false;
        bool inPersistedGroupOrRaid = false;
        bool hasIncomingPlayerFriend = false;
        bool hasActiveOrder = false;
    };

    inline constexpr bool IsProtected(ProtectionFacts const& facts)
    {
        return facts.inGuild
            || facts.hasGuildInvite
            || facts.inOnlineGroupOrRaid
            || facts.inPersistedGroupOrRaid
            || facts.hasIncomingPlayerFriend
            || facts.hasActiveOrder;
    }

    inline constexpr char const* Reason(ProtectionFacts const& facts)
    {
        if (facts.inGuild)
            return "guild_member";
        if (facts.hasGuildInvite)
            return "guild_invite";
        if (facts.inOnlineGroupOrRaid)
            return "online_group";
        if (facts.inPersistedGroupOrRaid)
            return "persisted_group";
        if (facts.hasIncomingPlayerFriend)
            return "player_friend";
        if (facts.hasActiveOrder)
            return "active_order";
        return "unprotected";
    }
}

#endif
