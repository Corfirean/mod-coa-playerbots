#include "training/TrainingSession.h"
#include <stdexcept>
#include <algorithm>
#include <numeric>

namespace CoaTraining
{
void Session::WriteReport(char const* reason)
{
    auto write = [&](char const* filename, Json const& value)
    {
        std::ofstream file(directory / filename, std::ios::binary);
        file << value.Dump() << '\n';
        if (!file) throw std::runtime_error("Training report write failed: " + (directory / filename).string());
    };
    Json summary;
    summary["schemaVersion"] = 1;
    summary["sessionId"] = id; summary["batchId"] = batchId;
    summary["phaseMask"] = phaseMask;
    summary["arenaId"] = arenaId; summary["scenario"] = scenario.name;
    summary["seed"] = seed; summary["durationMs"] = timeMs;
    summary["requestedDurationMs"] = scenario.durationMs;
    summary["completionReason"] = reason;
    summary["botGuid"] = uint64(candidate.GetCounter());
    for (auto const& m : members)
        if (m.guid == candidate)
        {
            summary["classId"] = m.classId; summary["specId"] = m.spec; summary["role"] = m.role;
        }
    double seconds = timeMs / 1000.0;
    summary["combat"]["damage"] = damage;
    summary["combat"]["damageSemantics"] = "post-mitigation pre-health-application hook; capped to current HP";
    summary["combat"]["dps"] = seconds ? Json(damage / seconds) : Json();
    summary["combat"]["effectiveHealing"] = healing;
    summary["combat"]["hps"] = seconds ? Json(healing / seconds) : Json();
    summary["combat"]["damageTaken"] = damageTaken;
    summary["combat"]["castingMs"] = castingMs;
    summary["combat"]["movingMs"] = movingMs;
    summary["combat"]["noTargetIdleMs"] = idleMs;
    summary["combat"]["gcdUtilization"] = Json();
    summary["mechanics"]["avoidableHits"] = avoidableHits;
    summary["mechanics"]["avoidableDamage"] = avoidableDamage;
    summary["mechanics"]["interruptCasts"] = interruptCasts;
    summary["mechanics"]["interruptsStopped"] = interruptsStopped;
    summary["mechanics"]["interruptKicksAttempted"] = kickAttempts;
    summary["mechanics"]["interruptKicksAccepted"] = kicksAccepted;
    summary["mechanics"]["interruptsByKick"] = interruptsByKick;
    summary["threat"]["aggroMs"] = aggroMs;
    summary["threat"]["aggroLosses"] = aggroLosses;
    summary["threat"]["recoveryTimesMs"] = recoveries;
    summary["healing"]["oomMs"] = oomMs;
    Json roster{Json::Array{}};
    uint32 deaths = 0;
    uint64 criticalMs = 0;
    for (auto const& m : members)
    {
        Json row;
        row["guid"] = uint64(m.guid.GetCounter()); row["classId"] = m.classId;
        row["specId"] = m.spec; row["role"] = m.role; row["gear"] = m.gear;
        row["level"] = m.level; row["spellbook"] = m.spellbook; row["weaponSkills"] = m.weaponSkills;
        row["activeSpellbook"] = m.activeSpellbook;
        row["lowestHpPct"] = m.lowestHp; row["dead"] = m.dead;
        row["deathTimeMs"] = m.dead ? Json(m.deathMs) : Json();
        row["criticalPersonSeconds"] = m.criticalMs / 1000.0;
        criticalMs += m.criticalMs; deaths += m.dead;
        roster.Push(row);
    }
    summary["combat"]["deaths"] = deaths;
    summary["healing"]["criticalPersonSeconds"] = criticalMs / 1000.0;
    summary["normalization"]["roster"] = roster;
    Json resourceSummary;
    for (auto const& [key, ms] : cappedMs) resourceSummary[key]["timeCappedMs"] = ms;
    for (auto const& [key, ms] : starvedMs) resourceSummary[key]["timeAtZeroMs"] = ms;
    summary["resources"] = resourceSummary;
    Json abilityRows;
    for (auto const& [spell, a] : abilities)
    {
        Json row;
        row["availableDecisionWindows"] = a.available;
        row["selected"] = a.selected; row["rejected"] = a.rejected;
        row["castsAttempted"] = a.attempted; row["castsAccepted"] = a.accepted;
        row["castsFailed"] = a.failed;
        for (auto const& [r, n] : a.reasons) row["reasons"][r] = n;
        abilityRows[std::to_string(spell)] = row;
    }
    Json auraRows{Json::Array{}};
    for (auto const& [key, a] : auras)
    {
        Json row;
        row["spellId"] = key.spell; row["casterGuid"] = uint64(key.caster.GetCounter());
        row["targetGuid"] = uint64(key.target.GetCounter());
        uint64 uptime = a.Uptime(timeMs);
        row["uptimeMs"] = uptime; row["uptimePct"] = timeMs ? Json(100.0 * uptime / timeMs) : Json();
        row["applies"] = a.applies; row["refreshes"] = a.refreshes; row["removes"] = a.removes;
        auraRows.Push(row);
    }
    summary["support"]["personalDps"] = seconds ? Json(damage / seconds) : Json();
    summary["support"]["auraReport"] = "auras.json";
    summary["support"]["requiredEffectsConfigured"] = !requiredBuffs.empty() || !requiredDebuffs.empty();
    Json supportBuffs, supportDebuffs;
    for (uint32 spell : requiredBuffs)
    {
        uint64 covered = buffCoverageMs[spell];
        supportBuffs[std::to_string(spell)]["coveredPersonMs"] = covered;
        supportBuffs[std::to_string(spell)]["partyCoveragePct"] = timeMs ?
            Json(100.0 * covered / (uint64(timeMs) * members.size())) : Json();
    }
    for (uint32 spell : requiredDebuffs)
        supportDebuffs[std::to_string(spell)]["uptimePct"] = timeMs ?
            Json(100.0 * debuffUptimeMs[spell] / timeMs) : Json();
    summary["support"]["requiredBuffs"] = supportBuffs;
    summary["support"]["requiredDebuffs"] = supportDebuffs;
    summary["reproducibility"]["mechanicScheduleDeterministic"] = true;
    summary["reproducibility"]["combatRngSeeded"] = false;
    write("summary.json", summary);
    write("abilities.json", abilityRows); write("resources.json", resourceSummary);
    write("auras.json", auraRows);
    write("threat.json", summary["threat"]);
    write("healing.json", summary["healing"]);
    write("support.json", summary["support"]);
    Json timelineRows{Json::Array{}};
    for (auto const& m : timeline)
    {
        Json row; row["timeMs"] = m.timeMs; row["kind"] = m.kind;
        row["amount"] = m.amount; row["targetSlot"] = m.targetSlot; timelineRows.Push(row);
    }
    write("mechanics.json", timelineRows);
    if (std::string(reason) == "duration" || std::string(reason) == "wipe")
    {
        batchDps.push_back(seconds ? damage / seconds : 0);
        Json episode; episode["sessionId"] = id; episode["seed"] = seed;
        episode["directory"] = directory.string(); episode["deaths"] = deaths;
        episode["avoidableHits"] = avoidableHits;
        batchEpisodes.Push(episode);
        auto sorted = batchDps;
        std::sort(sorted.begin(), sorted.end());
        double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
        double variance = 0;
        for (double value : sorted) variance += (value - mean) * (value - mean);
        Json batch;
        batch["batchId"] = batchId; batch["completedRuns"] = sorted.size();
        batch["episodes"] = batchEpisodes;
        batch["dps"]["mean"] = mean;
        batch["dps"]["median"] = (sorted[(sorted.size() - 1) / 2] + sorted[sorted.size() / 2]) / 2;
        batch["dps"]["min"] = sorted.front(); batch["dps"]["max"] = sorted.back();
        batch["dps"]["stddev"] = std::sqrt(variance / sorted.size());
        std::ofstream file(directory.parent_path() / (batchId + "-aggregate.json"), std::ios::binary);
        file << batch.Dump() << '\n';
        if (!file) throw std::runtime_error("Training batch report write failed");
    }
    events.flush(); decisions.flush(); samples.flush();
    if (!events || !decisions || !samples) throw std::runtime_error("Training telemetry stream write failed");
}
}
