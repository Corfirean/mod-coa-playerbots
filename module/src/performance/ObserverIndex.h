#ifndef COA_BOTS_OBSERVER_INDEX_H
#define COA_BOTS_OBSERVER_INDEX_H

#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace CoaBotScheduling
{
    class ObserverIndex
    {
    public:
        void Clear() { _cells.clear(); }

        void Add(std::uint32_t map, std::uint32_t instance, std::uint32_t phase, float x, float y, float z)
        {
            _cells[{map, instance, Cell(x), Cell(y)}].push_back({phase, x, y, z});
        }

        std::pair<bool, bool> Query(std::uint32_t map, std::uint32_t instance, std::uint32_t phase,
            float x, float y, float z) const
        {
            bool leave = false;
            if (_cells.empty()) return {false, false};
            auto cx = Cell(x), cy = Cell(y);
            for (auto ix = cx - 1; ix <= cx + 1; ++ix)
                for (auto iy = cy - 1; iy <= cy + 1; ++iy)
                {
                    auto it = _cells.find({map, instance, ix, iy});
                    if (it == _cells.end()) continue;
                    for (auto const& observer : it->second)
                    {
                        if (!(phase & observer.phase)) continue;
                        float dx = observer.x - x, dy = observer.y - y, dz = observer.z - z;
                        float distance = dx * dx + dy * dy + dz * dz;
                        if (distance <= 250.0f * 250.0f) return {true, true};
                        leave |= distance <= 350.0f * 350.0f;
                    }
                }
            return {false, leave};
        }

    private:
        static std::int32_t Cell(float coordinate) { return std::int32_t(std::floor(coordinate / 350.0f)); }
        struct Observer { std::uint32_t phase; float x, y, z; };
        using Key = std::tuple<std::uint32_t, std::uint32_t, std::int32_t, std::int32_t>;
        std::map<Key, std::vector<Observer>> _cells;
    };
}
#endif
