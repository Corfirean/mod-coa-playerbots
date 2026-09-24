/*
 * mod-coa-playerbots
 *
 * In-memory talent build storage and allocation for custom Ascension classes.
 * Sourced from reference/ascensionsidekick-level-builds.json.
 */

#include "BotTalentBuilds.h"
#include "AscensionClassServiceBridge.h"
#include "AscensionCoATalentData.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "ClassSpecRoles.h"
#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "SpellMgr.h"
#include <algorithm>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace BotTalentBuilds
{
namespace
{
std::unordered_map<uint32, TalentBuild> s_builds;
std::mutex s_buildsMutex;
bool s_initialized = false;

uint32 MakeBuildKey(uint8 classId, uint32 specId)
{
    return (uint32(classId) << 16) | (specId & 0xFFFF);
}

AscensionCompatData::CoATalentEntry const* FindTalentEntry(uint32 entryId)
{
    auto itr = std::lower_bound(
        AscensionCompatData::CoATalentEntries.begin(),
        AscensionCompatData::CoATalentEntries.end(), entryId,
        [](AscensionCompatData::CoATalentEntry const& entry, uint32 id) {
            return entry.EntryId < id;
        });

    if (itr != AscensionCompatData::CoATalentEntries.end() && itr->EntryId == entryId)
        return &*itr;
    return nullptr;
}

uint32 ResolveTalentSpellId(uint32 entryId, uint8 rank)
{
    if (rank == 0)
        return 0;

    if (AscensionCompatData::CoATalentEntry const* entry = FindTalentEntry(entryId))
    {
        if (rank <= entry->SpellCount && rank <= 3)
            return entry->SpellIds[rank - 1];
    }
    return 0;
}

std::string FindBuildsJsonPath()
{
    std::string configPath = sConfigMgr->GetOption<std::string>("CoaBots.TalentBuildsPath", "");
    if (!configPath.empty() && std::filesystem::exists(configPath))
        return configPath;

    std::vector<std::string> candidates = {
        "C:/games/source/server/mod-coa-playerbots/reference/ascensionsidekick-level-builds.json",
        "reference/ascensionsidekick-level-builds.json",
        "../reference/ascensionsidekick-level-builds.json",
        "../../reference/ascensionsidekick-level-builds.json",
        "../mod-coa-playerbots/reference/ascensionsidekick-level-builds.json"
    };

    for (auto const& path : candidates)
    {
        if (std::filesystem::exists(path))
            return path;
    }
    return "";
}
} // anonymous namespace

void Initialize()
{
    std::lock_guard<std::mutex> lock(s_buildsMutex);
    if (s_initialized)
        return;

    std::string jsonPath = FindBuildsJsonPath();
    if (jsonPath.empty())
    {
        LOG_WARN("module.coa-playerbots", "BotTalentBuilds: could not locate ascensionsidekick-level-builds.json.");
        s_initialized = true;
        return;
    }

    try
    {
        boost::property_tree::ptree pt;
        boost::property_tree::read_json(jsonPath, pt);

        for (auto const& pair : pt)
        {
            // Key format "{classId}:{specId}"
            std::string keyStr = pair.first;
            auto colonPos = keyStr.find(':');
            if (colonPos == std::string::npos)
                continue;

            uint8 classId = uint8(std::stoul(keyStr.substr(0, colonPos)));
            uint32 specId = uint32(std::stoul(keyStr.substr(colonPos + 1)));

            TalentBuild build;
            build.classId = classId;
            build.specId = specId;
            build.clsName = pair.second.get<std::string>("cls", "");
            build.specName = pair.second.get<std::string>("spec", "");
            build.role = pair.second.get<std::string>("role", "");

            if (auto picksOpt = pair.second.get_child_optional("picks"))
            {
                for (auto const& pickPair : *picksOpt)
                {
                    TalentPick pick;
                    pick.level = pickPair.second.get<uint8>("level", 0);
                    pick.tree = pickPair.second.get<uint8>("tree", 0);
                    pick.entryId = pickPair.second.get<uint32>("entry", 0);
                    pick.rank = pickPair.second.get<uint8>("rank", 0);
                    pick.spellId = ResolveTalentSpellId(pick.entryId, pick.rank);

                    build.picks.push_back(pick);
                }
            }

            s_builds[MakeBuildKey(classId, specId)] = std::move(build);
        }

        // Picks resolve against mod-ascension-compat's talent table, which it loads from the client
        // DBCs in its own OnStartup. If that ever runs after this, every pick resolves to 0 and no bot
        // learns a single talent while this log still reads "loaded 70 builds" -- so say how many
        // resolved, and let ApplyBuildForLevel resolve lazily as a fallback.
        uint32 picks = 0;
        uint32 unresolved = 0;
        for (auto const& [key, build] : s_builds)
            for (TalentPick const& pick : build.picks)
            {
                ++picks;
                if (!pick.spellId)
                    ++unresolved;
            }

        LOG_INFO("module.coa-playerbots", "BotTalentBuilds: successfully loaded {} talent builds from '{}' ({} picks, {} unresolved).",
            s_builds.size(), jsonPath, picks, unresolved);
    }
    catch (std::exception const& ex)
    {
        LOG_ERROR("module.coa-playerbots", "BotTalentBuilds: exception parsing '{}': {}", jsonPath, ex.what());
    }

    s_initialized = true;
}

TalentBuild const* GetBuild(uint8 classId, uint32 specId)
{
    if (!s_initialized)
        Initialize();

    auto itr = s_builds.find(MakeBuildKey(classId, specId));
    if (itr != s_builds.end())
        return &itr->second;

    return nullptr;
}

void ApplyBuildForLevel(Player* bot, uint8 toLevel)
{
    if (!bot || toLevel < 10)
        return;

    uint32 specId = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
    if (!specId)
        specId = ChooseSpecForBot(bot);

    TalentBuild const* build = GetBuild(bot->getClass(), specId);
    if (!build)
        return;

    uint8 maxPickLevel = std::min<uint8>(toLevel, 60);
    uint32 pointsSpent = 0;
    uint32 pointsSkipped = 0;

    for (TalentPick const& pick : build->picks)
    {
        if (pick.level > maxPickLevel)
            break;

        AscensionCompatData::CoATalentEntry const* entry = FindTalentEntry(pick.entryId);
        if (!entry)
            continue;

        // Set through the real budget-checked path (AscensionClassServiceBridge::
        // SetTalentRank, see docs/core-patches.md's "Patch 3") instead of learnSpell()-ing
        // pick.spellId directly -- that used to bypass the class/spec talent-point budget
        // entirely and skip the in-memory active-spec bookkeeping
        // GetActiveSpecialization() depends on. A failure here (most commonly: this pick
        // costs more than this level's remaining budget) is expected for a bot below the
        // level a real player would have earned the points for, so it's skipped rather
        // than forced through.
        std::string error;
        if (AscensionClassServiceBridge::SetTalentRank(bot, *entry, pick.rank, error))
            ++pointsSpent;
        else
            ++pointsSkipped;
    }

    if (pointsSkipped)
        LOG_DEBUG("module.coa-playerbots", "BotTalentBuilds: bot '{}' skipped {} of {} build pick(s) for spec {} at level {} (budget or other limit).",
            bot->GetName(), pointsSkipped, pointsSpent + pointsSkipped, specId, toLevel);

    // Unrelated to the Ascension AE/TE point system above -- keeps the stock WotLK talent
    // UI from showing unspent points for a character that never spends them the normal way.
    uint32 totalEarned = bot->CalculateTalentsPoints();
    bot->SetFreeTalentPoints(totalEarned >= pointsSpent ? totalEarned - pointsSpent : 0);
}

uint32 ChooseSpecForBot(Player* bot)
{
    if (!bot)
        return 0;

    uint32 existingSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
    if (existingSpec)
        return existingSpec;

    uint8 classId = bot->getClass();

    // Population role weighting: target 20% Tank, 20% Healer, 60% DPS
    std::vector<Player*> onlineBots = sBotMgr->GetOnlineBots();
    uint32 tankCount = 0;
    uint32 healerCount = 0;

    for (Player* otherBot : onlineBots)
    {
        if (!otherBot)
            continue;
        BotRole role = BotAI::GetRole(otherBot->GetGUID());
        if (role == BotRole::Tank)
            ++tankCount;
        else if (role == BotRole::Healer)
            ++healerCount;
    }

    uint32 chosenSpec = 0;
    size_t total = onlineBots.size();
    float tankTargetPct = sConfigMgr->GetOption<float>("CoaBots.SpecBalance.TankTargetPct", 20.0f);
    float healerTargetPct = sConfigMgr->GetOption<float>("CoaBots.SpecBalance.HealerTargetPct", 20.0f);

    if (total > 0 && float(tankCount) * 100.0f < tankTargetPct * float(total))
        chosenSpec = BotAI::FindSpecForRole(classId, BotRole::Tank);

    if (!chosenSpec && total > 0 && float(healerCount) * 100.0f < healerTargetPct * float(total))
        chosenSpec = BotAI::FindSpecForRole(classId, BotRole::Healer);

    if (!chosenSpec)
        chosenSpec = BotAI::FindSpecForRole(classId, BotRole::Dps);

    if (!chosenSpec)
        chosenSpec = BotAI::FindSpecForRole(classId, BotRole::Tank);

    if (!chosenSpec)
        chosenSpec = BotAI::FindSpecForRole(classId, BotRole::Healer);

    if (chosenSpec)
    {
        // Through the real AscensionClassServiceBridge::SwitchSpecialization() (see
        // docs/core-patches.md's "Patch 3"), not a raw PlayerSetting write -- a raw write
        // never touched the in-memory active-spec map GetActiveSpecialization() reads, so
        // every spec-gated automatic talent grant silently never fired for a freshly
        // spec'd bot even though the PlayerSetting itself looked correct.
        if (!AscensionClassServiceBridge::SwitchSpecialization(bot, chosenSpec))
        {
            LOG_ERROR("module.coa-playerbots", "BotTalentBuilds: SwitchSpecialization rejected spec {} for bot '{}' (class {}).",
                chosenSpec, bot->GetName(), uint32(classId));
            return 0;
        }

        BotRole role = BotAI::GetRoleForClassSpec(classId, chosenSpec);
        BotAI::SetRole(bot->GetGUID(), role);

        LOG_INFO("module.coa-playerbots", "BotTalentBuilds: assigned spec {} ('{}') to bot '{}' (class {}, role {}).",
            chosenSpec, BotAI::GetSpecName(classId, chosenSpec) ? BotAI::GetSpecName(classId, chosenSpec) : "unknown",
            bot->GetName(), uint32(classId), uint32(role));
    }

    return chosenSpec;
}

} // namespace BotTalentBuilds
