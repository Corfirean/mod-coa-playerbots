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
    }

    float DamageTracker::SampleIncomingDps(Unit* unit)
    {
        if (!unit)
            return 0.0f;

        ObjectGuid guid = unit->GetGUID();
        uint32 currentHealth = unit->GetHealth();
        uint32 now = getMSTime();

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
