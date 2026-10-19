"""Compile the production race selector against small config/DBC fixtures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
text = (root / "src/BotSpawnRandom.cpp").read_text(encoding="utf-8")
selector = text[text.index("std::vector<uint8> GetValidBotRaces()"):text.index("std::mt19937& Rng()")]
source = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using uint8 = uint8_t;
struct Config {
    std::unordered_map<std::string, bool> flags;
    std::unordered_map<std::string, std::string> strings;
    template<class T> T GetOption(std::string const& key, T fallback) {
        if constexpr (std::is_same_v<T, bool>) {
            auto it = flags.find(key); return it == flags.end() ? fallback : it->second;
        } else {
            auto it = strings.find(key); return it == strings.end() ? fallback : it->second;
        }
    }
} config;
Config* sConfigMgr = &config;
struct Dbc { bool LookupEntry(uint8 race) { return race >= 12 && race <= 128; } } sChrRacesStore;
''' + selector + r'''
bool contains(std::vector<uint8> const& races, uint8 race) {
    return std::find(races.begin(), races.end(), race) != races.end();
}
int main() {
    assert(GetValidBotRaces().size() == 10);
    config.flags["CoaBots.CustomRaces.Enable"] = true;
    assert(!contains(GetValidBotRaces(), 12));
    config.flags["CoACustomRaces.Enable"] = true;
    assert(!contains(GetValidBotRaces(), 12));
    config.flags["CoACustomRaces.BotsEnable"] = true;
    assert(contains(GetValidBotRaces(), 12));
    config.flags["CoaBots.CustomRaces.Enable"] = false;
    assert(!contains(GetValidBotRaces(), 12));
    config.flags["CoaBots.CustomRaces.Enable"] = true;
    config.strings["AiPlayerbot.ExcludedBotRaces"] = "33,128";
    config.strings["CoaBots.ExcludedBotRaces"] = "12,1";
    auto races = GetValidBotRaces();
    assert(!contains(races, 33) && !contains(races, 128));
    assert(!contains(races, 12) && !contains(races, 1));
    assert(contains(races, 13));
    config.strings["CoaBots.ExcludedBotRaces"] = "257,1junk,-255";
    assert(contains(GetValidBotRaces(), 1));
    config.flags["CoACustomRaces.Enable"] = false;
    config.strings["CoaBots.ExcludedBotRaces"] = "1,2,3,4,5,6,7,8,10,11";
    assert(GetValidBotRaces().empty());
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "race_selection.cpp"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++17", str(cpp)], cwd=tmp, check=True)
    subprocess.run([str(Path(tmp) / "race_selection.exe")], check=True)
print("Bot race permissions and exclusion regressions passed")
