#include "training/TrainingData.h"
#include <algorithm>

namespace CoaTraining
{
bool Scenario::Find(std::string const& name, Scenario& s)
{
    s = Scenario{};
    s.name = name;
    if (name == "ST_STATIC" || name == "BURST") return true;
    if (name == "RESOURCE_ENDURANCE") { s.durationMs = 600000; return true; }
    if (name == "ST_MOVEMENT") { s.movement = true; return true; }
    if (name == "CLEAVE") { s.enemyCount = 2; return true; }
    if (name == "EXECUTE") { s.targetHealthPct = 30; return true; }
    if (name == "AOE") { s.enemyCount = 5; return true; }
    if (name == "SURVIVAL") { s.melee = 400; s.tankbuster = 1800; return true; }
    if (name == "GROUND_HAZARDS") { s.hazardDamage = 1800; return true; }
    if (name == "RESURRECT_AFTER_PULL")
    {
        s.group = true; s.resurrectionDiagnostic = true; s.resurrectionEndCombat = true; return true;
    }
    if (name == "RESURRECT_AREA")
    {
        s.group = true; s.melee = 150; s.resurrectionDiagnostic = true; return true;
    }
    if (name == "DISPEL_MAGIC")
    {
        // Native instant magic DoT on a helper: no forced aura or cleanse bypass.
        s.group = true; s.melee = 150; s.dispelDebuffSpell = 589; return true;
    }
    if (name == "GROUP_STRESS")
    {
        s.group = true; s.melee = 400; s.tankbuster = 1800;
        s.partyDamage = 900; s.targetedDamage = 1400; return true;
    }
    if (name == "THREAT_STRESS") { s.group = true; s.melee = 200; s.threatPerSecond = 1000; return true; }
    if (name == "HEAL_RAMP") { s.group = true; s.rampStep = 100; return true; }
    if (name == "HEAL_POOL") { s.group = true; s.poolRefill = true; return true; }
    if (name == "SUPPORT_GROUP") { s.group = true; s.melee = 150; s.partyDamage = 300; return true; }
    return false;
}

std::vector<Mechanic> Scenario::Timeline(uint32_t seed, uint32_t members) const
{
    std::vector<Mechanic> result;
    uint32_t rng = seed;
    auto append = [&](uint32_t start, uint32_t interval, char const* kind, uint32_t amount, bool random)
    {
        for (uint32_t t = start; t < durationMs; t += interval)
        {
            rng = rng * 1664525u + 1013904223u;
            result.push_back({t, kind, amount, random && members ? rng % members : 0});
        }
    };
    result.push_back({0, "pull", 0, 0});
    append(35000, 45000, "interruptible_cast", 0, false);
    if (melee) append(2000, 2000, "melee", melee, false);
    if (tankbuster) append(7000, 15000, "tankbuster", tankbuster, false);
    if (partyDamage) append(14000, 20000, "party_aoe", partyDamage, false);
    if (targetedDamage) append(25000, 17000, "targeted_damage", targetedDamage, true);
    if (hazardDamage)
    {
        append(10000, 20000, "hazard_warning", 0, true);
        append(13000, 20000, "hazard_explosion", hazardDamage, false);
    }
    if (movement) append(10000, 15000, "boss_move", 0, false);
    if (threatPerSecond) append(1000, 1000, "threat_pressure", threatPerSecond, false);
    if (dispelDebuffSpell) append(3000, 15000, "dispel_debuff", dispelDebuffSpell, false);
    if (rampStep)
        for (uint32_t t = 2000, k = 1; t < durationMs; t += rampIntervalMs, ++k) result.push_back({t, "ramp_hit", rampStep * k, 0});
    if (poolRefill) append(2000, 1000, "pool_refill", 50, false);
    if (resurrectionDiagnostic) append(3000, durationMs, "resurrection_death", 0, false);
    std::stable_sort(result.begin(), result.end(), [](auto const& a, auto const& b) { return a.timeMs < b.timeMs; });
    return result;
}
}
