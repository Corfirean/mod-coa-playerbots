#include "BotMgr.h"
#include <algorithm>
#include "AscensionCoATalentData.h"
#include "BotAI.h"
#include "ClassSpecRoles.h"
#include "CellImpl.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Corpse.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "GuildPackets.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "LootMgr.h"
#include "Mail.h"
#include "ObjectAccessor.h"
#include "PetDefines.h"
#include "Player.h"
#include "QueryHolder.h"
#include "SharedDefines.h"
#include "SpellAuraDefines.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

namespace
{
// Acore::NearestHostileUnitCheck (GridNotifiers.h) does exactly this shrinking-radius
// "nearest" search already, but its constructor only accepts a Creature const* -- a bot is a
// Player, so it can't be reused directly. This is the same check, generalized to any Unit.
class NearestHostileUnitInObjectRangeCheck
{
public:
    explicit NearestHostileUnitInObjectRangeCheck(Unit const* me, float range) : _me(me), _range(range) { }
    bool operator()(Unit* u)
    {
        if (!_me->IsWithinDistInMap(u, _range, true, false, false))
            return false;
        if (!_me->IsValidAttackTarget(u))
            return false;
        _range = _me->GetDistance(u); // shrink the search radius to the closest hit so far
        return true;
    }

private:
    Unit const* _me;
    float _range;
};

char const* RoleToString(BotRole role)
{
    switch (role)
    {
        case BotRole::Tank: return "tank";
        case BotRole::Healer: return "healer";
        case BotRole::Support: return "support";
        case BotRole::Dps: default: return "dps";
    }
}

// itemEntry -> every learned-spell id with a SPELL_EFFECT_CREATE_ITEM effect producing it.
// Built once (first CraftOrder call) by scanning the full spell store -- crafting orders are
// rare, one-off events, not a per-tick operation, so a one-time O(spell count) scan is cheap
// relative to how infrequently this runs, and far simpler than trying to reach the same data
// through SkillLineAbility/profession bookkeeping this module doesn't otherwise track. A bot
// merely *knowing* the resulting spell (Player::HasSpell) already proves it leveled the right
// profession to the right skill -- no separate skill-level check needed.
std::unordered_map<uint32, std::vector<uint32>> const& CraftingRecipeIndex()
{
    static std::unordered_map<uint32, std::vector<uint32>> index = []
    {
        std::unordered_map<uint32, std::vector<uint32>> map;
        for (uint32 id = 0; id < sSpellMgr->GetSpellInfoStoreSize(); ++id)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
            if (!info)
                continue;
            for (SpellEffectInfo const& effect : info->GetEffects())
                if (effect.Effect == SPELL_EFFECT_CREATE_ITEM && effect.ItemType)
                    map[effect.ItemType].push_back(id);
        }
        return map;
    }();
    return index;
}

bool CrafterHasReagentsFor(Player* crafter, SpellInfo const* spellInfo)
{
    for (uint8 i = 0; i < MAX_SPELL_REAGENTS; ++i)
    {
        if (spellInfo->Reagent[i] <= 0 || !spellInfo->ReagentCount[i])
            continue;
        if (crafter->GetItemCount(uint32(spellInfo->Reagent[i]), false) < spellInfo->ReagentCount[i])
            return false;
    }
    return true;
}

// Mails a single already-in-inventory item stack from `sender` to `receiver`, real MailDraft
// path -- same sequence WorldSession::HandleSendMail uses (item removed from the sender's
// inventory/DB, ownership transferred, then attached to the draft), just without the gold
// cost or a client-authored subject/body. Works whether `receiverCharLowGuid` is online or
// not (MailReceiver's lowguid-only constructor doesn't require a live Player).
void MailCraftedItem(Player* sender, ObjectGuid::LowType receiverCharLowGuid, Item* item, std::string const& subject)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    sender->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);
    item->DeleteFromInventoryDB(trans);
    if (item->GetState() == ITEM_UNCHANGED)
        item->FSetState(ITEM_CHANGED);
    item->SetOwnerGUID(ObjectGuid::Create<HighGuid::Player>(receiverCharLowGuid));
    item->SaveToDB(trans);

    MailDraft(subject, "")
        .AddItem(item)
        .SendMailTo(trans, MailReceiver(receiverCharLowGuid), MailSender(sender), MAIL_CHECK_MASK_COPIED);

    sender->SaveInventoryAndGoldToDB(trans);
    CharacterDatabase.CommitTransaction(trans);
}

struct ProfessionSkillEntry
{
    uint32 skillId;
    char const* name;
};

// The 11 standard WotLK profession skill lines (8 primary crafting/gathering + First
// Aid/Cooking/Fishing) -- stable, well-known skill line ids, not custom to this server.
constexpr ProfessionSkillEntry PROFESSION_SKILLS[] =
{
    { 164, "Blacksmithing" }, { 165, "Leatherworking" }, { 171, "Alchemy" },
    { 182, "Herbalism" },     { 186, "Mining" },         { 197, "Tailoring" },
    { 202, "Engineering" },   { 333, "Enchanting" },     { 393, "Skinning" },
    { 755, "Jewelcrafting" }, { 773, "Inscription" },
    { 129, "First Aid" },     { 185, "Cooking" },        { 356, "Fishing" },
};
}

BotMgr* BotMgr::instance()
{
    static BotMgr instance;
    return &instance;
}

WorldSession* BotMgr::FindBotSession(ObjectGuid::LowType charLowGuid) const
{
    for (WorldSession* session : _botSessions)
    {
        if (Player* bot = session->GetPlayer())
            if (bot->GetGUID().GetCounter() == charLowGuid)
                return session;
    }
    return nullptr;
}

void BotMgr::SpawnBot(ObjectGuid::LowType charLowGuid, ChatHandler* handler)
{
    ObjectGuid playerGuid = ObjectGuid::Create<HighGuid::Player>(charLowGuid);

    uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(playerGuid);
    if (!accountId)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no character with guid {} found in the character cache.", charLowGuid);
        return;
    }

    // Real WorldSession, real Player, but a null socket instead of a real
    // client connection — this is the core mechanism proven in pilot/. No
    // bot-aware constructor flag: CoA's WorldSession constructor is used
    // exactly as-is (see mod-coa-playerbots' core-diff-analysis.md for why
    // playerbots-fork's own constructor adds one and why this project
    // deliberately doesn't, yet).
    WorldSession* botSession = new WorldSession(accountId, "", 0x0, nullptr, SEC_PLAYER,
        EXPANSION_WRATH_OF_THE_LICH_KING, time_t(0), sWorld->GetDefaultDbcLocale(), 0, false, false, 0);

    std::shared_ptr<LoginQueryHolder> holder = std::make_shared<LoginQueryHolder>(accountId, playerGuid);
    if (!holder->Initialize())
    {
        LOG_ERROR("module.coa-playerbots", "BotMgr: LoginQueryHolder::Initialize() failed for guid {}", charLowGuid);
        delete botSession;
        return;
    }

    _botSessions.push_back(botSession);

    // Deliberately sWorld->AddQueryHolderCallback, not botSession->AddQueryHolderCallback:
    // a detached, null-socket session is never registered with WorldSessionMgr, so its own
    // (private, World-friend-only) ProcessQueryCallbacks() never gets driven by anything.
    // World::ProcessQueryCallbacks() runs unconditionally every tick regardless of session
    // registration, so callbacks queued on the World-level processor still complete. See
    // pilot/README.md for the full story of how this was found.
    sWorld->AddQueryHolderCallback(CharacterDatabase.DelayQueryHolder(holder)).AfterComplete(
        [botSession](SQLQueryHolderBase const& completedHolder)
        {
            botSession->HandlePlayerLoginFromDB(static_cast<LoginQueryHolder const&>(completedHolder));

            if (Player* bot = botSession->GetPlayer())
            {
                LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' ({}) logged in successfully.",
                    bot->GetName(), bot->GetGUID().ToString());
            }
            else
            {
                LOG_ERROR("module.coa-playerbots", "BotMgr: bot login failed, Player is null after HandlePlayerLoginFromDB.");
            }
        });

    if (handler)
        handler->PSendSysMessage("BotMgr: login query issued for guid {}, watch the server log for the result.", charLowGuid);
}

void BotMgr::DoAcceptInvite(WorldSession* session)
{
    // HandleGroupAcceptOpcode only does recvData.read_skip<uint32>() before the real logic
    // (RemoveInvite, validation, Create-if-new, AddMember, BroadcastGroupUpdate) — calling it
    // directly with a minimal padding packet reuses that real logic verbatim instead of
    // duplicating it by hand. Same trick pilot/ already used for login.
    WorldPacket fakePacket;
    fakePacket << uint32(0);
    session->HandleGroupAcceptOpcode(fakePacket);

    Player* bot = session->GetPlayer();
    if (!bot)
    {
        LOG_ERROR("module.coa-playerbots", "BotMgr: DoAcceptInvite: session has no Player after HandleGroupAcceptOpcode.");
        return;
    }

    Group* group = bot->GetGroup();
    if (!group)
    {
        LOG_ERROR("module.coa-playerbots", "BotMgr: DoAcceptInvite: bot '{}' has no group after HandleGroupAcceptOpcode -- AddMember must have failed or returned early.", bot->GetName());
        return;
    }

    LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' is now in a group, leader guid {}.", bot->GetName(), group->GetLeaderGUID().ToString());

    if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
    {
        LOG_INFO("module.coa-playerbots", "BotMgr: resolved leader '{}' (in world: {}).", leader->GetName(), leader->IsInWorld());
        if (leader != bot)
        {
            LOG_INFO("module.coa-playerbots", "BotMgr: teleporting bot '{}' to group leader '{}'.",
                bot->GetName(), leader->GetName());
            bot->TeleportTo(leader->GetWorldLocation());

            // Don't fire the ack here -- see FinishPendingTeleport's comment in the
            // header for why doing it in the same tick as TeleportTo() crashes.
            _pendingTeleportAck.push_back(session);
        }
    }
    else
    {
        LOG_ERROR("module.coa-playerbots", "BotMgr: DoAcceptInvite: ObjectAccessor::FindPlayer could not resolve leader guid {}.",
            group->GetLeaderGUID().ToString());
    }
}

void BotMgr::EnsureBotBankRights(Player* bot, Guild* guild)
{
    if (!guild || !bot)
        return;

    // If guild has no tabs yet, and bot is the Guild Master, create initial tab 0
    if (guild->_GetPurchasedTabsSize() == 0)
    {
        if (guild->GetLeaderGUID() == bot->GetGUID())
            guild->_CreateNewBankTab();
        else
            return;
    }

    uint8 rankId = bot->GetRank();
    Guild::RankInfo* rankInfo = guild->GetRankInfo(rankId);
    if (!rankInfo)
        return;

    for (uint8 tabId = 0; tabId < guild->_GetPurchasedTabsSize(); ++tabId)
    {
        int8 currentRights = rankInfo->GetBankTabRights(tabId);
        int32 currentSlots = rankInfo->GetBankTabSlotsPerDay(tabId);

        bool needsUpdate = false;
        uint8 newRights = currentRights;
        uint32 newSlots = currentSlots > 0 ? uint32(currentSlots) : 50;

        if ((currentRights & GUILD_BANK_RIGHT_DEPOSIT_ITEM) != GUILD_BANK_RIGHT_DEPOSIT_ITEM)
        {
            newRights |= GUILD_BANK_RIGHT_DEPOSIT_ITEM | GUILD_BANK_RIGHT_VIEW_TAB;
            needsUpdate = true;
        }

        if (currentSlots == 0)
        {
            newSlots = 50;
            needsUpdate = true;
        }

        if (needsUpdate)
        {
            GuildBankRightsAndSlots rightsAndSlots(tabId, newRights, newSlots);
            rankInfo->SetBankTabSlotsAndRights(rightsAndSlots, true);
        }
    }

    if (rankInfo->GetBankMoneyPerDay() == 0)
        rankInfo->SetBankMoneyPerDay(50 * GOLD);
}

void BotMgr::DoAcceptGuildInvite(WorldSession* session)
{
    Player* bot = session->GetPlayer();
    if (!bot)
    {
        LOG_ERROR("module.coa-playerbots", "BotMgr: DoAcceptGuildInvite: session has no Player.");
        return;
    }

    uint32 invitedGuildId = bot->GetGuildIdInvited();
    if (!invitedGuildId)
    {
        LOG_WARN("module.coa-playerbots", "BotMgr: DoAcceptGuildInvite: bot '{}' has no pending guild invite.", bot->GetName());
        return;
    }

    WorldPacket packet(CMSG_GUILD_ACCEPT, 0);
    WorldPackets::Guild::AcceptGuildInvite acceptPacket(std::move(packet));
    session->HandleGuildAcceptOpcode(acceptPacket);

    // If accept failed (e.g. guild disbanded, cross-faction disallowed), clear the invite
    // so the auto-accept loop doesn't spin forever.
    if (bot->GetGuildIdInvited() == invitedGuildId && !bot->GetGuildId())
    {
        LOG_WARN("module.coa-playerbots", "BotMgr: DoAcceptGuildInvite: failed to join guild {} for bot '{}', clearing invite.",
            invitedGuildId, bot->GetName());
        bot->SetGuildIdInvited(0);
    }
    else
    {
        LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' successfully joined guild '{}' (id {}).",
            bot->GetName(), bot->GetGuildName(), bot->GetGuildId());
        if (Guild* guild = bot->GetGuild())
            EnsureBotBankRights(bot, guild);
    }
}

void BotMgr::FinishPendingTeleport(WorldSession* session)
{
    Player* bot = session->GetPlayer();
    if (!bot)
        return;

    // HandleMoveWorldportAck() already has a no-packet "for server-side calls"
    // overload; the near case needs a minimal packed-guid packet built the same
    // way as every other "call the real handler directly" trick this module uses.
    if (bot->IsBeingTeleportedNear())
    {
        WorldPacket ackPacket;
        ackPacket << bot->GetGUID().WriteAsPacked();
        ackPacket << uint32(0); // flags, unused by the handler
        ackPacket << uint32(0); // time, unused by the handler
        session->HandleMoveTeleportAck(ackPacket);
    }
    else if (bot->IsBeingTeleportedFar())
    {
        session->HandleMoveWorldportAck();
    }
    else
    {
        // Not (or no longer) mid-teleport -- nothing to finish.
        return;
    }

    LOG_INFO("module.coa-playerbots", "BotMgr: finished pending teleport for bot '{}'.", bot->GetName());

    // Start following only once the teleport has actually landed -- doing this
    // before the ack would give MoveFollow a stale/pre-teleport position to work from.
    if (Group* group = bot->GetGroup())
    {
        if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
        {
            if (leader != bot)
                bot->GetMotionMaster()->MoveFollow(leader, PET_FOLLOW_DIST, BotAI::ComputeFollowAngle(bot));
        }
    }
}

void BotMgr::TryReturnGhostToCorpseMap(WorldSession* session)
{
    Player* bot = session->GetPlayer();
    if (!bot || !bot->IsInWorld() || bot->IsAlive())
        return;

    if (bot->IsBeingTeleportedNear() || bot->IsBeingTeleportedFar())
        return;

    // BG ghosts are meant to stay at the graveyard they were sent to -- see
    // BotAI::UpdateDeathHandling's comment on why corpse-running is deliberately skipped there.
    if (!bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) || bot->InBattleground())
        return;

    Corpse* corpse = bot->GetCorpse();
    if (!corpse || corpse->GetMapId() == bot->GetMapId())
        return;

    LOG_INFO("module.coa-playerbots", "BotMgr: ghost '{}' is on map {} but its corpse is on map {} -- teleporting to the corpse's map.",
        bot->GetName(), bot->GetMapId(), corpse->GetMapId());

    bot->TeleportTo(corpse->GetMapId(), corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ(), bot->GetOrientation());
    _pendingTeleportAck.push_back(session);
}

void BotMgr::QueueTeleportAck(WorldSession* session)
{
    _pendingTeleportAck.push_back(session);
}

Player* BotMgr::FindBotPlayer(ObjectGuid::LowType charLowGuid) const
{
    WorldSession* session = FindBotSession(charLowGuid);
    return session ? session->GetPlayer() : nullptr;
}

std::vector<Player*> BotMgr::GetOnlineBots() const
{
    std::vector<Player*> bots;
    bots.reserve(_botSessions.size());
    for (WorldSession* session : _botSessions)
        if (Player* bot = session->GetPlayer())
            bots.push_back(bot);
    return bots;
}

void BotMgr::TryFollowLeaderAcrossMaps(WorldSession* session)
{
    Player* bot = session->GetPlayer();
    if (!bot || !bot->IsInWorld())
        return;

    // Already mid-teleport (this tick's own queue below, or something else entirely) --
    // let FinishPendingTeleport settle it first rather than stacking a second request.
    if (bot->IsBeingTeleportedNear() || bot->IsBeingTeleportedFar())
        return;

    Group* group = bot->GetGroup();
    if (!group)
        return;

    Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
    if (!leader || leader == bot || !leader->IsInWorld())
        return;

    if (bot->GetMapId() == leader->GetMapId() && bot->GetInstanceId() == leader->GetInstanceId())
        return;

    LOG_INFO("module.coa-playerbots", "BotMgr: leader '{}' is on map {} (instance {}), bot '{}' is on map {} (instance {}) -- teleporting bot to leader.",
        leader->GetName(), leader->GetMapId(), leader->GetInstanceId(), bot->GetName(), bot->GetMapId(), bot->GetInstanceId());

    bot->TeleportTo(leader->GetWorldLocation());
    _pendingTeleportAck.push_back(session);
}

void BotMgr::AcceptInvite(ObjectGuid::LowType charLowGuid, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    if (!session)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {} (spawn it first).", charLowGuid);
        return;
    }

    Player* bot = session->GetPlayer();
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot session for guid {} has no Player yet (login still pending?).", charLowGuid);
        return;
    }

    if (!bot->GetGroupInvite())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' has no pending group invite — invite it first from a real client.", bot->GetName());
        return;
    }

    DoAcceptInvite(session);

    if (handler)
        handler->PSendSysMessage("BotMgr: accept-invite issued for bot '{}', check .group list to confirm.", bot->GetName());
}

void BotMgr::AcceptGuildInvite(ObjectGuid::LowType charLowGuid, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    if (!session)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {} (spawn it first).", charLowGuid);
        return;
    }

    Player* bot = session->GetPlayer();
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot session for guid {} has no Player yet (login still pending?).", charLowGuid);
        return;
    }

    if (bot->GetGuildId() != 0)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' is already in a guild (id {}, '{}').",
                bot->GetName(), bot->GetGuildId(), bot->GetGuildName());
        return;
    }

    if (!bot->GetGuildIdInvited())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' has no pending guild invite — invite it first.", bot->GetName());
        return;
    }

    DoAcceptGuildInvite(session);

    if (handler)
    {
        if (bot->GetGuildId() != 0)
            handler->PSendSysMessage("BotMgr: accept-guild-invite succeeded for bot '{}', joined guild '{}' (id {}).",
                bot->GetName(), bot->GetGuildName(), bot->GetGuildId());
        else
            handler->PSendSysMessage("BotMgr: accept-guild-invite failed for bot '{}'.", bot->GetName());
    }
}

void BotMgr::GuildInvite(ObjectGuid::LowType charLowGuid, std::string const& targetName, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    if (!session)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {} (spawn it first).", charLowGuid);
        return;
    }

    Player* bot = session->GetPlayer();
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot session for guid {} has no Player yet (login still pending?).", charLowGuid);
        return;
    }

    Guild* guild = bot->GetGuild();
    if (!guild)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' is not in a guild.", bot->GetName());
        return;
    }

    guild->HandleInviteMember(session, targetName);

    if (handler)
        handler->PSendSysMessage("BotMgr: guild invite for '{}' issued by '{}' (guild '{}').",
            targetName, bot->GetName(), guild->GetName());
}

void BotMgr::GuildCreate(ObjectGuid::LowType charLowGuid, std::string const& guildName, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    if (!session)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {} (spawn it first).", charLowGuid);
        return;
    }

    Player* bot = session->GetPlayer();
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot session for guid {} has no Player yet.", charLowGuid);
        return;
    }

    if (bot->GetGuildId())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' is already in guild '{}' (id {}).",
                bot->GetName(), bot->GetGuildName(), bot->GetGuildId());
        return;
    }

    if (guildName.empty())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: guild name cannot be empty.");
        return;
    }

    if (sGuildMgr->GetGuildByName(guildName))
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: guild with name '{}' already exists.", guildName);
        return;
    }

    Guild* guild = new Guild;
    if (!guild->Create(bot, guildName))
    {
        delete guild;
        if (handler)
            handler->PSendSysMessage("BotMgr: failed to create guild '{}' for bot '{}'.", guildName, bot->GetName());
        return;
    }

    sGuildMgr->AddGuild(guild);
    EnsureBotBankRights(bot, guild);

    if (handler)
        handler->PSendSysMessage("BotMgr: bot '{}' created guild '{}' (id {}).",
            bot->GetName(), guild->GetName(), guild->GetId());
    LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' created guild '{}' (id {}).",
        bot->GetName(), guild->GetName(), guild->GetId());
}

uint32 BotMgr::GuildDepositItem(ObjectGuid::LowType charLowGuid, uint32 itemEntry, uint32 count, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    if (!session)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {}.", charLowGuid);
        return 0;
    }

    Player* bot = session->GetPlayer();
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot session for guid {} has no Player yet.", charLowGuid);
        return 0;
    }

    Guild* guild = bot->GetGuild();
    if (!guild)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' is not in a guild.", bot->GetName());
        return 0;
    }

    EnsureBotBankRights(bot, guild);

    if (guild->_GetPurchasedTabsSize() == 0)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: guild '{}' has no bank tabs.", guild->GetName());
        return 0;
    }

    uint32 totalDeposited = 0;
    uint32 targetToDeposit = (count == 0) ? 0xFFFFFFFF : count;

    while (totalDeposited < targetToDeposit)
    {
        Item* matchingItem = nullptr;
        uint8 foundBag = NULL_BAG;
        uint8 foundSlot = NULL_SLOT;

        // Search backpack
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        {
            if (Item* it = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            {
                if (it->GetEntry() == itemEntry)
                {
                    matchingItem = it;
                    foundBag = INVENTORY_SLOT_BAG_0;
                    foundSlot = slot;
                    break;
                }
            }
        }

        // Search bags
        if (!matchingItem)
        {
            for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            {
                if (Bag* bag = bot->GetBagByPos(bagSlot))
                {
                    for (uint8 slot = 0; slot < bag->GetBagSize(); ++slot)
                    {
                        if (Item* it = bag->GetItemByPos(slot))
                        {
                            if (it->GetEntry() == itemEntry)
                            {
                                matchingItem = it;
                                foundBag = bagSlot;
                                foundSlot = slot;
                                break;
                            }
                        }
                    }
                    if (matchingItem)
                        break;
                }
            }
        }

        if (!matchingItem)
            break;

        uint32 inStack = matchingItem->GetCount();
        uint32 remainingNeeded = targetToDeposit - totalDeposited;
        uint32 moveAmount = (remainingNeeded < inStack) ? remainingNeeded : 0; // 0 = entire stack
        uint32 actualMoved = (moveAmount > 0) ? moveAmount : inStack;

        // Deposit into tab 0
        guild->SwapItemsWithInventory(bot, false, 0, NULL_SLOT, foundBag, foundSlot, moveAmount);
        totalDeposited += actualMoved;
    }

    if (handler)
    {
        if (totalDeposited > 0)
            handler->PSendSysMessage("BotMgr: bot '{}' deposited {}x item {} into guild bank.",
                bot->GetName(), totalDeposited, itemEntry);
        else
            handler->PSendSysMessage("BotMgr: bot '{}' has no item {} in inventory to deposit.",
                bot->GetName(), itemEntry);
    }
    LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' deposited {}x item {} into guild bank (guild '{}').",
        bot->GetName(), totalDeposited, itemEntry, guild->GetName());

    return totalDeposited;
}

uint32 BotMgr::GuildWithdrawItem(ObjectGuid::LowType charLowGuid, uint32 itemEntry, uint32 count, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    if (!session)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {}.", charLowGuid);
        return 0;
    }

    Player* bot = session->GetPlayer();
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot session for guid {} has no Player yet.", charLowGuid);
        return 0;
    }

    Guild* guild = bot->GetGuild();
    if (!guild)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' is not in a guild.", bot->GetName());
        return 0;
    }

    EnsureBotBankRights(bot, guild);

    uint32 totalWithdrawn = 0;
    uint32 targetToWithdraw = (count == 0) ? 1 : count;

    for (uint8 tabId = 0; tabId < guild->_GetPurchasedTabsSize() && totalWithdrawn < targetToWithdraw; ++tabId)
    {
        Guild::BankTab const* tab = guild->GetBankTab(tabId);
        if (!tab)
            continue;

        for (uint8 slotId = 0; slotId < GUILD_BANK_MAX_SLOTS && totalWithdrawn < targetToWithdraw; ++slotId)
        {
            Item const* bankItem = tab->GetItem(slotId);
            if (!bankItem || bankItem->GetEntry() != itemEntry)
                continue;

            uint32 inBank = bankItem->GetCount();
            uint32 remainingNeeded = targetToWithdraw - totalWithdrawn;
            uint32 moveAmount = (remainingNeeded < inBank) ? remainingNeeded : 0; // 0 = entire stack
            uint32 actualMoved = (moveAmount > 0) ? moveAmount : inBank;

            guild->SwapItemsWithInventory(bot, true, tabId, slotId, NULL_BAG, NULL_SLOT, moveAmount);
            totalWithdrawn += actualMoved;
        }
    }

    if (handler)
    {
        if (totalWithdrawn > 0)
            handler->PSendSysMessage("BotMgr: bot '{}' withdrew {}x item {} from guild bank.",
                bot->GetName(), totalWithdrawn, itemEntry);
        else
            handler->PSendSysMessage("BotMgr: item {} not found in guild bank (or no withdraw rights).", itemEntry);
    }
    LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' withdrew {}x item {} from guild bank (guild '{}').",
        bot->GetName(), totalWithdrawn, itemEntry, guild->GetName());

    return totalWithdrawn;
}

void BotMgr::GuildDepositMoney(ObjectGuid::LowType charLowGuid, uint32 copper, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    if (!session)
        return;
    Player* bot = session->GetPlayer();
    if (!bot)
        return;
    Guild* guild = bot->GetGuild();
    if (!guild)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' is not in a guild.", bot->GetName());
        return;
    }

    if (bot->GetMoney() < copper)
        copper = bot->GetMoney();

    if (copper == 0)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' has no money to deposit.", bot->GetName());
        return;
    }

    guild->HandleMemberDepositMoney(session, copper);
    if (handler)
        handler->PSendSysMessage("BotMgr: bot '{}' deposited {} copper into guild bank.", bot->GetName(), copper);
}

void BotMgr::GuildWithdrawMoney(ObjectGuid::LowType charLowGuid, uint32 copper, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    if (!session)
        return;
    Player* bot = session->GetPlayer();
    if (!bot)
        return;
    Guild* guild = bot->GetGuild();
    if (!guild)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' is not in a guild.", bot->GetName());
        return;
    }

    EnsureBotBankRights(bot, guild);

    bool ok = guild->HandleMemberWithdrawMoney(session, copper);
    if (handler)
    {
        if (ok)
            handler->PSendSysMessage("BotMgr: bot '{}' withdrew {} copper from guild bank.", bot->GetName(), copper);
        else
            handler->PSendSysMessage("BotMgr: failed to withdraw {} copper (not enough funds or limit reached).", copper);
    }
}

void BotMgr::GuildGather(ObjectGuid::LowType charLowGuid, uint32 itemEntry, uint32 targetCount, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    if (!session)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {}.", charLowGuid);
        return;
    }
    Player* bot = session->GetPlayer();
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot session for guid {} has no Player yet.", charLowGuid);
        return;
    }
    Guild* guild = bot->GetGuild();
    if (!guild)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' is not in a guild.", bot->GetName());
        return;
    }

    EnsureBotBankRights(bot, guild);

    // 1. Immediately deposit any matching items already held in inventory
    uint32 deposited = GuildDepositItem(charLowGuid, itemEntry, targetCount, nullptr);

    if (deposited >= targetCount)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: guildgather complete -- bot '{}' deposited all {}x item {} immediately from inventory.",
                bot->GetName(), deposited, itemEntry);
        _guildGatherOrders.erase(bot->GetGUID());
        return;
    }

    uint32 remaining = targetCount - deposited;
    _guildGatherOrders[bot->GetGUID()] = { itemEntry, remaining };

    if (handler)
        handler->PSendSysMessage("BotMgr: guildgather order placed for bot '{}': need {}x item {} (already deposited {}x).",
            bot->GetName(), remaining, itemEntry, deposited);
    LOG_INFO("module.coa-playerbots", "BotMgr: guildgather order for bot '{}': item {} need {} (already deposited {}).",
        bot->GetName(), itemEntry, remaining, deposited);
}

void BotMgr::CraftOrder(ObjectGuid::LowType requesterCharLowGuid, uint32 itemEntry, uint32 count, ChatHandler* handler)
{
    Player* requester = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(requesterCharLowGuid));
    if (!requester)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no online player with guid {} found.", requesterCharLowGuid);
        return;
    }

    uint32 guildId = requester->GetGuildId();
    if (!guildId)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: '{}' is not in a guild -- crafting orders only look at guild-mate bots.", requester->GetName());
        return;
    }

    auto const& index = CraftingRecipeIndex();
    auto recipeItr = index.find(itemEntry);
    if (recipeItr == index.end())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no known recipe produces item {}.", itemEntry);
        return;
    }

    // Prefer a guild-mate bot that already has enough reagents to start right away; fall back
    // to the first one found that at least knows the recipe (it'll wait on reagents instead).
    Player* crafter = nullptr;
    uint32 chosenSpell = 0;
    for (Player* bot : GetOnlineBots())
    {
        if (bot->GetGuildId() != guildId)
            continue;
        for (uint32 spellId : recipeItr->second)
        {
            if (!bot->HasSpell(spellId))
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
            if (!info)
                continue;
            bool ready = CrafterHasReagentsFor(bot, info);
            if (!crafter || ready)
            {
                crafter = bot;
                chosenSpell = spellId;
            }
            if (ready)
                break;
        }
        if (crafter && CrafterHasReagentsFor(crafter, sSpellMgr->GetSpellInfo(chosenSpell)))
            break;
    }

    if (!crafter)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no online guild-mate bot knows a recipe for item {}.", itemEntry);
        return;
    }

    _craftOrders[crafter->GetGUID()] = { requester->GetGUID(), chosenSpell, itemEntry, count };

    if (handler)
        handler->PSendSysMessage("BotMgr: craft order placed -- bot '{}' will craft {}x item {} for '{}' (spell {}).",
            crafter->GetName(), count, itemEntry, requester->GetName(), chosenSpell);
    LOG_INFO("module.coa-playerbots", "BotMgr: craft order placed -- bot '{}' crafting {}x item {} for '{}' (spell {}).",
        crafter->GetName(), count, itemEntry, requester->GetName(), chosenSpell);
}

std::vector<std::string> BotMgr::GetGuildRosterInfo(Player* commander) const
{
    std::vector<std::string> lines;
    uint32 guildId = commander ? commander->GetGuildId() : 0;
    if (!guildId)
        return lines;

    for (Player* bot : GetOnlineBots())
    {
        if (bot->GetGuildId() != guildId)
            continue;

        std::string professions;
        for (ProfessionSkillEntry const& entry : PROFESSION_SKILLS)
        {
            if (!bot->HasSkill(entry.skillId))
                continue;
            if (!professions.empty())
                professions += ",";
            professions += std::string(entry.name) + "=" + std::to_string(bot->GetSkillValue(entry.skillId));
        }

        std::string task = "idle";
        if (auto craftItr = _craftOrders.find(bot->GetGUID()); craftItr != _craftOrders.end())
            task = "crafting " + std::to_string(craftItr->second.remainingCount) + "x item " + std::to_string(craftItr->second.itemEntry);
        else if (auto gatherItr = _guildGatherOrders.find(bot->GetGUID()); gatherItr != _guildGatherOrders.end())
            task = "gathering " + std::to_string(gatherItr->second.remainingCount) + "x item " + std::to_string(gatherItr->second.itemEntry);

        lines.push_back("ROSTER:" + std::to_string(bot->GetGUID().GetCounter()) + ":" + bot->GetName() + ":" +
            std::to_string(uint32(bot->getClass())) + ":" + std::to_string(bot->GetLevel()) + ":" + task + ":" + professions);
    }
    return lines;
}

// Called from Update() every tick, same cadence as the guildgather order drain. Casts are
// naturally throttled to "at most one per bot per tick" just by this loop's own shape -- no
// separate cooldown tracking needed since tradeskill casts aren't on the GCD.
void BotMgr::ProcessCraftOrders()
{
    if (_craftOrders.empty())
        return;

    for (auto itr = _craftOrders.begin(); itr != _craftOrders.end(); )
    {
        Player* crafter = FindBotPlayer(itr->first.GetCounter());
        SpellInfo const* info = crafter ? sSpellMgr->GetSpellInfo(itr->second.spellId) : nullptr;
        if (!crafter || !crafter->IsInWorld() || !info)
        {
            itr = _craftOrders.erase(itr);
            continue;
        }

        uint32 itemEntry = itr->second.itemEntry;

        // A cast was already fired on an earlier tick -- don't start another one on top of it
        // (some tradeskill recipes have a real cast time, not just instant ones; recasting
        // mid-cast would interrupt and restart it forever, never letting it finish). Wait for
        // it to actually finish, then check whether it produced anything.
        if (itr->second.awaitingCastResult)
        {
            if (crafter->IsNonMeleeSpellCast(false))
            {
                ++itr; // still casting -- check again next tick
                continue;
            }

            itr->second.awaitingCastResult = false;
            uint32 producedThisCast = crafter->GetItemCount(itemEntry, false) - itr->second.itemCountBeforeCast;
            if (!producedThisCast)
            {
                LOG_INFO("module.coa-playerbots", "BotMgr: craft order cast for bot '{}' (spell {}) finished but produced nothing -- will retry.",
                    crafter->GetName(), itr->second.spellId);
                ++itr;
                continue;
            }

            itr->second.remainingCount = (producedThisCast >= itr->second.remainingCount) ? 0 : itr->second.remainingCount - producedThisCast;
            if (itr->second.remainingCount > 0)
            {
                ++itr;
                continue;
            }
            // falls through to the mail-and-complete block below
        }
        else
        {
            if (!CrafterHasReagentsFor(crafter, info))
            {
                ++itr; // still waiting on reagents -- retry next tick
                continue;
            }

            itr->second.itemCountBeforeCast = crafter->GetItemCount(itemEntry, false);
            SpellCastResult result = crafter->CastSpell(crafter, itr->second.spellId, false);
            if (result != SPELL_CAST_OK)
            {
                LOG_INFO("module.coa-playerbots", "BotMgr: craft order cast failed for bot '{}' (spell {}, result {}) -- will retry.",
                    crafter->GetName(), itr->second.spellId, uint32(result));
                ++itr;
                continue;
            }

            itr->second.awaitingCastResult = true;
            ++itr; // check back once the cast (instant or not) has actually resolved
            continue;
        }

        // remainingCount == 0 here -- order complete: mail every matching stack currently in
        // the crafter's bags (the items this order itself just produced -- nothing else grants
        // this same itemEntry to a bot mid-order) to the requester.
        ObjectGuid::LowType requesterLowGuid = itr->second.requesterGuid.GetCounter();
        uint32 mailed = 0;

        auto mailIfMatch = [&](Item* item)
        {
            if (item && item->GetEntry() == itemEntry)
            {
                mailed += item->GetCount();
                MailCraftedItem(crafter, requesterLowGuid, item, "Crafting order complete");
            }
        };
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            mailIfMatch(crafter->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
        for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
            if (Bag* pBag = crafter->GetBagByPos(bag))
                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                    mailIfMatch(pBag->GetItemByPos(j));

        LOG_INFO("module.coa-playerbots", "BotMgr: craft order complete -- bot '{}' mailed {}x item {} to guid {}.",
            crafter->GetName(), mailed, itemEntry, requesterLowGuid);
        itr = _craftOrders.erase(itr);
    }
}

void BotMgr::Invite(ObjectGuid::LowType charLowGuid, std::string const& targetName, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    if (!session)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {} (spawn it first).", charLowGuid);
        return;
    }

    Player* bot = session->GetPlayer();
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot session for guid {} has no Player yet (login still pending?).", charLowGuid);
        return;
    }

    // CMSG_GROUP_INVITE's real body: a null-terminated target name, then a padding uint32
    // (unused by the handler) -- see WorldSession::HandleGroupInviteOpcode.
    WorldPacket packet;
    packet << targetName;
    packet << uint32(0);
    session->HandleGroupInviteOpcode(packet);

    if (handler)
        handler->PSendSysMessage("BotMgr: '{}' invited '{}' -- check .botcmd acceptinvite on the target if it's another bot.", bot->GetName(), targetName);
}

void BotMgr::Kill(ObjectGuid::LowType charLowGuid, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    Player* bot = session ? session->GetPlayer() : nullptr;
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {} (spawn it first).", charLowGuid);
        return;
    }

    if (!bot->IsAlive())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' is already dead.", bot->GetName());
        return;
    }

    Unit::Kill(bot, bot);

    if (handler)
        handler->PSendSysMessage("BotMgr: bot '{}' killed.", bot->GetName());
}

void BotMgr::DespawnBot(ObjectGuid::LowType charLowGuid, ChatHandler* handler)
{
    auto itr = std::find_if(_botSessions.begin(), _botSessions.end(), [charLowGuid](WorldSession* session)
    {
        Player* bot = session->GetPlayer();
        return bot && bot->GetGUID().GetCounter() == charLowGuid;
    });

    if (itr == _botSessions.end())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {}.", charLowGuid);
        return;
    }

    WorldSession* session = *itr;
    std::string name = session->GetPlayer() ? session->GetPlayer()->GetName() : "?";

    // Same shape as the failure-path cleanup already used in SpawnBot: LogoutPlayer
    // saves+removes the Player from world, but does not delete the WorldSession
    // itself -- that's still on us, same as any other owner of a WorldSession.
    ObjectGuid botGuid = session->GetPlayer() ? session->GetPlayer()->GetGUID() : ObjectGuid::Empty;

    session->LogoutPlayer(true);
    delete session;
    _botSessions.erase(itr);
    // Also drop it from the pending-teleport-ack queue if it's there -- otherwise
    // the next Update() tick would call FinishPendingTeleport on a freed session.
    _pendingTeleportAck.erase(std::remove(_pendingTeleportAck.begin(), _pendingTeleportAck.end(), session), _pendingTeleportAck.end());
    BotAI::Forget(botGuid);

    LOG_INFO("module.coa-playerbots", "BotMgr: despawned bot '{}' (guid {}).", name, charLowGuid);
    if (handler)
        handler->PSendSysMessage("BotMgr: bot '{}' despawned.", name);
}

void BotMgr::ListAuras(ObjectGuid::LowType charLowGuid, ChatHandler* handler)
{
    Player* target = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(charLowGuid));
    if (!target)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no online player with guid {} found.", charLowGuid);
        return;
    }

    if (handler)
    {
        handler->PSendSysMessage("BotMgr: auras on '{}' (guid {}), health {}/{} (mana {}/{}):",
            target->GetName(), charLowGuid,
            target->GetHealth(), target->GetMaxHealth(),
            target->GetPower(POWER_MANA), target->GetMaxPower(POWER_MANA));
        handler->PSendSysMessage("  Intellect={:.2f} Spirit={:.2f} manaRegenFlat={:.4f} manaRegenInterruptedFlat={:.4f} inCombat={}",
            target->GetStat(STAT_INTELLECT), target->GetStat(STAT_SPIRIT),
            target->GetFloatValue(static_cast<uint16>(UNIT_FIELD_POWER_REGEN_FLAT_MODIFIER) + AsUnderlyingType(POWER_MANA)),
            target->GetFloatValue(static_cast<uint16>(UNIT_FIELD_POWER_REGEN_INTERRUPTED_FLAT_MODIFIER) + AsUnderlyingType(POWER_MANA)),
            target->IsInCombat());
        handler->PSendSysMessage("  meleeCrit={:.4f}% offhandCrit={:.4f}% rangedCrit={:.4f}% expertise={} offhandExpertise={} ratingMultCritMelee={:.4f}",
            target->GetFloatValue(PLAYER_CRIT_PERCENTAGE),
            target->GetFloatValue(PLAYER_OFFHAND_CRIT_PERCENTAGE),
            target->GetFloatValue(PLAYER_RANGED_CRIT_PERCENTAGE),
            target->GetUInt32Value(PLAYER_EXPERTISE),
            target->GetUInt32Value(PLAYER_OFFHAND_EXPERTISE),
            target->GetRatingMultiplier(CR_CRIT_MELEE));
        Item* rangedWeapon = target->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
        handler->PSendSysMessage("  rangedWeaponEquipped={} mainWeaponEquipped={}",
            rangedWeapon ? "yes" : "no",
            target->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND) ? "yes" : "no");
    }

    uint32 count = 0;
    for (auto const& [spellId, application] : target->GetAppliedAuras())
    {
        Aura* aura = application->GetBase();
        SpellInfo const* spellInfo = aura->GetSpellInfo();
        bool preventsRegen = aura->HasEffectType(SPELL_AURA_PREVENT_REGENERATE_POWER);

        if (handler)
            handler->PSendSysMessage("  [{}] '{}' stacks={} duration={}ms{}",
                spellId,
                spellInfo->SpellName[handler->GetSessionDbcLocale()],
                aura->GetStackAmount(),
                aura->GetDuration(),
                preventsRegen ? " <-- PREVENT_REGENERATE_POWER" : "");
        ++count;
    }

    if (count == 0 && handler)
        handler->PSendSysMessage("  (no auras)");

    LOG_INFO("module.coa-playerbots", "BotMgr: ListAuras dumped {} auras for '{}' (guid {}).", count, target->GetName(), charLowGuid);
}

void BotMgr::RunChatCommand(ObjectGuid::LowType charLowGuid, std::string const& command, ChatHandler* handler)
{
    Player* target = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(charLowGuid));
    if (!target)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no online player with guid {} found.", charLowGuid);
        return;
    }

    ChatHandler asPlayer(target->GetSession());
    bool result = asPlayer.ParseCommands(command);

    if (handler)
        handler->PSendSysMessage("BotMgr: ran '{}' as '{}', ParseCommands returned {}.", command, target->GetName(), result);
}

void BotMgr::HasSpells(ObjectGuid::LowType charLowGuid, std::vector<uint32> const& spellIds, ChatHandler* handler)
{
    Player* target = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(charLowGuid));
    if (!target)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no online player with guid {} found.", charLowGuid);
        return;
    }

    if (!handler)
        return;

    for (uint32 spellId : spellIds)
        handler->PSendSysMessage("  HasSpell({}) = {}", spellId, target->HasSpell(spellId));
}

void BotMgr::AttackNearestHostile(ObjectGuid::LowType charLowGuid, float range, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    Player* bot = session ? session->GetPlayer() : nullptr;
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {}.", charLowGuid);
        return;
    }

    if (range <= 0.0f)
        range = 30.0f;

    // Same "nearest hostile via a grid searcher" shape as Creature::SelectNearestTarget, using
    // a local Unit-compatible reimplementation of Acore::NearestHostileUnitCheck (that helper's
    // own check type only accepts a Creature const*, a Player can't use it directly). This
    // used to use AnyUnfriendlyUnitInObjectRangeCheck + UnitLastSearcher instead, which finds
    // whichever hostile the grid traversal happens to visit *last* -- not the nearest one --
    // so a bot could "attack nearest" and end up charging a target on the far side of its
    // search radius while a real nearest enemy stood right next to it.
    Unit* target = nullptr;
    NearestHostileUnitInObjectRangeCheck check(bot, range);
    Acore::UnitLastSearcher<NearestHostileUnitInObjectRangeCheck> searcher(bot, target, check);
    Cell::VisitObjects(bot, searcher, range);

    if (!target)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no hostile unit found within {} yards of '{}'.", range, bot->GetName());
        return;
    }

    LOG_INFO("module.coa-playerbots", "BotMgr: pre-Attack diagnostics: botAlive={} targetAlive={} sameMap={} samePhase={} (botPhase={} targetPhase={}) botMounted={} priorVictim={}.",
        bot->IsAlive(), target->IsAlive(), bot->IsInMap(target), bot->InSamePhase(target),
        bot->GetPhaseMask(), target->GetPhaseMask(), bot->IsMounted(),
        bot->GetVictim() ? bot->GetVictim()->GetName() : "<null>");
    bool attacked = bot->Attack(target, true);
    LOG_INFO("module.coa-playerbots", "BotMgr: Attack() on '{}' returned {}; GetVictim() is now {}; attackers count {}.",
        target->GetName(), attacked, bot->GetVictim() ? bot->GetVictim()->GetName() : "<null>", bot->getAttackers().size());
    if (handler)
        handler->PSendSysMessage("BotMgr: bot '{}' is now attacking '{}' (Attack() returned {}).", bot->GetName(), target->GetName(), attacked);
}

void BotMgr::SetRole(ObjectGuid::LowType charLowGuid, std::string const& roleName, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    Player* bot = session ? session->GetPlayer() : nullptr;
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {}.", charLowGuid);
        return;
    }

    std::string normalized = roleName;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);

    if (normalized == "auto")
    {
        BotAI::ClearRoleOverride(bot->GetGUID());
        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        BotRole autoRole = BotAI::GetRoleForClassSpec(bot->getClass(), activeSpec);
        char const* specName = BotAI::GetSpecName(bot->getClass(), activeSpec);
        char const* roleStr = RoleToString(autoRole);
        LOG_INFO("module.coa-playerbots", "BotMgr::SetRole: bot '{}' role reset to auto (detected {} from spec {} '{}').",
            bot->GetName(), roleStr, activeSpec, specName ? specName : "unknown");
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' role reset to auto (detected {} from spec {} '{}').",
                bot->GetName(), roleStr, activeSpec, specName ? specName : "unknown");
        return;
    }

    BotRole role;
    if (normalized == "dps")
        role = BotRole::Dps;
    else if (normalized == "tank")
        role = BotRole::Tank;
    else if (normalized == "healer")
        role = BotRole::Healer;
    else if (normalized == "support")
        role = BotRole::Support;
    else
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: unknown role '{}' -- expected dps, tank, healer, support, or auto.", roleName);
        return;
    }

    // A role pick must actually correspond to a spec this class has -- most classes have no
    // Tank spec at all, several have no Healer spec either (see ClassSpecRoles.cpp's table).
    // Picking one of those roles anyway would just leave the bot's spec/talents mismatched
    // with what BotAI::Update now thinks its role is, silently breaking its rotation. If the
    // bot's current spec already satisfies the requested role, FindSpecForRole returns it
    // unchanged and no spec switch happens at all.
    uint32 currentSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
    uint32 targetSpec = BotAI::FindSpecForRole(bot->getClass(), role, currentSpec);
    if (!targetSpec)
    {
        LOG_INFO("module.coa-playerbots", "BotMgr::SetRole: bot '{}' (class {}) has no {} spec available -- role change refused.",
            bot->GetName(), uint32(bot->getClass()), normalized);
        if (handler)
            handler->PSendSysMessage("BotMgr: bot '{}' has no {} spec available for its class -- role change refused.",
                bot->GetName(), normalized);
        return;
    }

    if (targetSpec != currentSpec)
        LearnSpecialization(charLowGuid, targetSpec, handler);

    BotAI::SetRole(bot->GetGUID(), role);
    LOG_INFO("module.coa-playerbots", "BotMgr::SetRole: bot '{}' role manually set to {} (guid {}).",
        bot->GetName(), normalized, charLowGuid);
    if (handler)
        handler->PSendSysMessage("BotMgr: bot '{}' role manually set to {}.", bot->GetName(), normalized);
}

void BotMgr::CheckRole(ObjectGuid::LowType charLowGuid, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    Player* bot = session ? session->GetPlayer() : nullptr;
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {}.", charLowGuid);
        return;
    }

    BotRole role = BotAI::GetRole(bot->GetGUID());
    char const* roleStr = RoleToString(role);
    uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
    char const* specName = BotAI::GetSpecName(bot->getClass(), activeSpec);

    if (handler)
        handler->PSendSysMessage("BotMgr: bot '{}' effective role is {} (spec {} '{}').",
            bot->GetName(), roleStr, activeSpec, specName ? specName : "unknown");

    BotAI::ReportSpellbookRoleSignals(bot, handler);
}

void BotMgr::LearnSpecialization(ObjectGuid::LowType charLowGuid, uint32 specId, ChatHandler* handler)
{
    WorldSession* session = FindBotSession(charLowGuid);
    Player* bot = session ? session->GetPlayer() : nullptr;
    if (!bot)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: no active bot session for guid {}.", charLowGuid);
        return;
    }

    // Strip every talent spell belonging to any OTHER spec first -- both paid and automatic.
    // mod-ascension-compat's own player-facing switch (AscensionClassService::SwitchSpecialization)
    // does the same unconditionally, because its later SynchronizeProgression only ever ADDS
    // missing automatic grants for the *current* spec; nothing else ever prunes an automatic
    // grant left over from a spec that's no longer active. A cost-based skip here (as an earlier
    // version of this function had, deferring automatic cleanup to "next relog") never actually
    // happens: relog only calls SynchronizeProgression, so a bot that switched specs twice would
    // permanently accumulate free passives from every spec it had ever held.
    uint32 removed = 0;
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
    {
        if (entry.ClassId != bot->getClass() || entry.SpecId == 0 || entry.SpecId == specId)
            continue;
        for (uint8 i = 0; i < entry.SpellCount; ++i)
        {
            uint32 spellId = entry.SpellIds[i];
            if (spellId && bot->HasSpell(spellId))
            {
                bot->removeSpell(spellId, SPEC_MASK_ALL, false);
                ++removed;
            }
        }
    }

    uint32 learned = 0;
    for (AscensionCompatData::CoATalentEntry const& entry : AscensionCompatData::CoATalentEntries)
    {
        if (entry.ClassId != bot->getClass())
            continue;
        if (entry.SpecId != 0 && entry.SpecId != specId)
            continue;
        // AECost==0 && TECost==0 entries are "automatic" -- mod-ascension-compat's own
        // SynchronizeProgression grants those itself once the PlayerSetting below is in
        // place and the bot next logs in. We only need to reach the paid ones here.
        if (entry.AECost == 0 && entry.TECost == 0)
            continue;
        if (entry.RequiredLevel > bot->GetLevel() || !entry.SpellCount)
            continue;

        uint32 spellId = entry.SpellIds[entry.SpellCount - 1];
        if (spellId && sSpellMgr->GetSpellInfo(spellId) && !bot->HasSpell(spellId))
        {
            bot->learnSpell(spellId, false);
            ++learned;
        }
    }

    bot->UpdatePlayerSetting("core.ascension_active_spec", 0, specId);

    char const* specName = BotAI::GetSpecName(bot->getClass(), specId);
    BotRole autoRole = BotAI::GetRoleForClassSpec(bot->getClass(), specId);
    char const* roleStr = RoleToString(autoRole);

    if (handler)
        handler->PSendSysMessage("BotMgr: bot '{}' learned {} talent(s) and dropped {} from other specs for specialization {} '{}' (detected role: {}, relog to pick up automatic grants too).",
            bot->GetName(), learned, removed, specId, specName ? specName : "unknown", roleStr);
    LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' (class {}) learned {} talent(s), dropped {} from other specs, for spec {} '{}' (role: {}).",
        bot->GetName(), uint32(bot->getClass()), learned, removed, specId, specName ? specName : "unknown", roleStr);
}

void BotMgr::QuickFillGroup(Player* commander, ChatHandler* handler)
{
    constexpr uint32 TARGET_TOTAL = 5;
    constexpr uint32 TARGET_TANKS = 1;
    constexpr uint32 TARGET_HEALERS = 1;
    constexpr uint32 TARGET_DPS = 3;

    if (!commander)
        return;

    Group* group = commander->GetGroup();

    // Tally roles already covered by bots currently in the group -- real (non-bot) members
    // are counted toward the group's total size but not toward any specific role bucket:
    // there's no reliable way to infer a real player's role from here, and guessing wrong
    // would under-invite a bucket that's actually still empty.
    uint32 haveTanks = 0, haveHealers = 0, haveDps = 0;
    uint32 currentSize = 1; // just commander, if solo
    if (group)
    {
        currentSize = group->GetMembersCount();
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member)
                continue;
            if (!sBotMgr->FindBotPlayer(member->GetGUID().GetCounter()))
                continue; // real player -- see comment above

            switch (BotAI::GetRole(member->GetGUID()))
            {
                case BotRole::Tank:   ++haveTanks;   break;
                case BotRole::Healer: ++haveHealers; break;
                default:              ++haveDps;     break;
            }
        }
    }

    uint32 slotsLeft = (TARGET_TOTAL > currentSize) ? TARGET_TOTAL - currentSize : 0;
    if (!slotsLeft)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: group is already full ({} members).", currentSize);
        return;
    }

    uint32 needTanks = (haveTanks < TARGET_TANKS) ? TARGET_TANKS - haveTanks : 0;
    uint32 needHealers = (haveHealers < TARGET_HEALERS) ? TARGET_HEALERS - haveHealers : 0;
    uint32 needDps = (haveDps < TARGET_DPS) ? TARGET_DPS - haveDps : 0;

    // Candidate pool: any online bot not already in a group (this one or another), same
    // faction as the commander, still alive. Scored (not filtered) by guild membership and
    // level/gear closeness so the best-fit candidate wins per role, not just the first found.
    std::vector<Player*> pool;
    for (Player* bot : GetOnlineBots())
    {
        if (bot == commander)
            continue; // commander may itself be a tracked bot session (RA/.botcmd testing) -- never a candidate for its own group
        if (bot->GetGroup() || !bot->IsInWorld() || !bot->IsAlive())
            continue;
        if (bot->GetTeamId() != commander->GetTeamId())
            continue;
        pool.push_back(bot);
    }

    bool commanderInGuild = commander->GetGuildId() != 0;
    float commanderIlvl = commander->GetAverageItemLevel();
    auto isGuildmate = [&](Player* bot) { return commanderInGuild && bot->GetGuildId() == commander->GetGuildId(); };
    auto betterCandidate = [&](Player* a, Player* b)
    {
        bool aGuild = isGuildmate(a), bGuild = isGuildmate(b);
        if (aGuild != bGuild)
            return aGuild; // guildmates always win, regardless of level/ilvl fit
        int32 aLevelDiff = std::abs(int32(a->GetLevel()) - int32(commander->GetLevel()));
        int32 bLevelDiff = std::abs(int32(b->GetLevel()) - int32(commander->GetLevel()));
        if (aLevelDiff != bLevelDiff)
            return aLevelDiff < bLevelDiff;
        return std::abs(a->GetAverageItemLevel() - commanderIlvl) < std::abs(b->GetAverageItemLevel() - commanderIlvl);
    };

    auto pickForRole = [&](BotRole role, uint32 count) -> std::vector<Player*>
    {
        std::vector<Player*> matches;
        for (Player* bot : pool)
            if (BotAI::GetRole(bot->GetGUID()) == role)
                matches.push_back(bot);
        std::sort(matches.begin(), matches.end(), betterCandidate);
        if (matches.size() > count)
            matches.resize(count);
        return matches;
    };

    std::vector<Player*> selected;
    auto takeUpTo = [&](BotRole role, uint32& need)
    {
        if (!need || !slotsLeft)
            return;
        uint32 want = std::min(need, slotsLeft);
        std::vector<Player*> picked = pickForRole(role, want);
        for (Player* bot : picked)
        {
            selected.push_back(bot);
            // Remove from pool so a later role bucket can't also claim this bot.
            pool.erase(std::remove(pool.begin(), pool.end(), bot), pool.end());
        }
        need -= uint32(picked.size());
        slotsLeft -= uint32(picked.size());
    };

    takeUpTo(BotRole::Tank, needTanks);
    takeUpTo(BotRole::Healer, needHealers);
    takeUpTo(BotRole::Dps, needDps);

    for (Player* bot : selected)
    {
        WorldPacket packet;
        packet << bot->GetName();
        packet << uint32(0);
        commander->GetSession()->HandleGroupInviteOpcode(packet);
        LOG_INFO("module.coa-playerbots", "BotMgr::QuickFillGroup: '{}' invited bot '{}' (role {}).",
            commander->GetName(), bot->GetName(), RoleToString(BotAI::GetRole(bot->GetGUID())));
    }

    if (handler)
    {
        if (selected.empty())
            handler->PSendSysMessage("BotMgr: quick-fill found no eligible bots for the roles still needed.");
        else
            handler->PSendSysMessage("BotMgr: quick-fill invited {} bot(s).", uint32(selected.size()));
    }
}

void BotMgr::DoRollGreed(WorldSession* session, Roll* roll)
{
    // HandleLootRoll reads itemGUID/itemSlot/rollType then calls Group::CountRollVote,
    // which does the real vote bookkeeping and broadcasts the update via SendLootRoll
    // (already null-socket-tolerant). Same "build the real packet, call the real
    // handler" pattern as DoAcceptInvite/login.
    WorldPacket packet;
    packet << roll->itemGUID;
    packet << uint32(roll->itemSlot);
    packet << uint8(ROLL_GREED);
    session->HandleLootRoll(packet);
}

void BotMgr::Update(uint32 diff)
{
    if (_botSessions.empty())
        return;

    // Finish any teleport queued on a *previous* tick first, before this tick's
    // invite-check loop below has a chance to queue a fresh one -- that's what
    // gives it the one-tick separation from TeleportTo() it needs (see
    // FinishPendingTeleport's header comment).
    if (!_pendingTeleportAck.empty())
    {
        std::vector<WorldSession*> due;
        due.swap(_pendingTeleportAck);
        for (WorldSession* session : due)
            FinishPendingTeleport(session);
    }

    // Auto-accept: checked every tick, not throttled. A bot with a real Player
    // and a pending group or guild invite accepts it immediately, same as a human would.
    for (WorldSession* session : _botSessions)
    {
        Player* bot = session->GetPlayer();
        if (!bot)
            continue;

        if (bot->GetGroupInvite())
        {
            LOG_INFO("module.coa-playerbots", "BotMgr: auto-accepting pending group invite for bot '{}'.", bot->GetName());
            DoAcceptInvite(session);
        }

        if (bot->GetGuildIdInvited() && !bot->GetGuildId())
        {
            LOG_INFO("module.coa-playerbots", "BotMgr: auto-accepting pending guild invite for bot '{}' (guild id {}).",
                bot->GetName(), bot->GetGuildIdInvited());
            DoAcceptGuildInvite(session);
        }
    }

    // Cross-map/instance follow: checked every tick, throttled implicitly by
    // TryFollowLeaderAcrossMaps' own early-out once map+instance already match. Keeps a bot
    // with the group leader through dungeon/raid/BG transfers a real client would otherwise
    // have to walk through a portal for.
    for (WorldSession* session : _botSessions)
        TryFollowLeaderAcrossMaps(session);

    // Ghost-stranded-off-corpse-map recovery: see TryReturnGhostToCorpseMap's comment.
    for (WorldSession* session : _botSessions)
        TryReturnGhostToCorpseMap(session);

    // Loot rolls: also checked every tick. Policy for now is always Greed — real
    // need-eligibility (armor type/class fit) is future AI work, not this milestone.
    for (WorldSession* session : _botSessions)
    {
        Player* bot = session->GetPlayer();
        if (!bot)
            continue;

        Group* group = bot->GetGroup();
        if (!group)
            continue;

        for (Roll* roll : group->GetRolls())
        {
            auto voteItr = roll->playerVote.find(bot->GetGUID());
            if (voteItr != roll->playerVote.end() && voteItr->second == NOT_EMITED_YET)
            {
                LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' rolling Greed on item {} (slot {}).",
                    bot->GetName(), roll->itemid, roll->itemSlot);
                DoRollGreed(session, roll);
            }
        }
    }

    // Combat AI: chase/engage/cast for every active bot. See BotAI.cpp for why this is a
    // generic, class-agnostic "press known offensive spells" engine rather than a
    // hand-tuned per-class rotation.
    for (WorldSession* session : _botSessions)
        if (Player* bot = session->GetPlayer())
            BotAI::Update(bot, diff);

    // Active guild gather orders: check if bot collected the requested item, deposit into guild bank
    if (!_guildGatherOrders.empty())
    {
        for (auto itr = _guildGatherOrders.begin(); itr != _guildGatherOrders.end(); )
        {
            Player* bot = FindBotPlayer(itr->first.GetCounter());
            if (!bot || !bot->IsInWorld() || !bot->GetGuild())
            {
                itr = _guildGatherOrders.erase(itr);
                continue;
            }

            uint32 itemEntry = itr->second.itemEntry;
            uint32 remaining = itr->second.remainingCount;
            uint32 held = bot->GetItemCount(itemEntry, false);
            if (held > 0)
            {
                uint32 toDeposit = std::min(held, remaining);
                uint32 deposited = GuildDepositItem(bot->GetGUID().GetCounter(), itemEntry, toDeposit, nullptr);
                if (deposited > 0)
                {
                    if (deposited >= remaining)
                    {
                        LOG_INFO("module.coa-playerbots", "BotMgr: guildgather order completed for bot '{}' (item {}).",
                            bot->GetName(), itemEntry);
                        itr = _guildGatherOrders.erase(itr);
                        continue;
                    }
                    else
                    {
                        itr->second.remainingCount -= deposited;
                    }
                }
            }
            ++itr;
        }
    }

    ProcessCraftOrders();

    _heartbeatTimer += diff;
    if (_heartbeatTimer < 10000)
        return;
    _heartbeatTimer = 0;

    // Auto-deposit excess gold (> 500g) into guild bank for active bots
    for (WorldSession* session : _botSessions)
    {
        Player* bot = session->GetPlayer();
        if (!bot || !bot->IsInWorld() || !bot->GetGuild())
            continue;

        uint32 currentMoney = bot->GetMoney();
        constexpr uint32 goldLimit = 500 * GOLD;
        if (currentMoney > goldLimit)
        {
            uint32 excess = currentMoney - goldLimit;
            LOG_INFO("module.coa-playerbots", "BotMgr: bot '{}' auto-depositing excess gold ({} copper) to guild bank.",
                bot->GetName(), excess);
            GuildDepositMoney(bot->GetGUID().GetCounter(), excess, nullptr);
        }
    }

    for (WorldSession* session : _botSessions)
    {
        Player* bot = session->GetPlayer();
        LOG_INFO("module.coa-playerbots", "BotMgr heartbeat: account {} -> {}",
            session->GetAccountId(),
            bot ? (bot->IsInWorld() ? "in world" : "player exists, not in world") : "no player yet (login pending)");
    }

    CheckAllBotsMaxLevel();
}

void BotMgr::CheckAllBotsMaxLevel()
{
    uint8 maxLevel = uint8(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));

    bool anyBot = false;
    bool allMax = true;
    for (WorldSession* session : _botSessions)
    {
        Player* bot = session->GetPlayer();
        if (!bot || !bot->IsInWorld())
            continue;
        anyBot = true;
        if (bot->GetLevel() < maxLevel)
        {
            allMax = false;
            break;
        }
    }

    if (!anyBot || !allMax)
    {
        _allBotsMaxLevelNotified = false; // re-arm: a fresh notice is warranted next time this becomes true
        return;
    }

    if (_allBotsMaxLevelNotified)
        return;
    _allBotsMaxLevelNotified = true;

    LOG_INFO("module.coa-playerbots",
        "BotMgr: every active bot companion is already level {} -- no lower-level one left in the "
        "active roster to keep leveling. Spawning a fresh 'twink' isn't automated (no config-listed "
        "twink pool exists yet, and this project never creates characters without being asked) -- "
        "spawn an existing lower-level character yourself with `.botcmd spawnbot <guid>` if you want "
        "someone leveling in the world again.", uint32(maxLevel));
}
