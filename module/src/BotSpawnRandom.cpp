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
#include "BotProgression.h"
#include "BotTaxi.h"
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
#include <unordered_set>
#include <utility>
#include <vector>

namespace BotSpawn
{
namespace
{
// Defined further down in this same (TU-wide) unnamed namespace; forward-declared here so
// ProcessPendingRandomBotSpawns can hand it to spawnrandom bots too, same as spawnleveled already does.
void ApplyFreshBotSetup(Player* bot, uint8 level);

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

// Up to one template per faction for a class, indexed by TeamId.
struct ClassTemplates
{
    std::array<ClassTemplate, 2> byTeam;
};

// Level-80 template characters per custom class (12-32) and faction, queried live rather than
// hardcoded so the pool tracks the test-character roster as it changes. Excludes account 1
// (LOCAL, the human player's own account -- see BotMgr.h's standing rule) and every bot-hosting
// account, so a population bot that reaches 80 can never become the template for the next one.
//
// Keeping one template per faction, not just the lowest guid per class, is what balances classes:
// with only one, a class existed for one faction alone, and 311 Horde bots were split across the
// five classes whose lowest-guid template happened to be Horde -- confirmed live at 52-75 bots each
// for those against 12-25 for everything else.
std::unordered_map<uint8, ClassTemplates> BuildClassTemplateRoster()
{
    std::string prefix = sConfigMgr->GetOption<std::string>("CoaBots.RandomSpawn.AccountPrefix", "CoaBotHost");
    std::unordered_set<uint32> botAccounts;
    for (uint32 n = 1; n <= 10000; ++n)
    {
        uint32 accountId = AccountMgr::GetId(prefix + std::to_string(n));
        if (!accountId)
            break;
        botAccounts.insert(accountId);
    }

    std::unordered_map<uint8, ClassTemplates> roster;
    QueryResult result = CharacterDatabase.Query(
        "SELECT class, guid, race, account FROM characters WHERE account <> 1 AND level >= 80 "
        "AND class BETWEEN 12 AND 32 ORDER BY guid");
    if (!result)
        return roster;
    do
    {
        Field* fields = result->Fetch();
        if (botAccounts.count(fields[3].Get<uint32>()))
            continue;

        uint8 race = fields[2].Get<uint8>();
        size_t team = Player::TeamIdForRace(race) == TEAM_ALLIANCE ? 0 : 1;
        ClassTemplate& slot = roster[fields[0].Get<uint8>()].byTeam[team];
        if (!slot.guid)
            slot = ClassTemplate{ fields[1].Get<uint32>(), race };
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
    uint8 race, uint8 gender, std::vector<std::pair<std::string, std::string>> const& overrides = {})
{
    std::vector<std::string> const& columns = CharacterColumns();
    ObjectGuid::LowType newGuid = sObjectMgr->GetGenerator<HighGuid::Player>().Generate();

    std::ostringstream insertCols;
    std::ostringstream selectVals;
    for (std::string const& col : columns)
    {
        insertCols << "`" << col << "`,";
        auto overrideItr = std::find_if(overrides.begin(), overrides.end(),
            [&col](std::pair<std::string, std::string> const& entry) { return entry.first == col; });
        if (overrideItr != overrides.end())
            selectVals << overrideItr->second << ",";
        else if (col == "guid")
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
            // CloneCharacter never copies character_inventory/item_instance (cloning item guids
            // across characters would be its own can of worms -- see CloneCharacter's comment),
            // so a cloned bot starts with zero gear regardless of its template's own equipment.
            // Same fix as spawnleveled: hand it real level-appropriate gear via ApplyFreshBotSetup
            // instead of leaving it naked. Every spawnrandom bot is level 80 (see CreateBotClone).
            sBotMgr->SpawnBot(newGuid, nullptr, [](Player* bot) { ApplyFreshBotSetup(bot, 80); });
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

namespace
{
// A fresh population bot's spec is rolled here, at clone time, instead of inherited: the clone copies
// the template's character_settings, which handed every bot of a class the template's spec -- every
// live Tinker bot was Invention, every Witch Hunter Black Knight -- so ChooseSpecForBot's role
// balancing never ran for a single one of them. Writing it before the first login also means
// mod-ascension-compat reads this spec on login rather than caching the template's.
uint32 RollSpecForClass(uint8 classId)
{
    float tankPct = sConfigMgr->GetOption<float>("CoaBots.SpecBalance.TankTargetPct", 20.0f);
    float healerPct = sConfigMgr->GetOption<float>("CoaBots.SpecBalance.HealerTargetPct", 20.0f);
    float roll = float(RandomInt(1, 100));
    BotRole wanted = roll <= tankPct ? BotRole::Tank : roll <= tankPct + healerPct ? BotRole::Healer : BotRole::Dps;

    for (BotRole role : { wanted, BotRole::Dps, BotRole::Tank, BotRole::Healer })
    {
        std::vector<uint32> specs = BotAI::GetSpecsForRole(classId, role);
        if (!specs.empty())
            return specs[RandomInt(0, uint32(specs.size()) - 1)];
    }
    return 0;
}

std::string Zeros(uint32 count)
{
    std::string zeros;
    for (uint32 i = 0; i < count; ++i)
        zeros += "0 ";
    return zeros;
}

// Everything on the cloned row that belongs to the level-80 template rather than to a character of
// this level. Level is the one that matters most: written here, the bot's first login already happens
// at its real level, so mod-ascension-compat's progression sync gives it that level's kit. The old
// flow logged in at 80 and only lowered the level afterwards with GiveLevel, which removes nothing --
// confirmed live: bots averaged 1318 spells at levels 1-10 against 1389 at 80. The masks are written
// as the exact number of zero tokens the loader expects (14 taxi, 128 explored-zone, 6 title words).
std::vector<std::pair<std::string, std::string>> FreshCharacterOverrides(uint8 level)
{
    uint32 money = uint32(level) * level * 20 * RandomInt(50, 150) / 100;
    return {
        { "level", std::to_string(level) },
        { "xp", "0" },
        { "money", std::to_string(money) },
        { "totaltime", "0" },
        { "leveltime", "0" },
        { "taximask", "'" + Zeros(14) + "'" },
        { "exploredZones", "'" + Zeros(128) + "'" },
        { "knownTitles", "'" + Zeros(6) + "'" },
        { "chosenTitle", "0" },
    };
}

// level 0 keeps the template's own row untouched (the original .botcmd spawnrandom / BG-fill
// behaviour: a level-80 copy); any other level creates a fresh character of that level.
ObjectGuid::LowType CreateBotClone(uint8 race, ChatHandler* handler, uint8 level)
{
    std::unordered_map<uint8, ClassTemplates> templateRoster = BuildClassTemplateRoster();
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

    // A template of the requested race's own faction is preferred -- CloneCharacter copies a
    // template's map/position/homebind verbatim regardless of the new character's race, so cloning
    // an Orc from a Human template's row once put a Horde bot in the middle of Stormwind (confirmed
    // live). A fresh levelled bot (level != 0) is exempt when its class has no same-faction
    // template: ApplyFreshBotSetup force-relocates it to its own race's hub on first login, so the
    // template's position never matters. Without that exemption ten classes, whose templates are all
    // one faction, could never be played by the other.
    size_t wantedTeam = Player::TeamIdForRace(race) == TEAM_ALLIANCE ? 0 : 1;
    std::vector<uint8> availableClasses;
    availableClasses.reserve(templateRoster.size());
    for (auto const& [classId, tmpl] : templateRoster)
        if (tmpl.byTeam[wantedTeam].guid || (level && tmpl.byTeam[1 - wantedTeam].guid))
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
    ClassTemplates const& templates = templateRoster[classId];
    uint32 templateGuid = templates.byTeam[wantedTeam].guid ? templates.byTeam[wantedTeam].guid
        : templates.byTeam[1 - wantedTeam].guid;
    uint8 gender = uint8(RandomInt(0, 1));
    std::string name = GenerateUniqueName();

    std::vector<std::pair<std::string, std::string>> overrides;
    if (level)
        overrides = FreshCharacterOverrides(level);

    ObjectGuid::LowType newGuid = CloneCharacter(templateGuid, accountId, name, race, gender, overrides);

    uint32 spec = 0;
    if (level)
    {
        spec = RollSpecForClass(classId);
        CharacterDatabase.DirectExecute(
            "REPLACE INTO character_settings (guid, source, data) VALUES ({}, 'core.ascension_active_spec', '{} ')",
            newGuid, spec);
    }

    uint8 cachedLevel = level ? level : 80;
    sCharacterCache->AddCharacterCacheEntry(ObjectGuid::Create<HighGuid::Player>(newGuid), accountId,
        name, gender, race, classId, cachedLevel);

    LOG_INFO("module.coa-playerbots",
        "BotMgr: created random bot '{}' (guid {}, class {}, race {}, level {}, spec {}, account {}).",
        name, newGuid, classId, race, cachedLevel, spec, accountId);
    return newGuid;
}
}

ObjectGuid::LowType CreateOneRandomBot(uint8 race, ChatHandler* handler)
{
    return CreateBotClone(race, handler, 0);
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

// Candidate pool for one (class, subclass, InventoryType) combination, cached after its first
// query -- see CandidatePool's comment for why. {requiredLevel, itemLevel, entry, quality}.
struct LevelItemCandidate
{
    uint32 requiredLevel;
    uint32 itemLevel;
    uint32 entry;
    uint32 quality;
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
        "SELECT entry, ItemLevel, RequiredLevel, Quality FROM item_template WHERE class={} AND subclass={} "
        "AND InventoryType={} AND Quality BETWEEN 1 AND 4 AND AllowableClass=-1 "
        // Placeholder and test rows that exist in item_template but no player can ever obtain --
        // the first test batch equipped "RPGITEM PH - Plate Shoulder" and "CoA Test Bow".
        "AND name NOT LIKE '%RPGITEM%' AND name NOT LIKE '%[PH]%' AND name NOT LIKE '% PH %' "
        "AND name NOT LIKE '%Test%' AND name NOT LIKE 'Monster - %' AND name NOT LIKE '%Deprecated%' "
        "AND name NOT LIKE '%[DND]%' AND name NOT LIKE 'NPC %' "
        // Items with a requirement a fresh bot cannot meet. CanEquipNewItem rejects them anyway, but
        // they sort to the top of a quality-ordered pool, so on the first test batches whole weapon
        // pools were nothing but these and two bots ended up with no weapon at all.
        "AND RequiredSkill=0 AND requiredspell=0 AND requiredhonorrank=0 AND RequiredCityRank=0 "
        "AND RequiredReputationFaction=0 AND Map=0 AND area=0 AND HolidayId=0 "
        "AND (AllowableRace=-1 OR (AllowableRace & 1791)=1791)",
        itemClass, subclass, inventoryType);
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            pool.push_back({ fields[2].Get<uint32>(), fields[1].Get<uint32>(), fields[0].Get<uint32>(),
                fields[3].Get<uint32>() });
        } while (result->NextRow());
    }
    return cache.emplace(key, std::move(pool)).first->second;
}

// Highest quality one gear slot may roll for a bot of this level: mostly greens while levelling,
// blues toward the cap, the occasional epic at 80. Rolled per slot, so a bot ends up with a mix
// the way a real character does instead of a uniform set.
uint32 RollQualityCap(uint8 level)
{
    uint32 roll = RandomInt(1, 100);
    if (level <= 20)
        return roll <= 25 ? ITEM_QUALITY_NORMAL : roll <= 90 ? ITEM_QUALITY_UNCOMMON : ITEM_QUALITY_RARE;
    if (level <= 60)
        return roll <= 5 ? ITEM_QUALITY_NORMAL : roll <= 75 ? ITEM_QUALITY_UNCOMMON : ITEM_QUALITY_RARE;
    if (level < 80)
        return roll <= 55 ? ITEM_QUALITY_UNCOMMON : ITEM_QUALITY_RARE;
    return roll <= 20 ? ITEM_QUALITY_UNCOMMON : roll <= 90 ? ITEM_QUALITY_RARE : ITEM_QUALITY_EPIC;
}

// Candidates from the last few levels, so a bot looks like it levelled into its gear rather than
// being handed the best item its level can wear (the previous rule, "closest to level * 2.5 item
// level", amounted to exactly that). Falls back to anything wearable when that window is empty for a
// slot, which happens at the lowest levels and for rarer item types. Several candidates are returned
// so TryEquipBestOf's role-aware scoring still has a real choice.
std::vector<uint32> GearPool(uint32 itemClass, uint32 subclass, uint32 inventoryType, uint8 level,
    uint32 qualityCap, size_t maxCount = 6)
{
    std::vector<LevelItemCandidate> matches;
    for (uint32 window : { 5u, 255u })
    {
        for (LevelItemCandidate const& candidate : CandidatePool(itemClass, subclass, inventoryType))
            if (candidate.requiredLevel <= level && candidate.requiredLevel + window >= level &&
                candidate.quality <= qualityCap)
                matches.push_back(candidate);
        if (!matches.empty())
            break;
    }

    std::sort(matches.begin(), matches.end(), [](LevelItemCandidate const& a, LevelItemCandidate const& b)
    {
        if (a.quality != b.quality)
            return a.quality > b.quality;
        if (a.requiredLevel != b.requiredLevel)
            return a.requiredLevel > b.requiredLevel;
        return a.itemLevel > b.itemLevel;
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
    // within noise of each other (now that GearPool offers more than one).
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
        {
            if (BotAI::IsProfessionTool(existing->GetTemplate()))
            {
                if (!BotAI::MoveEquippedToolToBags(bot, slot))
                    return false;
            }
            else
                bot->DestroyItemCount(existing->GetEntry(), 1, true);
        }
        return bot->StoreNewItemInBestSlots(bestItem, 1);
    }
    return false;
}

// Level-bracketed gear-up for a freshly created/leveled bot -- same 15-slot coverage and
// "try every armor type, first that's actually equippable wins" shape as BotMgr::GearUpBot (see
// its header comment: getClass() returns this realm's own custom ClassId, not a stock 1-11
// value, so armor proficiency can't be inferred from class and has to be discovered by asking
// the real engine). Each slot rolls its own quality cap and draws from the last few levels'
// items (see GearPool), so bots look levelled rather than uniformly best-in-slot.
void GearUpFreshBotForLevel(Player* bot, uint8 level)
{
    static uint8 const armorSlots[8] = {
        EQUIPMENT_SLOT_HEAD, EQUIPMENT_SLOT_SHOULDERS, EQUIPMENT_SLOT_CHEST, EQUIPMENT_SLOT_WAIST,
        EQUIPMENT_SLOT_LEGS, EQUIPMENT_SLOT_FEET, EQUIPMENT_SLOT_WRISTS, EQUIPMENT_SLOT_HANDS,
    };
    static uint32 const armorInventoryTypes[8] = { 1, 3, 5, 6, 7, 8, 9, 10 };
    static uint8 const armorSubclasses[4] = { 4, 3, 2, 1 }; // plate, mail, leather, cloth

    for (int i = 0; i < 8; ++i)
    {
        uint32 cap = RollQualityCap(level);
        std::vector<uint32> candidates;
        for (uint8 subclass : armorSubclasses)
        {
            for (uint32 item : GearPool(4, subclass, armorInventoryTypes[i], level, cap))
                candidates.push_back(item);
            if (armorInventoryTypes[i] == 5) // chest: cloth robes use InventoryType=20, not 5
                for (uint32 robe : GearPool(4, subclass, 20, level, cap))
                    candidates.push_back(robe);
        }
        TryEquipBestOf(bot, armorSlots[i], candidates, level);
    }

    TryEquipBestOf(bot, EQUIPMENT_SLOT_NECK, GearPool(4, 0, 2, level, RollQualityCap(level)), level);
    TryEquipBestOf(bot, EQUIPMENT_SLOT_BACK, GearPool(4, 1, 16, level, RollQualityCap(level)), level);

    // Split a shared pool into two disjoint (alternating) candidate lists so the two ring and the
    // two trinket slots can never land on the same item id (confirmed live: every bot once got the
    // literal same pair, since only the single closest match was ever offered).
    auto splitAlternating = [](std::vector<uint32> const& pool)
    {
        std::vector<uint32> a, b;
        for (size_t i = 0; i < pool.size(); ++i)
            (i % 2 == 0 ? a : b).push_back(pool[i]);
        return std::make_pair(a, b);
    };

    auto [ringPoolA, ringPoolB] = splitAlternating(GearPool(4, 0, 11, level, RollQualityCap(level), 10));
    if (!ringPoolA.empty() || !ringPoolB.empty())
    {
        TryEquipBestOf(bot, EQUIPMENT_SLOT_FINGER1, ringPoolA.empty() ? ringPoolB : ringPoolA, level);
        TryEquipBestOf(bot, EQUIPMENT_SLOT_FINGER2, ringPoolB.empty() ? ringPoolA : ringPoolB, level);
    }

    auto [trinketPoolA, trinketPoolB] = splitAlternating(GearPool(4, 0, 12, level, RollQualityCap(level), 10));
    if (!trinketPoolA.empty() || !trinketPoolB.empty())
    {
        TryEquipBestOf(bot, EQUIPMENT_SLOT_TRINKET1, trinketPoolA.empty() ? trinketPoolB : trinketPoolA, level);
        TryEquipBestOf(bot, EQUIPMENT_SLOT_TRINKET2, trinketPoolB.empty() ? trinketPoolA : trinketPoolB, level);
    }

    // Main hand: one-handers and two-handers compete on score. Two-handers and staves used to be
    // missing from the candidate list entirely (only InventoryType 13 was ever searched), which is
    // why caster bots ended up with no weapon at all.
    static std::pair<uint32, uint32> const mainHandTypes[] = {
        { 0, 13 }, { 4, 13 }, { 7, 13 }, { 13, 13 }, { 15, 13 },   // one-handed axe/mace/sword/fist/dagger
        { 0, 21 }, { 4, 21 }, { 7, 21 }, { 15, 21 },               // main-hand-only variants
        { 1, 17 }, { 5, 17 }, { 6, 17 }, { 8, 17 }, { 10, 17 },   // two-handed axe/mace, polearm, sword, staff
    };
    uint32 weaponCap = RollQualityCap(level);
    std::vector<uint32> mainHand;
    for (auto const& [subclass, invType] : mainHandTypes)
        for (uint32 item : GearPool(2, subclass, invType, level, weaponCap, 12))
            mainHand.push_back(item);
    TryEquipBestOf(bot, EQUIPMENT_SLOT_MAINHAND, mainHand, level);

    Item* equippedMainHand = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    bool twoHanded = equippedMainHand && equippedMainHand->GetTemplate()->InventoryType == INVTYPE_2HWEAPON;
    if (!twoHanded)
    {
        // Off hand: whatever this bot can actually use there -- a shield for tanks, a held item for
        // casters, a second weapon for anyone the engine says can dual wield. CanEquipNewItem inside
        // TryEquipBestOf filters out everything else.
        uint32 offCap = RollQualityCap(level);
        std::vector<uint32> offHand;
        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        if (BotAI::GetRoleForClassSpec(bot->getClass(), activeSpec) == BotRole::Tank)
            for (uint32 item : GearPool(4, 6, 14, level, offCap))
                offHand.push_back(item);
        for (uint32 item : GearPool(4, 0, 23, level, offCap))
            offHand.push_back(item);
        if (bot->CanDualWield())
            for (uint32 subclass : { 0u, 4u, 7u, 13u, 15u })
                for (uint32 invType : { 13u, 22u })
                    for (uint32 item : GearPool(2, subclass, invType, level, offCap))
                        offHand.push_back(item);
        TryEquipBestOf(bot, EQUIPMENT_SLOT_OFFHAND, offHand, level);
    }

    // Ranged: bows, guns, crossbows, wands and thrown -- again only what the class can equip.
    static std::pair<uint32, uint32> const rangedTypes[] = { { 2, 15 }, { 3, 26 }, { 18, 26 }, { 19, 26 }, { 16, 25 } };
    uint32 rangedCap = RollQualityCap(level);
    std::vector<uint32> ranged;
    for (auto const& [subclass, invType] : rangedTypes)
        for (uint32 item : GearPool(2, subclass, invType, level, rangedCap))
            ranged.push_back(item);
    TryEquipBestOf(bot, EQUIPMENT_SLOT_RANGED, ranged, level);
}

// Profession skills shaped by the bot's personality. CoA lets a character hold every profession,
// so every one is learned, but at values below the level's cap and spread per bot, with the ones
// its personality leans toward (gathering, fishing) kept higher -- which is also what makes Gatherer
// bots the ones able to open the higher-level nodes.
void GrantShapedProfessions(Player* bot, uint8 level)
{
    constexpr uint32 SKILL_WOODCUTTING = 732;
    uint16 cap = uint16(std::max<uint32>(1, std::min<uint32>(450, uint32(level) * 6)));
    uint8 gathering = 50;
    uint8 fishing = 25;
    BotAI::GetProfessionLeans(bot, gathering, fishing);

    for (uint32 skillId : PROFESSION_SKILLS)
    {
        uint32 lo = 35;
        uint32 hi = 80;
        if (skillId == SKILL_HERBALISM || skillId == SKILL_MINING || skillId == SKILL_SKINNING ||
            skillId == SKILL_WOODCUTTING)
        {
            lo = 30 + gathering / 2;
            hi = std::min<uint32>(100, lo + 25);
        }
        else if (skillId == SKILL_FISHING)
        {
            lo = 20 + fishing / 2;
            hi = std::min<uint32>(100, lo + 25);
        }
        uint16 value = uint16(std::max<uint32>(1, uint32(cap) * RandomInt(lo, hi) / 100));
        bot->SetSkill(uint16(skillId), 1, value, cap);
    }
}

// Post-login setup for a freshly cloned population bot. Runs once, from BotMgr::SpawnBot's onReady
// callback -- it needs a real logged-in Player for learnSpell/SetSkill/StoreNewItemInBestSlots. The
// order matters: bags before anything that goes in them, abilities from the book before talents
// (a pick can build on an ability), skills before recipes (recipes are gated on skill).
void ApplyFreshBotSetup(Player* bot, uint8 level)
{
    if (!bot)
        return;

    // Sanity check: bot level should never exceed server max level
    uint8 serverMaxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    ASSERT(level <= serverMaxLevel && level >= 1);

    // Normally already true, since the level is written into the cloned row; kept for a bot whose
    // row predates that or was edited by hand.
    if (level != bot->GetLevel())
        bot->GiveLevel(level);

    for (int i = 0; i < 4; ++i)
        bot->StoreNewItemInBestSlots(BAG_ITEM_ID, 1);

    BotProgression::LearnAbilitiesFromBook(bot);

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

    GrantShapedProfessions(bot, level);
    BotProgression::LearnRecipesFromBook(bot);
    BotProgression::GrantCompanions(bot);
    BotProgression::GrantMounts(bot);
    BotTaxi::GrantNodesForLevel(bot);
    BotProgression::ProvisionFood(bot, 20);

    GearUpFreshBotForLevel(bot, level);

    // After gear, not before: a fishing pole handed out first went straight into the empty main
    // hand, and the gear-up then destroyed it while equipping a real weapon -- confirmed live on the
    // first test batch. With the weapons already equipped, every tool lands in the bags.
    BotProgression::GrantProfessionTools(bot);

    // Relocate fresh bot to a level-appropriate zone hub instead of leaving it at the cloned template's coordinates
    BotZoneProgression::RelocateBot(bot, true /*force initial relocation*/);

    LOG_INFO("module.coa-playerbots", "BotMgr: applied fresh-bot setup (level {}, spec {}, {} spells) to '{}'.",
        level, bot->GetPlayerSetting("core.ascension_active_spec", 0).value, bot->GetSpellMap().size(),
        bot->GetName());
}

struct LevelBracket
{
    uint8 low;
    uint8 high;
    uint32 weight;
};

// CoaBots.LeveledSpawn.Brackets, e.g. "1-20:40,21-60:33,61-79:20,80-80:7" -- level ranges with
// relative weights. Malformed entries are skipped; an unusable setting falls back to the default.
std::vector<LevelBracket> LevelBrackets()
{
    static std::string const defaultSpec = "1-20:40,21-60:33,61-79:20,80-80:7";
    auto parse = [](std::string const& spec)
    {
        std::vector<LevelBracket> brackets;
        std::stringstream stream(spec);
        std::string token;
        while (std::getline(stream, token, ','))
        {
            size_t dash = token.find('-');
            size_t colon = token.find(':');
            if (dash == std::string::npos || colon == std::string::npos || colon < dash)
                continue;
            try
            {
                uint32 low = std::stoul(token.substr(0, dash));
                uint32 high = std::stoul(token.substr(dash + 1, colon - dash - 1));
                uint32 weight = std::stoul(token.substr(colon + 1));
                if (low >= 1 && low <= high && high <= 255 && weight)
                    brackets.push_back({ uint8(low), uint8(high), weight });
            }
            catch (std::exception const&)
            {
            }
        }
        return brackets;
    };

    std::vector<LevelBracket> brackets = parse(sConfigMgr->GetOption<std::string>("CoaBots.LeveledSpawn.Brackets",
        defaultSpec));
    return brackets.empty() ? parse(defaultSpec) : brackets;
}

// Weighted level roll for SpawnLeveledBots, driven by CoaBots.LeveledSpawn.Brackets and never above
// the server's max player level.
uint8 RollWeightedLevel(uint8 serverMaxLevel = 80)
{
    std::vector<LevelBracket> brackets = LevelBrackets();
    uint32 total = 0;
    for (LevelBracket const& bracket : brackets)
        total += bracket.weight;

    uint32 roll = RandomInt(1, total);
    for (LevelBracket const& bracket : brackets)
    {
        if (roll <= bracket.weight)
            return std::min<uint8>(uint8(RandomInt(bracket.low, bracket.high)), serverMaxLevel);
        roll -= bracket.weight;
    }
    return 1;
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
                "BotMgr: queued {} leveled bot(s) (levels per CoaBots.LeveledSpawn.Brackets) for gradual spawn "
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

    uint8 serverMaxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    uint32 thisBatch = std::min(g_pendingLeveledBotCount, RandomSpawnBatchSize());
    for (uint32 i = 0; i < thisBatch; ++i)
    {
        uint8 level = RollWeightedLevel(serverMaxLevel);
        ObjectGuid::LowType newGuid = 0;
        constexpr uint32 MAX_RACE_ATTEMPTS = 10;
        for (uint32 attempt = 0; attempt < MAX_RACE_ATTEMPTS && !newGuid; ++attempt)
        {
            uint8 race = VALID_RACES[RandomInt(0, VALID_RACES.size() - 1)];
            newGuid = CreateBotClone(race, nullptr, level);
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
