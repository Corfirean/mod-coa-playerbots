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
#include "BotAI.h"
#include "BotMgr.h"
#include "BotTalentBuilds.h"
#include "CharacterCache.h"
#include "Chat.h"
#include "ClassSpecRoles.h"
#include "BotZoneProgression.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
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

// A template's own race matters, not just its guid -- see CreateOneRandomBot's comment on why.
struct ClassTemplate
{
    uint32 guid = 0;
    uint8 race = 0;
};

// One representative level-80 template guid (+ its race) per custom class (12-32), queried live
// rather than hardcoded so the pool tracks the test-character roster as it changes. Deliberately
// excludes account 1 (LOCAL, the human player's own account) -- see BotMgr.h's standing rule
// that characters there must never be used as bots.
std::unordered_map<uint8, ClassTemplate> BuildClassTemplateRoster()
{
    std::unordered_map<uint8, ClassTemplate> roster;
    QueryResult result = CharacterDatabase.Query(
        "SELECT c.class, c.guid, c.race FROM characters c "
        "JOIN (SELECT class, MIN(guid) AS guid FROM characters WHERE account <> 1 AND level >= 80 "
        "AND class BETWEEN 12 AND 32 GROUP BY class) m ON m.class = c.class AND m.guid = c.guid");
    if (!result)
        return roster;
    do
    {
        Field* fields = result->Fetch();
        roster[fields[0].Get<uint8>()] = ClassTemplate{ fields[1].Get<uint32>(), fields[2].Get<uint8>() };
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

namespace
{
// Confirmed live: creating and logging in 1000 bots back-to-back in one synchronous burst
// crashed the worldserver outright (process just gone, no crash dump -- consistent with the OS
// killing it under a resource spike, not a normal engine-level crash). These throttle constants
// spread that same work across real wall-clock time instead -- gentle enough that even the
// configured MaxCount (1000) shouldn't reproduce it, without making a small request (a handful
// of bots) feel slow.
uint32 RandomSpawnBatchSize() { return sConfigMgr->GetOption<uint32>("CoaBots.RandomSpawn.BatchSize", 5); }
uint32 RandomSpawnIntervalMs() { return sConfigMgr->GetOption<uint32>("CoaBots.RandomSpawn.BatchIntervalMs", 500); }

uint32 g_pendingRandomBotCount = 0;
uint32 g_pendingRandomBotRequested = 0;
uint32 g_pendingRandomBotCreated = 0;
uint32 g_pendingRandomBotThrottleMs = 0;
}

void SpawnRandomBots(uint32 requestedCount, ChatHandler* handler)
{
    uint32 defaultCount = sConfigMgr->GetOption<uint32>("CoaBots.RandomSpawn.DefaultCount", 10);
    uint32 maxCount = sConfigMgr->GetOption<uint32>("CoaBots.RandomSpawn.MaxCount", 1000);

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

    // Fail fast on a broken setup up front, rather than queuing a batch that would just fail
    // bot-by-bot once ProcessPendingRandomBotSpawns starts draining it.
    if (BuildClassTemplateRoster().empty())
    {
        if (handler)
            handler->PSendSysMessage(
                "BotMgr: no usable template characters found (need at least one level 80+ character "
                "per class, not on account 1).");
        return;
    }
    if (CharacterColumns().empty())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: could not read the characters table schema, aborting.");
        return;
    }

    if (handler)
    {
        if (g_pendingRandomBotCount)
            handler->PSendSysMessage(
                "BotMgr: {} bot(s) already queued from an earlier request -- adding {} more to that queue.",
                g_pendingRandomBotCount, count);
        else
            handler->PSendSysMessage(
                "BotMgr: queued {} bot(s) for gradual spawn ({} every {}ms -- watch Server.log for progress). "
                "Spawning a large batch all at once used to crash the server.",
                count, RandomSpawnBatchSize(), RandomSpawnIntervalMs());
    }

    g_pendingRandomBotCount += count;
    g_pendingRandomBotRequested += count;
}

void ProcessPendingRandomBotSpawns(uint32 diff)
{
    if (!g_pendingRandomBotCount)
        return;

    if (g_pendingRandomBotThrottleMs > diff)
    {
        g_pendingRandomBotThrottleMs -= diff;
        return;
    }
    g_pendingRandomBotThrottleMs = RandomSpawnIntervalMs();

    bool autoLogin = sConfigMgr->GetOption<bool>("CoaBots.RandomSpawn.AutoLogin", true);
    uint32 thisBatch = std::min(g_pendingRandomBotCount, RandomSpawnBatchSize());

    for (uint32 i = 0; i < thisBatch; ++i)
    {
        // A single failed race pick (e.g. no template of that specific race's faction exists
        // yet) isn't a broken setup -- retry this one slot with freshly rolled races a few times
        // before counting it as a real failure, rather than either wasting the slot or aborting
        // the whole queue over what's often just an empty faction in the current template roster.
        ObjectGuid::LowType newGuid = 0;
        constexpr uint32 MAX_RACE_ATTEMPTS = 10;
        for (uint32 attempt = 0; attempt < MAX_RACE_ATTEMPTS && !newGuid; ++attempt)
        {
            uint8 race = VALID_RACES[RandomInt(0, VALID_RACES.size() - 1)];
            newGuid = CreateOneRandomBot(race, nullptr);
        }

        if (!newGuid)
        {
            LOG_ERROR("module.coa-playerbots",
                "BotMgr: gradual spawn failed to create a bot after {} attempts ({} still queued) -- giving "
                "up on the rest of this batch rather than spinning on a broken setup.",
                MAX_RACE_ATTEMPTS, g_pendingRandomBotCount - i);
            g_pendingRandomBotCount = 0;
            g_pendingRandomBotCreated = 0;
            g_pendingRandomBotRequested = 0;
            return;
        }

        ++g_pendingRandomBotCreated;
        if (autoLogin)
            sBotMgr->SpawnBot(newGuid, nullptr);
    }
    g_pendingRandomBotCount -= thisBatch;

    if (!g_pendingRandomBotCount)
    {
        LOG_INFO("module.coa-playerbots", "BotMgr: gradual spawn finished -- created {} of {} requested random bots.",
            g_pendingRandomBotCreated, g_pendingRandomBotRequested);
        g_pendingRandomBotCreated = 0;
        g_pendingRandomBotRequested = 0;
    }
}

ObjectGuid::LowType CreateOneRandomBot(uint8 race, ChatHandler* handler)
{
    std::unordered_map<uint8, ClassTemplate> templateRoster = BuildClassTemplateRoster();
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

    // Only consider templates whose OWN race is the same faction as the one being requested here
    // -- CloneCharacter below copies a template's map/position/homebind verbatim regardless of
    // the new character's own race, so cloning e.g. an Orc from a Human template's row put a
    // Horde-race bot standing in the middle of Stormwind. Confirmed live.
    TeamId wantedTeam = Player::TeamIdForRace(race);
    std::vector<uint8> availableClasses;
    availableClasses.reserve(templateRoster.size());
    for (auto const& [classId, tmpl] : templateRoster)
        if (Player::TeamIdForRace(tmpl.race) == wantedTeam)
            availableClasses.push_back(classId);

    if (availableClasses.empty())
    {
        if (handler)
            handler->PSendSysMessage(
                "BotMgr: no template character of race {}'s faction found (need at least one "
                "level 80+ character of a matching-faction race, not on account 1).", uint32(race));
        return 0;
    }

    std::string accountPrefix = sConfigMgr->GetOption<std::string>("CoaBots.RandomSpawn.AccountPrefix", "CoaBotHost");
    uint32 charactersPerAccount = sWorld->getIntConfig(CONFIG_CHARACTERS_PER_ACCOUNT);
    uint32 accountId = FindOrCreateBotAccount(accountPrefix, charactersPerAccount, handler);
    if (!accountId)
        return 0;

    uint8 classId = availableClasses[RandomInt(0, availableClasses.size() - 1)];
    uint32 templateGuid = templateRoster[classId].guid;
    uint8 gender = uint8(RandomInt(0, 1));
    std::string name = GenerateUniqueName();

    ObjectGuid::LowType newGuid = CloneCharacter(templateGuid, accountId, name, race, gender);
    sCharacterCache->AddCharacterCacheEntry(ObjectGuid::Create<HighGuid::Player>(newGuid), accountId,
        name, gender, race, classId, 80);

    LOG_INFO("module.coa-playerbots", "BotMgr: created random bot '{}' (guid {}, class {}, race {}, account {}).",
        name, newGuid, classId, race, accountId);
    return newGuid;
}

namespace
{
// Every primary profession (11), the 3 uncapped secondaries, and this realm's own two custom
// ones -- confirmed live: unlike stock WotLK's 2-primary-profession cap, CoA lets one character
// hold every profession at once, so a fresh population bot just gets all of them instead of a
// random subset.
constexpr uint32 PROFESSION_SKILLS[] = {
    171, 164, 333, 202, 165, 197, 182, 186, 393, 755, 773, // Alchemy, Blacksmithing, Enchanting,
                                                            // Engineering, Leatherworking, Tailoring,
                                                            // Herbalism, Mining, Skinning, Jewelcrafting, Inscription
    185, 129, 356,                                         // Cooking, First Aid, Fishing
    732, 757,                                               // Woodcutting, Woodworking (CoA custom)
};

constexpr uint32 BAG_ITEM_ID = 1977;   // "20-slot Bag" -- plain, no class/level restriction
constexpr uint32 FOOD_ITEM_ID = 4540;  // "Tough Hunk of Bread" -- real vendor food, any level
constexpr uint32 WATER_ITEM_ID = 1179; // "Ice Cold Milk" -- real vendor drink, any level

// Candidate pool for one (class, subclass, InventoryType) combination, cached after its first
// query -- see FindLevelAppropriateItem's comment for why. {requiredLevel, itemLevel, entry}.
struct LevelItemCandidate
{
    uint32 requiredLevel;
    uint32 itemLevel;
    uint32 entry;
};

// Confirmed live: querying item_template fresh per bot per slot (`ORDER BY ABS(ItemLevel - X)`
// has no usable index, so every call was a full table scan) took a worldserver tick over 70
// SECONDS once real bots started spawning -- fine for one admin testing gear on a single 80,
// catastrophic multiplied across 8 slots x several candidate types x 1000+ bots. Each distinct
// (class, subclass, InventoryType) combination is now queried ONCE per server run (a plain,
// unsorted SELECT -- one scan, not one per bot) and cached; every bot after that does an
// in-memory scan of at most a few hundred rows instead of a fresh round trip to MySQL.
std::vector<LevelItemCandidate> const& CandidatePool(uint32 itemClass, uint32 subclass, uint32 inventoryType)
{
    static std::unordered_map<uint64, std::vector<LevelItemCandidate>> cache;
    uint64 key = (uint64(itemClass) << 40) | (uint64(subclass) << 24) | uint64(inventoryType);

    auto itr = cache.find(key);
    if (itr != cache.end())
        return itr->second;

    std::vector<LevelItemCandidate> pool;
    QueryResult result = WorldDatabase.Query(
        "SELECT entry, ItemLevel, RequiredLevel FROM item_template WHERE class={} AND subclass={} "
        "AND InventoryType={} AND Quality BETWEEN 1 AND 3 AND AllowableClass=-1",
        itemClass, subclass, inventoryType);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            pool.push_back({ fields[2].Get<uint32>(), fields[1].Get<uint32>(), fields[0].Get<uint32>() });
        } while (result->NextRow());
    }
    return cache.emplace(key, std::move(pool)).first->second;
}

// "Closest ItemLevel to this bracket's target, among items this level can actually wear" --
// the whole selection rule, now answered from the cached in-memory pool instead of a live query.
uint32 FindLevelAppropriateItem(uint32 itemClass, uint32 subclass, uint32 inventoryType, uint8 level, float targetIlvl)
{
    uint32 best = 0;
    float bestDiff = 0.0f;
    for (LevelItemCandidate const& candidate : CandidatePool(itemClass, subclass, inventoryType))
    {
        if (candidate.requiredLevel > level)
            continue;
        float diff = std::abs(float(candidate.itemLevel) - targetIlvl);
        if (!best || diff < bestDiff)
        {
            best = candidate.entry;
            bestDiff = diff;
        }
    }
    return best;
}

// Same selection rule as FindLevelAppropriateItem, but returns up to maxCount entries instead of
// just the single closest one -- every bot of the same (class, subclass, inventoryType, level)
// used to get the literal same item id every time, since only the single closest match was ever
// offered as a candidate. Feeding a wider pool into TryEquipBestOf lets its existing
// ScoreItemForBot ranking (already role/stat aware) pick among genuinely different stat rolls
// instead of there only ever being one option to "pick" -- this is what actually produces
// variety, not a random roll independent of role fitness.
std::vector<uint32> FindLevelAppropriateItemPool(uint32 itemClass, uint32 subclass, uint32 inventoryType, uint8 level, float targetIlvl, size_t maxCount = 6)
{
    std::vector<LevelItemCandidate> matches;
    for (LevelItemCandidate const& candidate : CandidatePool(itemClass, subclass, inventoryType))
        if (candidate.requiredLevel <= level)
            matches.push_back(candidate);

    std::sort(matches.begin(), matches.end(), [targetIlvl](LevelItemCandidate const& a, LevelItemCandidate const& b)
    {
        return std::abs(float(a.itemLevel) - targetIlvl) < std::abs(float(b.itemLevel) - targetIlvl);
    });

    std::vector<uint32> pool;
    for (size_t i = 0; i < matches.size() && i < maxCount; ++i)
        pool.push_back(matches[i].entry);
    return pool;
}

// Same "check CanEquipNewItem before ever touching what's already there" shape as
// BotMgr::GearUpBot -- see its header comment for why the order matters (a candidate this bot's
// class/spec genuinely can't use must never cost it whatever it already had equipped).
bool TryEquipBestOf(Player* bot, uint8 slot, std::vector<uint32> const& candidates, uint8 level)
{
    uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
    BotRole role = BotAI::GetRoleForClassSpec(bot->getClass(), activeSpec);

    uint32 bestItem = 0;
    float bestScore = -1.0f;

    if (Item* existing = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
    {
        if (ItemTemplate const* existingTmpl = existing->GetTemplate())
            bestScore = BotAI::ScoreItemForBot(bot, existingTmpl, role);
    }

    // Small, deterministic per-(bot, slot, item) jitter -- purely to stop every bot of the same
    // class/level/role from equipping the literal same item id when several candidates score
    // within noise of each other (now that FindLevelAppropriateItemPool offers more than one).
    // Seeded from stable identifiers (not rand()) so the same bot always resolves the same way
    // between gearing passes rather than re-rolling its look on every geartrainer run. Kept small
    // relative to ScoreItemForBot's typical spread (single stat points already swing the score by
    // several points) so it only breaks near-ties, never overrides a genuinely better item.
    auto jitter = [&](uint32 itemId) -> float
    {
        uint32 h = bot->GetGUID().GetCounter() * 2654435761u + slot * 40503u + itemId * 2246822519u;
        return float(h % 500) / 100.0f; // 0.0 - 5.0
    };

    for (uint32 itemId : candidates)
    {
        if (!itemId)
            continue;
        ItemTemplate const* tmpl = sObjectMgr->GetItemTemplate(itemId);
        if (!tmpl)
            continue;

        uint16 dest = uint16(slot) | (uint16(INVENTORY_SLOT_BAG_0) << 8);
        if (bot->CanEquipNewItem(NULL_SLOT, dest, itemId, true) != EQUIP_ERR_OK)
            continue;

        float score = BotAI::ScoreItemForBot(bot, tmpl, role) + jitter(itemId);
        if (score > bestScore)
        {
            bestScore = score;
            bestItem = itemId;
        }
    }

    if (bestItem)
    {
        if (Item* existing = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            bot->DestroyItemCount(existing->GetEntry(), 1, true);
        return bot->StoreNewItemInBestSlots(bestItem, 1);
    }
    return false;
}

// Level-bracketed gear-up for a freshly created/leveled bot -- same 15-slot coverage and
// "try every armor type, first that's actually equippable wins" shape as BotMgr::GearUpBot (see
// its header comment: getClass() returns this realm's own custom ClassId, not a stock 1-11
// value, so armor proficiency can't be inferred from class and has to be discovered by asking
// the real engine). The difference here is scale: with 8 requested level brackets x 4 armor
// types x 8 slots, a hand-picked table the size of GearUpBot's would mean over 250 curated item
// ids. Querying item_template live for "closest ItemLevel to this bracket's midpoint, among
// items this level can already wear" gets the same big-bracket, not-exact-BiS gearing without
// that curation burden, and keeps working if this realm's item data changes later.
void GearUpFreshBotForLevel(Player* bot, uint8 level)
{
    float targetIlvl = float(level) * 2.5f; // roughly matches GearUpBot's own level80->ilvl200 baseline

    static uint8 const armorSlots[8] = {
        EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_SHOULDERS, EQUIPMENT_SLOT_CHEST, EQUIPMENT_SLOT_WAIST,
        EQUIPMENT_SLOT_LEGS, EQUIPMENT_SLOT_FEET, EQUIPMENT_SLOT_WRISTS, EQUIPMENT_SLOT_HANDS,
    };
    static uint32 const armorInventoryTypes[8] = { 1, 3, 5, 6, 7, 8, 9, 10 };
    static uint8 const armorSubclasses[4] = { 4, 3, 2, 1 }; // plate, mail, leather, cloth

    for (int i = 0; i < 8; ++i)
    {
        std::vector<uint32> candidates;
        for (uint8 subclass : armorSubclasses)
        {
            for (uint32 item : FindLevelAppropriateItemPool(4, subclass, armorInventoryTypes[i], level, targetIlvl))
                candidates.push_back(item);
            if (armorInventoryTypes[i] == 5) // chest: cloth robes use InventoryType=20, not 5
                for (uint32 robe : FindLevelAppropriateItemPool(4, subclass, 20, level, targetIlvl))
                    candidates.push_back(robe);
        }
        TryEquipBestOf(bot, armorSlots[i], candidates, level);
    }

    // Neck/back: armor-type-agnostic (subclass 0), pooled the same way as armor for variety.
    TryEquipBestOf(bot, EQUIPMENT_SLOT_NECK, FindLevelAppropriateItemPool(4, 0, 2, level, targetIlvl), level);
    TryEquipBestOf(bot, EQUIPMENT_SLOT_BACK, FindLevelAppropriateItemPool(4, 1, 16, level, targetIlvl), level);

    // Split a shared pool into two disjoint (alternating) candidate lists so each ring/trinket
    // slot gets real variety via TryEquipBestOf's own scoring+jitter, while the two slots can
    // never both land on the same item id (confirmed live: every bot was getting the literal
    // same two rings/trinkets, since only the single closest-by-ilvl pair was ever offered).
    auto splitAlternating = [](std::vector<uint32> const& pool)
    {
        std::vector<uint32> a, b;
        for (size_t i = 0; i < pool.size(); ++i)
            (i % 2 == 0 ? a : b).push_back(pool[i]);
        return std::make_pair(a, b);
    };

    auto [ringPoolA, ringPoolB] = splitAlternating(FindLevelAppropriateItemPool(4, 0, 11, level, targetIlvl, 10));
    if (!ringPoolA.empty() || !ringPoolB.empty())
    {
        TryEquipBestOf(bot, EQUIPMENT_SLOT_FINGER1, ringPoolA.empty() ? ringPoolB : ringPoolA, level);
        TryEquipBestOf(bot, EQUIPMENT_SLOT_FINGER2, ringPoolB.empty() ? ringPoolA : ringPoolB, level);
    }

    auto [trinketPoolA, trinketPoolB] = splitAlternating(FindLevelAppropriateItemPool(4, 0, 12, level, targetIlvl, 10));
    if (!trinketPoolA.empty() || !trinketPoolB.empty())
    {
        TryEquipBestOf(bot, EQUIPMENT_SLOT_TRINKET1, trinketPoolA.empty() ? trinketPoolB : trinketPoolA, level);
        TryEquipBestOf(bot, EQUIPMENT_SLOT_TRINKET2, trinketPoolB.empty() ? trinketPoolA : trinketPoolB, level);
    }

    // Mainhand: try common 1H weapon subclasses in turn -- same fallback-chain reasoning as
    // GearUpBot's mainhandCandidates (weapon-skill proficiency varies noticeably across this
    // project's 21 custom classes, unlike armor).
    static uint32 const weaponSubclasses[] = { 7, 4, 0, 13, 15, 10 }; // sword, mace, axe, fist, dagger, staff
    std::vector<uint32> weaponCandidates;
    for (uint32 subclass : weaponSubclasses)
        for (uint32 weapon : FindLevelAppropriateItemPool(2, subclass, 13 /*INVTYPE_WEAPON*/, level, targetIlvl))
            weaponCandidates.push_back(weapon);
    TryEquipBestOf(bot, EQUIPMENT_SLOT_MAINHAND, weaponCandidates, level);

    // Tanks equip a Shield in off-hand (e.g. Guardian Vanguard, Templar Oathkeeper)
    uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
    BotRole role = BotAI::GetRoleForClassSpec(bot->getClass(), activeSpec);
    if (role == BotRole::Tank)
    {
        // Class 4 (Armor), Subclass 6 (Shield), InventoryType 14 (INVTYPE_SHIELD)
        if (uint32 shield = FindLevelAppropriateItem(4, 6, 14, level, targetIlvl))
            TryEquipBestOf(bot, EQUIPMENT_SLOT_OFFHAND, { shield }, level);
    }
}

// Post-login setup for a freshly cloned population bot: brings it down from the level-80
// template to its actually-requested level via the real engine call (recalculates HP/mana/
// stats correctly, same as a GM `.levelup`), then grants professions/bags/food/water/gear. Runs
// once, from BotMgr::SpawnBot's onReady callback -- needs a real logged-in Player object for
// GiveLevel/SetSkill/StoreNewItemInBestSlots to work, so it can't happen at DB-clone time.
void ApplyFreshBotSetup(Player* bot, uint8 level)
{
    if (!bot)
        return;

    if (level != bot->GetLevel())
        bot->GiveLevel(level);

    if (level >= 10)
    {
        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        if (!activeSpec)
        {
            activeSpec = BotTalentBuilds::ChooseSpecForBot(bot);
            if (activeSpec)
                sBotMgr->LearnSpecialization(bot->GetGUID().GetCounter(), activeSpec, nullptr);
        }
        else
        {
            BotTalentBuilds::ApplyBuildForLevel(bot, level);
        }
    }

    GrantAllProfessions(bot, level);

    for (int i = 0; i < 4; ++i)
        bot->StoreNewItemInBestSlots(BAG_ITEM_ID, 1);
    bot->StoreNewItemInBestSlots(FOOD_ITEM_ID, 20);
    bot->StoreNewItemInBestSlots(WATER_ITEM_ID, 20);

    GearUpFreshBotForLevel(bot, level);

    // Relocate fresh bot to a level-appropriate zone hub instead of leaving it at the cloned template's coordinates
    BotZoneProgression::RelocateBot(bot, true /*force initial relocation*/);

    LOG_INFO("module.coa-playerbots", "BotMgr: applied fresh-bot setup (level {}, professions, bags, food/water, gear) to '{}'.",
        level, bot->GetName());
}

// Weighted level roll for SpawnLeveledBots: mostly 1-10 (explicit user request -- a realistic
// "mostly fresh characters, some further along" population), tapering off toward 80.
uint8 RollWeightedLevel()
{
    uint32 roll = RandomInt(1, 100);
    if (roll <= 60) return uint8(RandomInt(1, 10));
    if (roll <= 80) return uint8(RandomInt(11, 30));
    if (roll <= 95) return uint8(RandomInt(31, 60));
    return uint8(RandomInt(61, 80));
}

uint32 g_pendingLeveledBotCount = 0;
uint32 g_pendingLeveledBotRequested = 0;
uint32 g_pendingLeveledBotCreated = 0;
uint32 g_pendingLeveledBotThrottleMs = 0;
}

// Standalone so it can be applied to a bot that never went through ApplyFreshBotSetup at all --
// confirmed live: this project's original hand-made test characters (created long before the
// population/leveling system existed) have zero profession skills, since they were never run
// through this path. Safe to call repeatedly (SetSkill on an already-known skill is a no-op
// beyond refreshing the cap). Needs real (BotSpawn-external) linkage, unlike ApplyFreshBotSetup
// above, to be callable from BotCommand.cpp's professiontrainer command.
void GrantAllProfessions(Player* bot, uint8 level)
{
    uint16 cap = uint16(std::max<uint32>(1, std::min<uint32>(450, uint32(level) * 6)));
    for (uint32 skillId : PROFESSION_SKILLS)
        bot->SetSkill(uint16(skillId), 1, cap, cap);

    if (!bot->HasSpell(7620))
        bot->learnSpell(7620);
}

void SpawnLeveledBots(uint32 requestedCount, ChatHandler* handler)
{
    uint32 maxCount = sConfigMgr->GetOption<uint32>("CoaBots.RandomSpawn.MaxCount", 1000);
    uint32 count = requestedCount;
    if (count > maxCount * 2) // this path is requested in bigger batches (1000-1500) than plain spawnrandom
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: clamping requested {} leveled bots to {}.", count, maxCount * 2);
        count = maxCount * 2;
    }
    if (count == 0)
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: nothing to do (count is 0).");
        return;
    }

    if (BuildClassTemplateRoster().empty())
    {
        if (handler)
            handler->PSendSysMessage(
                "BotMgr: no usable template characters found (need at least one level 80+ character "
                "per class, not on account 1).");
        return;
    }
    if (CharacterColumns().empty())
    {
        if (handler)
            handler->PSendSysMessage("BotMgr: could not read the characters table schema, aborting.");
        return;
    }

    if (handler)
    {
        if (g_pendingLeveledBotCount)
            handler->PSendSysMessage(
                "BotMgr: {} leveled bot(s) already queued from an earlier request -- adding {} more.",
                g_pendingLeveledBotCount, count);
        else
            handler->PSendSysMessage(
                "BotMgr: queued {} leveled bot(s) (levels 1-80, weighted mostly 1-10) for gradual spawn "
                "({} every {}ms -- watch Server.log for progress).",
                count, RandomSpawnBatchSize(), RandomSpawnIntervalMs());
    }

    g_pendingLeveledBotCount += count;
    g_pendingLeveledBotRequested += count;
}

void ProcessPendingLeveledBotSpawns(uint32 diff)
{
    if (!g_pendingLeveledBotCount)
        return;

    if (g_pendingLeveledBotThrottleMs > diff)
    {
        g_pendingLeveledBotThrottleMs -= diff;
        return;
    }
    g_pendingLeveledBotThrottleMs = RandomSpawnIntervalMs();

    uint32 thisBatch = std::min(g_pendingLeveledBotCount, RandomSpawnBatchSize());
    for (uint32 i = 0; i < thisBatch; ++i)
    {
        uint8 level = RollWeightedLevel();
        ObjectGuid::LowType newGuid = 0;
        constexpr uint32 MAX_RACE_ATTEMPTS = 10;
        for (uint32 attempt = 0; attempt < MAX_RACE_ATTEMPTS && !newGuid; ++attempt)
        {
            uint8 race = VALID_RACES[RandomInt(0, VALID_RACES.size() - 1)];
            newGuid = CreateOneRandomBot(race, nullptr);
        }

        if (!newGuid)
        {
            LOG_ERROR("module.coa-playerbots",
                "BotMgr: leveled gradual spawn failed to create a bot after {} attempts ({} still queued) -- "
                "giving up on the rest rather than spinning on a broken setup.",
                MAX_RACE_ATTEMPTS, g_pendingLeveledBotCount - i);
            g_pendingLeveledBotCount = 0;
            g_pendingLeveledBotCreated = 0;
            g_pendingLeveledBotRequested = 0;
            return;
        }

        ++g_pendingLeveledBotCreated;
        sBotMgr->SpawnBot(newGuid, nullptr, [level](Player* bot) { ApplyFreshBotSetup(bot, level); });
    }
    g_pendingLeveledBotCount -= thisBatch;

    if (!g_pendingLeveledBotCount)
    {
        LOG_INFO("module.coa-playerbots", "BotMgr: leveled gradual spawn finished -- created {} of {} requested bots.",
            g_pendingLeveledBotCreated, g_pendingLeveledBotRequested);
        g_pendingLeveledBotCreated = 0;
        g_pendingLeveledBotRequested = 0;
    }
}
}
