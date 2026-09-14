/*
 * mod-coa-playerbots
 *
 * Generic, class-agnostic combat AI for a bot Player. See BotAI.cpp's header
 * comment for why this doesn't hand-tune a rotation per class -- CoA has 21
 * custom classes (mod-ascension-compat) and no rotation exists for any of them
 * yet (docs/architecture.md's "Beyond parity" section).
 */

#ifndef COA_PLAYERBOTS_BOT_AI_H
#define COA_PLAYERBOTS_BOT_AI_H

#include "Define.h"
#include "ObjectGuid.h"

class ChatHandler;
class Player;

// Bot role (Dps/Tank/Healer) that changes which target-selection and spell-selection
// path BotAI::Update() takes. By default, auto-detected from the bot's active Ascension
// specialization (Player::GetPlayerSetting("core.ascension_active_spec", 0)) via ClassSpecRoles.
// Can be manually overridden via BotMgr::SetRole / `.botcmd setrole`, or reset to auto with
// `.botcmd setrole <guid> auto`.
enum class BotRole
{
    Dps,
    Tank,
    Healer,
    // Fights like Dps (same target-acquisition/engage/cast path) but additionally maintains
    // a party/raid-wide buff on itself when one's known and ready -- see docs/roles.md's role
    // taxonomy: real Ascension "Support" specs are DPS-hybrids with buffing layered on top
    // (Bloodmage's Fleshweaver is the one pure-support exception), not a backline-only role.
    Support,
};

// Manual movement/engagement override from the bot-control addon (see docs/addon-protocol.md
// and BotAddonChat.cpp) -- orthogonal to BotRole: a Tank can be told to Stay just as easily
// as a Dps. None/Follow both mean "behave normally" (acquire the leader's target or whatever's
// attacking it, follow when idle); Follow only exists as an explicit verb to cancel a
// previously-set Stay/Pull. Stay suppresses following the leader while idle (still
// self-defends, same as always). Pull is one-shot: forces an immediate Attack() on a specific
// target, then reverts to None so normal role logic takes over the resulting fight.
enum class BotManualCommand
{
    None,
    Follow,
    Stay,
    Pull,
};

namespace BotAI
{
    // One tick of role-aware AI for a single bot. Dps/Tank: acquires a target (its own
    // combat victim, its group leader's if the leader is fighting, or whichever attacker
    // is currently hitting the bot if neither -- see the attacker-retaliation fallback in
    // BotAI.cpp), chases into melee range, auto-attacks, and casts any of its own known,
    // ready, affordable, in-range offensive spells; Tank additionally prioritizes a taunt
    // if it doesn't currently hold the target's aggro. Healer: finds the lowest-health
    // group member (self included) and casts a real known heal on them instead, falling
    // back to the Dps behavior when nobody needs healing. Falls back to following the group
    // leader once there's no target/heal work. Called from BotMgr::Update() for every
    // active bot session, every tick. While dead, runs death-handling instead (wait a grace
    // period for a real resurrect, else release spirit and corpse-run; wait at the graveyard
    // instead of corpse-running while in a battleground) -- see BotAI.cpp's
    // UpdateDeathHandling for the full behavior.
    void Update(Player* bot, uint32 diff);

    // Drops any per-bot AI state (cast-gate timer, assigned role) held for this guid. Call
    // when a bot despawns, so BotAI's internal state map doesn't grow unbounded across
    // repeated spawn/despawn cycles.
    void Forget(ObjectGuid botGuid);

    // Manually assigns (or reassigns) a bot's role, overriding auto-detection.
    void SetRole(ObjectGuid botGuid, BotRole role);

    // Clears any manual role override for this bot, restoring automatic role detection
    // from its active Ascension specialization.
    void ClearRoleOverride(ObjectGuid botGuid);

    // Returns the bot's effective role (manual override if set, else auto-detected).
    BotRole GetRole(ObjectGuid botGuid);

    // Sets a manual movement/engagement command (see BotManualCommand above). pullTarget is
    // only used for Pull -- the guid of whatever the commanding player currently has
    // selected, resolved by the caller (BotAddonChat.cpp) so this API doesn't need to know
    // anything about who issued the command.
    void SetManualCommand(ObjectGuid botGuid, BotManualCommand command, ObjectGuid pullTarget = ObjectGuid::Empty);

    // Immediately stops the bot's current attack and reverts any manual command to None
    // (falls back to normal follow/idle behavior next tick).
    void StopAttack(ObjectGuid botGuid);

    // Fully disables BotAI::Update() for this guid while suspended -- for any other system
    // that spawns a session through BotMgr (any spawned Player becomes a tracked bot session,
    // subject to this file's own combat AI every tick) but needs sole control of that Player
    // itself. Built for mod-ascension-compat's AscensionClassTester: it spawns test
    // characters via .botcmd spawnbot for convenience, then manipulates them directly
    // (repositioning, swapping gear, forcing casts) -- without this, BotAI's own combat loop
    // was independently attacking the same test dummies at the same time, and the two
    // systems' concurrent CombatStop()/item-swap/cast calls on the same Player crashed the
    // server (confirmed live via a crash dump landing in Item::IsBroken mid-delayed-spell-
    // effect). Suspension is per-guid and persists until explicitly cleared or the bot
    // despawns (Forget() below already clears all per-guid state, suspension included).
    void SetSuspended(ObjectGuid botGuid, bool suspended);

    // Diagnostic: scans the bot's own known spellbook with the same three predicates
    // SelectSpell/SelectTauntSpell/SelectHealSpell use (offensive/taunt/heal-shaped),
    // reporting counts and a few example spell ids for each to `handler`. Doesn't check
    // cooldown/range/cost (there's no target for those checks to run against) -- this is
    // "what could this bot ever cast in principle," not "what can it cast right now."
    // Built to empirically map a class's numeric Ascension specialization id to its real
    // role (docs/roles.md only has spec *names*, nothing maps those back to the SpecId
    // CoATalentEntry::SpecId actually uses) -- set a candidate spec id, relog the bot, run
    // this, and see which predicate lights up.
    void ReportSpellbookRoleSignals(Player* bot, ChatHandler* handler);
}

#endif // COA_PLAYERBOTS_BOT_AI_H
