/*
 * mod-coa-playerbots
 *
 * Why cloning an existing character row instead of simulating the real character-creation
 * packet flow (Player::Create + CharacterCreateInfo, the path CharacterHandler.cpp's
 * HandleCharCreateOpcode uses): that path builds a level-1 character in a starting zone with
 * only base race/class starting gear -- correct for a real new player, but not what a bot
 * meant to be dropped into a group or the open world at level 80 needs, and driving it
 * headlessly (no real socket, no client-side model/appearance round trip) is a second proven
 * mechanism to build on top of, on top of the one BotMgr::SpawnBot already established for
 * logging existing characters in. Every one of this project's 21 custom classes already has
 * at least one hand-verified, fully-progressed level-80 test character (this session's combat
 * rotation work) sitting on a non-LOCAL account -- cloning one of those and only swapping
 * guid/account/name/race/gender gets a new bot everything a fresh Player::Create() character
 * would need mod-ascension-compat's own OnPlayerLogin repair hook to backfill anyway
 * (RepairStarterKit/SynchronizeProgression/SynchronizeProficiencies -- see AGENTS.md), for a
 * fraction of the engine surface this module would otherwise have to drive by hand.
 *
 * Synchronous by design: CharacterDatabase.Execute/Query here block the calling thread (the
 * world thread, for a console/GM command) until each statement completes. Fine for an admin
 * spinning up a batch of test/population bots on a dev realm with nobody else online; a large
 * batch (hundreds) will cause a visible tick hitch and should not be run casually against a
 * realm with real players connected. Making this properly async (a queued worker + per-bot
 * QueryCallback the way BotMgr::SpawnBot already does for login) is future work if that ever
 * becomes a real requirement.
 */

#include "BotSpawnRandom.h"
#include "AccountMgr.h"
#include "BotMgr.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "World.h"
#include <array>
#include <chrono>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace BotSpawn
{
namespace
{
// Standard playable WotLK races. 9 (Goblin) is NPC-only; nothing here is Death-Knight-only,
// so no starting-level special case is needed.
constexpr std::array<uint8, 10> VALID_RACES = { 1, 2, 3, 4, 5, 6, 7, 8, 10, 11 };

std::mt19937& Rng()
{
    static std::mt19937 rng(std::random_device{}());
    return rng;
}

uint32 RandomInt(uint32 minInclusive, uint32 maxInclusive)
{
    std::uniform_int_distribution<uint32> dist(minInclusive, maxInclusive);
    return dist(Rng());
}

// Character names are letters-only; combining small consonant-heavy onset/coda sets with a
// vowel (or vowel pair) between them gives pronounceable, WoW-plausible results without a
// real name dataset. Retried by the caller on a DB collision.
std::string GenerateRandomName()
{
    static constexpr std::array<char const*, 20> ONSETS = {
        "Th", "Br", "Cr", "Dr", "Gr", "Kr", "Fr", "Sh", "Sk", "Sn",
        "St", "Tr", "Vr", "Wr", "Zar", "Mor", "Kel", "Val", "Ral", "Bel"
    };
    static constexpr std::array<char const*, 10> VOWELS = { "a", "e", "i", "o", "u", "ae", "io", "ou", "ei", "ya" };
    static constexpr std::array<char const*, 16> CODAS = {
        "n", "r", "s", "th", "x", "l", "m", "d", "k", "rin", "dor", "las", "mir", "gar", "noth", "wyn"
    };

    uint32 syllables = RandomInt(2, 3);
    std::string name = ONSETS[RandomInt(0, ONSETS.size() - 1)];
    name += VOWELS[RandomInt(0, VOWELS.size() - 1)];
    for (uint32 i = 1; i < syllables; ++i)
    {
        name += CODAS[RandomInt(0, CODAS.size() - 1)];
        name += VOWELS[RandomInt(0, VOWELS.size() - 1)];
    }
    name += CODAS[RandomInt(0, CODAS.size() - 1)];

    // WotLK character names cap at 12 bytes -- rare with these sets, but a long onset plus
    // long codas can exceed it.
    if (name.size() > 12)
        name.resize(12);
    name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
    for (size_t i = 1; i < name.size(); ++i)
        name[i] = char(std::tolower(static_cast<unsigned char>(name[i])));
    return name;
}

std::string GenerateUniqueName()
{
    for (uint32 attempt = 0; attempt < 50; ++attempt)
    {
        std::string candidate = GenerateRandomName();
        if (!sCharacterCache->GetCharacterGuidByName(candidate))
            return candidate;
    }
    // Exhausted retries (astronomically unlikely given the combinatorics above) -- fall back
    // to a name that's unique by construction even though it doesn't match the usual shape.
    return "Cb" + std::to_string(sObjectMgr->GetGenerator<HighGuid::Player>().GetNextAfterMaxUsed());
}

// One representative level-80 template guid per custom class (12-32), queried live rather
// than hardcoded so the pool tracks the test-character roster as it changes. Deliberately
// excludes account 1 (LOCAL, the human player's own account) -- see BotMgr.h's standing rule
// that characters there must never be used as bots.
std::unordered_map<uint8, uint32> BuildClassTemplateRoster()
{
    std::unordered_map<uint8, uint32> roster;
    QueryResult result = CharacterDatabase.Query(
        "SELECT class, MIN(guid) FROM characters WHERE account <> 1 AND level >= 80 "
        "AND class BETWEEN 12 AND 32 GROUP BY class");
    if (!result)
        return roster;
    do
    {
        Field* fields = result->Fetch();
        roster[fields[0].Get<uint8>()] = fields[1].Get<uint32>();
    } while (result->NextRow());
    return roster;
}

// Cached column list for the `characters` table, fetched once per server run. Cloning a row
// while overriding a handful of columns means naming every column explicitly in the INSERT
// (guid is a real primary key, not auto-increment, so a plain `INSERT ... SELECT *` from an
// existing row always collides on it) -- reading the list live avoids hand-maintaining an
// ~80-entry copy that would silently rot the moment the schema changes.
std::vector<std::string> const& CharacterColumns()
{
    static std::vector<std::string> columns = []
    {
        std::vector<std::string> cols;
        QueryResult result = CharacterDatabase.Query("SHOW COLUMNS FROM characters");
        if (result)
        {
            do
            {
                cols.push_back(result->Fetch()[0].Get<std::string>());
            } while (result->NextRow());
        }
        return cols;
    }();
    return columns;
}

// Finds an existing "<prefix>N" account with room under CharactersPerAccount, or creates the
// next one in sequence. Bots need their own accounts -- never account 1, and account 2's own
// pool is small and shared with hand-made test characters -- so a large batch has somewhere
// to keep going once any single account fills up.
uint32 FindOrCreateBotAccount(std::string const& prefix, uint32 charactersPerAccount, ChatHandler* handler)
{
    for (uint32 n = 1; n <= 10000; ++n)
    {
        std::string username = prefix + std::to_string(n);
        uint32 accountId = AccountMgr::GetId(username);
        if (!accountId)
        {
            std::string password = GenerateRandomName() + std::to_string(RandomInt(1000, 9999));
            if (sAccountMgr->CreateAccount(username, password) != AOR_OK)
            {
                if (handler)
                    handler->PSendSysMessage("BotMgr: failed to create bot-hosting account '{}'.", username);
                continue;
            }
            // AccountMgr::CreateAccount queues its INSERT on the login DB's async worker pool
            // rather than writing it synchronously, so a GetId() called immediately afterward
            // can still see nothing -- retry briefly rather than assume creation failed.
            for (uint32 attempt = 0; attempt < 20 && !accountId; ++attempt)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                accountId = AccountMgr::GetId(username);
            }
            if (!accountId)
            {
                if (handler)
                    handler->PSendSysMessage(
                        "BotMgr: created bot-hosting account '{}' but it never became visible, giving up on it.",
                        username);
                continue;
            }
            if (handler)
                handler->PSendSysMessage("BotMgr: created bot-hosting account '{}' (id {}).", username, accountId);
            return accountId;
        }
        if (AccountMgr::GetCharactersCount(accountId) < charactersPerAccount)
            return accountId;
    }
    return 0;
}

// Clones one `characters` row (guid/account/name/race/gender/online/at_login overridden,
// everything else -- level, gear, spells, position -- inherited from the template) plus its
// `character_homebind` row (needed for hearthstone/recall and death handling; every existing
// test-bot character has one). Name is our own letters-only generated string, so no escaping
// beyond simple quoting is needed.
ObjectGuid::LowType CloneCharacter(uint32 templateGuid, uint32 accountId, std::string const& name,
    uint8 race, uint8 gender)
{
    std::vector<std::string> const& columns = CharacterColumns();
    ObjectGuid::LowType newGuid = sObjectMgr->GetGenerator<HighGuid::Player>().Generate();

    std::ostringstream insertCols;
    std::ostringstream selectVals;
    for (std::string const& col : columns)
    {
        insertCols << "`" << col << "`,";
        if (col == "guid")
            selectVals << newGuid << ",";
        else if (col == "account")
            selectVals << accountId << ",";
        else if (col == "name")
            selectVals << "'" << name << "',";
        else if (col == "race")
            selectVals << uint32(race) << ",";
        else if (col == "gender")
            selectVals << uint32(gender) << ",";
        else if (col == "online")
            selectVals << "0,";
        else if (col == "at_login")
            selectVals << "0,";
        // Confirmed live: a bot cloned from a template character that had been left with
        // `.gm on` (extra_flags & PLAYER_EXTRA_GM_ON) toggled at its last logout inherited that
        // bit forever, making the resulting bot silently un-invitable by anyone -- HandleGroupInviteOpcode
        // refuses to invite a GM-flagged target unless the inviter is also a GM, sending back the
        // exact same "Cannot find player" the client shows for a genuinely nonexistent name (no
        // server-side error either), so this looked identical to a random-invite bug for a long
        // time. A bot should never carry any of this column's runtime toggles (GM mode, GM
        // invisibility, taxi-cheat, decline-group-invites, etc.) regardless of what its template
        // happened to have set, so it's zeroed here the same way `online`/`at_login` already are.
        else if (col == "extra_flags")
            selectVals << "0,";
        else if (col == "creation_date")
            selectVals << "NOW(),";
        else if (col == "deleteInfos_Account" || col == "deleteInfos_Name" || col == "deleteDate")
            selectVals << "NULL,";
        else
            selectVals << "`" << col << "`,";
    }
    std::string insertColsStr = insertCols.str();
    insertColsStr.pop_back();
    std::string selectValsStr = selectVals.str();
    selectValsStr.pop_back();

    // DirectExecute, not Execute: the plain Execute() overloads always queue onto the async
    // worker pool regardless of name -- confirmed live the hard way, spawning 100 bots in a
    // row put all 100 on the same account because FindOrCreateBotAccount's very next
    // GetCharactersCount() check kept racing ahead of these inserts actually landing, so the
    // count it saw never caught up to CharactersPerAccount. DirectExecute runs synchronously
    // on the calling thread, so by the time this function returns the row genuinely exists.
    CharacterDatabase.DirectExecute("INSERT INTO characters (" + insertColsStr + ") SELECT " + selectValsStr +
        " FROM characters WHERE guid = " + std::to_string(templateGuid));

    CharacterDatabase.DirectExecute(
        "INSERT INTO character_homebind (guid, mapId, zoneId, posX, posY, posZ) "
        "SELECT {}, mapId, zoneId, posX, posY, posZ FROM character_homebind WHERE guid = {}",
        newGuid, templateGuid);

    // core.ascension_active_spec (mod-ascension-compat's PlayerSetting, read by
    // ClassSpecRoles/BotAI::GetRole to pick a bot's tank/healer/dps role) lives here, not on
    // the `characters` row above -- without this copy every cloned bot silently reverts to
    // specId 0 ("unknown"), which GetRoleForClassSpec always treats as Dps. Confirmed live:
    // a 100-bot spawnrandom batch had zero tanks/healers among them, breaking QuickFillGroup
    // for any of them (found while investigating a "quick fill only got 2 people" report --
    // the bots it *did* find were all it could find, all Dps by this same cause). Copies every
    // setting, not just the spec one, on the same "don't hand-pick which columns matter"
    // reasoning as the full-row `characters` clone above.
    CharacterDatabase.DirectExecute(
        "INSERT INTO character_settings (guid, source, data) "
        "SELECT {}, source, data FROM character_settings WHERE guid = {}",
        newGuid, templateGuid);

    return newGuid;
}
}

void SpawnRandomBots(uint32 requestedCount, ChatHandler* handler)
{
    uint32 defaultCount = sConfigMgr->GetOption<uint32>("CoaBots.RandomSpawn.DefaultCount", 10);
    uint32 maxCount = sConfigMgr->GetOption<uint32>("CoaBots.RandomSpawn.MaxCount", 1000);
    std::string accountPrefix = sConfigMgr->GetOption<std::string>("CoaBots.RandomSpawn.AccountPrefix", "CoaBotHost");
    bool autoLogin = sConfigMgr->GetOption<bool>("CoaBots.RandomSpawn.AutoLogin", true);

    uint32 count = requestedCount ? requestedCount : defaultCount;
    if (count > maxCount)
    {
        if (handler)
            handler->PSendSysMessage(
                "BotMgr: clamping requested {} bots to the configured max of {} (CoaBots.RandomSpawn.MaxCount).",
                count, maxCount);
        count = maxCount;
    }
    if (count == 0)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: nothing to do (count is 0).");
        return;
    }

    std::unordered_map<uint8, uint32> templateRoster = BuildClassTemplateRoster();
    if (templateRoster.empty())
    {
        if (handler)
            handler->PSendSysMessage(
                "BotMgr: no usable template characters found (need at least one level 80+ character "
                "per class, not on account 1).");
        return;
    }
    std::vector<uint8> availableClasses;
    availableClasses.reserve(templateRoster.size());
    for (auto const& [classId, guid] : templateRoster)
        availableClasses.push_back(classId);

    if (CharacterColumns().empty())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: could not read the characters table schema, aborting.");
        return;
    }

    uint32 charactersPerAccount = sWorld->getIntConfig(CONFIG_CHARACTERS_PER_ACCOUNT);
    uint32 created = 0;

    for (uint32 i = 0; i < count; ++i)
    {
        uint32 accountId = FindOrCreateBotAccount(accountPrefix, charactersPerAccount, handler);
        if (!accountId)
        {
            if (handler)
                handler->PSendSysMessage(
                    "BotMgr: could not find or create a bot-hosting account, stopping after {} of {}.",
                    created, count);
            break;
        }

        uint8 classId = availableClasses[RandomInt(0, availableClasses.size() - 1)];
        uint32 templateGuid = templateRoster[classId];
        uint8 race = VALID_RACES[RandomInt(0, VALID_RACES.size() - 1)];
        uint8 gender = uint8(RandomInt(0, 1));
        std::string name = GenerateUniqueName();

        ObjectGuid::LowType newGuid = CloneCharacter(templateGuid, accountId, name, race, gender);

        // Without this, the new row is invisible to anything keyed off the in-memory cache
        // (including BotMgr::SpawnBot's own account lookup) until the next full server
        // restart -- sCharacterCache is only populated at boot or via the normal login-packet
        // path, not for a character inserted straight into the DB. See AGENTS.md.
        sCharacterCache->AddCharacterCacheEntry(ObjectGuid::Create<HighGuid::Player>(newGuid), accountId,
            name, gender, race, classId, 80);

        ++created;
        LOG_INFO("module.coa-playerbots", "BotMgr: created random bot '{}' (guid {}, class {}, account {}).",
            name, newGuid, classId, accountId);

        // Reuses the exact same async, null-socket login path every other bot in this module
        // goes through (BotMgr::SpawnBot) -- each call only queues a DB login query and
        // returns immediately, so this loop doesn't block waiting on any one bot's login.
        if (autoLogin)
            sBotMgr->SpawnBot(newGuid, nullptr);
    }

    if (handler)
        handler->PSendSysMessage(autoLogin
            ? "BotMgr: created {} of {} requested random bots and queued their login (watch Server.log)."
            : "BotMgr: created {} of {} requested random bots. Use `.botcmd spawnbot <guid>` to log one in "
              "(see Server.log for the guids just created).",
            created, count);
}

ObjectGuid::LowType CreateOneRandomBot(uint8 race, ChatHandler* handler)
{
    std::unordered_map<uint8, uint32> templateRoster = BuildClassTemplateRoster();
    if (templateRoster.empty())
    {
        if (handler)
            handler->PSendSysMessage(
                "BotMgr: no usable template characters found (need at least one level 80+ character "
                "per class, not on account 1).");
        return 0;
    }
    if (CharacterColumns().empty())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: could not read the characters table schema, aborting.");
        return 0;
    }

    std::vector<uint8> availableClasses;
    availableClasses.reserve(templateRoster.size());
    for (auto const& [classId, guid] : templateRoster)
        availableClasses.push_back(classId);

    std::string accountPrefix = sConfigMgr->GetOption<std::string>("CoaBots.RandomSpawn.AccountPrefix", "CoaBotHost");
    uint32 charactersPerAccount = sWorld->getIntConfig(CONFIG_CHARACTERS_PER_ACCOUNT);
    uint32 accountId = FindOrCreateBotAccount(accountPrefix, charactersPerAccount, handler);
    if (!accountId)
        return 0;

    uint8 classId = availableClasses[RandomInt(0, availableClasses.size() - 1)];
    uint32 templateGuid = templateRoster[classId];
    uint8 gender = uint8(RandomInt(0, 1));
    std::string name = GenerateUniqueName();

    ObjectGuid::LowType newGuid = CloneCharacter(templateGuid, accountId, name, race, gender);
    sCharacterCache->AddCharacterCacheEntry(ObjectGuid::Create<HighGuid::Player>(newGuid), accountId,
        name, gender, race, classId, 80);

    LOG_INFO("module.coa-playerbots", "BotMgr: created random bot '{}' (guid {}, class {}, race {}, account {}).",
        name, newGuid, classId, race, accountId);
    return newGuid;
}
}
