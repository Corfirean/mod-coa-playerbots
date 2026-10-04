import pathlib
import shutil
import subprocess
import tempfile
import unittest


class GroupLootTest(unittest.TestCase):
    def test_group_setting_preserves_solo_and_clears_only_disabled_group_loot(self):
        compiler = shutil.which('cl')
        if not compiler:
            self.skipTest('MSVC compiler environment is required')
        source = (pathlib.Path(__file__).resolve().parents[1] / 'src/BotAI.cpp').read_text()
        start = source.index('bool TryProcessPendingLoot(')
        end = source.index('    // If we have a single lastCombatTargetGuid', start)
        guard = source[start:end]
        program = r'''
#include <cassert>
#include <vector>
using uint32 = unsigned;
struct ObjectGuid { static constexpr unsigned Empty = 0; };
struct Player { bool grouped; void* GetGroup() { return grouped ? this : nullptr; } };
struct BotAIState { std::vector<unsigned> pendingLootGuids{42}; unsigned lastCombatTargetGuid = 77; };
struct Manager { bool enabled; bool AutoLootInGroup() const { return enabled; } } manager;
auto sBotMgr = &manager;
enum class MoveOwner { Loot };
unsigned released = 0;
namespace BotMovement { void ForceReleaseOwner(Player*, MoveOwner) { ++released; } }
''' + guard + r'''
    return true;
}
int main() {
    for (bool grouped : {false, true}) {
        for (bool enabled : {false, true}) {
            Player player{grouped}; BotAIState state; manager.enabled = enabled; released = 0;
            bool const permitted = TryProcessPendingLoot(&player, 1, state);
            bool const blocked = grouped && !enabled;
            assert(permitted == !blocked);
            assert(state.pendingLootGuids.size() == (blocked ? 0 : 1));
            assert(state.lastCombatTargetGuid == (blocked ? 0 : 77));
            assert(released == (blocked ? 1 : 0));
        }
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = pathlib.Path(directory) / 'loot.cpp'
            exe = pathlib.Path(directory) / 'loot.exe'
            cpp.write_text(program)
            result = subprocess.run([compiler, '/nologo', '/EHsc', '/std:c++20', str(cpp), '/Fe:' + str(exe)],
                                    cwd=directory, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(subprocess.run([str(exe)], cwd=directory).returncode, 0)


if __name__ == '__main__':
    unittest.main()
