/*
 * mod-coa-playerbots
 *
 * CoA has 21 custom Ascension classes and, per docs/research/ascension-class-status.md
 * and docs/architecture.md, zero rotation AI exists for any of them in either base bot
 * project (NPCBots or Playerbots) -- both only know the 11 stock classes. Some of these
 * custom classes (Venomancer especially -- see docs/venomancer-completion.md's 190-point
 * audit) have deep, bespoke resource/form/summon systems that would each need substantial
 * dedicated research to hand-write a faithful rotation for.
 *
 * Rather than hand-curating a priority list per class (21 of them, each a multi-file research
 * project), this AI works generically off the bot's own real, already-learned spellbook
 * (Player::GetSpellMap()) and each spell's real SpellInfo properties -- the same data a real
 * client's own cast-bar would use. It doesn't know what a spell is "for," only whether it
 * looks like an offensive/heal/taunt ability that's currently usable (see the Is*Spell
 * predicates below). This is deliberately a floor, not a ceiling: it makes every class
 * immediately capable of *some* role-appropriate behavior with no per-class work, and can be
 * refined into real per-class priority logic later once a given class's kit has been
 * properly researched (see AGENTS.md's "Not yet decided" section, and docs/roles.md for the
 * per-class/per-spec role assignments this is meant to eventually dispatch on automatically).
 *
 * Role (Dps/Tank/Healer/Support) auto-detects from the bot's active Ascension spec
 * (ClassSpecRoles) by default, and can be manually overridden (BotAI::SetRole,
 * `.botcmd setrole`, or the COABOT addon's SETROLE verb) -- see BotAI::Update's
 * manualRoleOverride check.
 */

#include "BotAI.h"
#include "ClassSpecRoles.h"
#include "Chat.h"
#include "Corpse.h"
#include "Group.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PetDefines.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <unordered_map>

namespace
{
// This AzerothCore fork predates TrinityCore's SpellHistory refactor (no per-spell "time
// until GCD/cooldown clears" query API), so there's no cheap way to ask "is this bot off
// its real global cooldown right now." Approximating it with a fixed per-bot timer is good
// enough for "looks like a player acting at a reasonable pace," not meant to exactly match
// any specific class's real (and possibly haste-modified) GCD.
constexpr uint32 APPROXIMATE_GCD_MS = 1500;
constexpr float MELEE_ENGAGE_RANGE = 4.0f;

// How close a Healer bot tries to stay to whoever it's healing -- well within the range of
// most real heal spells (checked properly per-spell in SelectHealSpell anyway; this just
// keeps a healer from parking itself at the exact edge of range where minor movement causes
// spells to start failing).
constexpr float HEAL_ENGAGE_RANGE = 25.0f;

// Retry gate used when nothing was castable this tick (e.g. everything's on cooldown or
// unaffordable) -- much shorter than the real GCD so a newly-ready spell gets used quickly,
// without re-scanning the whole spellbook every single world tick while idle-in-combat.
constexpr uint32 NO_CANDIDATE_RETRY_MS = 500;

struct BotAIState
{
    uint32 nextCastAllowedMs = 0;
    uint32 noCandidateLogMs = 0;
    uint32 nextBuffCheckMs = 0;
    BotRole role = BotRole::Dps;
    bool manualRoleOverride = false;

    // Death handling (see UpdateDeathHandling): true once this death has already started its
    // grace-period wait for an incoming resurrect, so the wait isn't restarted every tick.
    bool awaitingRezGrace = false;
    uint32 deathReleaseGraceMs = 0;

    // Manual command from the bot-control addon -- see BotManualCommand's comment in BotAI.h.
    BotManualCommand manualCommand = BotManualCommand::None;
    ObjectGuid pullTargetGuid;

    // See BotAI::SetSuspended's comment in BotAI.h.
    bool suspended = false;
};

// How long a freshly-dead bot waits, body not yet released, before giving up on a real
// group healer resurrecting it and releasing its own spirit -- long enough to cover a
// healer's own GCD/cast-time/reaction, short enough that a bot doesn't sit dead for a full
// minute in a group with no healer at all. See UpdateDeathHandling.
constexpr uint32 DEATH_REZ_GRACE_MS = 15000;

// How often a Support bot re-checks whether its party/raid buff needs (re)applying. Real
// Ascension buffs of this shape run tens of minutes; checking every 15s is cheap and still
// reapplies promptly after a dispel/death/relog without scanning the spellbook every tick.
constexpr uint32 BUFF_CHECK_INTERVAL_MS = 15000;

std::unordered_map<ObjectGuid, BotAIState> states;

// Filters a learned spell down to "looks like something a player would press on an enemy
// in combat," without knowing anything about what the spell is actually named or which of
// the 21 custom classes it belongs to:
//  - NeedsExplicitUnitTarget(): excludes self-buffs, ground-targeted effects, and passives
//    that don't take a unit target at all -- keeps this to simple single-target casts.
//  - !IsPositive(): a targeted-enemy spell should never be a buff/heal.
//  - !IsPassive(), CanBeUsedInCombat(): excludes passives and out-of-combat-only spells.
//  - !IsAutoRepeatRangedSpell(): Auto Shot/Wand behave as a standing toggle the engine
//    manages itself once triggered, not something to re-cast every tick.
//  - at least one clearly offensive effect (direct/weapon damage, or a periodic-damage
//    aura): the actual "is this a damage ability" signal. Without this, plenty of
//    NeedsExplicitUnitTarget()+non-positive spells would still slip through (e.g. a
//    non-damaging enemy-targeted utility effect) with no real offensive purpose.
bool IsUsableOffensiveSpell(SpellInfo const* spellInfo)
{
    if (!spellInfo || spellInfo->IsPassive() || spellInfo->IsPositive())
        return false;
    if (!spellInfo->NeedsExplicitUnitTarget() || !spellInfo->CanBeUsedInCombat())
        return false;
    if (spellInfo->IsAutoRepeatRangedSpell())
        return false;

    return spellInfo->HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE) ||
        spellInfo->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE) ||
        spellInfo->HasEffect(SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL) ||
        spellInfo->HasEffect(SPELL_EFFECT_WEAPON_PERCENT_DAMAGE) ||
        spellInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE) ||
        spellInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE_PERCENT);
}

// Same idea as IsUsableOffensiveSpell, but for "is this a taunt": the two real WotLK taunt
// shapes are a direct SPELL_EFFECT_ATTACK_ME (e.g. Warrior Taunt) or a SPELL_AURA_MOD_TAUNT
// aura (e.g. Growl-style pet taunts). Still requires an explicit enemy target and combat
// usability, same as the offensive filter, since a taunt is itself an enemy-targeted spell.
bool IsUsableTauntSpell(SpellInfo const* spellInfo)
{
    if (!spellInfo || spellInfo->IsPassive())
        return false;
    if (!spellInfo->NeedsExplicitUnitTarget() || !spellInfo->CanBeUsedInCombat())
        return false;

    return spellInfo->HasEffect(SPELL_EFFECT_ATTACK_ME) || spellInfo->HasAura(SPELL_AURA_MOD_TAUNT);
}

// Same idea again, but for "is this a single-target heal": a positive spell (never an enemy
// ability) with a direct SPELL_EFFECT_HEAL/HEAL_PCT or a SPELL_AURA_PERIODIC_HEAL (a HoT).
bool IsUsableHealSpell(SpellInfo const* spellInfo)
{
    if (!spellInfo || spellInfo->IsPassive() || !spellInfo->IsPositive())
        return false;
    if (!spellInfo->NeedsExplicitUnitTarget() || !spellInfo->CanBeUsedInCombat())
        return false;

    return spellInfo->HasEffect(SPELL_EFFECT_HEAL) || spellInfo->HasEffect(SPELL_EFFECT_HEAL_PCT) ||
        spellInfo->HasAura(SPELL_AURA_PERIODIC_HEAL);
}

// Real Ascension "Support" specs are DPS-hybrids with a party/raid buff layered on top
// (docs/roles.md's role taxonomy), not a backline-only role -- so this only needs to find
// "a buff that reaches the whole party/raid," not every possible positive spell (that would
// also match single-target heals and personal self-buffs, neither of which need Support's
// own special handling: heals aren't this role's job, and ordinary self-buffs get learned
// and used passively/on cooldown by the class kit regardless of what BotAI does). The three
// AREA_AURA effect types below are the actual WotLK engine mechanism real party/raid buffs
// use (e.g. Fortitude/Blessing-of-Kings-style spells): cast on self, the engine propagates
// the aura to nearby party/raid/friendly members automatically -- no ally-targeting needed.
bool IsUsableBuffSpell(SpellInfo const* spellInfo)
{
    if (!spellInfo || spellInfo->IsPassive() || !spellInfo->IsPositive())
        return false;

    return spellInfo->HasEffect(SPELL_EFFECT_APPLY_AREA_AURA_PARTY) ||
        spellInfo->HasEffect(SPELL_EFFECT_APPLY_AREA_AURA_RAID) ||
        spellInfo->HasEffect(SPELL_EFFECT_APPLY_AREA_AURA_FRIEND);
}

// Shared shape for all three "pick a ready, affordable, in-range known spell matching this
// predicate" scans below -- only the predicate and the range-table selection (hostile vs.
// beneficial spells keep separate min/max range entries) differ.
uint32 SelectKnownSpell(Player* bot, Unit* target, bool positiveRange, bool (*predicate)(SpellInfo const*))
{
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!predicate(spellInfo))
            continue;
        if (bot->HasSpellCooldown(spellId))
            continue;

        // Both bounds matter for a ranged spell: SelectKnownSpell used to only check
        // maxRange, so a bot standing inside a spell's real minRange (e.g. a hunter-style
        // shot with a ~5yd dead zone) would still pick it as "in range," guaranteeing
        // SPELL_FAILED_TOO_CLOSE on cast. Check both.
        float dist = bot->GetDistance(target);
        float maxRange = spellInfo->GetMaxRange(positiveRange, bot);
        if (maxRange > 0.0f && dist > maxRange)
            continue;
        float minRange = spellInfo->GetMinRange(positiveRange);
        if (minRange > 0.0f && dist < minRange)
            continue;

        int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
        if (cost > 0 && bot->GetPower(Powers(spellInfo->PowerType)) < cost)
            continue;

        return spellId;
    }
    return 0;
}

// First ready, affordable, in-range candidate wins -- not the "best" one by any priority
// (there's no per-class priority data for any of the 21 classes yet). Iteration order over
// an unordered_map is arbitrary, so which spell gets picked among several simultaneously
// ready candidates isn't meaningful; it's still a real, currently-castable spell from the
// bot's own spellbook either way.
uint32 SelectSpell(Player* bot, Unit* target) { return SelectKnownSpell(bot, target, false, IsUsableOffensiveSpell); }
uint32 SelectTauntSpell(Player* bot, Unit* target) { return SelectKnownSpell(bot, target, false, IsUsableTauntSpell); }
uint32 SelectHealSpell(Player* bot, Unit* target) { return SelectKnownSpell(bot, target, true, IsUsableHealSpell); }
// Self-targeted (the party/raid-area effect propagates from there) -- distance is always 0,
// so the range check in SelectKnownSpell trivially passes regardless of maxRange.
uint32 SelectBuffSpell(Player* bot) { return SelectKnownSpell(bot, bot, true, IsUsableBuffSpell); }

// Stops whatever follow motion is already running -- needed because a FOLLOW_MOTION_TYPE
// generator, once started, keeps chasing its target on its own every tick until something
// explicitly clears it. Simply skipping a *new* MoveFollow call (what this function used to
// do below) is not enough to stop one already in progress.
void ClearActiveFollow(Player* bot)
{
    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == FOLLOW_MOTION_TYPE)
        bot->GetMotionMaster()->Clear();
}

void ResumeFollowingLeader(Player* bot, BotAIState const& state)
{
    // Stay means exactly this: don't resume following just because there's nothing to fight
    // right now. Self-defense (the attacker-retaliation fallback in UpdateOffensive) still
    // applies regardless -- Stay only suppresses the idle-follow behavior, not combat. Confirmed
    // live bug report: a bot already mid-follow when Stay was issued kept walking after the
    // leader regardless, because this branch used to just return without stopping the
    // already-running follow generator -- explicitly clear it now.
    if (state.manualCommand == BotManualCommand::Stay)
    {
        ClearActiveFollow(bot);
        return;
    }

    Group* group = bot->GetGroup();
    if (!group)
    {
        // No group (left/kicked/disbanded) means no leader to follow -- same "stop, don't
        // just skip re-issuing" fix as Stay above. Confirmed live bug report: a bot kept
        // following the player after the player left the party, because this branch also
        // used to return without touching an already-active follow generator.
        ClearActiveFollow(bot);
        return;
    }

    Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
    if (!leader || leader == bot)
    {
        ClearActiveFollow(bot);
        return;
    }

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
        bot->GetMotionMaster()->MoveFollow(leader, PET_FOLLOW_DIST, bot->GetFollowAngle());
}

// Lowest-health-percent group member (bot included), below a "worth healing" threshold --
// avoids a healer bot compulsively topping off everyone at 99% the instant a HoT ticks.
// Solo (no group): only ever considers the bot itself.
Player* FindHealTarget(Player* bot)
{
    constexpr float HEAL_THRESHOLD_PCT = 95.0f;

    Player* best = nullptr;
    float bestPct = HEAL_THRESHOLD_PCT;

    auto consider = [&](Player* candidate)
    {
        if (!candidate || !candidate->IsAlive() || !candidate->IsInWorld() || candidate->GetMap() != bot->GetMap())
            return;
        float pct = candidate->GetHealthPct();
        if (pct < bestPct)
        {
            best = candidate;
            bestPct = pct;
        }
    };

    consider(bot);
    if (Group* group = bot->GetGroup())
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            consider(itr->GetSource());

    return best;
}

void LogCastAttempt(Player* bot, uint32 spellId, Unit* target, char const* verb)
{
    SpellCastResult result = bot->CastSpell(target, spellId, false);
    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' {} spell {} on '{}' (result {}).",
        bot->GetName(), verb, spellId, target->GetName(), uint32(result));
}

// Support-only: periodically (not every tick -- a spellbook scan for something that changes
// on a multi-minute cadence at most doesn't need to run 20x/second) tries to (re)apply a
// party/raid buff. Runs independent of combat/target state -- a real support player keeps
// their buff up whether or not anyone's fighting right now, not just mid-encounter.
void TryMaintainBuff(Player* bot, uint32 diff, BotAIState& state)
{
    if (state.nextBuffCheckMs > diff)
    {
        state.nextBuffCheckMs -= diff;
        return;
    }
    state.nextBuffCheckMs = BUFF_CHECK_INTERVAL_MS;

    if (uint32 spellId = SelectBuffSpell(bot))
        LogCastAttempt(bot, spellId, bot, "cast buff");
}

// Dps and Tank share this whole loop -- Tank's only difference is a taunt-priority check
// spliced in right before the normal offensive-spell pick (see the `role == BotRole::Tank`
// branch below). Also used as a Healer's fallback when nobody currently needs healing (a
// real healer doesn't stand still doing nothing just because no one's low), passed
// BotRole::Dps so no taunt logic applies -- a healer without a Tank role assigned shouldn't
// be trying to hold aggro.
void UpdateOffensive(Player* bot, uint32 diff, BotRole role, BotAIState& state)
{
    Unit* target = bot->GetVictim();
    // Stay means "don't go looking for a fight" -- skip inheriting the leader's target, but
    // an existing victim (e.g. a Pull that just fired) and the attacker-retaliation fallback
    // below both still apply, since Stay is about not *initiating*, not about refusing to
    // fight at all.
    if (!target && state.manualCommand != BotManualCommand::Stay)
    {
        if (Group* group = bot->GetGroup())
            if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
                if (leader != bot && leader->IsInCombat())
                    target = leader->GetVictim();
    }

    // Being attacked does not, by itself, make a Player "have a victim" -- Unit::GetVictim()
    // tracks who *this* unit is attacking, not who's attacking it (this matches how a real
    // client behaves too: nothing auto-retaliates without an explicit attack action). Without
    // this, a bot with no group/leader (or whose leader isn't fighting) would just stand there
    // and take free hits from anything that aggroes it with no retaliation at all -- confirmed
    // live: a solo bot died to a low-level Teldrassil critter without ever reaching the cast
    // logic below, because `target` stayed null the entire fight. A real player would fight
    // back against whatever's hitting them; so should this.
    if (!target)
    {
        for (Unit* attacker : bot->getAttackers())
        {
            if (attacker && attacker->IsAlive() && bot->IsValidAttackTarget(attacker))
            {
                target = attacker;
                break;
            }
        }
    }

    if (!target || !target->IsAlive() || !bot->IsValidAttackTarget(target))
    {
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();

        if (!bot->IsInCombat())
            ResumeFollowingLeader(bot, state);
        return;
    }

    if (state.nextCastAllowedMs > diff)
    {
        state.nextCastAllowedMs -= diff;
        return;
    }
    state.nextCastAllowedMs = 0;

    // Try a spell FIRST, regardless of distance -- SelectKnownSpell already checks each
    // candidate's own real min/max range, so a ranged/caster kit can fire from wherever it's
    // actually standing without ever needing to close to melee. This used to be gated behind
    // "must be within MELEE_ENGAGE_RANGE (4.0yd) first," which meant every ranged bot ran all
    // the way into melee range before its first cast attempt, then failed with
    // SPELL_FAILED_TOO_CLOSE against its own dead-zone (e.g. a 5-30yd shot).
    //
    // Tank priority: if this bot doesn't currently hold the target's aggro (a real threat-
    // table check isn't available without the SpellHistory-era threat API this fork
    // predates -- "is the target currently swinging on me" is the cheap, good-enough proxy),
    // try a taunt before falling through to the normal offensive pick.
    uint32 spellId = 0;
    if (role == BotRole::Tank && target->GetVictim() != bot)
        spellId = SelectTauntSpell(bot, target);
    if (!spellId)
        spellId = SelectSpell(bot, target);

    if (spellId)
    {
        // A castable spell was found in its own valid range -- no need to close distance.
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();
        LogCastAttempt(bot, spellId, target, "cast");
        state.nextCastAllowedMs = APPROXIMATE_GCD_MS;
        return;
    }

    // Nothing usable right now (everything on cooldown/unaffordable/out of range, or a
    // pure-melee kit with no spell-based attacks at all) -- fall back to closing to melee
    // range for a basic attack, same as the original behavior for melee bots.
    float distance = bot->GetDistance(target);
    if (distance > MELEE_ENGAGE_RANGE)
    {
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
            bot->GetMotionMaster()->MoveChase(target);
        state.nextCastAllowedMs = NO_CANDIDATE_RETRY_MS;
        return;
    }

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        bot->GetMotionMaster()->Clear();

    if (bot->GetVictim() != target)
        bot->Attack(target, true);

    state.nextCastAllowedMs = NO_CANDIDATE_RETRY_MS;

    // Throttled, not per-attempt -- this branch is expected to hit constantly for a
    // spellbook with nothing usable (e.g. a pure-melee kit with no spell-based
    // attacks at all), so only log it occasionally as a "still no candidate" signal.
    if (state.noCandidateLogMs <= diff)
    {
        LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' has no usable offensive spell ready right now (target '{}').",
            bot->GetName(), target->GetName());
        state.noCandidateLogMs = 4000;
    }
    else
    {
        state.noCandidateLogMs -= diff;
    }
}

void UpdateHealer(Player* bot, uint32 diff, BotAIState& state)
{
    Player* healTarget = FindHealTarget(bot);
    if (!healTarget)
    {
        // Nobody needs healing right now -- a real healer doesn't stand idle with a full
        // group, they contribute damage. Dps, not Tank: a Healer bot has no business trying
        // to hold aggro just because there's nothing to heal this tick.
        UpdateOffensive(bot, diff, BotRole::Dps, state);
        return;
    }

    float distance = bot->GetDistance(healTarget);
    if (distance > HEAL_ENGAGE_RANGE)
    {
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
            bot->GetMotionMaster()->MoveChase(healTarget, HEAL_ENGAGE_RANGE - 5.0f);
        return;
    }

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        bot->GetMotionMaster()->Clear();

    if (state.nextCastAllowedMs > diff)
    {
        state.nextCastAllowedMs -= diff;
        return;
    }
    state.nextCastAllowedMs = 0;

    if (uint32 spellId = SelectHealSpell(bot, healTarget))
    {
        LogCastAttempt(bot, spellId, healTarget, "cast heal");
        state.nextCastAllowedMs = APPROXIMATE_GCD_MS;
        return;
    }

    state.nextCastAllowedMs = NO_CANDIDATE_RETRY_MS;

    if (state.noCandidateLogMs <= diff)
    {
        LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' has no usable heal spell ready right now (target '{}' at {:.0f}% health).",
            bot->GetName(), healTarget->GetName(), healTarget->GetHealthPct());
        state.noCandidateLogMs = 4000;
    }
    else
    {
        state.noCandidateLogMs -= diff;
    }
}

// Support: maintain the party/raid buff, then fight exactly like Dps (a real Support player
// still pulls their weight on damage -- see docs/roles.md's role taxonomy: every Support spec
// but one is a DPS-hybrid, not a backline-only role). No taunt priority (that's Tank's job).
void UpdateSupport(Player* bot, uint32 diff, BotAIState& state)
{
    TryMaintainBuff(bot, diff, state);
    UpdateOffensive(bot, diff, BotRole::Dps, state);
}

// Called every tick a bot is dead, instead of the normal role update -- see BotAI::Update.
// Mirrors what a real player does on death, in three phases:
//  1. Body not yet released: a real player who sees a healer coming waits instead of
//     instantly giving up -- so this waits DEATH_REZ_GRACE_MS, accepting a resurrect
//     immediately if one lands (Player::isResurrectRequested() -- set by EffectResurrect
//     when a real resurrection spell is cast on this corpse, the same check
//     WorldSession::HandleResurrectResponseOpcode itself makes), then gives up and releases
//     spirit itself the same way HandleRepopRequestOpcode does for a real client.
//  2. Ghost, not in a battleground: corpse-run -- chase the bot's own corpse, then reclaim it
//     once in range, the same two steps a real player's clicks (repop, walk, reclaim) cause.
//  3. Ghost, in a battleground: real WotLK battlegrounds auto-resurrect every ghost at a
//     graveyard on their own periodic timer -- corpses can be arena-lengths away across a
//     contested map, and running back would fight the BG's own flow. Just wait where
//     RepopAtGraveyard already placed the bot (see docs/roles.md's death-handling notes for
//     what's confirmed vs. still-to-verify here).
void UpdateDeathHandling(Player* bot, uint32 diff, BotAIState& state)
{
    if (bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
    {
        state.awaitingRezGrace = false;

        if (bot->InBattleground())
            return;

        Corpse* corpse = bot->GetCorpse();
        if (!corpse)
            return;

        // A dungeon's nearest graveyard can be outside the instance entirely (confirmed live
        // in Deadmines: RepopAtGraveyard() sent the ghost to a Westfall graveyard on the
        // Eastern Kingdoms continent map while the corpse stayed on the instance map) --
        // walking toward the corpse's raw x/y/z would just wander the wrong map forever since
        // they're not even in the same coordinate space. BotMgr::TryReturnGhostToCorpseMap
        // handles getting the bot onto the corpse's map first (a real client would walk back
        // through the dungeon entrance themselves; a bot has nothing to walk through, same
        // reasoning as the leader-follow teleport) -- nothing to do here until that lands.
        if (corpse->GetMapId() != bot->GetMapId())
            return;

        if (bot->GetDistance(corpse) > CORPSE_RECLAIM_RADIUS)
        {
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                bot->GetMotionMaster()->MovePoint(0, corpse->GetPositionX(), corpse->GetPositionY(), corpse->GetPositionZ());
            return;
        }

        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();

        // The handler itself enforces the post-release reclaim delay and the exact-range
        // recheck -- harmless (and expected) to call this every tick until it actually lands.
        WorldPacket reclaimPacket;
        reclaimPacket << bot->GetGUID();
        bot->GetSession()->HandleReclaimCorpseOpcode(reclaimPacket);
        return;
    }

    if (bot->isResurrectRequested())
    {
        bot->ResurectUsingRequestData();
        state.awaitingRezGrace = false;
        return;
    }

    if (!state.awaitingRezGrace)
    {
        state.awaitingRezGrace = true;
        state.deathReleaseGraceMs = DEATH_REZ_GRACE_MS;
        return;
    }

    if (state.deathReleaseGraceMs > diff)
    {
        state.deathReleaseGraceMs -= diff;
        return;
    }

    WorldPacket repopPacket;
    repopPacket << uint8(0);
    bot->GetSession()->HandleRepopRequestOpcode(repopPacket);
}
}

namespace BotAI
{
void Update(Player* bot, uint32 diff)
{
    if (!bot || !bot->IsInWorld())
        return;

    BotAIState& state = states[bot->GetGUID()];

    if (state.suspended)
        return;

    if (!bot->IsAlive())
    {
        UpdateDeathHandling(bot, diff, state);
        return;
    }

    // Pull is one-shot: force the engage now, then fall through to normal role logic for the
    // resulting fight (UpdateOffensive/UpdateHealer/UpdateSupport all start from
    // bot->GetVictim(), which Attack() below just set) -- nothing else needs to know a Pull
    // ever happened.
    if (state.manualCommand == BotManualCommand::Pull)
    {
        if (Unit* target = ObjectAccessor::GetUnit(*bot, state.pullTargetGuid);
            target && target->IsAlive() && bot->IsValidAttackTarget(target))
            bot->Attack(target, true);
        state.manualCommand = BotManualCommand::None;
    }

    // If role has not been manually assigned, resolve automatically from active Ascension spec
    if (!state.manualRoleOverride)
    {
        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        state.role = GetRoleForClassSpec(bot->getClass(), activeSpec);
    }

    if (state.role == BotRole::Healer)
        UpdateHealer(bot, diff, state);
    else if (state.role == BotRole::Support)
        UpdateSupport(bot, diff, state);
    else
        UpdateOffensive(bot, diff, state.role, state);
}

void Forget(ObjectGuid botGuid)
{
    states.erase(botGuid);
}

void SetRole(ObjectGuid botGuid, BotRole role)
{
    BotAIState& state = states[botGuid];
    state.role = role;
    state.manualRoleOverride = true;
}

void ClearRoleOverride(ObjectGuid botGuid)
{
    auto itr = states.find(botGuid);
    if (itr != states.end())
    {
        itr->second.manualRoleOverride = false;
        if (Player* bot = ObjectAccessor::FindPlayer(botGuid))
        {
            uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
            itr->second.role = GetRoleForClassSpec(bot->getClass(), activeSpec);
        }
    }
}

void SetManualCommand(ObjectGuid botGuid, BotManualCommand command, ObjectGuid pullTarget)
{
    BotAIState& state = states[botGuid];
    state.manualCommand = command;
    state.pullTargetGuid = (command == BotManualCommand::Pull) ? pullTarget : ObjectGuid::Empty;
}

void StopAttack(ObjectGuid botGuid)
{
    BotAIState& state = states[botGuid];
    state.manualCommand = BotManualCommand::None;
    state.pullTargetGuid = ObjectGuid::Empty;

    if (Player* bot = ObjectAccessor::FindPlayer(botGuid))
        bot->AttackStop();
}

void SetSuspended(ObjectGuid botGuid, bool suspended)
{
    BotAIState& state = states[botGuid];
    state.suspended = suspended;
    if (suspended)
    {
        if (Player* bot = ObjectAccessor::FindPlayer(botGuid))
            bot->AttackStop();
    }
}

BotRole GetRole(ObjectGuid botGuid)
{
    auto itr = states.find(botGuid);
    if (itr != states.end() && itr->second.manualRoleOverride)
        return itr->second.role;

    if (Player* bot = ObjectAccessor::FindPlayer(botGuid))
    {
        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        return GetRoleForClassSpec(bot->getClass(), activeSpec);
    }

    return itr != states.end() ? itr->second.role : BotRole::Dps;
}

void ReportSpellbookRoleSignals(Player* bot, ChatHandler* handler)
{
    if (!bot)
        return;

    uint32 offensive = 0, taunt = 0, heal = 0, buff = 0;
    uint32 exampleOffensive = 0, exampleTaunt = 0, exampleHeal = 0, exampleBuff = 0;

    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (IsUsableOffensiveSpell(spellInfo))
        {
            ++offensive;
            if (!exampleOffensive)
                exampleOffensive = spellId;
        }
        if (IsUsableTauntSpell(spellInfo))
        {
            ++taunt;
            if (!exampleTaunt)
                exampleTaunt = spellId;
        }
        if (IsUsableHealSpell(spellInfo))
        {
            ++heal;
            if (!exampleHeal)
                exampleHeal = spellId;
        }
        if (IsUsableBuffSpell(spellInfo))
        {
            ++buff;
            if (!exampleBuff)
                exampleBuff = spellId;
        }
    }

    if (handler)
        handler->PSendSysMessage(
            "BotAI: '{}' spellbook role signals -- offensive={} (e.g. {}), taunt={} (e.g. {}), heal={} (e.g. {}), buff={} (e.g. {}).",
            bot->GetName(), offensive, exampleOffensive, taunt, exampleTaunt, heal, exampleHeal, buff, exampleBuff);
    LOG_INFO("module.coa-playerbots",
        "BotAI: '{}' (class {}) spellbook role signals -- offensive={} taunt={} heal={} buff={}.",
        bot->GetName(), uint32(bot->getClass()), offensive, taunt, heal, buff);
}
}
