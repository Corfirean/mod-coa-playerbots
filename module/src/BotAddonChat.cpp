/*
 * mod-coa-playerbots
 *
 * Server-side half of the bot-control addon protocol (see docs/addon-protocol.md and the
 * client AddOn, docs/addon-client.md). A real player's UI addon sends a self-whisper carrying
 * an addon message ("COABOT\t<VERB>:<botGuidLow>[:<arg>]") -- Player::Whisper() already runs
 * every outgoing whisper through PlayerScript::OnPlayerCanUseChat(..., Player* receiver)
 * before actually delivering it (see mod-ascension-compat's CoABugReport.cpp for another,
 * earlier use of exactly this same self-whisper-as-addon-channel trick), so this only needs to
 * recognize the prefix, dispatch the command, and return false to consume it -- no new opcode
 * or core change needed.
 */

#include "BotAI.h"
#include "BotFormations.h"
#include "BotMgr.h"
#include "Chat.h"
#include "ClassSpecRoles.h"
#include "Group.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "MotionMaster.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "Unit.h"
#include "WorldPacket.h"
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace
{
constexpr char PROTOCOL_PREFIX[] = "COABOT\t";
constexpr std::size_t PROTOCOL_PREFIX_LEN = sizeof(PROTOCOL_PREFIX) - 1;

std::vector<std::string> SplitColon(std::string const& body)
{
    std::vector<std::string> parts;
    std::istringstream stream(body);
    std::string part;
    while (std::getline(stream, part, ':'))
        parts.push_back(part);
    return parts;
}

// Name<->code tables for the gear-preference verbs (GETGEAR/SETGEARPREF) -- wire format uses
// plain lowercase names, matching every other verb's convention (role/formation names), rather
// than raw ItemSubclassArmor/ItemSubclassWeapon numbers the addon would have no way to interpret.
struct GearTypeEntry
{
    char const* name;
    uint32 subclass; // raw ItemSubclassArmor / ItemSubclassWeapon value
};

constexpr GearTypeEntry ARMOR_TYPES[] = {
    { "cloth",   ITEM_SUBCLASS_ARMOR_CLOTH },
    { "leather", ITEM_SUBCLASS_ARMOR_LEATHER },
    { "mail",    ITEM_SUBCLASS_ARMOR_MAIL },
    { "plate",   ITEM_SUBCLASS_ARMOR_PLATE },
};

constexpr GearTypeEntry WEAPON_TYPES[] = {
    { "sword",  ITEM_SUBCLASS_WEAPON_SWORD },
    { "mace",   ITEM_SUBCLASS_WEAPON_MACE },
    { "axe",    ITEM_SUBCLASS_WEAPON_AXE },
    { "fist",   ITEM_SUBCLASS_WEAPON_FIST },
    { "dagger", ITEM_SUBCLASS_WEAPON_DAGGER },
    { "staff",  ITEM_SUBCLASS_WEAPON_STAFF },
};

bool GearSubclassByName(GearTypeEntry const* table, std::size_t count, std::string const& name, uint32& outSubclass)
{
    for (std::size_t i = 0; i < count; ++i)
    {
        if (name == table[i].name)
        {
            outSubclass = table[i].subclass;
            return true;
        }
    }
    return false;
}

char const* GearNameBySubclass(GearTypeEntry const* table, std::size_t count, uint32 subclass)
{
    for (std::size_t i = 0; i < count; ++i)
        if (table[i].subclass == subclass)
            return table[i].name;
    return "auto";
}

// Mandatory per docs/addon-protocol.md's "Server-side authorization" section: the guid must
// really be one of our tracked bot sessions (not an arbitrary character), AND the commanding
// player must actually be grouped with it -- never someone else's bot. Both checks fail
// closed (return nullptr) rather than reporting why, matching the doc's "silently drop"
// policy -- an addon bug or a stale/renamed bot shouldn't be able to flood chat with errors.
Player* ResolveAuthorizedBot(Player* commander, ObjectGuid::LowType botGuidLow)
{
    Player* bot = sBotMgr->FindBotPlayer(botGuidLow);
    if (!bot)
        return nullptr;

    Group* botGroup = bot->GetGroup();
    if (!botGroup || botGroup != commander->GetGroup())
        return nullptr;

    return bot;
}

// Server -> client half of the protocol -- same self-whisper trick in reverse (see
// mod-ascension-compat's CoABugReport.cpp for the precedent this is copied from):
// build a CHAT_MSG_WHISPER/LANG_ADDON packet addressed from the player to themself and hand
// it directly to their own session, since a bot's own null-socket session can't send it and
// there's no other recipient involved. Body gets the same "COABOT\t" prefix as an outgoing
// client message so one client-side CHAT_MSG_ADDON handler covers both directions.
void SendCoaBotReply(Player* recipient, std::string const& body)
{
    WorldPacket packet;
    ChatHandler::BuildChatPacket(packet, CHAT_MSG_WHISPER, LANG_ADDON, recipient->GetGUID(), recipient->GetGUID(),
        std::string(PROTOCOL_PREFIX) + body, 0, recipient->GetName(), recipient->GetName(), 0, false);
    recipient->SendDirectMessage(&packet);
}

void HandleCoaBotMessage(Player* commander, std::string const& body)
{
    std::vector<std::string> parts = SplitColon(body);
    if (parts.size() < 2)
    {
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' sent malformed COABOT body '{}' (fewer than 2 colon-parts) -- dropped.",
            commander->GetName(), body);
        return;
    }

    std::string const& verb = parts[0];

    // QUICKFILL acts on the commander's own group as a whole, not one specific bot -- no
    // botGuidLow/authorization gate applies (the wire format's colon-part is a "0" placeholder
    // to satisfy the >= 2 parts check above, see docs/addon-protocol.md).
    if (verb == "QUICKFILL")
    {
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' -> QUICKFILL.", commander->GetName());
        // A real ChatHandler (not nullptr) so the player actually sees *why* nothing happened
        // when the bot pool is empty or the group's already full -- QuickFillGroup already had
        // this feedback built in for the .botcmd path, it just never reached the addon path.
        ChatHandler handler(commander->GetSession());
        sBotMgr->QuickFillGroup(commander, &handler);
        return;
    }

    // AUTODUNGEON also acts on the commander's whole group, not one bot -- the second colon-part
    // carries the actual on/off value (1 or 0) instead of the usual "0" placeholder, since this
    // verb needs it. See BotMgr::SetAutoDungeonMode for what this actually changes.
    if (verb == "AUTODUNGEON" && parts.size() >= 2)
    {
        bool enabled = parts[1] == "1";
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' -> AUTODUNGEON {}.", commander->GetName(), enabled ? "on" : "off");
        sBotMgr->SetAutoDungeonMode(commander->GetGUID(), enabled);
        return;
    }

    // FORMATION acts on the commander's whole group, same shape as AUTODUNGEON -- the second
    // colon-part carries the formation name (see BotFormations.h's ParseFormation for accepted
    // names/synonyms). Group-wide state, so restrict to the group leader same as the equivalent
    // .botcmd formation GM command (BotCommand.cpp's HandleBotFormationCommand) -- otherwise any
    // grouped player could silently override the leader's own formation choice.
    if (verb == "FORMATION" && parts.size() >= 3)
    {
        Group* group = commander->GetGroup();
        if (!group || group->GetLeaderGUID() != commander->GetGUID())
        {
            LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' sent FORMATION but isn't the group leader -- dropped.",
                commander->GetName());
            return;
        }

        BotGroupFormation formation = ParseFormation(parts[2]);
        sBotMgr->SetGroupFormation(commander->GetGUID(), formation);
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' -> FORMATION {}.", commander->GetName(), FormationToString(formation));

        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (member && member != commander && sBotMgr->FindBotPlayer(member->GetGUID().GetCounter()))
            {
                if (member->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
                    member->GetMotionMaster()->MoveFollow(commander, BotAI::ComputeFollowDistance(member), BotAI::ComputeFollowAngle(member));
            }
        }
        return;
    }

    // GETFORMATION: query, no state change -- lets the addon show the group's currently active
    // formation (e.g. after a UI reload) instead of guessing. Mirrors GETROLES's query shape.
    if (verb == "GETFORMATION")
    {
        Group* group = commander->GetGroup();
        ObjectGuid leaderGuid = group ? group->GetLeaderGUID() : commander->GetGUID();
        SendCoaBotReply(commander, std::string("FORMATION:") + FormationToString(sBotMgr->GetGroupFormation(leaderGuid)));
        return;
    }

    // TELEPORT: "bring bots to me", group-wide, same "0" placeholder convention as QUICKFILL.
    // Out-of-combat-only gating and the per-bot in-combat skip both live in
    // BotMgr::TeleportBotsToPlayer itself.
    if (verb == "TELEPORT")
    {
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' -> TELEPORT.", commander->GetName());
        sBotMgr->TeleportBotsToPlayer(commander, nullptr);
        return;
    }

    // CRAFTORDER also doesn't target one specific bot -- the requester is always the sender
    // (commander), and BotMgr::CraftOrder itself finds the guild-mate bot to craft it. Reuses
    // the second colon-part for itemEntry instead of a bot guid.
    if (verb == "CRAFTORDER" && parts.size() >= 2)
    {
        uint32 itemEntry = std::strtoul(parts[1].c_str(), nullptr, 10);
        uint32 count = parts.size() >= 3 ? std::strtoul(parts[2].c_str(), nullptr, 10) : 1;
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' -> CRAFTORDER item {} x{}.", commander->GetName(), itemEntry, count);
        sBotMgr->CraftOrder(commander->GetGUID().GetCounter(), itemEntry, count ? count : 1, nullptr);
        return;
    }

    // GUILDROSTER: task-board query, also acts on the sender's whole guild rather than one
    // bot -- send as GUILDROSTER:0 (same "0" placeholder convention as QUICKFILL, see its
    // comment above). Replies with one ROSTER:... message per guild bot (many small replies
    // rather than one giant one, since a big guild's roster could exceed the chat length cap).
    if (verb == "GUILDROSTER")
    {
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' -> GUILDROSTER.", commander->GetName());
        for (std::string const& line : sBotMgr->GetGuildRosterInfo(commander))
            SendCoaBotReply(commander, line);
        return;
    }

    // GATHERORDER: same shape as CRAFTORDER -- the requester is always the sender, and
    // BotMgr::GatherOrder itself picks the guild-mate bot. Second colon-part is the itemEntry,
    // third (optional) is count.
    if (verb == "GATHERORDER" && parts.size() >= 2)
    {
        uint32 itemEntry = std::strtoul(parts[1].c_str(), nullptr, 10);
        uint32 count = parts.size() >= 3 ? std::strtoul(parts[2].c_str(), nullptr, 10) : 1;
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' -> GATHERORDER item {} x{}.", commander->GetName(), itemEntry, count);
        sBotMgr->GatherOrder(commander->GetGUID().GetCounter(), itemEntry, count ? count : 1, nullptr);
        return;
    }

    // GETGATHERCATALOG / GETRECIPECATALOG: catalog queries backing the addon's icon-menu
    // pickers (see docs/addon-protocol.md) -- replies with several small GCAT:/RCAT: messages,
    // same "one guid placeholder, several chunked replies" shape as GUILDROSTER.
    if (verb == "GETGATHERCATALOG")
    {
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' -> GETGATHERCATALOG.", commander->GetName());
        for (std::string const& line : sBotMgr->GetGatherCatalog())
            SendCoaBotReply(commander, line);
        return;
    }
    if (verb == "GETRECIPECATALOG")
    {
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' -> GETRECIPECATALOG.", commander->GetName());
        for (std::string const& line : sBotMgr->GetRecipeCatalog(commander))
            SendCoaBotReply(commander, line);
        return;
    }

    ObjectGuid::LowType botGuidLow = std::strtoul(parts[1].c_str(), nullptr, 10);
    if (!botGuidLow)
    {
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' sent COABOT verb '{}' with unparseable/zero guid '{}' -- dropped.",
            commander->GetName(), verb, parts[1]);
        return;
    }

    Player* bot = ResolveAuthorizedBot(commander, botGuidLow);
    if (!bot)
    {
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' sent COABOT verb '{}' for guid {} but it failed authorization "
            "(not a tracked bot session, or not grouped with the sender) -- dropped.",
            commander->GetName(), verb, botGuidLow);
        return;
    }

    LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' -> bot '{}' verb '{}'{}{}.",
        commander->GetName(), bot->GetName(), verb,
        parts.size() >= 3 ? " arg='" : "", parts.size() >= 3 ? parts[2] + "'" : "");

    if (verb == "FOLLOW")
        BotAI::SetManualCommand(bot->GetGUID(), BotManualCommand::Follow);
    else if (verb == "STAY")
        BotAI::SetManualCommand(bot->GetGUID(), BotManualCommand::Stay);
    else if (verb == "PULL")
    {
        // The target is whatever the *commanding player* currently has selected, resolved
        // here server-side -- the wire message only ever carries the bot's own guid, per the
        // protocol doc's reasoning (no target-guid resolution needed on the addon side).
        if (Unit* target = commander->GetSelectedUnit())
            BotAI::SetManualCommand(bot->GetGUID(), BotManualCommand::Pull, target->GetGUID());
    }
    else if (verb == "STOPATTACK")
        BotAI::StopAttack(bot->GetGUID());
    else if (verb == "SETROLE" && parts.size() >= 3)
        sBotMgr->SetRole(botGuidLow, parts[2], nullptr);
    else if (verb == "LEARNSPEC" && parts.size() >= 3)
        sBotMgr->LearnSpecialization(botGuidLow, std::strtoul(parts[2].c_str(), nullptr, 10), nullptr);
    else if (verb == "GETROLES")
    {
        // Query, not a command: lets the addon grey out role buttons a bot's class can never
        // actually hold (see SETROLE's silent-refusal note above) without needing its own copy
        // of the classId->role table -- the addon only ever sees the bot's underlying WoW
        // class, not its Ascension classId, so it has no way to derive this locally.
        static char const* const ROLE_NAMES[] = { "dps", "tank", "healer", "support" };
        uint32 mask = BotAI::GetAvailableRolesMask(bot->getClass());
        std::string roles;
        for (uint32 i = 0; i < 4; ++i)
        {
            if (mask & (1u << i))
            {
                if (!roles.empty())
                    roles += ",";
                roles += ROLE_NAMES[i];
            }
        }
        // Also tell the addon the bot's *current* effective role (not just which ones its
        // class could hold) -- otherwise a bot left on "auto" always just shows the literal
        // label "Auto" with no indication of what it's actually playing as right now.
        BotRole currentRole = BotAI::GetRole(bot->GetGUID());
        char const* currentRoleStr = ROLE_NAMES[currentRole == BotRole::Tank ? 1 : currentRole == BotRole::Healer ? 2 : currentRole == BotRole::Support ? 3 : 0];

        uint32 currentSpecId = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        char const* currentSpecName = BotAI::GetSpecName(bot->getClass(), currentSpecId);
        SendCoaBotReply(commander, "ROLES:" + std::to_string(botGuidLow) + ":" + roles + ":" + currentRoleStr + ":" +
            std::to_string(currentSpecId) + ":" + (currentSpecName ? currentSpecName : ""));
    }
    else if (verb == "GETSPECS")
    {
        static char const* const ROLE_NAMES[] = { "dps", "tank", "healer", "support" };
        for (BotAI::SpecInfo const& spec : BotAI::GetAllSpecs(bot->getClass()))
        {
            SendCoaBotReply(commander, "SPEC:" + std::to_string(botGuidLow) + ":" + std::to_string(spec.specId) + ":" +
                ROLE_NAMES[uint32(spec.role)] + ":" + spec.name);
        }
    }
    else if (verb == "SETGEARPREF" && parts.size() >= 4)
    {
        bool weapon = parts[2] == "weapon";
        if (parts[2] != "armor" && !weapon)
            return;

        if (parts[3] == "auto")
        {
            sBotMgr->SetGearPreference(bot, weapon, 0);
            return;
        }

        uint32 subclass = 0;
        bool found = weapon
            ? GearSubclassByName(WEAPON_TYPES, (sizeof(WEAPON_TYPES) / sizeof(WEAPON_TYPES[0])), parts[3], subclass)
            : GearSubclassByName(ARMOR_TYPES, (sizeof(ARMOR_TYPES) / sizeof(ARMOR_TYPES[0])), parts[3], subclass);
        if (!found)
        {
            LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' sent SETGEARPREF with unrecognized {} type '{}' -- dropped.",
                commander->GetName(), weapon ? "weapon" : "armor", parts[3]);
            return;
        }

        sBotMgr->SetGearPreference(bot, weapon, subclass);
        LOG_INFO("module.coa-playerbots", "BotAddonChat: '{}' -> SETGEARPREF bot '{}' {} = {}.",
            commander->GetName(), bot->GetName(), weapon ? "weapon" : "armor", parts[3]);
    }
    else if (verb == "GETGEAR")
    {
        for (std::string const& line : sBotMgr->GetEquippedGearInfo(bot))
            SendCoaBotReply(commander, line);

        std::string legalArmor;
        for (uint32 subclass : sBotMgr->GetLegalArmorSubclasses(bot))
        {
            if (!legalArmor.empty())
                legalArmor += ",";
            legalArmor += GearNameBySubclass(ARMOR_TYPES, (sizeof(ARMOR_TYPES) / sizeof(ARMOR_TYPES[0])), subclass);
        }

        std::string legalWeapon;
        for (uint32 subclass : sBotMgr->GetLegalWeaponSubclasses(bot))
        {
            if (!legalWeapon.empty())
                legalWeapon += ",";
            legalWeapon += GearNameBySubclass(WEAPON_TYPES, (sizeof(WEAPON_TYPES) / sizeof(WEAPON_TYPES[0])), subclass);
        }

        uint32 armorPref = sBotMgr->GetGearPreference(bot, false);
        uint32 weaponPref = sBotMgr->GetGearPreference(bot, true);
        std::string armorPrefName = armorPref == 0 ? "auto" : GearNameBySubclass(ARMOR_TYPES, (sizeof(ARMOR_TYPES) / sizeof(ARMOR_TYPES[0])), armorPref);
        std::string weaponPrefName = weaponPref == 0 ? "auto" : GearNameBySubclass(WEAPON_TYPES, (sizeof(WEAPON_TYPES) / sizeof(WEAPON_TYPES[0])), weaponPref - 1);

        SendCoaBotReply(commander, "GEARPREFS:" + std::to_string(botGuidLow) + ":" + legalArmor + ":" + legalWeapon +
            ":" + armorPrefName + ":" + weaponPrefName);
    }
}

class coa_bot_addon_chat_script : public PlayerScript
{
public:
    coa_bot_addon_chat_script() : PlayerScript("coa_bot_addon_chat_script", { PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& message, Player* receiver) override
    {
        if (type != CHAT_MSG_WHISPER || language != LANG_ADDON || receiver != player)
            return true;
        if (message.compare(0, PROTOCOL_PREFIX_LEN, PROTOCOL_PREFIX) != 0)
            return true;

        HandleCoaBotMessage(player, message.substr(PROTOCOL_PREFIX_LEN));
        return false; // consume -- a self-whisper carrying our protocol never needs real delivery
    }
};
}

void AddSC_coa_bot_addon_chat_script()
{
    new coa_bot_addon_chat_script();
}
