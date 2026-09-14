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
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "LootMgr.h"
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
                bot->GetMotionMaster()->MoveFollow(leader, PET_FOLLOW_DIST, bot->GetFollowAngle());
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

Player* BotMgr::FindBotPlayer(ObjectGuid::LowType charLowGuid) const
{
    WorldSession* session = FindBotSession(charLowGuid);
    return session ? session->GetPlayer() : nullptr;
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
    // and a pending invite accepts it immediately, same as a human would.
    for (WorldSession* session : _botSessions)
    {
        Player* bot = session->GetPlayer();
        if (bot && bot->GetGroupInvite())
        {
            LOG_INFO("module.coa-playerbots", "BotMgr: auto-accepting pending group invite for bot '{}'.", bot->GetName());
            DoAcceptInvite(session);
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

    _heartbeatTimer += diff;
    if (_heartbeatTimer < 10000)
        return;
    _heartbeatTimer = 0;

    for (WorldSession* session : _botSessions)
    {
        Player* bot = session->GetPlayer();
        LOG_INFO("module.coa-playerbots", "BotMgr heartbeat: account {} -> {}",
            session->GetAccountId(),
            bot ? (bot->IsInWorld() ? "in world" : "player exists, not in world") : "no player yet (login pending)");
    }
}
