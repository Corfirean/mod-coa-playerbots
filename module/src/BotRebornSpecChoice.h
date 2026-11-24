/*
 * mod-coa-playerbots
 *
 * Choosing a Warcraft Reborn bot's specialization. Pure logic (no core types) so a test can run it by the thousand.
 *
 * The first version took (guid % number of specs) + 1, which gave a priest two healing specs out of three and a paladin two
 * of three that are not damage, so the population came out around a third healers and a quarter tanks. This follows the
 * same population targets as the CoA classes (CoaBots.SpecBalance.TankTargetPct / HealerTargetPct): while the tanks are
 * under their share, a class that can tank gets its tank spec; then the same for healers; otherwise it picks one of its
 * damage specs, spread by a seed so a class does not always land on its first one.
 */
#ifndef COA_PLAYERBOTS_BOT_REBORN_SPEC_CHOICE_H
#define COA_PLAYERBOTS_BOT_REBORN_SPEC_CHOICE_H

#include <cstdint>
#include <vector>

namespace BotRebornSpec
{
    enum class Kind : uint8_t { Damage = 0, Tank = 1, Healer = 2 };

    struct Option
    {
        uint32_t spec;
        Kind kind;
        uint32_t weight = 1;   // relative chance among the options of the same kind
    };

    struct Population
    {
        uint32_t total = 0;
        uint32_t tanks = 0;
        uint32_t healers = 0;
    };

    inline Option const* PickOfKind(std::vector<Option> const& options, Kind kind, uint64_t seed)
    {
        uint64_t sum = 0;
        for (Option const& option : options)
            if (option.kind == kind)
                sum += option.weight ? option.weight : 1;
        if (!sum)
            return nullptr;
        // A multiplicative hash so consecutive guids do not walk the options in order.
        uint64_t roll = (seed * 0x9E3779B97F4A7C15ull >> 24) % sum;
        for (Option const& option : options)
        {
            if (option.kind != kind)
                continue;
            uint64_t weight = option.weight ? option.weight : 1;
            if (roll < weight)
                return &option;
            roll -= weight;
        }
        return nullptr;
    }

    // Returns the chosen spec number, or 0 when the class has no option at all.
    inline uint32_t Choose(std::vector<Option> const& options, Population const& population, float tankTargetPct,
        float healerTargetPct, uint64_t seed)
    {
        Option const* chosen = nullptr;
        // A shortfall fills most, not all, of the time: if every bot of a class that can heal healed while the healers were short, a
        // class like the priest would never be seen as Shadow, and the damage specs of the hybrids would sit unused.
        bool const fill = ((seed * 0xD1B54A32D192ED03ull) >> 33) % 100 < 85;
        if (fill && population.total > 0 && float(population.tanks) * 100.0f < tankTargetPct * float(population.total))
            chosen = PickOfKind(options, Kind::Tank, seed);
        if (!chosen && fill && population.total > 0 && float(population.healers) * 100.0f < healerTargetPct * float(population.total))
            chosen = PickOfKind(options, Kind::Healer, seed);
        if (!chosen)
            chosen = PickOfKind(options, Kind::Damage, seed);
        if (!chosen)
            chosen = PickOfKind(options, Kind::Tank, seed);
        if (!chosen)
            chosen = PickOfKind(options, Kind::Healer, seed);
        return chosen ? chosen->spec : 0;
    }
}

#endif // COA_PLAYERBOTS_BOT_REBORN_SPEC_CHOICE_H
