// Warcraft Reborn bot data and decisions: the generated talent builds, the extra trainer abilities, the pet talent plan, the stat
// models and the spec chooser. Standalone (no core): the tables are the very files the module compiles in.
//
//   cl /std:c++20 /EHsc /I module/src module/tests/reborn_data_tests.cpp
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;

struct RebornPick { uint32 spell; uint32 level; };
struct RebornBuild { uint8 classId; uint8 spec; uint16 first; uint16 count; };
struct RebornExtraSpell { uint8 classId; uint8 level; uint32 spell; };
struct RebornPetPick { uint32 talent; uint32 rankIndex; };

#include "RebornTalentBuilds.inc"
#include "RebornExtraSpells.inc"
#include "RebornPetTalents.inc"
#include "BotRebornSpecChoice.h"
#include "BotSpecStats.h"

static int failures = 0;
#define CHECK(condition, ...) do { if (!(condition)) { ++failures; std::printf("FAIL %s:%d  %s  ", __FILE__, __LINE__, #condition); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// The stock classes and their spec kinds, as ClassSpecRoles.cpp lists them (tools/reborn_talent_planner/validate.py keeps the two equal).
using BotRebornSpec::Kind;
struct StockSpec { uint8 classId; uint32 spec; Kind kind; };
static const StockSpec STOCK[] = {
    { 1, 1, Kind::Damage }, { 1, 2, Kind::Damage }, { 1, 3, Kind::Tank },
    { 2, 1, Kind::Healer }, { 2, 2, Kind::Tank }, { 2, 3, Kind::Damage },
    { 3, 1, Kind::Damage }, { 3, 2, Kind::Damage }, { 3, 3, Kind::Damage },
    { 4, 1, Kind::Damage }, { 4, 2, Kind::Damage }, { 4, 3, Kind::Damage },
    { 5, 1, Kind::Healer }, { 5, 2, Kind::Healer }, { 5, 3, Kind::Damage },
    { 7, 1, Kind::Damage }, { 7, 2, Kind::Damage }, { 7, 3, Kind::Healer }, { 7, 4, Kind::Tank },
    { 8, 1, Kind::Damage }, { 8, 2, Kind::Damage }, { 8, 3, Kind::Damage },
    { 9, 1, Kind::Damage }, { 9, 2, Kind::Damage }, { 9, 3, Kind::Damage }, { 9, 4, Kind::Tank },
    { 11, 1, Kind::Damage }, { 11, 2, Kind::Damage }, { 11, 3, Kind::Healer }, { 11, 4, Kind::Tank },
};

static void TestTalentBuilds()
{
    std::set<std::pair<int, int>> seen;
    for (RebornBuild const& build : REBORN_BUILDS)
    {
        CHECK(seen.insert(std::make_pair(int(build.classId), int(build.spec))).second, "build (%d,%d) listed twice", build.classId, build.spec);
        bool known = false;
        for (StockSpec const& s : STOCK)
            known = known || (s.classId == build.classId && s.spec == build.spec);
        CHECK(known, "build (%d,%d) is no stock spec", build.classId, build.spec);
        // 51 points: level 10 pays the first, every level after it one more.
        CHECK(build.count == 51, "build (%d,%d) has %d points", build.classId, build.spec, build.count);
        uint32 previous = 0;
        for (uint32 i = 0; i < build.count; ++i)
        {
            RebornPick const& pick = REBORN_PICKS[build.first + i];
            CHECK(pick.spell != 0, "build (%d,%d) pick %u has no spell", build.classId, build.spec, i);
            CHECK(pick.level == 10 + i, "build (%d,%d) pick %u is paid at level %u, expected %u", build.classId, build.spec, i, pick.level, 10 + i);
            CHECK(pick.level >= previous, "levels go backwards");
            previous = pick.level;
        }
    }
    for (StockSpec const& s : STOCK)
        CHECK(seen.count(std::make_pair(int(s.classId), int(s.spec))), "stock spec (%d,%d) has no talent build", s.classId, int(s.spec));
    CHECK(seen.size() == std::size(STOCK), "%zu builds for %zu specs", seen.size(), std::size(STOCK));
}

static void TestExtraSpells()
{
    std::set<std::pair<int, uint32>> seen;
    for (RebornExtraSpell const& entry : REBORN_EXTRA_SPELLS)
    {
        CHECK(entry.classId >= 1 && entry.classId <= 11 && entry.classId != 6 && entry.classId != 10, "class %d", entry.classId);
        CHECK(entry.level >= 1 && entry.level <= 60, "level %d", entry.level);
        CHECK(entry.spell != 0, "spell 0");
        CHECK(seen.insert(std::make_pair(int(entry.classId), entry.spell)).second, "spell %u listed twice for class %d", entry.spell, entry.classId);
    }
    // The classes whose pets, poisons and stances depend on these.
    auto has = [&](int cls, uint32 spell) { return seen.count(std::make_pair(cls, spell)) > 0; };
    CHECK(has(9, 697) && has(9, 691) && has(9, 712), "warlock demons");
    CHECK(has(3, 883) && has(3, 982) && has(3, 1515), "hunter pet spells");
    CHECK(has(4, 2060001) && has(4, 2060011), "rogue poisons");
    CHECK(has(1, 71) && has(1, 2457), "warrior stances");
}

static void TestPetTalents()
{
    CHECK(std::size(REBORN_PET_PICKS) <= 16, "more pet points than a level 80 pet earns");
    std::map<uint32, uint32> next;   // talent -> next rank index
    for (RebornPetPick const& pick : REBORN_PET_PICKS)
    {
        CHECK(pick.rankIndex < 3, "rank index %u", pick.rankIndex);
        CHECK(next[pick.talent] == pick.rankIndex, "talent %u rank %u out of order", pick.talent, pick.rankIndex);
        next[pick.talent] = pick.rankIndex + 1;
    }
}

static void TestStatModels()
{
    std::set<std::pair<int, int>> keys;
    for (BotAI::SpecStatModel const& model : BotAI::SpecStatModels)
        CHECK(keys.insert(std::make_pair(int(model.classId), int(model.specId))).second, "stat model (%d,%d) listed twice", model.classId, int(model.specId));
    for (StockSpec const& s : STOCK)
    {
        BotAI::SpecStatModel const* model = BotAI::FindSpecStatModel(s.classId, s.spec);
        CHECK(model != nullptr, "stock spec (%d,%d) has no stat model", s.classId, int(s.spec));
        if (!model)
            continue;
        CHECK(model->primary >= 3 && model->primary <= 6, "primary stat %d", model->primary);
        // A caster's primary is Intellect or Spirit, a melee spec's Strength or Agility.
        bool caster = model->usesSpellPower && !model->usesAttackPower;
        CHECK(!caster || model->primary == 5 || model->primary == 6, "spell power spec (%d,%d) scores stat %d", s.classId, int(s.spec), model->primary);
        CHECK(caster || model->primary == 3 || model->primary == 4, "attack power spec (%d,%d) scores stat %d", s.classId, int(s.spec), model->primary);
    }
}

// A population of bots takes its specs one after another, as they are made. Tanks and healers should settle near their share and every
// damage spec should be used, whatever the class mix.
static void TestSpecChoice()
{
    std::map<uint8, std::vector<BotRebornSpec::Option>> options;
    for (StockSpec const& s : STOCK)
        options[s.classId].push_back({ s.spec, s.kind, 1 });

    for (uint32 mix = 0; mix < 3; ++mix)
    {
        BotRebornSpec::Population population;
        std::map<std::pair<int, int>, uint32> used;
        // Class mixes: even; the live realm's (hunters and paladins heavy); rogues and mages only plus a few healers.
        std::vector<uint8> classes;
        static const std::vector<uint8> EVEN = { 1, 2, 3, 4, 5, 7, 8, 9, 11 };
        static const std::vector<uint8> LIVE = { 1, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 7, 7, 8, 8, 9, 9, 9, 11, 11 };
        static const std::vector<uint8> ODD = { 4, 4, 4, 8, 8, 8, 3, 3, 5, 11, 2 };
        std::vector<uint8> const& source = mix == 0 ? EVEN : mix == 1 ? LIVE : ODD;
        for (uint32 i = 0; i < 600; ++i)
            classes.push_back(source[i % source.size()]);

        std::map<uint8, Kind> kindOfSpec[12];
        for (StockSpec const& s : STOCK)
            kindOfSpec[s.classId][uint8(s.spec)] = s.kind;

        for (uint32 i = 0; i < classes.size(); ++i)
        {
            uint8 cls = classes[i];
            uint32 spec = BotRebornSpec::Choose(options[cls], population, 20.0f, 20.0f, 1000 + i * 7);
            CHECK(spec != 0, "no spec for class %d", cls);
            Kind kind = kindOfSpec[cls][uint8(spec)];
            ++population.total;
            population.tanks += kind == Kind::Tank;
            population.healers += kind == Kind::Healer;
            ++used[std::make_pair(int(cls), int(spec))];
        }
        float tankPct = 100.0f * population.tanks / population.total;
        float healerPct = 100.0f * population.healers / population.total;
        std::printf("  mix %u: %.1f%% tanks, %.1f%% healers\n", mix, tankPct, healerPct);
        // Only the odd mix (no tank classes but a paladin and a druid) may fall short of the share.
        CHECK(tankPct <= 22.0f, "mix %u: too many tanks (%.1f%%)", mix, tankPct);
        CHECK(healerPct <= 22.0f, "mix %u: too many healers (%.1f%%)", mix, healerPct);
        if (mix < 2)
        {
            CHECK(tankPct >= 17.0f, "mix %u: too few tanks (%.1f%%)", mix, tankPct);
            CHECK(healerPct >= 17.0f, "mix %u: too few healers (%.1f%%)", mix, healerPct);
            // Every damage spec of a class is used (a class does not always land on its first).
            for (StockSpec const& s : STOCK)
                if (s.kind == Kind::Damage && std::count(classes.begin(), classes.end(), s.classId))
                {
                    uint32 count = used[std::make_pair(int(s.classId), int(s.spec))];
                    CHECK(count > 0, "mix %u: spec (%d,%d) never chosen", mix, s.classId, int(s.spec));
                }
        }
    }
    // With nothing known yet the first bots take damage specs.
    BotRebornSpec::Population empty;
    CHECK(BotRebornSpec::Choose(options[5], empty, 20.0f, 20.0f, 1) == 3, "a priest into an empty population takes Shadow");
    CHECK(BotRebornSpec::Choose({}, empty, 20.0f, 20.0f, 1) == 0, "no options, no spec");
}

int main()
{
    TestTalentBuilds();
    TestExtraSpells();
    TestPetTalents();
    TestStatModels();
    TestSpecChoice();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("reborn data tests passed\n");
    return 0;
}
