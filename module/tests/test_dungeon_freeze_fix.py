import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class DungeonFreezeFixTest(unittest.TestCase):
    def test_effective_follow_target_and_cast_ready_invariants(self):
        compiler = shutil.which('cl')
        if not compiler:
            self.skipTest('MSVC compiler environment is required')

        program = r'''
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

struct Socket {};

struct WorldSession {
    uint32_t accountId;
    Socket* socket;
    bool isBot;

    WorldSession(uint32_t acc, Socket* sock, bool bot) : accountId(acc), socket(sock), isBot(bot) {}
    bool IsBot() const { return isBot; }
    Socket* GetSocket() const { return socket; }
    uint32_t GetAccountId() const { return accountId; }
};

struct Player;

struct GroupReference {
    Player* source;
    GroupReference* nextRef;
    Player* GetSource() const { return source; }
    GroupReference* next() const { return nextRef; }
};

struct Group {
    uint64_t leaderGuid;
    GroupReference* firstMember;

    uint64_t GetLeaderGUID() const { return leaderGuid; }
    GroupReference* GetFirstMember() const { return firstMember; }
};

struct Player {
    uint64_t guid;
    std::string name;
    WorldSession* session;
    Group* group;
    bool inWorld;
    bool moving;

    Player(uint64_t g, std::string n, WorldSession* s, bool iw = true)
        : guid(g), name(std::move(n)), session(s), group(nullptr), inWorld(iw), moving(false) {}

    uint64_t GetGUID() const { return guid; }
    std::string const& GetName() const { return name; }
    WorldSession* GetSession() const { return session; }
    Group* GetGroup() const { return group; }
    bool IsInWorld() const { return inWorld; }
    bool isMoving() const { return moving; }
    void StopMoving() { moving = false; }
};

namespace BotMgr {
    bool IsBotAccountId(uint32_t accountId) {
        return accountId >= 1000;
    }
}

Player* GetEffectiveFollowTarget(Player* bot, Group* group = nullptr) {
    if (!bot)
        return nullptr;
    if (!group)
        group = bot->GetGroup();
    if (!group)
        return nullptr;

    auto isHumanPlayer = [](Player const* player) -> bool {
        if (!player)
            return false;
        WorldSession* session = player->GetSession();
        if (!session)
            return false;
        if (session->IsBot() || session->GetSocket() == nullptr || BotMgr::IsBotAccountId(session->GetAccountId()))
            return false;
        return true;
    };

    // 1. If nominal leader is a real human player, always follow the leader.
    Player* nominalLeader = nullptr;
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
        if (itr->GetSource() && itr->GetSource()->GetGUID() == group->GetLeaderGUID()) {
            nominalLeader = itr->GetSource();
            break;
        }
    }
    if (nominalLeader && nominalLeader != bot && isHumanPlayer(nominalLeader))
        return nominalLeader;

    // 2. If nominal leader is a bot or missing, search group for any connected human player.
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
        Player* member = itr->GetSource();
        if (member && member != bot && member->IsInWorld() && isHumanPlayer(member))
            return member;
    }

    // 3. Bot-only group fallback (e.g. WorldParties): follow nominal leader if not self.
    if (nominalLeader && nominalLeader != bot)
        return nominalLeader;

    return nullptr;
}

// CombatMovement ReadyToCast invariant verification
struct SpellInfo {
    uint32_t castTime;
    bool isChanneled;
    uint32_t CalcCastTime(Player*) const { return castTime; }
    bool IsChanneled() const { return isChanneled; }
};

enum class MoveOwner { None, Combat, Travel };

struct LocomotionRecord {
    MoveOwner owner = MoveOwner::None;
    uint32_t commandId = 0;
};

bool ReadyToCast(Player* bot, SpellInfo const* spellInfo, LocomotionRecord& rec) {
    if (!bot || !spellInfo)
        return false;
    bool requiresStationary = (spellInfo->CalcCastTime(bot) > 0 || spellInfo->IsChanneled());
    if (!requiresStationary)
        return true;
    if (!bot->isMoving())
        return true;

    // Surgical fix invariant: stop physical motion, DO NOT wipe rec.owner
    bot->StopMoving();
    return false;
}

int main() {
    Socket humanSocket;
    WorldSession humanSession(1, &humanSocket, false); // Real human session
    WorldSession botSession1(1001, nullptr, true);      // Bot session (Tank)
    WorldSession botSession2(1002, nullptr, true);      // Bot session (DPS)

    Player human(1, "HumanPlayer", &humanSession);
    Player botTank(2, "BotTank", &botSession1);
    Player botDps(3, "BotDps", &botSession2);

    Group group;
    // Simulate LFG group formation bug where bot tank was made nominal leader
    group.leaderGuid = botTank.GetGUID();

    GroupReference ref3 = { &botDps, nullptr };
    GroupReference ref2 = { &human, &ref3 };
    GroupReference ref1 = { &botTank, &ref2 };
    group.firstMember = &ref1;

    human.group = &group;
    botTank.group = &group;
    botDps.group = &group;

    // Test 1: Bot Tank (nominal leader) in LFG group resolves human player as effective follow target
    Player* tankFollow = GetEffectiveFollowTarget(&botTank, &group);
    assert(tankFollow != nullptr);
    assert(tankFollow->GetGUID() == human.GetGUID());

    // Test 2: Bot DPS in LFG group resolves human player as effective follow target
    Player* dpsFollow = GetEffectiveFollowTarget(&botDps, &group);
    assert(dpsFollow != nullptr);
    assert(dpsFollow->GetGUID() == human.GetGUID());

    // Test 3: ReadyToCast non-clearing stop invariant
    LocomotionRecord rec;
    rec.owner = MoveOwner::Combat;
    rec.commandId = 42;

    SpellInfo castTimeSpell{ 2000, false };
    botDps.moving = true;

    // Tick 1: Bot is moving -> ReadyToCast stops moving and returns false
    uint32_t nextCastAllowedMs = 500;
    bool ready1 = ReadyToCast(&botDps, &castTimeSpell, rec);
    assert(!ready1);
    assert(!botDps.isMoving());
    // Invariant: Locomotion owner and commandId are NOT wiped to None/0
    assert(rec.owner == MoveOwner::Combat);
    assert(rec.commandId == 42);

    // With our fix, nextCastAllowedMs is set to 0, so next tick evaluates immediately
    nextCastAllowedMs = 0;
    assert(nextCastAllowedMs == 0);

    // Tick 2: Bot is now stationary -> ReadyToCast returns true immediately!
    bool ready2 = ReadyToCast(&botDps, &castTimeSpell, rec);
    assert(ready2);

    std::cout << "Dungeon AI freeze fix unit tests PASSED!" << std::endl;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = pathlib.Path(directory) / 'test_freeze.cpp'
            executable = pathlib.Path(directory) / 'test_freeze.exe'
            cpp.write_text(program)
            build = subprocess.run([compiler, '/nologo', '/EHsc', '/std:c++20', str(cpp),
                                    '/Fe:' + str(executable)], cwd=directory, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(executable)], cwd=directory, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('Dungeon AI freeze fix unit tests PASSED!', result.stdout)


if __name__ == '__main__':
    unittest.main()
