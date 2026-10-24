/*
 * mod-coa-playerbots
 *
 * Group tactical layer (combat-intelligence package 3): decisions that belong to the group, not to each bot on its own.
 *
 * First task: interrupts. Every bot used to look only at the cast of its own current target and every bot that happened to know a kick
 * would use it on any interruptible cast (the old reservation only stopped two bots kicking the same cast). The director looks at the cast
 * of every enemy that is fighting the group, ignores casts that do not matter, names one primary and one backup interrupter per cast and
 * hands each bot its assignment. The snapshot is shared by the whole group, rebuilt at most every SNAPSHOT_INTERVAL_MS, and dropped when the
 * group has not asked for it for a while, so the cost per bot per tick is one hash lookup.
 */

#ifndef COA_PLAYERBOTS_GROUP_DIRECTOR_H
#define COA_PLAYERBOTS_GROUP_DIRECTOR_H

#include "Define.h"
#include "ObjectGuid.h"

class Player;
class Unit;

namespace BotAI
{
    class GroupDirector
    {
    public:
        // The enemy `bot` has been assigned to interrupt right now (and the spell to do it with), or nullptr. Only bots of a group are
        // served; a bot without a group keeps the old own-target behaviour in CombatUtility.
        static Unit* GetInterruptTarget(Player* bot, uint32& outSpellId);

        // The bot's cast of `spellId` at `enemy` was accepted (`landed`) or refused; a refusal hands the cast to the backup immediately.
        static void ReportInterruptAttempt(Player* bot, ObjectGuid enemy, bool accepted);

        // Drops everything held for a bot / a group (despawn, group disband, map change).
        static void ForgetBot(ObjectGuid botGuid);
        static void ForgetGroup(ObjectGuid groupGuid);

        // Number of live group snapshots (diagnostics and leak checks).
        static uint32 SnapshotCount();
    };
}

#endif // COA_PLAYERBOTS_GROUP_DIRECTOR_H
