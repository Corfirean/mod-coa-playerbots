#include "BotProtectionPolicy.h"
#include <cassert>
#include <cstring>
#include <iostream>

int main()
{
    using namespace BotProtectionPolicy;

    ProtectionFacts empty;
    assert(!IsProtected(empty));
    assert(std::strcmp(Reason(empty), "unprotected") == 0);

    ProtectionFacts guild;
    guild.inGuild = true;
    assert(IsProtected(guild));
    assert(std::strcmp(Reason(guild), "guild_member") == 0);

    ProtectionFacts invite;
    invite.hasGuildInvite = true;
    assert(IsProtected(invite));

    ProtectionFacts onlineGroup;
    onlineGroup.inOnlineGroupOrRaid = true;
    assert(IsProtected(onlineGroup));

    ProtectionFacts persistedGroup;
    persistedGroup.inPersistedGroupOrRaid = true;
    assert(IsProtected(persistedGroup));

    ProtectionFacts incomingFriend;
    incomingFriend.hasIncomingPlayerFriend = true;
    assert(IsProtected(incomingFriend));

    ProtectionFacts activeOrder;
    activeOrder.hasActiveOrder = true;
    assert(IsProtected(activeOrder));

    std::cout << "Protection policy: all 6 protection invariants (guild, invite, online/persisted group, incoming friend, order) passed.\n";
    return 0;
}
