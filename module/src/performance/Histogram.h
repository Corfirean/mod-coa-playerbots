#ifndef COA_BOTS_PERFORMANCE_HISTOGRAM_H
#define COA_BOTS_PERFORMANCE_HISTOGRAM_H

#include <algorithm>
#include <array>
#include <cstdint>

namespace CoaBotPerf
{
    struct Histogram
    {
        static constexpr std::size_t LinearBins = 4096;
        static constexpr std::size_t BinCount = LinearBins + 32;
        std::array<std::uint64_t, BinCount> bins{};
        std::uint64_t count = 0, sumUs = 0, maxUs = 0;
        std::array<std::uint64_t, 4> over{};

        static std::size_t Index(std::uint64_t us)
        {
            if (us < LinearBins * 250)
                return std::size_t(us / 250);
            std::size_t index = LinearBins;
            for (us /= LinearBins * 250; us > 1 && index + 1 < BinCount; us >>= 1)
                ++index;
            return index;
        }

        static std::uint64_t Upper(std::size_t index)
        {
            return index < LinearBins ? (index + 1) * 250 : (std::uint64_t(LinearBins) * 250 << (index - LinearBins + 1));
        }

        void Observe(std::uint64_t us)
        {
            ++bins[Index(us)];
            ++count;
            sumUs += us;
            maxUs = std::max(maxUs, us);
            constexpr std::array<std::uint64_t, 4> thresholds{50000, 100000, 250000, 1000000};
            for (std::size_t i = 0; i < over.size(); ++i)
                over[i] += us > thresholds[i];
        }

        void Merge(Histogram const& other)
        {
            for (std::size_t i = 0; i < bins.size(); ++i)
                bins[i] += other.bins[i];
            for (std::size_t i = 0; i < over.size(); ++i)
                over[i] += other.over[i];
            count += other.count;
            sumUs += other.sumUs;
            maxUs = std::max(maxUs, other.maxUs);
        }

        std::uint64_t Percentile(unsigned percent) const
        {
            if (!count)
                return 0;
            std::uint64_t const rank = (count * std::min(percent, 100u) + 99) / 100;
            std::uint64_t seen = 0;
            for (std::size_t i = 0; i < bins.size(); ++i)
                if ((seen += bins[i]) >= rank)
                    return std::min(maxUs, Upper(i));
            return maxUs;
        }
    };
}
#endif
