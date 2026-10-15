#ifndef COA_TRAINING_SESSION_H
#define COA_TRAINING_SESSION_H
#include "training/TrainingArenaManager.h"
#include "Position.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

class Creature;

namespace CoaTraining
{
struct Member
{
    ObjectGuid guid;
    WorldLocation home;
    uint32 phase = 1;
    uint32 spec = 0;
    uint32 classId = 0;
    std::string role;
    Json gear;
    float lowestHp = 100;
    uint64 criticalMs = 0;
    uint32 deathMs = 0;
    bool dead = false;
    uint32 level = 0;
    Json spellbook{Json::Array{}};
    Json activeSpellbook{Json::Array{}};
    Json weaponSkills{Json::Array{}};
};
struct AbilityStats
{
    uint64 available = 0, selected = 0, rejected = 0, attempted = 0, accepted = 0, failed = 0;
    std::map<std::string, uint64> reasons;
};
struct AuraKey
{
    uint32 spell = 0;
    ObjectGuid caster, target;
    bool operator<(AuraKey const& rhs) const
    {
        return std::tie(spell, caster, target) < std::tie(rhs.spell, rhs.caster, rhs.target);
    }
};
struct Session
{
    std::string id, batchId;
    uint32 arenaId = 1;
    uint32 phaseMask = 0x40000000;
    ObjectGuid candidate, boss;
    std::vector<ObjectGuid> enemies;
    std::vector<Member> members;
    Scenario scenario;
    uint32 seed = 1001, timeMs = 0, sampleMs = 0, preparationMs = 0;
    uint32 ownedEntitySampleMs = 0;
    uint32 runsRemaining = 1;
    bool running = false, startRequested = false, finished = false;
    bool createdGroup = false;
    std::set<uint32> hazardSpells;
    uint32 interruptStartMs = 0, interruptExpectedEndMs = 0;
    bool watchingInterruptCast = false;
    std::vector<uint32> requiredBuffs, requiredDebuffs;
    std::map<uint32, uint64> buffCoverageMs, debuffUptimeMs;
    std::vector<double> batchDps;
    Json batchEpisodes{Json::Array{}};
    size_t nextMechanic = 0;
    std::vector<Mechanic> timeline;
    uint64 damage = 0, healing = 0, damageTaken = 0, avoidableDamage = 0, avoidableHits = 0;
    uint64 castingMs = 0, movingMs = 0, idleMs = 0, aggroMs = 0, oomMs = 0;
    uint32 aggroLosses = 0;
    bool lastAggro = true;
    uint32 lostAt = 0;
    Json recoveries{Json::Array{}};
    float hazardX = 0, hazardY = 0, hazardZ = 0;
    bool hazardActive = false, avoidableEvent = false;
    std::map<uint32, AbilityStats> abilities;
    std::map<AuraKey, AuraStats> auras;
    std::map<std::string, uint64> cappedMs, starvedMs;
    Json decision;
    bool decisionActive = false;
    std::filesystem::path directory;
    std::ofstream events, decisions, samples;
    void Event(Json row);
    void ResetTelemetry();
    void Sample(uint32 diff);
    void WriteReport(char const* reason);
};
Json Gear(Player* bot);
void Reset(Player* bot);
void InstallBossAI(Creature* boss);
std::string Role(Player* bot);
}
#endif
