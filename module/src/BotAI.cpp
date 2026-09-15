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
#include "BotClassRotations.h"
#include "BotClassRotationsBloodmage.h"
#include "BotClassRotationsChronomancer.h"
#include "BotClassRotationsFelsworn.h"
#include "BotClassRotationsNecromancer.h"
#include "BotClassRotationsPrimalist.h"
#include "BotClassRotationsReaper.h"
#include "BotClassRotationsRunemaster.h"
#include "BotClassRotationsStormbringer.h"
#include "BotClassRotationsXoroth.h"
#include "ClassSpecRoles.h"
#include "Bag.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Corpse.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DBCStructure.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Item.h"
#include "Log.h"
#include "LootMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ObjectMgr.h"
#include "PetDefines.h"
#include "Player.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <cmath>
#include <unordered_map>
#include <unordered_set>

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

    // See TryGrindWhenSolo -- the point an ungrouped bot wanders back to once it's fought its
    // way too far away, so a solo bot roughly stays in one area instead of drifting across the
    // whole zone one grind-chase at a time. Set the first time this bot is ever seen idle and
    // ungrouped; re-set if a map change (teleport/summon) makes the old one meaningless.
    bool hasGrindAnchor = false;
    uint32 grindAnchorMapId = 0;
    float grindAnchorX = 0.0f, grindAnchorY = 0.0f, grindAnchorZ = 0.0f;
    uint32 nextGrindScanMs = 0;

    // The most recent Unit this bot had as Unit::GetVictim(), remembered across the single tick
    // where that stops being true (killed, fled out of range, whatever) so TryAutoLootDeadTarget
    // has something to check -- by the time a tick sees GetVictim()==null, the fight is already
    // over and there's no other way to ask "what was I just fighting."
    ObjectGuid lastCombatTargetGuid;

    // See TryStartGathering/TryFinishGathering. Set once a gathering cast is actually underway;
    // empty means "not currently gathering anything."
    ObjectGuid gatherTargetGuid;
    uint32 nextGatherScanMs = 0;

    // Set by TryStartGathering while walking toward a node found by an earlier scan, cleared
    // once in range (see TryContinueGatherWalk). Deliberately separate from gatherTargetGuid
    // (the casting phase) and checked before ever falling through to TryGrindWhenSolo -- without
    // that separation, TryGrindWhenSolo's own "clear a stray POINT_MOTION_TYPE once back near my
    // anchor" cleanup fired on the *gather* walk instead (both reuse the same generic
    // POINT_MOTION_TYPE, and the gather scan's own 4-second throttle meant grinding ran on the
    // in-between ticks), cancelling the walk before the bot ever arrived -- confirmed live: the
    // node kept getting found fresh every scan, at a *different* distance each time, because the
    // bot kept being yanked back toward its grind anchor mid-walk instead of ever reaching it.
    ObjectGuid gatherWalkTargetGuid;

    // See TryStartFishing/TryWaitForFishingCast/TryFinishFishing. fishingCastInProgress covers
    // the real cast-time delay between casting Fishing and its bobber actually existing;
    // fishingBobberGuid then covers the (much longer, variable) wait for that bobber to reach
    // GO_READY once it does.
    bool fishingCastInProgress = false;
    ObjectGuid fishingBobberGuid;
    uint32 fishingTimeoutMs = 0;
    uint32 nextFishingScanMs = 0;
    // Grace window (see TryWaitForFishingCast) for the bobber to actually appear after
    // IsNonMeleeSpellCast first reports the Fishing cast as finished -- confirmed live that the
    // two don't land the same tick.
    uint32 fishingBobberGraceMs = 0;

    // See TryStartQuesting/TryContinueQuestWalk. No separate "in progress" state is needed
    // beyond this -- unlike gathering/fishing, accepting or turning in a quest is instant (no
    // cast time), so the walk is the only phase that can span more than one tick.
    ObjectGuid questWalkTargetGuid;
    uint32 nextQuestScanMs = 0;

    // See TryStartQuesting/TryContinueQuestObjectiveWalk. Separate from questWalkTargetGuid
    // (which is always a quest-giver Creature, processed via ProcessQuestGiver) because arriving
    // at a quest *objective* GameObject instead calls its own real Use() -- same "dedicated walk
    // guid per distinct arrival action" shape as gatherWalkTargetGuid vs. the grind anchor.
    ObjectGuid questObjectiveWalkTargetGuid;

    // See TryMaintainProgression -- shared throttle for both the mount-ownership check and the
    // gear-upgrade bag scan.
    uint32 nextProgressionCheckMs = 0;
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

// A grouped bot has a leader to fight alongside or follow; an ungrouped one has neither, and
// without this would just stand exactly where it was spawned forever. These four constants
// govern that solo behavior -- see TryGrindWhenSolo.
constexpr float GRIND_SEARCH_RADIUS = 30.0f;         // matches BotMgr::AttackNearestHostile's default
constexpr float GRIND_LEASH_RADIUS = 40.0f;          // don't drift farther than this from the anchor
constexpr uint32 GRIND_SCAN_INTERVAL_MS = 3000;      // idle behavior -- no need to grid-scan every tick
constexpr uint32 GRIND_MAX_LEVEL_ABOVE = 3;          // skip mobs this far above the bot's own level

// Herbalism/Mining nodes only, for this increment -- LOCKTYPE_PICKLOCK, LOCKTYPE_FISHING and
// LOCKTYPE_INSCRIPTION (SkillByLockType's other three mappings) are separate features
// (lockboxes, fishing's own water-finding geometry, and inscription's milling minigame
// respectively), not what "gathering" means here. These are the real WotLK gathering spell
// ids (confirmed against the client's own Spell.dbc: both have SPELL_EFFECT_OPEN_LOCK with
// EffectMiscValue 2/3 matching LOCKTYPE_HERBALISM/LOCKTYPE_MINING) -- casting one at a node is
// exactly what a real player's right-click on it does.
constexpr uint32 SPELL_HERB_GATHERING = 2366;
constexpr uint32 SPELL_MINING = 2575;
constexpr float GATHER_SEARCH_RADIUS = 30.0f;
constexpr uint32 GATHER_SCAN_INTERVAL_MS = 4000;

// Real WotLK Fishing (confirmed against Spell.dbc: EquippedItemClass/SubclassMask requires a
// fishing pole, Effect 0 is a summon-object effect whose EffectMiscValue is 35591 -- the real
// bobber GameObject template). Deliberately does not search for water the way it searches for
// hostiles/nodes -- FindNearbyWater only samples a ring around wherever the bot already is (see
// its own comment), so this only ever fires for a bot already standing near a lake/river, not
// one that goes looking for the nearest water in the zone.
constexpr uint32 SPELL_FISHING = 7620;
constexpr uint32 FISHING_BOBBER_ENTRY = 35591;
constexpr float FISHING_MIN_DISTANCE = 5.0f;
constexpr float FISHING_MAX_DISTANCE = 18.0f;
constexpr uint32 FISHING_SCAN_INTERVAL_MS = 6000;
constexpr uint32 FISHING_BITE_TIMEOUT_MS = 40000;

constexpr float QUEST_SEARCH_RADIUS = 20.0f;
constexpr uint32 QUEST_SCAN_INTERVAL_MS = 5000;

// How often TryMaintainProgression re-checks mount/gear state -- infrequent on purpose (a
// bot's spellbook/bags don't change fast enough to need per-tick scanning), shared by both
// checks it bundles so there's one throttle field, not two.
constexpr uint32 PROGRESSION_CHECK_INTERVAL_MS = 10000;

std::unordered_map<ObjectGuid, BotAIState> states;

// Real, verified (checked by parsing this server's own Spell.dbc byte-for-byte -- SpellName and
// EffectApplyAuraName fields, not recalled from memory) basic 60%-speed racial ground mount for
// each WotLK race. Used only to guarantee every bot owns *something* thematic to ride -- a bot
// that already knows a mount keeps using that (SelectKnownMountSpell always prefers whatever's
// already known), this is strictly a "never own zero mounts" floor, granted through the same
// real Player::learnSpell a trainer purchase or quest reward would call. Actually riding one for
// zone-to-zone travel is a follow-up (see AGENTS.md) -- this alone only guarantees the bot HAS a
// mount to use once that travel logic exists.
uint32 RacialGroundMountSpellFor(uint8 race)
{
    switch (race)
    {
        case RACE_HUMAN:         return 458;   // Brown Horse
        case RACE_ORC:           return 6654;  // Brown Wolf
        case RACE_DWARF:         return 6777;  // Gray Ram
        case RACE_NIGHTELF:      return 10793; // Striped Nightsaber
        case RACE_UNDEAD_PLAYER: return 13819; // Warhorse
        case RACE_TAUREN:        return 18990; // Brown Kodo
        case RACE_GNOME:         return 10873; // Red Mechanostrider
        case RACE_TROLL:         return 8395;  // Emerald Raptor
        case RACE_BLOODELF:      return 34795; // Red Hawkstrider
        case RACE_DRAENEI:       return 34406; // Brown Elekk
        default:                 return 0;
    }
}

bool IsMountSpell(SpellInfo const* spellInfo)
{
    return spellInfo && !spellInfo->IsPassive() && spellInfo->HasAura(SPELL_AURA_MOUNTED);
}

// Same "first known match wins" shape as SelectKnownSpell, but for whatever mount(s) this bot
// already knows -- doesn't rank multiple known mounts by speed (no CanFly-aware "best mount"
// logic yet), just "something to ride beats walking."
uint32 SelectKnownMountSpell(Player* bot)
{
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;
        if (IsMountSpell(sSpellMgr->GetSpellInfo(spellId)))
            return spellId;
    }
    return 0;
}

// A freshly-leveled or GM-created bot typically owns no mount at all.
void EnsureBotHasMount(Player* bot)
{
    if (SelectKnownMountSpell(bot))
        return;
    if (uint32 spellId = RacialGroundMountSpellFor(bot->getRace()))
        bot->learnSpell(spellId);
}

// Periodic, throttled bag scan for a gear upgrade -- same shape as TryMaintainBuff. Reuses the
// real engine's own authoritative "can this class/race actually equip this" check
// (Player::CanEquipItem, the same one WorldSession::HandleAutoEquipItemOpcode itself calls --
// covers armor-type proficiency, weapon-type proficiency, level requirement, everything) and
// Player::FindEquipSlot (the same real method that picks the correct slot for a two-hander, a
// ring, a trinket, etc). Deliberately does NOT attempt a per-class/per-spec stat-priority system
// -- that needs the same kind of dedicated per-class research as BotClassRotations.cpp, which
// doesn't exist yet for most of the 21 custom classes. ItemTemplate::ItemLevel is used as the
// single comparison metric instead: a real, always-present, designer-calibrated "how good is
// this overall" scalar -- a floor, not a ceiling, exactly like the rest of this file's
// class-agnostic heuristics.
void TryUpgradeGearOnce(Player* bot)
{
    auto considerItem = [&](Item* item) -> bool
    {
        if (!item)
            return false;
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return false;

        uint8 eslot = bot->FindEquipSlot(proto, NULL_SLOT, true);
        if (eslot == NULL_SLOT)
            return false;

        Item* current = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, eslot);
        if (current && current->GetTemplate()->ItemLevel >= proto->ItemLevel)
            return false; // not an upgrade over what's already worn there

        uint16 dest = uint16(eslot) | (uint16(INVENTORY_SLOT_BAG_0) << 8);
        if (bot->CanEquipItem(NULL_SLOT, dest, item, true) != EQUIP_ERR_OK)
            return false; // class/race/level can't actually use this one

        bot->SwapItem(item->GetPos(), dest);
        LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' equipped '{}' (ilvl {}) over slot {} (was ilvl {}).",
            bot->GetName(), proto->Name1, proto->ItemLevel, uint32(eslot), current ? current->GetTemplate()->ItemLevel : 0);
        return true;
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (considerItem(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)))
            return; // one upgrade per scan is plenty -- avoids repeated bag-order churn same tick

    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* pBag = bot->GetBagByPos(bag);
        if (!pBag)
            continue;
        for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            if (considerItem(pBag->GetItemByPos(j)))
                return;
    }
}

// Called every tick from BotAI::Update, unconditional on role/combat/group state (gearing up
// and owning a mount matter regardless of what else the bot is doing right now, same reasoning
// as TryMaintainBuff running independent of combat state) -- throttled internally so the actual
// bag scan and spellbook scan only run once every PROGRESSION_CHECK_INTERVAL_MS.
void TryMaintainProgression(Player* bot, uint32 diff, BotAIState& state)
{
    if (state.nextProgressionCheckMs > diff)
    {
        state.nextProgressionCheckMs -= diff;
        return;
    }
    state.nextProgressionCheckMs = PROGRESSION_CHECK_INTERVAL_MS;

    EnsureBotHasMount(bot);
    TryUpgradeGearOnce(bot);
}

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
        if (!bot->HasItemFitToSpellRequirements(spellInfo))
            continue;
        if (spellInfo->CasterAuraState && !bot->HasAuraState(AuraStateType(spellInfo->CasterAuraState)))
            continue;
        if (target && spellInfo->TargetAuraState && !target->HasAuraState(AuraStateType(spellInfo->TargetAuraState)))
            continue;
        if (spellInfo->CasterAuraSpell && !bot->HasAura(spellInfo->CasterAuraSpell))
            continue;
        if (target && spellInfo->TargetAuraSpell && !target->HasAura(spellInfo->TargetAuraSpell))
            continue;

        if (BotAI::IsSpellInFailureCooldown(bot->GetGUID(), spellId))
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
        if (minRange > 0.0f && bot->IsWithinRange(target, minRange + bot->GetMeleeRange(target)))
            continue;

        if (spellInfo->PowerType == POWER_HEALTH)
        {
            int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
            if (cost > 0 && bot->GetHealth() <= (uint32)cost)
                continue;
        }
        else
        {
            int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
            if (cost > 0 && bot->GetPower(Powers(spellInfo->PowerType)) < cost)
                continue;
        }

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

// Reads this bot's own quest log (Player::GetQuestSlotQuestId/GetQuestSlotCounter -- the same
// real quest-log data a client's own quest log window reads, not a synthetic re-derivation) to
// find which specific creatures and GameObjects still owe this bot kill/use credit toward an
// active quest (Quest::RequiredNpcOrGo: positive is a creature entry, negative is a GameObject
// entry -- QuestDef.h). Used to bias grind target selection toward creatures a quest actually
// wants dead (see GrindHostileUnitCheck/TryGrindWhenSolo) and to find quest objects worth
// walking to and using (see QuestObjectiveGoCheck/TryStartQuesting). Without this, a solo bot's
// grind only ever kills whatever's nearest, so kill/collect quests only progress by luck when
// the right creature happens to wander by -- kill/loot credit itself already fires for free via
// the normal engine paths (Unit::Kill -> RewardPlayerAndGroupAtKill -> KilledMonsterCredit, and
// Player::StoreItem's own ItemAddedQuestCheck) the instant a bot lands the real kill or loot,
// same as any other player; the only missing piece was ever choosing the right target.
void CollectQuestObjectiveEntries(Player* bot, std::unordered_set<uint32>& wantedCreatures, std::unordered_set<uint32>& wantedGameObjects)
{
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (!questId || bot->GetQuestStatus(questId) != QUEST_STATUS_INCOMPLETE)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            int32 entry = quest->RequiredNpcOrGo[i];
            if (!entry || bot->GetQuestSlotCounter(slot, i) >= quest->RequiredNpcOrGoCount[i])
                continue; // no such objective slot, or already satisfied

            if (entry > 0)
                wantedCreatures.insert(uint32(entry));
            else
                wantedGameObjects.insert(uint32(-entry));
        }
    }
}

// Same shrinking-radius nearest-hostile shape as BotMgr::AttackNearestHostile's own
// NearestHostileUnitInObjectRangeCheck, plus a level cap that check doesn't need: a directed
// `.botcmd attack` trusts whatever the caller aimed it at, but a bot picking its own fights
// while nobody's around to notice it dying must not pick something far above its own level.
// Optionally restricted to a specific set of creature entries (see CollectQuestObjectiveEntries)
// -- TryGrindWhenSolo runs this twice, once with the bot's own wanted-kill entries to prefer a
// quest target over a random one, then again unrestricted as the normal fallback.
class GrindHostileUnitCheck
{
public:
    GrindHostileUnitCheck(Unit const* me, float range, uint32 maxLevel, std::unordered_set<uint32> const* requiredEntries = nullptr)
        : _me(me), _range(range), _maxLevel(maxLevel), _requiredEntries(requiredEntries) { }
    bool operator()(Unit* u)
    {
        if (_requiredEntries && !_requiredEntries->count(u->GetEntry()))
            return false;
        if (!_me->IsWithinDistInMap(u, _range, true, false, false))
            return false;
        if (!_me->IsValidAttackTarget(u))
            return false;
        if (u->GetLevel() > _maxLevel)
            return false;
        _range = _me->GetDistance(u); // shrink the search radius to the closest hit so far
        return true;
    }

private:
    Unit const* _me;
    float _range;
    uint32 _maxLevel;
    std::unordered_set<uint32> const* _requiredEntries;
};

// Real "open, take everything, release" loot flow for a bot's own solo kill -- same
// "synthesize the packet, call the real handler" pattern already used elsewhere in this module
// (group-accept, corpse-reclaim, loot-roll). Without this, a solo bot's kills (from
// TryGrindWhenSolo or anything else) would die and just sit there unlooted forever, since
// nothing else in this module ever opens a loot window at all.
//
// Deliberately solo-only: a grouped kill's loot follows the group's loot method (round-robin,
// master loot, need/greed) -- BotMgr::DoRollGreed already handles the one piece of that this
// module deals with (rare-item rolls); normal group kill loot is a separate, unimplemented
// question this doesn't attempt to answer.
void TryAutoLootDeadTarget(Player* bot, BotAIState& state)
{
    ObjectGuid targetGuid = state.lastCombatTargetGuid;
    state.lastCombatTargetGuid = ObjectGuid::Empty; // one-shot regardless of outcome below

    if (targetGuid.IsEmpty() || bot->GetGroup())
        return;

    Creature* creature = ObjectAccessor::GetCreature(*bot, targetGuid);
    if (!creature || creature->IsAlive() || !creature->IsWithinDistInMap(bot, INTERACTION_DISTANCE))
        return;
    if (creature->loot.isLooted() || creature->GetLootRecipientGUID() != bot->GetGUID())
        return;

    WorldPacket openPacket;
    openPacket << creature->GetGUID();
    bot->GetSession()->HandleLootOpcode(openPacket);

    if (bot->GetLootGUID() != creature->GetGUID())
        return; // handler declined to open it (out of range/not the recipient/etc.)

    Loot& loot = creature->loot;
    for (uint8 slot = 0; slot < loot.items.size(); ++slot)
    {
        WorldPacket storePacket;
        storePacket << slot;
        bot->GetSession()->HandleAutostoreLootItemOpcode(storePacket);
    }
    if (loot.gold)
    {
        WorldPacket moneyPacket;
        bot->GetSession()->HandleLootMoneyOpcode(moneyPacket);
    }

    WorldPacket releasePacket;
    releasePacket << creature->GetGUID();
    bot->GetSession()->HandleLootReleaseOpcode(releasePacket);

    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' auto-looted '{}'.", bot->GetName(), creature->GetName());
}

// Called from UpdateOffensive's idle branch, only for a bot with no group at all (a grouped
// bot follows/assists its leader instead -- see ResumeFollowingLeader). Mirrors a real solo
// player: look for a nearby fight, and don't wander far from one spot doing it. Manual Stay
// already excludes this at the call site, same as it excludes inheriting the leader's target.
void TryGrindWhenSolo(Player* bot, uint32 diff, BotAIState& state)
{
    if (!state.hasGrindAnchor || state.grindAnchorMapId != bot->GetMapId())
    {
        // First time this bot's been seen idle and ungrouped, or the old anchor was on a map
        // this bot isn't even on anymore (teleported/summoned elsewhere) -- anchor here instead
        // of trying to walk back across two different maps.
        state.hasGrindAnchor = true;
        state.grindAnchorMapId = bot->GetMapId();
        state.grindAnchorX = bot->GetPositionX();
        state.grindAnchorY = bot->GetPositionY();
        state.grindAnchorZ = bot->GetPositionZ();
        return;
    }

    if (bot->GetDistance(state.grindAnchorX, state.grindAnchorY, state.grindAnchorZ) > GRIND_LEASH_RADIUS)
    {
        // A grind-chase can drag a bot well past its anchor by the time the fight ends --
        // walk back before looking for another one, same idea as ResumeFollowingLeader walking
        // a grouped bot back to its leader.
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
            bot->GetMotionMaster()->MovePoint(0, state.grindAnchorX, state.grindAnchorY, state.grindAnchorZ);
        return;
    }

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        bot->GetMotionMaster()->Clear();

    if (state.nextGrindScanMs > diff)
    {
        state.nextGrindScanMs -= diff;
        return;
    }
    state.nextGrindScanMs = GRIND_SCAN_INTERVAL_MS;

    Unit* target = nullptr;

    // Prefer a creature an active quest actually wants dead over a random one, when there's a
    // choice -- see CollectQuestObjectiveEntries's own comment for why this is the only piece
    // missing for kill/collect quests to progress on their own.
    std::unordered_set<uint32> wantedCreatures, wantedGameObjects;
    CollectQuestObjectiveEntries(bot, wantedCreatures, wantedGameObjects);
    if (!wantedCreatures.empty())
    {
        GrindHostileUnitCheck questCheck(bot, GRIND_SEARCH_RADIUS, bot->GetLevel() + GRIND_MAX_LEVEL_ABOVE, &wantedCreatures);
        Acore::UnitLastSearcher<GrindHostileUnitCheck> questSearcher(bot, target, questCheck);
        Cell::VisitObjects(bot, questSearcher, GRIND_SEARCH_RADIUS);
    }

    if (!target)
    {
        GrindHostileUnitCheck check(bot, GRIND_SEARCH_RADIUS, bot->GetLevel() + GRIND_MAX_LEVEL_ABOVE);
        Acore::UnitLastSearcher<GrindHostileUnitCheck> searcher(bot, target, check);
        Cell::VisitObjects(bot, searcher, GRIND_SEARCH_RADIUS);
    }

    if (target)
        bot->Attack(target, true);
}

// Maps a gathering skill to the real spell that gathers it -- returns 0 for anything else
// SkillByLockType can produce (lockpicking/fishing/inscription aren't "gathering" here).
uint32 GatheringSpellFor(SkillType skillId)
{
    if (skillId == SKILL_HERBALISM)
        return SPELL_HERB_GATHERING;
    if (skillId == SKILL_MINING)
        return SPELL_MINING;
    return 0;
}

// The real eligibility check Spell::CanOpenLock itself makes at cast time (Spell.cpp), against
// one already-known GameObject -- shared by GatherableNodeCheck (deciding what's worth walking
// toward across a whole area scan) and TryContinueGatherWalk (re-checking this exact node once
// the bot has actually arrived). Returns 0 if this bot's skill can't open it (or it isn't a
// herbalism/mining node at all).
uint32 GatherSpellForNode(Player const* bot, GameObject* go)
{
    LockEntry const* lockInfo = sLockStore.LookupEntry(go->GetGOInfo()->GetLockId());
    if (!lockInfo)
        return 0;

    for (uint8 i = 0; i < MAX_LOCK_CASE; ++i)
    {
        if (lockInfo->Type[i] != LOCK_KEY_SKILL)
            continue;

        SkillType skillId = SkillByLockType(LockType(lockInfo->Index[i]));
        uint32 spellId = GatheringSpellFor(skillId);
        if (spellId && uint32(bot->GetSkillValue(skillId)) >= lockInfo->Skill[i])
            return spellId;
    }
    return 0;
}

// Same shrinking-radius nearest-in-range shape as GrindHostileUnitCheck, but for a lockable
// herbalism/mining node this bot's own skill can actually open. Fills in gatherSpellId with
// whichever of the two gathering spells applies to whatever node is found.
class GatherableNodeCheck
{
public:
    GatherableNodeCheck(Player const* bot, float range, uint32& gatherSpellId)
        : _bot(bot), _range(range), _gatherSpellId(gatherSpellId) { }

    bool operator()(GameObject* go)
    {
        if (!go->isSpawned() || !_bot->IsWithinDistInMap(go, _range))
            return false;

        uint32 spellId = GatherSpellForNode(_bot, go);
        if (!spellId)
            return false;

        _gatherSpellId = spellId;
        _range = _bot->GetDistance(go); // shrink the search radius to the closest hit so far
        return true;
    }

private:
    Player const* _bot;
    float _range;
    uint32& _gatherSpellId;
};

// Checked every idle-solo tick while a gathering cast (started by TryStartGathering) is
// underway. Player::IsNonMeleeSpellCast reflects the real, skill-adjusted cast time --  not
// instant -- so this just waits for it to actually finish before deciding what happened.
// Success or failure, EffectOpenLock (Spell.cpp) already did the real work: on success it
// calls SendLoot itself, exactly as if this were HandleLootOpcode; on failure (interrupted,
// moved out of range mid-cast, etc.) bot->GetLootGUID() simply never becomes the node's guid
// and this quietly does nothing. Same "open, take everything, release" shape as
// TryAutoLootDeadTarget, just against the node's own Loot instead of a creature's, and with no
// separate open step needed since the spell effect already did that part.
void TryFinishGathering(Player* bot, BotAIState& state)
{
    if (bot->IsNonMeleeSpellCast(false))
        return; // still casting -- check again next tick

    ObjectGuid nodeGuid = state.gatherTargetGuid;
    state.gatherTargetGuid = ObjectGuid::Empty; // one-shot regardless of outcome below

    GameObject* node = ObjectAccessor::GetGameObject(*bot, nodeGuid);
    if (!node || bot->GetLootGUID() != node->GetGUID() || node->loot.isLooted())
        return;

    Loot& loot = node->loot;
    for (uint8 slot = 0; slot < loot.items.size(); ++slot)
    {
        WorldPacket storePacket;
        storePacket << slot;
        bot->GetSession()->HandleAutostoreLootItemOpcode(storePacket);
    }

    WorldPacket releasePacket;
    releasePacket << node->GetGUID();
    bot->GetSession()->HandleLootReleaseOpcode(releasePacket);

    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' gathered from node {}.", bot->GetName(), node->GetEntry());
}

// Checked every idle-solo tick while state.gatherWalkTargetGuid is set (a node TryStartGathering
// found but was too far to cast at yet). Deliberately does NOT touch movement while still out of
// range -- the MovePoint TryStartGathering already issued keeps running on its own; re-checking
// or re-issuing it here was the original bug (see gatherWalkTargetGuid's own comment in
// BotAIState). Once in range, casts exactly like TryStartGathering's own immediate-range branch
// would have.
void TryContinueGatherWalk(Player* bot, BotAIState& state)
{
    GameObject* node = ObjectAccessor::GetGameObject(*bot, state.gatherWalkTargetGuid);
    if (!node || !node->isSpawned())
    {
        state.gatherWalkTargetGuid = ObjectGuid::Empty;
        return;
    }

    if (bot->GetDistance(node) > INTERACTION_DISTANCE)
        return; // still walking -- nothing to do until it arrives or the caller re-decides

    state.gatherWalkTargetGuid = ObjectGuid::Empty;

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        bot->GetMotionMaster()->Clear();

    uint32 gatherSpellId = GatherSpellForNode(bot, node);
    if (!gatherSpellId)
        return; // shouldn't normally change mid-walk, but the node/skill match is re-verified anyway

    SpellCastResult result = bot->CastSpell(node, gatherSpellId, false);
    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' cast gathering spell {} on node {} (result {}).",
        bot->GetName(), gatherSpellId, node->GetEntry(), uint32(result));
    if (result == SPELL_CAST_OK)
        state.gatherTargetGuid = node->GetGUID();
}

// Called from the idle-solo routine (same scope as TryGrindWhenSolo), only when no gathering
// cast or walk is already in progress. Scans for a nearby eligible node and either casts the
// real gathering spell at it immediately (already in range) or starts walking toward it and
// hands off to TryContinueGatherWalk for the rest -- either way, exactly what a player's own
// right-click on the node would trigger. Returns true if it did anything at all (moving counts,
// not just casting) so the caller knows not to also try grinding this tick.
bool TryStartGathering(Player* bot, uint32 diff, BotAIState& state)
{
    if (state.nextGatherScanMs > diff)
    {
        state.nextGatherScanMs -= diff;
        return false;
    }
    state.nextGatherScanMs = GATHER_SCAN_INTERVAL_MS;

    uint32 gatherSpellId = 0;
    GameObject* node = nullptr;
    GatherableNodeCheck check(bot, GATHER_SEARCH_RADIUS, gatherSpellId);
    Acore::GameObjectLastSearcher<GatherableNodeCheck> searcher(bot, node, check);
    Cell::VisitObjects(bot, searcher, GATHER_SEARCH_RADIUS);

    if (!node || !gatherSpellId)
        return false;

    if (bot->GetDistance(node) > INTERACTION_DISTANCE)
    {
        state.gatherWalkTargetGuid = node->GetGUID();
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
            bot->GetMotionMaster()->MovePoint(0, node->GetPositionX(), node->GetPositionY(), node->GetPositionZ());
        return true;
    }

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        bot->GetMotionMaster()->Clear();

    SpellCastResult result = bot->CastSpell(node, gatherSpellId, false);
    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' cast gathering spell {} on node {} (result {}).",
        bot->GetName(), gatherSpellId, node->GetEntry(), uint32(result));
    if (result == SPELL_CAST_OK)
        state.gatherTargetGuid = node->GetGUID();
    return true;
}

bool IsFishingPole(Item const* item)
{
    if (!item)
        return false;
    ItemTemplate const* proto = item->GetTemplate();
    return proto && proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE;
}

// Real Fishing (7620) is gated on EquippedItemClass/SubclassMask like any weapon-restricted
// spell -- casting it at all requires a fishing pole in the mainhand slot first. Only equips one
// the bot already owns (bags checked, then equipped bags); doesn't acquire one from a vendor.
// bot->SwapItem is the same real Player method WorldSession::HandleAutoEquipItemSlotOpcode
// itself calls once it's unpacked the client's packet -- calling it directly skips synthesizing
// a packet for a case with no other validation worth reusing.
bool EnsureFishingPoleEquipped(Player* bot)
{
    if (IsFishingPole(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND)))
        return true;

    uint16 mainHandDst = uint16(EQUIPMENT_SLOT_MAINHAND) | (uint16(INVENTORY_SLOT_BAG_0) << 8);

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
    {
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot); IsFishingPole(item))
        {
            bot->SwapItem(item->GetPos(), mainHandDst);
            return true;
        }
    }
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* pBag = bot->GetBagByPos(bag);
        if (!pBag)
            continue;
        for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
        {
            if (Item* item = pBag->GetItemByPos(j); IsFishingPole(item))
            {
                bot->SwapItem(item->GetPos(), mainHandDst);
                return true;
            }
        }
    }
    return false;
}

// Deliberately simple compared to mod-playerbots' own FishingAction, which walks the shoreline
// to find water anywhere nearby -- this only samples a ring of points at the real spell's own
// casting distance around wherever the bot already is. Good enough for "already standing near a
// lake/river" (which idle solo bots will be plenty of the time just by existing in most outdoor
// zones), not "go seek out the nearest water in the zone."
bool FindNearbyWater(Player* bot, float& outX, float& outY, float& outZ)
{
    Map* map = bot->GetMap();
    uint32 phaseMask = bot->GetPhaseMask();
    float bx = bot->GetPositionX(), by = bot->GetPositionY(), bz = bot->GetPositionZ();

    for (float dist : {FISHING_MAX_DISTANCE, (FISHING_MIN_DISTANCE + FISHING_MAX_DISTANCE) / 2.0f, FISHING_MIN_DISTANCE})
    {
        for (int i = 0; i < 16; ++i)
        {
            float angle = (2.0f * float(M_PI) * i) / 16.0f;
            float x = bx + dist * std::cos(angle);
            float y = by + dist * std::sin(angle);

            LiquidData liquid = map->GetLiquidData(phaseMask, x, y, bz + 2.0f, 2.0f, std::nullopt);
            if (liquid.Status == LIQUID_MAP_NO_WATER)
                continue;
            if (!bot->IsWithinLOS(x, y, liquid.Level))
                continue;

            outX = x;
            outY = y;
            outZ = liquid.Level;
            return true;
        }
    }
    return false;
}

// Checked every idle-solo tick while state.fishingBobberGuid is set. The bobber (spawned by
// Fishing's own summon-object effect once the cast in TryStartFishing actually finishes) reaches
// GO_READY once something bites -- using it then is exactly what a real player's click on it
// does, and loots via the same open/store/release shape as everything else in this file. Gives
// up after FISHING_BITE_TIMEOUT_MS with nothing (a real player would eventually reel in and
// recast too, rather than wait forever).
void TryFinishFishing(Player* bot, uint32 diff, BotAIState& state)
{
    GameObject* bobber = ObjectAccessor::GetGameObject(*bot, state.fishingBobberGuid);
    if (!bobber || bobber->GetOwnerGUID() != bot->GetGUID())
    {
        state.fishingBobberGuid = ObjectGuid::Empty;
        return;
    }

    if (bobber->getLootState() != GO_READY)
    {
        if (state.fishingTimeoutMs <= diff)
            state.fishingBobberGuid = ObjectGuid::Empty;
        else
            state.fishingTimeoutMs -= diff;
        return;
    }

    state.fishingBobberGuid = ObjectGuid::Empty;
    bobber->Use(bot);

    if (bot->GetLootGUID() != bobber->GetGUID())
        return;

    Loot& loot = bobber->loot;
    for (uint8 slot = 0; slot < loot.items.size(); ++slot)
    {
        WorldPacket storePacket;
        storePacket << slot;
        bot->GetSession()->HandleAutostoreLootItemOpcode(storePacket);
    }

    WorldPacket releasePacket;
    releasePacket << bobber->GetGUID();
    bot->GetSession()->HandleLootReleaseOpcode(releasePacket);

    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' caught something fishing.", bot->GetName());
}

// Checked every idle-solo tick while state.fishingCastInProgress is set -- waits for the real,
// skill-adjusted cast time from TryStartFishing's CastSpell to actually finish (the bobber
// doesn't exist as a GameObject until the cast's own effect summons it, so there's nothing to
// look for before then), then scans for it by entry+ownership rather than assuming any
// particular guid.
void TryWaitForFishingCast(Player* bot, uint32 diff, BotAIState& state)
{
    if (bot->IsNonMeleeSpellCast(false))
        return; // still casting -- check again next tick

    std::list<GameObject*> nearby;
    Acore::AllGameObjectsWithEntryInRange check(bot, FISHING_BOBBER_ENTRY, FISHING_MAX_DISTANCE + 5.0f);
    Acore::GameObjectListSearcher<Acore::AllGameObjectsWithEntryInRange> searcher(bot, nearby, check);
    Cell::VisitObjects(bot, searcher, FISHING_MAX_DISTANCE + 5.0f);

    LOG_INFO("module.coa-playerbots", "BotAI DEBUG: bot '{}' bobber search found {} candidate(s), botGuid={}.",
        bot->GetName(), nearby.size(), bot->GetGUID().ToString());
    for (GameObject* go : nearby)
        LOG_INFO("module.coa-playerbots", "BotAI DEBUG:   candidate owner={} state={} spawned={}.",
            go->GetOwnerGUID().ToString(), uint32(go->getLootState()), go->isSpawned());

    for (GameObject* go : nearby)
    {
        if (go->GetOwnerGUID() == bot->GetGUID())
        {
            state.fishingCastInProgress = false;
            state.fishingBobberGuid = go->GetGUID();
            state.fishingTimeoutMs = FISHING_BITE_TIMEOUT_MS;
            return;
        }
    }

    // Not found yet -- IsNonMeleeSpellCast(false) going false and the summon-object effect that
    // actually creates the bobber GameObject don't land on the same tick (confirmed live: every
    // attempt failed here on the very first check). Keep checking for a couple of seconds before
    // concluding the cast genuinely failed after CastSpell's own initial OK (interrupted, moved,
    // whatever) rather than giving up on the first miss.
    constexpr uint32 BOBBER_APPEAR_GRACE_MS = 2000;
    if (state.fishingBobberGraceMs == 0)
        state.fishingBobberGraceMs = BOBBER_APPEAR_GRACE_MS;
    else if (state.fishingBobberGraceMs <= diff)
    {
        state.fishingCastInProgress = false;
        state.fishingBobberGraceMs = 0;
    }
    else
        state.fishingBobberGraceMs -= diff;
}

// Called from the idle-solo routine, only when nothing else (gathering, an in-progress fishing
// cast or bite-wait) is already claiming this tick. Only ever fires for a bot already near open
// water (see FindNearbyWater) with a fishing pole somewhere in its own bags -- faces the water
// and casts the real spell exactly like a player's own cast would, then hands off to
// TryWaitForFishingCast for the rest.
bool TryStartFishing(Player* bot, uint32 diff, BotAIState& state)
{
    if (state.nextFishingScanMs > diff)
    {
        state.nextFishingScanMs -= diff;
        return false;
    }
    state.nextFishingScanMs = FISHING_SCAN_INTERVAL_MS;

    float waterX = 0.0f, waterY = 0.0f, waterZ = 0.0f;
    if (!FindNearbyWater(bot, waterX, waterY, waterZ))
        return false;

    if (!EnsureFishingPoleEquipped(bot))
        return false;

    bot->SetOrientation(bot->GetAngle(waterX, waterY));

    SpellCastResult result = bot->CastSpell(bot, SPELL_FISHING, false);
    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' cast Fishing (result {}).", bot->GetName(), uint32(result));
    if (result != SPELL_CAST_OK)
        return false;

    state.fishingCastInProgress = true;
    return true;
}

// Same shrinking-radius nearest-in-range shape as every other search in this file, but for a
// quest-giver creature that currently has something for this bot -- the exact same DIALOG_STATUS
// check that drives the real "!"/"?" minimap icons (Player::GetQuestDialogStatus), checked here
// just to decide who's worth walking to. GameObject quest givers (mailboxes, some quest chains'
// item-triggered turn-ins) aren't covered -- creatures are the large majority of quest givers,
// and this is deliberately the simpler slice for a first pass.
class QuestGiverCheck
{
public:
    QuestGiverCheck(Player* bot, float range) : _bot(bot), _range(range) { }
    bool operator()(Creature* creature)
    {
        if (!creature->IsAlive() || !creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
            return false;
        if (!_bot->IsWithinDistInMap(creature, _range))
            return false;

        switch (_bot->GetQuestDialogStatus(creature))
        {
            case DIALOG_STATUS_REWARD:
            case DIALOG_STATUS_REWARD2:
            case DIALOG_STATUS_REWARD_REP:
            case DIALOG_STATUS_AVAILABLE:
            case DIALOG_STATUS_AVAILABLE_REP:
                break;
            default:
                return false; // nothing for this bot here right now
        }

        _range = _bot->GetDistance(creature); // shrink the search radius to the closest hit so far
        return true;
    }

private:
    Player* _bot;
    float _range;
};

// Same shrinking-radius shape as GatherableNodeCheck, but for an in-world GameObject an active
// quest still wants used (see CollectQuestObjectiveEntries) -- deliberately restricted to
// GAMEOBJECT_TYPE_GOOBER, the real WotLK "click this lever/mechanism/pile of goo" quest-object
// shape: GameObject::Use's own GOOBER case (GameObject.cpp) already calls
// player->KillCreditGO(...) itself once used, exactly like a real client's right-click, so
// nothing beyond calling the real Use() is needed for this type. GAMEOBJECT_TYPE_CHEST quest
// objectives (a lootable box, not a "use" trigger) are deliberately NOT matched here --
// GameObject::Use() has no case for CHEST at all (confirmed reading its switch in
// GameObject.cpp: it falls to `default:` and silently does nothing), so pathing a bot up to one
// would just get it stuck retrying forever with nothing to show for it. Those, plus every other
// objective shape this file doesn't attempt (escort, explore, dialogue), fall back to the
// `.quest complete`/`.quest reward` GM commands (both already RA-console accessible) as the
// deliberate manual escape hatch for a bot that can't make progress any other way.
class QuestObjectiveGoCheck
{
public:
    QuestObjectiveGoCheck(Player const* bot, float range, std::unordered_set<uint32> const& wantedEntries)
        : _bot(bot), _range(range), _wantedEntries(wantedEntries) { }

    bool operator()(GameObject* go)
    {
        if (go->GetGoType() != GAMEOBJECT_TYPE_GOOBER || !_wantedEntries.count(go->GetEntry()))
            return false;
        if (!go->isSpawned() || !_bot->IsWithinDistInMap(go, _range))
            return false;

        _range = _bot->GetDistance(go); // shrink the search radius to the closest hit so far
        return true;
    }

private:
    Player const* _bot;
    float _range;
    std::unordered_set<uint32> const& _wantedEntries;
};

// No attempt at "best" reward scoring here (mod-playerbots' own StatsWeightCalculator does that
// properly) -- a solo bot with nobody to ask just takes the first choice. Deliberately the
// simplest thing that unblocks turning the quest in at all, not an optimal pick.
uint32 PickQuestRewardIndex(Quest const* /*quest*/)
{
    return 0;
}

// Turns in every quest this questgiver has ready for the bot, then accepts every quest it's
// currently offering -- both via the same real Player methods a client's own quest-dialog
// clicks ultimately call (CanRewardQuest/RewardQuest, CanTakeQuest/AddQuest), not packet
// simulation. A quest's own start spell (if any) is cast exactly like a real accept would
// trigger it.
void ProcessQuestGiver(Player* bot, Creature* npc)
{
    QuestRelationBounds involved = sObjectMgr->GetCreatureQuestInvolvedRelationBounds(npc->GetEntry());
    for (auto itr = involved.first; itr != involved.second; ++itr)
    {
        uint32 questId = itr->second;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest || bot->GetQuestStatus(questId) == QUEST_STATUS_NONE || bot->GetQuestRewardStatus(questId))
            continue;

        // Some quests (go here, report to X) only ever reach QUEST_STATUS_COMPLETE via this
        // call -- a real client's quest-giver dialog triggers it implicitly on open, this is
        // the direct equivalent.
        if (bot->CanCompleteQuest(questId))
            bot->CompleteQuest(questId);
        if (bot->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
            continue;
        if (!bot->CanRewardQuest(quest, false))
            continue;

        bot->RewardQuest(quest, PickQuestRewardIndex(quest), npc, true);
        LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' turned in quest {} ('{}').",
            bot->GetName(), questId, quest->GetTitle());
    }

    QuestRelationBounds offered = sObjectMgr->GetCreatureQuestRelationBounds(npc->GetEntry());
    for (auto itr = offered.first; itr != offered.second; ++itr)
    {
        uint32 questId = itr->second;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest || bot->GetQuestStatus(questId) != QUEST_STATUS_NONE)
            continue;
        if (!bot->CanTakeQuest(quest, false) || !bot->CanAddQuest(quest, false))
            continue;

        bot->AddQuest(quest, npc);
        if (uint32 srcSpell = quest->GetSrcSpell())
            bot->CastSpell(bot, srcSpell, true);

        LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' accepted quest {} ('{}').",
            bot->GetName(), questId, quest->GetTitle());
    }
}

// Checked every idle-solo tick while state.questWalkTargetGuid is set -- mirrors
// TryContinueGatherWalk exactly (see its own comment for why a separate walk-tracking guid,
// not the grind anchor's POINT_MOTION_TYPE, is required). Once in range,
// Player::GetNPCIfCanInteractWith re-validates distance/alive/flag exactly like a real client's
// own interaction check before actually processing the quest giver.
void TryContinueQuestWalk(Player* bot, BotAIState& state)
{
    Creature* npc = ObjectAccessor::GetCreature(*bot, state.questWalkTargetGuid);
    if (!npc || !npc->IsAlive())
    {
        state.questWalkTargetGuid = ObjectGuid::Empty;
        return;
    }

    if (bot->GetDistance(npc) > INTERACTION_DISTANCE)
        return; // still walking

    state.questWalkTargetGuid = ObjectGuid::Empty;

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        bot->GetMotionMaster()->Clear();

    if (Creature* validated = bot->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_QUESTGIVER))
        ProcessQuestGiver(bot, validated);
}

// Checked every idle-solo tick while state.questObjectiveWalkTargetGuid is set -- mirrors
// TryContinueGatherWalk/TryContinueQuestWalk exactly (see gatherWalkTargetGuid's own comment for
// why a dedicated walk-tracking guid, not the grind anchor's own POINT_MOTION_TYPE, is
// required). Once in range, calls the object's own real Use() -- see QuestObjectiveGoCheck's
// comment for why this alone is enough for a GOOBER-type quest object.
void TryContinueQuestObjectiveWalk(Player* bot, BotAIState& state)
{
    GameObject* go = ObjectAccessor::GetGameObject(*bot, state.questObjectiveWalkTargetGuid);
    if (!go || !go->isSpawned())
    {
        state.questObjectiveWalkTargetGuid = ObjectGuid::Empty;
        return;
    }

    if (bot->GetDistance(go) > INTERACTION_DISTANCE)
        return; // still walking

    state.questObjectiveWalkTargetGuid = ObjectGuid::Empty;

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        bot->GetMotionMaster()->Clear();

    go->Use(bot);
    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' used quest object (entry {}).", bot->GetName(), go->GetEntry());
}

// Called from the idle-solo routine, only when no gather/fish activity is already claiming this
// tick and no quest walk is already underway. Scans for a nearby quest giver with something for
// this bot and either processes it immediately (already in range) or walks to it first -- and if
// no quest giver has anything right now, falls back to looking for an in-world quest object an
// active quest still wants used (see QuestObjectiveGoCheck) -- exactly the same "scan, walk if
// needed, act" shape as gathering, just with two possible kinds of target.
bool TryStartQuesting(Player* bot, uint32 diff, BotAIState& state)
{
    if (state.nextQuestScanMs > diff)
    {
        state.nextQuestScanMs -= diff;
        return false;
    }
    state.nextQuestScanMs = QUEST_SCAN_INTERVAL_MS;

    Creature* npc = nullptr;
    QuestGiverCheck giverCheck(bot, QUEST_SEARCH_RADIUS);
    Acore::CreatureLastSearcher<QuestGiverCheck> giverSearcher(bot, npc, giverCheck);
    Cell::VisitObjects(bot, giverSearcher, QUEST_SEARCH_RADIUS);

    if (npc)
    {
        if (bot->GetDistance(npc) > INTERACTION_DISTANCE)
        {
            state.questWalkTargetGuid = npc->GetGUID();
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
                bot->GetMotionMaster()->MovePoint(0, npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ());
            return true;
        }

        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();

        if (Creature* validated = bot->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_QUESTGIVER))
            ProcessQuestGiver(bot, validated);
        return true;
    }

    // Nothing for a quest giver to do right now -- see if an active quest still needs an
    // in-world object used instead.
    std::unordered_set<uint32> wantedCreatures, wantedGameObjects;
    CollectQuestObjectiveEntries(bot, wantedCreatures, wantedGameObjects);
    if (wantedGameObjects.empty())
        return false;

    GameObject* go = nullptr;
    QuestObjectiveGoCheck objectiveCheck(bot, QUEST_SEARCH_RADIUS, wantedGameObjects);
    Acore::GameObjectLastSearcher<QuestObjectiveGoCheck> objectiveSearcher(bot, go, objectiveCheck);
    Cell::VisitObjects(bot, objectiveSearcher, QUEST_SEARCH_RADIUS);

    if (!go)
        return false;

    if (bot->GetDistance(go) > INTERACTION_DISTANCE)
    {
        state.questObjectiveWalkTargetGuid = go->GetGUID();
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE)
            bot->GetMotionMaster()->MovePoint(0, go->GetPositionX(), go->GetPositionY(), go->GetPositionZ());
        return true;
    }

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        bot->GetMotionMaster()->Clear();

    go->Use(bot);
    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' used quest object (entry {}).", bot->GetName(), go->GetEntry());
    return true;
}

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

SpellCastResult LogCastAttempt(Player* bot, uint32 spellId, Unit* target, char const* verb)
{
    if (target && target != bot)
    {
        bot->SetInFront(target);
        bot->SetFacingToObject(target);
    }
    SpellCastResult result = bot->CastSpell(target, spellId, false);
    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' {} spell {} on '{}' (result {}).",
        bot->GetName(), verb, spellId, target->GetName(), uint32(result));
    if (result != SPELL_CAST_OK)
        BotAI::RecordSpellCastFailure(bot->GetGUID(), spellId);
    return result;
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

    // Remembered every tick there's a real, live fight, so the tick right after it ends (target
    // dead/gone, this whole condition below becomes true) still knows what was just being
    // fought -- see TryAutoLootDeadTarget, which is the only reader of this.
    if (target && target->IsAlive() && bot->IsValidAttackTarget(target))
        state.lastCombatTargetGuid = target->GetGUID();

    if (!target || !target->IsAlive() || !bot->IsValidAttackTarget(target))
    {
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();

        if (!bot->IsInCombat())
        {
            // Whatever this bot was just fighting (tracked above, every tick there was a live
            // target) may have just died -- try to loot it exactly once before moving on.
            TryAutoLootDeadTarget(bot, state);

            ResumeFollowingLeader(bot, state);

            // Ungrouped means no leader to fight alongside or follow -- ResumeFollowingLeader
            // above already reduces to a no-op for that case (nothing to clear), so this is
            // purely additive: give a solo bot something of its own to do instead of standing
            // wherever it was spawned forever. Stay still means "don't go looking for a fight,"
            // same as it already means for inheriting a leader's target above. Peaceful
            // activities take priority over grinding (a real player would rather quest/gather/
            // fish than start a fight) -- an in-progress one is always continued/finished first;
            // otherwise a fresh scan runs in the order the user asked for these features
            // (quests, then gathering, then fishing) before falling back to looking for
            // something to kill.
            if (!bot->GetGroup() && state.manualCommand != BotManualCommand::Stay)
            {
                if (!state.gatherTargetGuid.IsEmpty())
                    TryFinishGathering(bot, state);
                else if (!state.gatherWalkTargetGuid.IsEmpty())
                    TryContinueGatherWalk(bot, state);
                else if (!state.fishingBobberGuid.IsEmpty())
                    TryFinishFishing(bot, diff, state);
                else if (state.fishingCastInProgress)
                    TryWaitForFishingCast(bot, diff, state);
                else if (!state.questWalkTargetGuid.IsEmpty())
                    TryContinueQuestWalk(bot, state);
                else if (!state.questObjectiveWalkTargetGuid.IsEmpty())
                    TryContinueQuestObjectiveWalk(bot, state);
                else if (!TryStartQuesting(bot, diff, state) && !TryStartGathering(bot, diff, state) && !TryStartFishing(bot, diff, state))
                    TryGrindWhenSolo(bot, diff, state);
            }
        }
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
    {
        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        spellId = BotAI::SelectClassRotationSpell(bot, target, bot->getClass(), activeSpec);
    }
    if (!spellId)
        spellId = BotAI::SelectReaperRotationSpell(bot, target);
    if (!spellId)
        spellId = BotAI::SelectFelswornRotationSpell(bot, target);
    if (!spellId)
        spellId = BotAI::SelectBloodmageRotationSpell(bot, target);
    if (!spellId)
        spellId = BotAI::SelectStormbringerRotationSpell(bot, target);
    if (!spellId)
        spellId = BotAI::SelectPrimalistRotationSpell(bot, target);
    if (!spellId)
        spellId = BotAI::SelectRunemasterRotationSpell(bot, target);
    if (!spellId)
        spellId = BotAI::SelectXorothRotationSpell(bot, target);
    if (!spellId)
        spellId = BotAI::SelectChronomancerRotationSpell(bot, target);
    if (!spellId)
        spellId = BotAI::SelectNecromancerRotationSpell(bot, target);
    if (!spellId)
        spellId = SelectSpell(bot, target);

    if (spellId)
    {
        // A castable spell was found in its own valid range -- no need to close distance.
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        {
            bot->GetMotionMaster()->Clear();
            bot->StopMoving();
        }

        Unit* castTarget = target;
        if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId))
        {
            if (spellInfo->IsPositive() || !spellInfo->NeedsExplicitUnitTarget())
                castTarget = bot;
        }

        SpellCastResult result = LogCastAttempt(bot, spellId, castTarget, "cast");
        if (result != SPELL_CAST_OK)
            BotAI::RecordSpellCastFailure(bot->GetGUID(), spellId);
        state.nextCastAllowedMs = (result == SPELL_CAST_OK) ? APPROXIMATE_GCD_MS : NO_CANDIDATE_RETRY_MS;
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
        SpellCastResult result = LogCastAttempt(bot, spellId, healTarget, "cast heal");
        state.nextCastAllowedMs = (result == SPELL_CAST_OK) ? APPROXIMATE_GCD_MS : NO_CANDIDATE_RETRY_MS;
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

    // Mount ownership and gear upgrades matter regardless of role/combat/group state -- same
    // reasoning as TryMaintainBuff running independent of combat state.
    TryMaintainProgression(bot, diff, state);

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
    BotAI::ForgetRotationState(botGuid);
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
