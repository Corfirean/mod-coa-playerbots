#ifndef COA_TRAINING_DATA_H
#define COA_TRAINING_DATA_H

#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace CoaTraining
{
class Json
{
public:
    using Object = std::map<std::string, Json>;
    using Array = std::vector<Json>;
    Json() = default;
    Json(char const* value) : data(std::string(value)) { }
    Json(std::string value) : data(std::move(value)) { }
    Json(bool value) : data(value) { }
    template<class T, std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>, int> = 0>
    Json(T value) : data(double(value)) { }
    Json(Object value) : data(std::move(value)) { }
    Json(Array value) : data(std::move(value)) { }
    Json& operator[](std::string const& key)
    {
        if (!std::holds_alternative<Object>(data)) data = Object{};
        return std::get<Object>(data)[key];
    }
    void Push(Json value)
    {
        if (!std::holds_alternative<Array>(data)) data = Array{};
        std::get<Array>(data).push_back(std::move(value));
    }
    std::string Dump() const
    {
        std::ostringstream out;
        out.precision(15);
        std::visit([&](auto const& value)
        {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) out << "null";
            else if constexpr (std::is_same_v<T, bool>) out << (value ? "true" : "false");
            else if constexpr (std::is_same_v<T, double>)
            {
                if (std::isfinite(value)) out << value; else out << "null";
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                out << '"';
                char const* hex = "0123456789abcdef";
                for (unsigned char c : value)
                {
                    if (c == '"' || c == '\\') out << '\\' << c;
                    else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
                    else out << c;
                }
                out << '"';
            }
            else
            {
                constexpr bool object = std::is_same_v<T, Object>;
                out << (object ? '{' : '[');
                bool first = true;
                for (auto const& item : value)
                {
                    if (!first) out << ',';
                    first = false;
                    if constexpr (object) out << Json(item.first).Dump() << ':' << item.second.Dump();
                    else out << item.Dump();
                }
                out << (object ? '}' : ']');
            }
        }, data);
        return out.str();
    }
private:
    std::variant<std::monostate, bool, double, std::string, Object, Array> data;
};

struct Mechanic
{
    uint32_t timeMs = 0;
    std::string kind;
    uint32_t amount = 0;
    uint32_t targetSlot = 0;
    bool operator==(Mechanic const&) const = default;
};

struct AuraStats
{
    bool active = false;
    uint32_t since = 0, stacks = 0, applies = 0, refreshes = 0, removes = 0;
    uint64_t uptimeMs = 0;
    void Apply(uint32_t now, uint32_t count)
    {
        if (active) ++refreshes;
        else { active = true; since = now; ++applies; }
        stacks = count;
    }
    void Remove(uint32_t now)
    {
        if (active) uptimeMs += now - since;
        active = false; ++removes;
    }
    uint64_t Uptime(uint32_t now) const { return uptimeMs + (active ? now - since : 0); }
};

struct Scenario
{
    std::string name;
    uint32_t durationMs = 180000;
    uint32_t melee = 0;
    uint32_t tankbuster = 0;
    uint32_t partyDamage = 0;
    uint32_t targetedDamage = 0;
    uint32_t hazardDamage = 0;
    uint32_t threatPerSecond = 0;
    uint32_t enemyCount = 1;
    uint32_t targetHealthPct = 100; // EXECUTE: the dummy starts below the finishing-blow thresholds (e.g. 35% for Electrocute)
    uint32_t dispelDebuffSpell = 0;
    bool resurrectionDiagnostic = false;
    bool resurrectionEndCombat = false;
    bool group = false;
    bool movement = false;
    std::vector<Mechanic> Timeline(uint32_t seed, uint32_t members) const;
    static bool Find(std::string const& name, Scenario& result);
};
}
#endif
