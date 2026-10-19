import pathlib
import os
import shutil
import subprocess
import tempfile
import unittest


class AddonAuthorizationTest(unittest.TestCase):
    def test_rejected_requests_do_not_dereference_a_missing_bot(self):
        compiler = shutil.which('cl')
        if not compiler:
            self.skipTest('MSVC compiler environment is required')
        root = pathlib.Path(__file__).resolve().parents[2]
        source_ref = os.environ.get('COA_ADDON_SOURCE_REF')
        source = (subprocess.check_output(['git', 'show', f'{source_ref}:module/src/BotAddonChat.cpp'], cwd=root).decode()
                  if source_ref else (root / 'module/src/BotAddonChat.cpp').read_text())
        start = source.index('    Player* bot = ResolveAuthorizedBot(commander, botGuidLow);')
        end = source.index('\n    LOG_', source.index('\n    }', start) + 6)
        rejection = source[start:end]
        program = r'''
#include <string>
#include <cstdlib>
struct Player {
    unsigned guid = 1;
    unsigned GetGUID() const { return guid; }
    std::string GetName() const { return "Commander"; }
};
namespace BotAI::BotDebugLog {
    std::string LoggerName(unsigned guid) { return std::to_string(guid); }
}
unsigned logs = 0;
template<class... Args> void Log(Args const&...) { ++logs; }
#define LOG_INFO(...) Log(__VA_ARGS__)
#define LOG_DEBUG(...) Log(__VA_ARGS__)
Player* ResolveAuthorizedBot(Player*, unsigned) { return nullptr; }
void Request(Player* commander, std::string const& verb, unsigned botGuidLow) {
''' + rejection + r'''
    std::abort();
}
int main() {
    Player commander;
    for (auto verb : {"GETROLES", "GETSPECS", "FOLLOW", "SETROLE"})
        Request(&commander, verb, 42);
    return logs == 4 ? 0 : 1;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = pathlib.Path(directory) / 'rejected.cpp'
            executable = pathlib.Path(directory) / 'rejected.exe'
            cpp.write_text(program)
            build = subprocess.run([compiler, '/nologo', '/EHsc', '/std:c++20', str(cpp),
                                    '/Fe:' + str(executable)], cwd=directory, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(executable)], cwd=directory, capture_output=True)
            self.assertEqual(result.returncode, 0, 'Rejected addon request crashed or continued past its guard')


if __name__ == '__main__':
    unittest.main()
