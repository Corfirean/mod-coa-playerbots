/*
 * mod-coa-playerbots
 *
 * Generic "is this bot actually getting anywhere in this fight" watchdog -- the combat-tick
 * counterpart to BotNavProgress.h's movement-specific one. Pure (a snapshot and a clock in, a
 * decision out) so it is unit-tested standalone, same shape as NavProgress.
 *
 * Unlike NavProgress this never tries to recover anything -- it only flags. A bot doing nothing
 * for 9s straight (no HP change on itself or its target, no meaningful position change, the same
 * cast/target as last time it was sampled) is a genuine AI deadlock worth a loud log line and
 * nothing else; auto-recovering it here would just mask whatever the real bug is instead of
 * surfacing it in the per-bot debug log the 21-bot class-fleet exercise reads back afterward.
 */

#ifndef COA_PLAYERBOTS_BOT_STUCK_DETECTOR_H
#define COA_PLAYERBOTS_BOT_STUCK_DETECTOR_H

#include "Define.h"
#include "ObjectGuid.h"
#include <cmath>

struct StuckDetectorRules
{
    float hpPctEpsilon = 0.5f;   // self/target HP% change that counts as progress
    float positionYards = 1.0f;  // distance moved that counts as progress
    uint32 stallMs = 9000;       // no progress for this long trips the detector
};

struct StuckDetectorSnapshot
{
    // A proxy for "is the bot trying something new" that doesn't require plumbing the actual
    // chosen ability out of DpsEngine/HealerEngine/TankEngine: changes whenever the bot starts a
    // new cast (CastGuard::CurrentSpellId) or switches target, both real signs of an attempted
    // action even before it lands.
    uint32 castSpellId = 0;
    ObjectGuid targetGuid;
    float selfHpPct = 0.0f;
    float targetHpPct = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct StuckDetector
{
    StuckDetectorSnapshot last;
    uint32 lastProgressAt = 0;
    bool hasBaseline = false;

    // Returns true the moment the stall crosses the threshold (once per stall, not every tick
    // after) -- the caller logs and moves on; nothing here resets the "AI is stuck" condition
    // itself, only the timer, so a genuinely still-stuck bot logs again after another full
    // stallMs window rather than spamming every tick.
    bool Update(StuckDetectorSnapshot const& now, uint32 nowMs, StuckDetectorRules const& rules = {})
    {
        if (!hasBaseline)
        {
            last = now;
            lastProgressAt = nowMs;
            hasBaseline = true;
            return false;
        }

        bool hpChanged = std::fabs(now.selfHpPct - last.selfHpPct) >= rules.hpPctEpsilon ||
            std::fabs(now.targetHpPct - last.targetHpPct) >= rules.hpPctEpsilon;
        float dx = now.x - last.x, dy = now.y - last.y, dz = now.z - last.z;
        bool moved = std::sqrt(dx * dx + dy * dy + dz * dz) >= rules.positionYards;
        bool triedSomethingNew = now.castSpellId != last.castSpellId || now.targetGuid != last.targetGuid;

        last = now;

        if (hpChanged || moved || triedSomethingNew)
        {
            lastProgressAt = nowMs;
            return false;
        }

        if (nowMs - lastProgressAt <= rules.stallMs)
            return false;

        lastProgressAt = nowMs;
        return true;
    }
};

#endif // COA_PLAYERBOTS_BOT_STUCK_DETECTOR_H
