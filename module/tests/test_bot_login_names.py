import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class BotLoginNames(unittest.TestCase):
    def test_actual_generator_rejects_filtered_and_taken_names_and_fails_closed(self):
        compiler = shutil.which('cl')
        if not compiler:
            self.skipTest('MSVC is required for the native generator test')
        source = (Path(__file__).parents[1] / 'src/BotSpawnRandom.cpp').read_text(encoding='utf-8')
        begin = source.index('std::string GenerateUniqueName(')
        end = source.index("// A template's own race", begin)
        generator = source[begin:end]
        harness = r"""
#include <cassert>
#include <cstdint>
#include <string>
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
constexpr int CHAR_NAME_SUCCESS = 0;
int mode = 0;
int lore = 0;
int fallback = 0;
std::string GenerateLoreName(uint8, uint8) { ++lore; return mode == 2 ? "Validname" : "Filteredname"; }
std::string GenerateRandomName() { ++fallback; return fallback == 1 ? "Takenname" : "Freshname"; }
struct ObjectMgr {
    static int CheckPlayerName(std::string const& name) { return mode == 1 || name == "Filteredname" ? 1 : 0; }
};
struct Cache { bool GetCharacterGuidByName(std::string const& name) { return name == "Takenname"; } } cache;
Cache* sCharacterCache = &cache;
// GENERATOR
int main() {
    assert(GenerateUniqueName(1, 0) == "Freshname");
    assert(lore == 50 && fallback == 2);
    mode = 1; lore = fallback = 0;
    assert(GenerateUniqueName(1, 0).empty());
    assert(lore == 50 && fallback == 50);
    mode = 2; lore = fallback = 0;
    assert(GenerateUniqueName(1, 0) == "Validname");
    assert(lore == 1 && fallback == 0);
}
""".replace('// GENERATOR', generator)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'names.cpp').write_text(harness)
            subprocess.run([compiler, '/nologo', '/std:c++20', '/EHsc', 'names.cpp', '/Fe:names.exe'], cwd=root, check=True)
            subprocess.run([str(root / 'names.exe')], check=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))


if __name__ == '__main__':
    unittest.main()
