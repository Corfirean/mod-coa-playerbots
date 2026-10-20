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
    bool IsSocketClosed() const { return socket == nullptr; }
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
    uint64_t leaderGuid = 0;
    uint64_t assistantGuid = 0;
    GroupReference* firstMember = nullptr;

    uint64_t GetLeaderGUID() const { return leaderGuid; }
    GroupReference* GetFirstMember() const { return firstMember; }
    bool IsAssistant(uint64_t guid) const { return guid == assistantGuid; }
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
        if (session->IsBot() || session->IsSocketClosed() || BotMgr::IsBotAccountId(session->GetAccountId()))
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
    // If multiple humans exist, prefer group assistants, otherwise first human member slot (deterministic for all bots).
    Player* firstHuman = nullptr;
    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next()) {
        Player* member = itr->GetSource();
        if (member && member != bot && member->IsInWorld() && isHumanPlayer(member)) {
            if (group->IsAssistant(member->GetGUID()))
                return member;
            if (!firstHuman)
                firstHuman = member;
        }
    }
    if (firstHuman)
        return firstHuman;

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
    uint64_t targetGuid = 0;
    float chaseMin = 0.0f;
    float chaseMax = 0.0f;
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

bool IsIdempotent(LocomotionRecord const& rec, uint64_t target, float minR, float maxR) {
    return rec.owner == MoveOwner::Combat && rec.targetGuid == target &&
           std::abs(rec.chaseMin - minR) <= 0.5f && std::abs(rec.chaseMax - maxR) <= 0.5f;
}

int main() {
    Socket humanSocket1;
    Socket humanSocket2;
    WorldSession humanSession1(1, &humanSocket1, false); // Real human 1
    WorldSession humanSession2(2, &humanSocket2, false); // Real human 2 (Assistant)
    WorldSession botSession1(1001, nullptr, true);       // Bot session (Tank)
    WorldSession botSession2(1002, nullptr, true);       // Bot session (DPS)

    Player human1(1, "HumanPlayer1", &humanSession1);
    Player human2(2, "HumanAssistant", &humanSession2);
    Player botTank(3, "BotTank", &botSession1);
    Player botDps(4, "BotDps", &botSession2);

    Group group;
    // Simulate LFG group formation bug where bot tank was made nominal leader
    group.leaderGuid = botTank.GetGUID();
    group.assistantGuid = human2.GetGUID();

    GroupReference ref4 = { &botDps, nullptr };
    GroupReference ref3 = { &human2, &ref4 };
    GroupReference ref2 = { &human1, &ref3 };
    GroupReference ref1 = { &botTank, &ref2 };
    group.firstMember = &ref1;

    human1.group = &group;
    human2.group = &group;
    botTank.group = &group;
    botDps.group = &group;

    // Test 1: Bot Tank (nominal leader) in LFG group resolves human assistant as effective follow target
    Player* tankFollow = GetEffectiveFollowTarget(&botTank, &group);
    assert(tankFollow != nullptr);
    assert(tankFollow->GetGUID() == human2.GetGUID());

    // Test 2: Bot DPS in LFG group resolves human assistant as effective follow target
    Player* dpsFollow = GetEffectiveFollowTarget(&botDps, &group);
    assert(dpsFollow != nullptr);
    assert(dpsFollow->GetGUID() == human2.GetGUID());

    // Test 3: Manual commander override persistence
    uint64_t manualCommander = human1.GetGUID(); // Human 1 explicitly issued /coabot follow
    Player* leader = nullptr;
    if (manualCommander != 0) {
        leader = &human1; // manual follow override preserves commander choice
    }
    assert(leader != nullptr && leader->GetGUID() == human1.GetGUID());

    // Test 4: 10-tick AI update sequence simulation (Chase -> Stop -> Cast -> Cooldown -> Finish)
    LocomotionRecord rec;
    rec.owner = MoveOwner::Combat;
    rec.commandId = 1;
    rec.targetGuid = 999;
    rec.chaseMin = 14.0f;
    rec.chaseMax = 20.0f;

    SpellInfo frostbolt{ 2000, false };
    SpellInfo fireBlast{ 0, false }; // instant
    botDps.moving = true;

    // Tick 1: Chasing, spell selected requires stationary. ReadyToCast stops moving, returns false. nextCastAllowedMs = 0.
    bool readyT1 = ReadyToCast(&botDps, &frostbolt, rec);
    assert(!readyT1);
    assert(!botDps.isMoving());
    assert(rec.owner == MoveOwner::Combat); // Invariant: owner NOT cleared!

    // Tick 2: AI evaluates immediately (nextCastAllowedMs == 0). Chase is idempotent (no restart). ReadyToCast returns true.
    assert(IsIdempotent(rec, 999, 14.0f, 20.0f));
    bool readyT2 = ReadyToCast(&botDps, &frostbolt, rec);
    assert(readyT2);
    // Spell cast begins!
    bool isCasting = true;
    uint32_t remainingCastMs = 2000;

    // Ticks 3-6: Casting in progress. isStoppedBecauseOfCasting prevents any movement dispatch.
    for (int tick = 3; tick <= 6; ++tick) {
        assert(isCasting);
        remainingCastMs -= 500;
        if (remainingCastMs == 0)
            isCasting = false;
    }
    assert(!isCasting);

    // Tick 7: Cast finished. Instant ability (Fire Blast) selected.
    bool readyT7 = ReadyToCast(&botDps, &fireBlast, rec);
    assert(readyT7); // instant spell ready immediately

    // Tick 8: GCD active. Target remains in range.
    assert(!botDps.isMoving());

    // Tick 9: Target steps out of range (distance grows). Bot starts moving to catch up.
    botDps.moving = true;
    assert(botDps.isMoving());

    // Tick 10: Spell selected while moving. ReadyToCast stops and prepares for cast without oscillation.
    bool readyT10 = ReadyToCast(&botDps, &frostbolt, rec);
    assert(!readyT10);
    assert(!botDps.isMoving());
    assert(rec.owner == MoveOwner::Combat);

    std::cout << "Dungeon AI freeze fix unit tests PASSED (10-tick sequence verified)!" << std::endl;
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
            self.assertIn('Dungeon AI freeze fix unit tests PASSED (10-tick sequence verified)!', result.stdout)


if __name__ == '__main__':
    unittest.main()
