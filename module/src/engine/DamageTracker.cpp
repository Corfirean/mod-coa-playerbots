/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: DamageTracker implementation
 */

#include "engine/DamageTracker.h"
#include "Timer.h"
#include "Unit.h"
#include <unordered_map>

namespace BotAI
{
    namespace
    {
        struct Sample
        {
            uint32 health = 0;
            uint32 sampleTimeMs = 0;
            float smoothedDps = 0.0f;
        };

        std::unordered_map<ObjectGuid, Sample> s_samples;

        // Two samples closer together than this aren't trustworthy for a rate calculation (the
        // health delta is mostly quantization/measurement noise, not a real trend) -- return the
        // existing smoothed estimate unchanged instead of updating from a near-zero time delta.
        constexpr uint32 MIN_SAMPLE_INTERVAL_MS = 200;

        // Stale-entry cleanup (item 21, Phase 2 fixup): only a bot's own guid is dropped
        // explicitly (see Forget, called from BotAI::Forget on despawn) -- this map also holds
        // samples for real players and other bots' allies/targets, none of which have any
        // "despawned" hook this poll-based AI can observe directly. A unit that dies, despawns,
        // or simply stops being sampled (a bot moved on and never checks it again) would
        // otherwise sit here for the rest of the server's uptime. A lazy sweep, run at most once
        // a minute and only from inside an already-happening SampleIncomingDps call (no
        // dedicated timer), drops any entry that hasn't been refreshed in a while.
        constexpr uint32 STALE_ENTRY_MS = 300000;  // 5 minutes with no fresh sample
        constexpr uint32 SWEEP_INTERVAL_MS = 60000; // don't walk the whole map more than this often
        uint32 s_nextSweepAt = 0;

        void SweepStaleEntries(uint32 now)
        {
            if (now < s_nextSweepAt)
                return;
            s_nextSweepAt = now + SWEEP_INTERVAL_MS;

            for (auto itr = s_samples.begin(); itr != s_samples.end();)
            {
                if (now - itr->second.sampleTimeMs > STALE_ENTRY_MS)
                    itr = s_samples.erase(itr);
                else
                    ++itr;
            }
        }
    }

    float DamageTracker::SampleIncomingDps(Unit* unit)
    {
        if (!unit)
            return 0.0f;

        ObjectGuid guid = unit->GetGUID();
        uint32 currentHealth = unit->GetHealth();
        uint32 now = getMSTime();
        SweepStaleEntries(now);

        auto itr = s_samples.find(guid);
        if (itr == s_samples.end())
        {
            s_samples[guid] = Sample{ currentHealth, now, 0.0f };
            return 0.0f;
        }

        Sample& sample = itr->second;
        uint32 dt = now - sample.sampleTimeMs;
        if (dt < MIN_SAMPLE_INTERVAL_MS)
            return sample.smoothedDps;

        float instantRate = 0.0f;
        if (currentHealth < sample.health)
            instantRate = float(sample.health - currentHealth) * 1000.0f / float(dt);

        sample.smoothedDps = sample.smoothedDps * 0.5f + instantRate * 0.5f;
        sample.health = currentHealth;
        sample.sampleTimeMs = now;
        return sample.smoothedDps;
    }

    void DamageTracker::Forget(ObjectGuid guid)
    {
        s_samples.erase(guid);
    }
}
