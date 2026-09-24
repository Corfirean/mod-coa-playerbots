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
#include "AscensionCollectibleSpellData.h"
#include "BotClassRotations.h"
#include "BotMgr.h"
#include "BotTalentBuilds.h"
#include "BotClassRotationsBloodmage.h"
#include "BotClassRotationsChronomancer.h"
#include "BotClassRotationsFelsworn.h"
#include "BotClassRotationsNecromancer.h"
#include "BotClassRotationsPrimalist.h"
#include "BotClassRotationsReaper.h"
#include "BotClassRotationsRunemaster.h"
#include "BotClassRotationsStormbringer.h"
#include "BotClassRotationsXoroth.h"
#include "engine/HealerEngine.h"
#include "engine/DpsEngine.h"
#include "engine/TankEngine.h"
#include "engine/SpellResolver.h"
#include "engine/ActionEvaluator.h"
#include "engine/CombatContext.h"
#include "engine/CombatMovement.h"
#include "engine/CombatReservations.h"
#include "engine/CombatUtility.h"
#include "engine/DamageTracker.h"
#include "engine/HealEvaluator.h"
#include "engine/SpellPredicates.h"
#include "engine/TargetEvaluator.h"
#include "engine/ThreatEvaluator.h"
#include "ClassSpecRoles.h"
#include "CombatManager.h"
#include "Config.h"
#include "Bag.h"
#include "CellImpl.h"
#include <deque>
#include "CharacterCache.h"
#include "Chat.h"
#include "Corpse.h"
#include "Creature.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "DBCStructure.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Item.h"
#include "BotAvoidance.h"
#include "BotBattlegroundAI.h"
#include "BotFormations.h"
#include "BotMovement.h"
#include "BotProgression.h"
#include "BotTaxi.h"
#include "BotWorldBehavior.h"
#include "BotZoneProgression.h"
#include "WorldBrain.h"
#include "WorldParties.h"
#include "GameTime.h"
#include "Log.h"
#include "LootMgr.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectDefines.h"
#include "ObjectMgr.h"
#include "PetDefines.h"
#include "PetitionMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "Unit.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace
{
// Real per-spell GCD is now queried directly off the engine's own Player::GetGlobalCooldownMgr()
// (populated automatically by every real CastSpell -- see engine/SpellPredicates.h's
// IsOffGlobalCooldown), not approximated. After a successful cast this AI reaction gate is all
// that throttles the next decision tick -- short enough that an off-GCD/instant follow-up fires
// promptly, long enough that a spellbook scan doesn't run every single 100ms world tick.
constexpr uint32 AI_REACTION_GATE_MS = 150;
constexpr float MELEE_ENGAGE_RANGE = 4.0f;
constexpr float RANGED_ENGAGE_DISTANCE = 22.0f;

// How close a Healer bot tries to stay to whoever it's healing. Set to 35 yards to accommodate
// heal spells with 35-40 yard range without requiring the target to be within the rigid 25yd bound.
// This allows healers to reach party members who are far from the group and still be in spell range.
constexpr float HEAL_ENGAGE_RANGE = 35.0f;

// Retry gate used when nothing was castable this tick (e.g. everything's on cooldown or
// unaffordable) -- much shorter than the real GCD so a newly-ready spell gets used quickly,
// without re-scanning the whole spellbook every single world tick while idle-in-combat.
constexpr uint32 NO_CANDIDATE_RETRY_MS = 500;

enum class BotArchetype : uint8 { Balanced, Quester, Gatherer, Angler, Grinder, Explorer };
enum class SoloIntent : uint8 { None, Quest, Gather, Fish, Grind, Explore };

struct BotPersonality
{
    BotArchetype archetype = BotArchetype::Balanced;
    uint32 seed = 0;
    uint8 questing = 50, gathering = 50, fishing = 25, grinding = 50, patience = 50, sociability = 50;
    // Sociability currently makes questing (the most hub/group-adjacent solo activity) more attractive.
};

struct BotAIState
{
    uint32 nextCastAllowedMs = 0;
    uint32 noCandidateLogMs = 0;
    uint32 nextBuffCheckMs = 0;
    BotRole role = BotRole::Dps;
    bool manualRoleOverride = false;
    uint8 lastLevel = 0;
    BotPersonality personality;
    bool hasPersonality = false;
    SoloIntent soloIntent = SoloIntent::None;
    uint32 soloIntentRemainingMs = 0;
    uint32 soloIntentGeneration = 0;

    // See TryMatchLeaderMountState -- a "mount" spell in this server's account-wide collection
    // is actually a wrapper (mod-ascension-compat's spell_ascension_local_mount script) that
    // resolves to a real ground/flying spell internally; a wrapper collected for a mount with no
    // ground form (e.g. a drake with only Flying150/280/310 variants) reports SPELL_CAST_OK but
    // silently applies nothing when a ground mount is what's actually wanted, with zero
    // server-side trace either way. SelectKnownMountSpell has no way to know this ahead of time
    // (it can only see the wrapper spell's own SpellInfo, not mod-ascension-compat's internal
    // MountWrapper table), so this remembers which spellIds turned out not to actually work and
    // skips them on future attempts, trying a different known mount instead of retrying the same
    // dead end forever.
    std::unordered_set<uint32> knownBadMountSpells;

    // Confirmed live (root cause of a near-total mount outage: one bot had blacklisted 816
    // distinct mount spells and never mounted once): checking bot->IsMounted() synchronously in
    // the same statement as the CastSpell() call that just requested the mount reads stale state
    // -- SPELL_AURA_MOUNTED isn't guaranteed visible via IsMounted() until the tick after the
    // cast resolves, even for a real SPELL_CAST_OK (255) result and an unwrapped racial mount
    // with nothing scripted in front of it. Checking that early was blacklisting good, working
    // mounts as fast as a bot could cycle through its known list, exhausting the entire
    // collection within seconds and leaving it permanently grounded (no known mounts left to
    // try). This defers the real "did it actually mount" check to the following tick instead of
    // trusting an instantaneous read, while still catching the genuinely-broken wrapper mounts
    // this blacklist was originally built for (see knownBadMountSpells' own comment) -- those
    // still fail the check a full tick later, just without punishing spells that only needed one
    // more tick to show up as mounted.
    uint32 pendingMountSpellId = 0;

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

    // Nodes this bot has tried and failed to gather from, each mapped to the GameTime ms at which
    // it becomes eligible again (see GATHER_NODE_RETRY_MS). Absolute deadlines rather than
    // countdowns on purpose: nothing has to tick this map every frame, which matters with
    // thousands of bots each holding one.
    std::unordered_map<ObjectGuid, uint32> gatherNodeRetryAtMs;

    // Remaining time to reach the current gatherWalkTargetGuid before giving up on it.
    uint32 gatherWalkTimeoutMs = 0;

    // Set by TryStartGathering while walking toward a node found by an earlier scan, cleared
    // once in range (see TryContinueGatherWalk). Deliberately separate from gatherTargetGuid,
    // which covers the casting phase: this one answers "which node am I walking to", and the
    // two phases need different arrival handling.
    //
    // Historically this guid also had to carry movement ownership. Every subsystem issued
    // MovePoint(0, ...) and cleaned up with a bare "if the generator is POINT_MOTION_TYPE,
    // Clear() it", so TryGrindWhenSolo's own cleanup cancelled the *gather* walk on the ticks
    // between gather scans -- confirmed live: the node kept being found fresh at a different
    // distance every scan because the bot was yanked back toward its grind anchor mid-walk and
    // never arrived. Ownership now lives in BotMovement (see BotMovement.h), so a subsystem can
    // only stop movement it actually claimed, and this guid is free to mean just the target.
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
    uint32 fishingReactionDelayMs = 0;

    // Quest state lives in the open-world layer now (world/WorldBrain.h): the task, its phase,
    // its target and its area persist there, not as walk-tracking guids here.

    // See TryMaintainProgression -- shared throttle for both the mount-ownership check and the
    // gear-upgrade bag scan.
    uint32 nextProgressionCheckMs = 0;

    // Flight paths a character of this level would know are granted once per session: bots created
    // before flight paths existed knew none and could never fly, and the grant is idempotent, so
    // doing it again on the next login is harmless but not worth doing every check.
    bool flightPathsGranted = false;

    // Loot queue: corpses of creatures killed in combat or by party members
    std::deque<ObjectGuid> pendingLootGuids;

    // Out-of-combat rest state (eating/drinking between pulls)
    bool isResting = false;

    // Death failsafe: duration spent as a ghost (for stuck ghost fallback)
    uint32 ghostDurationMs = 0;

    // Natural follow latency
    uint32 followReactionDelayMs = 0;
    bool followWaitingReaction = false;
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
float GrindSearchRadius() { return sConfigMgr->GetOption<float>("CoaBots.Grind.SearchRadius", 30.0f); } // matches BotMgr::AttackNearestHostile's default
constexpr float GRIND_LEASH_RADIUS = 40.0f;          // don't drift farther than this from the anchor
constexpr uint32 GRIND_SCAN_INTERVAL_MS = 3000;      // idle behavior -- no need to grid-scan every tick
uint32 GrindMaxLevelAbove() { return sConfigMgr->GetOption<uint32>("CoaBots.Grind.MaxLevelAbove", 3); } // skip mobs this far above the bot's own level

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

// How long a node the bot failed to gather from is skipped for. Without this, a node that keeps
// refusing the cast is re-found by the very next scan and the bot spins scan -> walk -> arrive
// -> fail forever. Confirmed live: 8040 of 8042 gather casts in ten minutes across 200 bots
// failed with SPELL_FAILED_OUT_OF_RANGE, 8032 of them against one single node entry. Long enough
// that the bot goes and does something else, short enough that a node which failed for a
// transient reason (someone else looted it first, it despawned mid-walk) comes back into play.
constexpr uint32 GATHER_NODE_RETRY_MS = 60000;

// Budget for actually reaching a node's interact distance once a walk has started. Guards the
// opposite failure from the one above: a node the bot can never path close enough to (blocked
// approach, node embedded in geometry) would otherwise be walked toward indefinitely, because
// the arrival test simply never becomes true.
constexpr uint32 GATHER_WALK_TIMEOUT_MS = 20000;

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

// How often TryMaintainProgression re-checks mount/gear state -- infrequent on purpose (a
// bot's spellbook/bags don't change fast enough to need per-tick scanning), shared by both
// checks it bundles so there's one throttle field, not two.
constexpr uint32 PROGRESSION_CHECK_INTERVAL_MS = 10000;

// TryMaintainEquipment: repair/bag-cleanup only ever fire opportunistically, when a bot
// happens to already be standing next to the right NPC (quest hubs usually have both a
// repairer and a vendor) -- deliberately no pathing-to-a-vendor logic, that's flight-path
// scale future work. Kept tight (most quest-hub NPCs cluster within a few yards of each
// other) so a bot doesn't "repair" off some unrelated vendor two zones over.
constexpr float VENDOR_SEARCH_RADIUS = 20.0f;
// Repair once any equipped piece drops below this percent of max durability -- proactive
// (matches how a real player tops off before it hits 0%, not a last-second panic repair).
constexpr uint32 DURABILITY_REPAIR_THRESHOLD_PCT = 25;
// Bag cleanup only kicks in once free space is this low -- avoids destroying/selling grey
// clutter a bot might still want a use for (e.g. as a gathering-node byproduct) while there's
// still room to carry it.
constexpr uint32 BAG_CLEANUP_FREE_SLOT_THRESHOLD = 2;

std::unordered_map<ObjectGuid, BotAIState> states;

// Stored in the existing core character_settings table. This survives relogs/restarts without
// a module-specific table or any per-tick database work.
constexpr char const* BOT_PROFILE_SETTING = "coa.bot_profile";
constexpr uint32 PROFILE_VERSION = 1;

uint32 MixProfileSeed(uint32 value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

uint8 ProfileValue(uint32& seed, uint8 base)
{
    seed = MixProfileSeed(seed);
    return uint8(std::max(5, std::min(95, int32(base) + int32(seed % 31) - 15)));
}

char const* ArchetypeName(BotArchetype archetype)
{
    switch (archetype)
    {
        case BotArchetype::Quester: return "Quester";
        case BotArchetype::Gatherer: return "Gatherer";
        case BotArchetype::Angler: return "Angler";
        case BotArchetype::Grinder: return "Grinder";
        case BotArchetype::Explorer: return "Explorer";
        default: return "Balanced";
    }
}

char const* IntentName(SoloIntent intent)
{
    switch (intent)
    {
        case SoloIntent::Quest: return "questing";
        case SoloIntent::Gather: return "gathering";
        case SoloIntent::Fish: return "fishing";
        case SoloIntent::Grind: return "grinding";
        case SoloIntent::Explore: return "exploring";
        default: return "none";
    }
}

void SavePersonality(Player* bot, BotPersonality const& profile)
{
    bot->UpdatePlayerSetting(BOT_PROFILE_SETTING, 0, PROFILE_VERSION);
    bot->UpdatePlayerSetting(BOT_PROFILE_SETTING, 1, uint32(profile.archetype));
    bot->UpdatePlayerSetting(BOT_PROFILE_SETTING, 2, profile.questing);
    bot->UpdatePlayerSetting(BOT_PROFILE_SETTING, 3, profile.gathering);
    bot->UpdatePlayerSetting(BOT_PROFILE_SETTING, 4, profile.fishing);
    bot->UpdatePlayerSetting(BOT_PROFILE_SETTING, 5, profile.grinding);
    bot->UpdatePlayerSetting(BOT_PROFILE_SETTING, 6, profile.patience);
    bot->UpdatePlayerSetting(BOT_PROFILE_SETTING, 7, profile.sociability);
    bot->UpdatePlayerSetting(BOT_PROFILE_SETTING, 8, profile.seed);
}

void EnsurePersonality(Player* bot, BotAIState& state)
{
    if (state.hasPersonality)
        return;
    if (bot->GetPlayerSetting(BOT_PROFILE_SETTING, 0).value == PROFILE_VERSION)
    {
        state.personality.archetype = BotArchetype(std::min<uint32>(5, bot->GetPlayerSetting(BOT_PROFILE_SETTING, 1).value));
        state.personality.questing = uint8(bot->GetPlayerSetting(BOT_PROFILE_SETTING, 2).value);
        state.personality.gathering = uint8(bot->GetPlayerSetting(BOT_PROFILE_SETTING, 3).value);
        state.personality.fishing = uint8(bot->GetPlayerSetting(BOT_PROFILE_SETTING, 4).value);
        state.personality.grinding = uint8(bot->GetPlayerSetting(BOT_PROFILE_SETTING, 5).value);
        state.personality.patience = uint8(bot->GetPlayerSetting(BOT_PROFILE_SETTING, 6).value);
        state.personality.sociability = uint8(bot->GetPlayerSetting(BOT_PROFILE_SETTING, 7).value);
        state.personality.seed = bot->GetPlayerSetting(BOT_PROFILE_SETTING, 8).value;
    }
    else
    {
        BotPersonality profile;
        uint32 seed = MixProfileSeed(bot->GetGUID().GetCounter() ^ (uint32(bot->getClass()) << 24));
        profile.seed = seed;
        profile.archetype = BotArchetype(seed % 6);
        uint8 quest = 50, gather = 50, fish = 25, grind = 50;
        switch (profile.archetype)
        {
            case BotArchetype::Quester: quest = 85; grind = 35; break;
            case BotArchetype::Gatherer: gather = 88; quest = 55; grind = 35; break;
            case BotArchetype::Angler: fish = 90; gather = 45; quest = 40; break;
            case BotArchetype::Grinder: grind = 88; quest = 35; gather = 30; break;
            case BotArchetype::Explorer: quest = 70; gather = 65; fish = 45; grind = 30; break;
            default: break;
        }
        profile.questing = ProfileValue(seed, quest);
        profile.gathering = ProfileValue(seed, gather);
        profile.fishing = ProfileValue(seed, fish);
        profile.grinding = ProfileValue(seed, grind);
        profile.patience = ProfileValue(seed, 55);
        profile.sociability = ProfileValue(seed, 45);
        SavePersonality(bot, profile);
        state.personality = profile;
        LOG_INFO("module.coa-playerbots", "BotMgr: assigned {} profile to '{}' (guid {}, seed {}).", ArchetypeName(profile.archetype), bot->GetName(), bot->GetGUID().GetCounter(), profile.seed);
    }
    state.hasPersonality = true;
}

void UpdateSoloIntent(Player* bot, uint32 diff, BotAIState& state)
{
    EnsurePersonality(bot, state);
    if (state.soloIntent != SoloIntent::None && state.soloIntentRemainingMs > diff)
    {
        state.soloIntentRemainingMs -= diff;
        return;
    }
    BotPersonality const& p = state.personality;
    uint32 seed = MixProfileSeed(p.seed ^ ++state.soloIntentGeneration ^ (bot->GetZoneId() * 2654435761U));
    auto score = [&seed](uint8 preference) { seed = MixProfileSeed(seed); return int32(preference) * 10 + int32(seed % 161) - 80; };
    uint8 socialQuesting = uint8(std::min(100U, uint32(p.questing) + p.sociability / 4));
    std::array<std::pair<SoloIntent, int32>, 5> options = {{{ SoloIntent::Quest, score(socialQuesting) }, { SoloIntent::Gather, score(p.gathering) }, { SoloIntent::Fish, score(p.fishing) }, { SoloIntent::Grind, score(p.grinding) }, { SoloIntent::Explore, score(uint8(std::min(100U, (uint32(p.questing) + p.gathering) / 2 + 10))) }}};
    auto chosen = std::max_element(options.begin(), options.end(), [](auto const& left, auto const& right) { return left.second < right.second; });
    state.soloIntent = chosen->first;
    uint32 minMs = sConfigMgr->GetOption<uint32>("CoaBots.Profile.IntentMinMs", 900000);
    uint32 maxMs = std::max(minMs, sConfigMgr->GetOption<uint32>("CoaBots.Profile.IntentMaxMs", 2100000));
    uint32 baseDuration = minMs + (MixProfileSeed(seed) % (maxMs - minMs + 1));
    state.soloIntentRemainingMs = uint32((uint64(baseDuration) * (50 + p.patience)) / 100);
    LOG_DEBUG("module.coa-playerbots", "BotMgr: '{}' chose solo intent {} for {}ms.", bot->GetName(), IntentName(state.soloIntent), state.soloIntentRemainingMs);
}

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
        case RACE_UNDEAD_PLAYER: return 17462; // Red Skeletal Horse (13819 is the paladin class Warhorse)
        case RACE_TAUREN:        return 18990; // Brown Kodo
        case RACE_GNOME:         return 10873; // Red Mechanostrider
        case RACE_TROLL:         return 8395;  // Emerald Raptor
        case RACE_BLOODELF:      return 34795; // Red Hawkstrider
        case RACE_DRAENEI:       return 34406; // Brown Elekk
        default:                 return 0;
    }
}

// Faction-specific default flying mounts: Golden Gryphon (Alliance) and Tawny Wind Rider (Horde),
// the same two BotProgression::GrantMounts hands out, so this fallback is always one the bot knows.
uint32 DefaultFlyingMountSpellFor(Player const* bot)
{
    return bot->GetTeamId() == TEAM_ALLIANCE ? 32235 : 32243;
}

bool IsMountSpell(SpellInfo const* spellInfo)
{
    return spellInfo && !spellInfo->IsPassive() && spellInfo->HasAura(SPELL_AURA_MOUNTED) &&
        spellInfo->GetMaxDuration() == -1;
}

// Determines if a spell confers flying capability, checking engine flight auras as well as
// Ascension mount wrapper definitions.
bool IsFlyingMountSpell(SpellInfo const* spellInfo)
{
    if (!spellInfo || spellInfo->IsPassive() || spellInfo->GetMaxDuration() != -1)
        return false;

    if (spellInfo->HasAura(SPELL_AURA_FLY) ||
        spellInfo->HasAura(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) ||
        spellInfo->HasAura(SPELL_AURA_MOD_FLIGHT_SPEED_ALWAYS))
        return true;

    auto const& entries = AscensionCollectibles::MountWrappers;
    auto itr = std::lower_bound(entries.begin(), entries.end(), spellInfo->Id,
        [](AscensionCollectibles::MountWrapper const& entry, uint32 id) { return entry.SpellId < id; });
    if (itr != entries.end() && itr->SpellId == spellInfo->Id)
    {
        return (itr->Flying150 != 0 || itr->Flying280 != 0 || itr->Flying310 != 0);
    }

    return false;
}

// Checks whether a Unit (leader or bot) is currently mounted on a flying mount
bool IsUnitOnFlyingMount(Unit const* unit)
{
    if (!unit || !unit->IsMounted())
        return false;

    if (unit->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) ||
        unit->HasAuraType(SPELL_AURA_FLY) ||
        unit->HasAuraType(SPELL_AURA_MOD_FLIGHT_SPEED_ALWAYS) ||
        unit->IsFlying())
        return true;

    for (AuraEffect const* aura : unit->GetAuraEffectsByType(SPELL_AURA_MOUNTED))
    {
        SpellInfo const* spellInfo = aura->GetSpellInfo();
        if (spellInfo && (spellInfo->HasAura(SPELL_AURA_FLY) ||
            spellInfo->HasAura(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) ||
            spellInfo->HasAura(SPELL_AURA_MOD_FLIGHT_SPEED_ALWAYS)))
            return true;
    }
    return false;
}

// Searches bot's known spells for a matching mount.
// If wantFlying == true, matches ONLY flying mounts.
// If wantFlying == false, matches STRICTLY ground-only mounts (excludes any flying mounts or wrappers with flight forms).
uint32 SelectKnownMountSpell(Player* bot, bool wantFlying = false, std::unordered_set<uint32> const& excluded = {})
{
    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;
        if (excluded.count(spellId))
            continue;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo || spellInfo->IsPassive() || spellInfo->GetMaxDuration() != -1)
            continue;
        if (!spellInfo->HasAura(SPELL_AURA_MOUNTED))
            continue;

        bool flying = IsFlyingMountSpell(spellInfo);
        if (wantFlying)
        {
            if (flying)
                return spellId;
        }
        else
        {
            // Ground only: strictly exclude flying mounts
            if (!flying)
                return spellId;
        }
    }
    return 0;
}

// Riding skill and Cold Weather Flying are deliberately not granted here any more: this realm runs
// AscensionCompat.MaxRidingFromStart, which gives every character -- player or bot -- full riding
// from its first login, so a bot rides by exactly the same rule a player does.
void EnsureBotHasMount(Player* bot)
{
    BotProgression::GrantMounts(bot);
}

void ClearActiveFollow(Player* bot);

// Resolves in-progress mount cast for both grouped and solo bots
void ResolvePendingMountCast(Player* bot, BotAIState& state)
{
    if (bot->IsMounted())
    {
        state.pendingMountSpellId = 0;
        return;
    }

    if (!state.pendingMountSpellId)
        return;

    // Mount casts have a 1.5s (1500 ms) cast time. If the bot is still casting,
    // wait for it to complete -- DO NOT interrupt, blacklist, or restart!
    if (bot->IsNonMeleeSpellCast(false))
        return;

    uint32 justTried = state.pendingMountSpellId;
    state.pendingMountSpellId = 0;

    if (bot->IsMounted())
    {
        LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' confirmed mounted (spell {}).", bot->GetName(), justTried);
    }
    else
    {
        // Cast completed or was interrupted without leaving the bot mounted.
        // Only blacklist custom/wrapper spells if bot was stationary, out of combat, and not interrupted.
        if (!bot->IsInCombat() && !bot->isMoving() &&
            justTried != RacialGroundMountSpellFor(bot->getRace()) &&
            justTried != DefaultFlyingMountSpellFor(bot))
        {
            state.knownBadMountSpells.insert(justTried);
            LOG_INFO("module.coa-playerbots",
                "BotAI: bot '{}' mount spell {} finished cast but bot is not mounted -- marking bad.",
                bot->GetName(), justTried);
        }
    }
}

// Reusable mounting helper for both group-following and autonomous long-distance travel
bool TryMount(Player* bot, BotAIState& state, bool wantFlying = false)
{
    if (!bot || !bot->IsInWorld() || !bot->IsAlive())
        return false;

    if (bot->IsMounted())
        return true;

    if (bot->IsInCombat() || bot->IsNonMeleeSpellCast(false) || !bot->IsOutdoors())
        return false;

    if (bot->GetLevel() < 20)
        return false;

    if (state.pendingMountSpellId)
        return false;

    if (wantFlying)
    {
        // Only allow flying mounts in Outland (map 530) or Northrend (map 571) with level >= 60
        uint32 mapId = bot->GetMapId();
        if ((mapId != 530 && mapId != 571) || bot->GetLevel() < 60)
            wantFlying = false;
    }

    uint32 spellId = 0;
    if (wantFlying)
    {
        uint32 defaultFly = DefaultFlyingMountSpellFor(bot);
        if (defaultFly && bot->HasSpell(defaultFly) && !state.knownBadMountSpells.count(defaultFly))
            spellId = defaultFly;

        if (!spellId)
            spellId = SelectKnownMountSpell(bot, true /*wantFlying*/, state.knownBadMountSpells);

        if (!spellId)
            spellId = RacialGroundMountSpellFor(bot->getRace());
    }
    else
    {
        uint32 racialSpellId = RacialGroundMountSpellFor(bot->getRace());
        if (racialSpellId && bot->HasSpell(racialSpellId) && !state.knownBadMountSpells.count(racialSpellId))
            spellId = racialSpellId;

        if (!spellId)
            spellId = SelectKnownMountSpell(bot, false /*wantFlying*/, state.knownBadMountSpells);
    }

    if (!spellId)
        return false;

    ClearActiveFollow(bot);
    bot->StopMoving();

    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' attempting to mount spell {} (wantFlying={}).",
        bot->GetName(), spellId, wantFlying);

    SpellCastResult result = bot->CastSpell(bot, spellId, false);
    if (result == SPELL_CAST_OK)
    {
        state.pendingMountSpellId = spellId;
        return true;
    }

    LOG_INFO("module.coa-playerbots",
        "BotAI: bot '{}' mount spell {} failed to cast (result {}).",
        bot->GetName(), spellId, uint32(result));

    if (spellId != RacialGroundMountSpellFor(bot->getRace()) && spellId != DefaultFlyingMountSpellFor(bot))
    {
        if (result != SPELL_FAILED_ONLY_OUTDOORS && result != SPELL_FAILED_MOVING &&
            result != SPELL_FAILED_SPELL_IN_PROGRESS && result != SPELL_FAILED_NOT_HERE)
        {
            state.knownBadMountSpells.insert(spellId);
        }
    }
    return false;
}

// Synchronizes the bot's mounted state and mount type (ground vs flying) with the group leader.
void TryMatchLeaderMountState(Player* bot, BotAIState& state)
{
    Group* group = bot->GetGroup();
    if (!group)
        return;

    Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
    if (!leader || leader == bot || !leader->IsInWorld() || leader->GetMap() != bot->GetMap())
        return;

    // 2. Leader is NOT mounted -> bot must dismount / cancel pending cast
    if (!leader->IsMounted())
    {
        if (bot->IsMounted())
        {
            LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' dismounting (leader '{}' is no longer mounted).",
                bot->GetName(), leader->GetName());
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        }
        if (state.pendingMountSpellId)
        {
            state.pendingMountSpellId = 0;
            bot->InterruptNonMeleeSpells(false);
        }
        return;
    }

    // 3. Leader IS mounted: determine ground vs flying mount type
    bool leaderFlying = IsUnitOnFlyingMount(leader);

    if (bot->IsMounted())
    {
        state.pendingMountSpellId = 0;
        bool botFlying = IsUnitOnFlyingMount(bot);
        if (leaderFlying != botFlying)
        {
            LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' switching mount type (botFlying={} vs leaderFlying={}).",
                bot->GetName(), botFlying, leaderFlying);
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        }
        else
        {
            return;
        }
    }

    TryMount(bot, state, leaderFlying);
}

// Rough per-role/class stat-fit weighting for ScoreItemForBot -- deliberately not a full
// per-spec stat-priority system (that needs the same kind of dedicated per-class research as
// BotClassRotations.cpp, which doesn't exist yet for most of the 21 custom classes), but enough
// to stop a bot "upgrading" into an item just because its raw ItemLevel is higher while its
// actual stat budget is wasted on stats that class/role never uses (e.g. a caster picking up a
// Strength/Agility weapon, or a Tank picking up a pure-Intellect trinket). Confirmed live: the
// ItemLevel-only version this replaces did exactly that. Base class (not the Ascension 12-32
// classId) is enough here since it's what actually drives armor/weapon proficiency and the
// underlying stock stat pools (Str/Agi/Int/Spirit/Stam) every one of the 21 custom classes still
// inherits.
} // anonymous namespace

namespace BotAI
{
float ScoreItemForBot(Player* bot, ItemTemplate const* proto, BotRole role)
{
    uint8 cls = bot->getClass();

    bool isCasterDps = (cls == CLASS_MAGE || cls == CLASS_WARLOCK || cls == CLASS_PRIEST ||
                        cls == 13 /* Witch Doctor */ || cls == 20 /* Bloodmage */ ||
                        cls == 22 /* Chronomancer */ || cls == 23 /* Necromancer */ ||
                        cls == 24 /* Pyromancer */ || cls == 25 /* Cultist */ ||
                        cls == 26 /* Starcaller */);
    bool isAgiDps = (cls == CLASS_HUNTER || cls == CLASS_ROGUE || cls == CLASS_DRUID || cls == CLASS_SHAMAN ||
                     cls == 14 /* Felsworn */ || cls == 15 /* Witch Hunter */ ||
                     cls == 16 /* Stormbringer */ || cls == 21 /* Ranger */ ||
                     cls == 28 /* Tinker */ || cls == 29 /* Venomancer */ ||
                     cls == 30 /* Primalist */);
    bool isStrDps = (cls == CLASS_WARRIOR || cls == CLASS_PALADIN || cls == CLASS_DEATH_KNIGHT ||
                     cls == 12 /* Barbarian */ || cls == 17 /* Xoroth */ ||
                     cls == 18 /* Guardian */ || cls == 19 /* Templar */ ||
                     cls == 31 /* Runemaster */ || cls == 32 /* Reaper */);

    bool wantsInt = role == BotRole::Healer || role == BotRole::Support || (role == BotRole::Dps && isCasterDps);
    bool wantsAgility = role == BotRole::Dps && isAgiDps;
    bool wantsStrength = (role == BotRole::Tank) || (role == BotRole::Dps && isStrDps);
    bool wantsSpirit = role == BotRole::Healer;
    bool wantsTankStats = role == BotRole::Tank;
    bool wantsDpsRatings = role == BotRole::Dps;
    bool wantsHealPower = role == BotRole::Healer || role == BotRole::Support;

    // Effective (quality-adjusted) item level as the base score -- still the floor, same as
    // before, just no longer the *only* signal: a stat-appropriate rare can now outscore a
    // stat-inappropriate epic of similar ilvl instead of always losing on raw ItemLevel alone.
    float score = proto->GetItemLevelIncludingQuality(bot->GetLevel());
    for (uint32 i = 0; i < MAX_ITEM_PROTO_STATS; ++i)
    {
        int32 value = proto->ItemStat[i].ItemStatValue;
        if (!value)
            continue;

        float weight = 0.0f;
        switch (proto->ItemStat[i].ItemStatType)
        {
            case ITEM_MOD_STAMINA:             weight = wantsTankStats ? 1.0f : 0.3f; break;
            case ITEM_MOD_INTELLECT:           weight = wantsInt ? 1.0f : 0.0f; break;
            case ITEM_MOD_SPIRIT:              weight = wantsSpirit ? 0.8f : 0.0f; break;
            case ITEM_MOD_STRENGTH:            weight = wantsStrength ? 1.0f : 0.0f; break;
            case ITEM_MOD_AGILITY:             weight = wantsAgility ? 1.0f : 0.0f; break;
            case ITEM_MOD_DEFENSE_SKILL_RATING:
            case ITEM_MOD_DODGE_RATING:
            case ITEM_MOD_PARRY_RATING:
            case ITEM_MOD_BLOCK_RATING:         weight = wantsTankStats ? 0.6f : 0.0f; break;
            case ITEM_MOD_HIT_RATING:
            case ITEM_MOD_CRIT_RATING:
            case ITEM_MOD_HASTE_RATING:
            case ITEM_MOD_ATTACK_POWER:         weight = wantsDpsRatings ? 0.4f : 0.0f; break;
            case ITEM_MOD_SPELL_POWER:          weight = (wantsHealPower || wantsInt) ? 0.4f : 0.0f; break;
            case ITEM_MOD_MANA_REGENERATION:    weight = wantsHealPower ? 0.4f : 0.0f; break;
            default: break;
        }
        score += float(value) * weight;
    }
    return score;
}
} // namespace BotAI

namespace
{
using BotAI::ScoreItemForBot;

// Periodic, throttled bag scan for a gear upgrade -- same shape as TryMaintainBuff. Reuses the
// real engine's own authoritative "can this class/race actually equip this" check
// (Player::CanEquipItem, the same one WorldSession::HandleAutoEquipItemOpcode itself calls --
// covers armor-type proficiency, weapon-type proficiency, level requirement, everything -- so an
// item the bot's class/spec genuinely can't wear, wrong armor or weapon type included, never
// gets this far) and Player::FindEquipSlot (the same real method that picks the correct slot for
// a two-hander, a ring, a trinket, an off-hand item, etc). ScoreItemForBot above decides whether
// a legal candidate is actually worth swapping to.
void TryUpgradeGearOnce(Player* bot, BotRole role)
{
    auto considerItem = [&](Item* item) -> bool
    {
        if (!item)
            return false;
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            return false;

        // Profession tools stay tools. CoA makes several of them scaling heirloom weapons, so on raw
        // score a Mining Pick beats a levelling bot's real weapon -- confirmed live, a level 17 bot
        // swapped its weapon for its pick. Fishing poles are equipped by the fishing code itself.
        if (BotAI::IsProfessionTool(proto))
            return false;

        uint8 eslot = bot->FindEquipSlot(proto, NULL_SLOT, true);
        if (eslot == NULL_SLOT)
            return false;

        Item* current = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, eslot);
        float newScore = ScoreItemForBot(bot, proto, role);
        if (current)
        {
            // Small margin, not a strict ">" -- avoids swapping back and forth every scan
            // between two items that score within noise of each other.
            float currentScore = ScoreItemForBot(bot, current->GetTemplate(), role);
            if (newScore <= currentScore * 1.05f)
                return false;
        }

        uint16 dest = uint16(eslot) | (uint16(INVENTORY_SLOT_BAG_0) << 8);
        if (bot->CanEquipItem(NULL_SLOT, dest, item, true) != EQUIP_ERR_OK)
            return false; // class/race/level can't actually use this one

        bot->SwapItem(item->GetPos(), dest);
        LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' equipped '{}' (score {:.0f}) over slot {} (was score {:.0f}).",
            bot->GetName(), proto->Name1, newScore, uint32(eslot), current ? ScoreItemForBot(bot, current->GetTemplate(), role) : 0.0f);
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

// Shrinking-radius nearest-in-range search for a nearby repair vendor.
class RepairNpcCheck
{
public:
    RepairNpcCheck(Player* bot, float range) : _bot(bot), _range(range) { }
    bool operator()(Creature* creature)
    {
        if (!creature->IsAlive() || !creature->IsArmorer())
            return false;
        if (!_bot->IsWithinDistInMap(creature, _range))
            return false;
        _range = _bot->GetDistance(creature);
        return true;
    }

private:
    Player* _bot;
    float _range;
};

// Same idea, but for a generic item vendor (may or may not be the same NPC as above).
class VendorNpcCheck
{
public:
    VendorNpcCheck(Player* bot, float range) : _bot(bot), _range(range) { }
    bool operator()(Creature* creature)
    {
        if (!creature->IsAlive() || !creature->IsVendor())
            return false;
        if (!_bot->IsWithinDistInMap(creature, _range))
            return false;
        _range = _bot->GetDistance(creature);
        return true;
    }

private:
    Player* _bot;
    float _range;
};

// Opportunistic gear repair and bag-clutter cleanup -- part of TryMaintainProgression's
// throttled bundle, so this only runs once every PROGRESSION_CHECK_INTERVAL_MS. Neither half
// paths the bot anywhere: both only act when the right NPC is already within
// VENDOR_SEARCH_RADIUS, i.e. the bot happens to be standing at a quest hub or town it was
// already going to visit anyway. Real engine calls throughout (Player::DurabilityRepairAll,
// Player::ModifyMoney, Player::DestroyItem) -- same "call the real thing" pattern as the rest
// of this module.
// Shared by TryMaintainEquipment (acting when a repair NPC happens to be near) and the ambient
// layer's Repair errand (walking to one), so both agree on what "needs repair" means.
bool HasGearBelowRepairThreshold(Player* bot)
{
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;
        uint32 maxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
        if (maxDurability && item->GetUInt32Value(ITEM_FIELD_DURABILITY) * 100 < maxDurability * DURABILITY_REPAIR_THRESHOLD_PCT)
            return true;
    }
    return false;
}

// The ambient Vendor errand only makes sense when a vendor can actually help: this module only
// ever sells grey items, so bags full of anything else would send the bot to a vendor forever.
bool HasSellableJunk(Player* bot)
{
    auto isJunk = [](Item const* item)
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        return proto && proto->Quality == ITEM_QUALITY_POOR && proto->SellPrice > 0;
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (isJunk(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot)))
            return true;

    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* pBag = bot->GetBagByPos(bag);
        if (!pBag)
            continue;
        for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            if (isJunk(pBag->GetItemByPos(j)))
                return true;
    }
    return false;
}

void TryMaintainEquipment(Player* bot)
{
    if (HasGearBelowRepairThreshold(bot))
    {
        Creature* repairNpc = nullptr;
        RepairNpcCheck repairCheck(bot, VENDOR_SEARCH_RADIUS);
        Acore::CreatureLastSearcher<RepairNpcCheck> repairSearcher(bot, repairNpc, repairCheck);
        Cell::VisitObjects(bot, repairSearcher, VENDOR_SEARCH_RADIUS);

        if (repairNpc)
        {
            uint32 cost = bot->DurabilityRepairAll(true, 1.0f, false);
            LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' repaired gear at '{}' for {} copper.",
                bot->GetName(), repairNpc->GetName(), cost);
        }
    }

    if (bot->GetFreeInventorySpace() > BAG_CLEANUP_FREE_SLOT_THRESHOLD)
        return;

    Creature* vendorNpc = nullptr;
    VendorNpcCheck vendorCheck(bot, VENDOR_SEARCH_RADIUS);
    Acore::CreatureLastSearcher<VendorNpcCheck> vendorSearcher(bot, vendorNpc, vendorCheck);
    Cell::VisitObjects(bot, vendorSearcher, VENDOR_SEARCH_RADIUS);

    // No vendor nearby: only force clutter out as a last resort once bags are truly full --
    // otherwise leave it for a later scan that might catch a vendor instead.
    if (!vendorNpc && bot->GetFreeInventorySpace() > 0)
        return;

    uint32 totalEarned = 0;
    uint32 itemsCleared = 0;

    auto clearIfJunk = [&](Item* item) -> bool
    {
        if (!item)
            return false;
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || proto->Quality != ITEM_QUALITY_POOR || BotAI::IsProfessionTool(proto))
            return false;
        if (item->IsNotEmptyBag() || item->IsRefundable() || bot->GetLootGUID() == item->GetGUID())
            return false;

        if (vendorNpc && proto->SellPrice > 0 && sScriptMgr->OnPlayerCanSellItem(bot, item, vendorNpc))
        {
            uint32 money = proto->SellPrice * item->GetCount();
            bot->ModifyMoney(money);
            totalEarned += money;
        }
        bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        ++itemsCleared;
        return true;
    };

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        clearIfJunk(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag* pBag = bot->GetBagByPos(bag);
        if (!pBag)
            continue;
        for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
            clearIfJunk(pBag->GetItemByPos(j));
    }

    if (itemsCleared)
    {
        if (vendorNpc)
            LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' sold {} junk item stack(s) to '{}' for {} copper.",
                bot->GetName(), itemsCleared, vendorNpc->GetName(), totalEarned);
        else
            LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' destroyed {} junk item stack(s) (bags full, no vendor nearby).",
                bot->GetName(), itemsCleared);
    }
}

// Confirmed live -- explicit user pushback on the original design: auto-signing the moment a
// bot is merely in the leader's group meant *every* bot that ever joins the party ends up
// bound to the leader's guild, with no way to bring a bot along without also committing it to
// membership. A real second player has to be individually asked ("Request Signature") before
// their client even offers them the choice; a bot should need the exact same explicit ask, not
// group membership alone. Requires one small core hook (PLAYERHOOK_ON_PETITION_OFFERED, fired
// from WorldSession::HandleOfferPetitionOpcode -- the real "Request Signature" action) since
// nothing else exposes "this specific player was just asked" as a signal a module can react to.
std::unordered_map<ObjectGuid, std::unordered_set<ObjectGuid>> requestedPetitionSignatures;

class coa_bot_petition_offer_script : public PlayerScript
{
public:
    coa_bot_petition_offer_script() : PlayerScript("coa_bot_petition_offer_script", { PLAYERHOOK_ON_PETITION_OFFERED }) { }

    void OnPetitionOffered(Player* player, ObjectGuid petitionGuid) override
    {
        if (sBotMgr->FindBotPlayer(player->GetGUID().GetCounter()))
            requestedPetitionSignatures[petitionGuid].insert(player->GetGUID());
    }
};

// Lets a solo player -- whose only "friends" available to sign a guild charter are their own
// bots -- actually found a guild through the normal charter/petition flow instead of being
// stuck with zero real players to ask. No new opcode needed for the sign itself: PetitionMgr
// already exposes everything read-only (GetPetitionByOwnerWithType, GetSignature), and signing
// is the same "build a minimal real packet, call the real handler" trick as everything else in
// this module (WorldSession::HandlePetitionSignOpcode does the actual DB insert + PetitionMgr
// bookkeeping). Only signs once the leader has actually clicked "Request Signature" on this
// specific bot (see coa_bot_petition_offer_script above) -- not merely because the bot is in
// the leader's group.
void TryAutoSignLeaderPetition(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return;

    Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
    if (!leader || leader == bot)
        return;

    Petition const* petition = sPetitionMgr->GetPetitionByOwnerWithType(leader->GetGUID(), GUILD_CHARTER_TYPE);
    if (!petition)
        return;

    auto requestedItr = requestedPetitionSignatures.find(petition->petitionGuid);
    if (requestedItr == requestedPetitionSignatures.end() || !requestedItr->second.count(bot->GetGUID()))
        return; // leader hasn't asked this specific bot to sign yet

    if (Signatures const* signatures = sPetitionMgr->GetSignature(petition->petitionGuid))
        if (signatures->signatureMap.count(bot->GetGUID()))
            return; // already signed

    // Confirmed live, twice now: bots kept re-attempting this every progression-check tick
    // forever, with `petition_sign` staying completely empty in the DB the whole time.
    // HandlePetitionSignOpcode is a void function that silently no-ops on several guards with
    // zero server-side trace, so the "signed" log line here used to report success it never
    // verified. The first round's fix (clearing a stale GetGuildIdInvited()) did NOT resolve
    // it -- confirmed by the failure log still firing every time after that fix shipped -- so
    // every other guard in that function's GUILD_CHARTER_TYPE branch is dumped here in full
    // before the call, since guessing again without evidence isn't productive. Team mismatch,
    // guild membership, and a stale invite have all already been ruled out via direct DB/RA
    // checks; this will catch whatever's actually left (trial-restriction misfire, a team-cache
    // miss, an already-at-max signature count, or the same-account "already signed" rule if two
    // bots share a bot-hosting account).
    if (bot->GetGuildIdInvited())
        bot->SetGuildIdInvited(0);
    if (uint32 guildId = bot->GetGuildId())
    {
        LOG_ERROR("module.coa-playerbots", "BotAI: bot '{}' can't sign '{}'s guild charter -- already in guild {}.",
            bot->GetName(), leader->GetName(), guildId);
        return;
    }

    WorldPacket signPacket;
    signPacket << petition->petitionGuid;
    signPacket << uint8(0);
    bot->GetSession()->HandlePetitionSignOpcode(signPacket);

    // Verify it actually landed instead of trusting the void call.
    bool actuallySigned = false;
    uint32 signCount = 0;
    bool alreadySignedByAccount = false;
    if (Signatures const* signedNow = sPetitionMgr->GetSignature(petition->petitionGuid))
    {
        actuallySigned = signedNow->signatureMap.count(bot->GetGUID()) != 0;
        signCount = uint32(signedNow->signatureMap.size());
        for (auto const& [signerGuid, accountId] : signedNow->signatureMap)
            if (accountId == bot->GetSession()->GetAccountId())
                alreadySignedByAccount = true;
    }

    if (actuallySigned)
    {
        LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' signed '{}'s guild charter for '{}'.",
            bot->GetName(), leader->GetName(), petition->petitionName);
        return;
    }

    // Confirmed by design, not a bug: HandlePetitionSignOpcode's one-signature-per-account rule
    // exists to stop a real player padding a charter with their own alts -- it doesn't reflect
    // a meaningful ownership boundary for bots, which get pooled onto a handful of hosting
    // accounts purely as infrastructure (see BotSpawnRandom.cpp's account-creation comments).
    // A solo player relying entirely on bots must be able to have *every* bot sign, not just one
    // per hosting account, or a full charter becomes impossible whenever two bots happen to
    // share one. Sign directly in that one specific case -- same DB insert + PetitionMgr
    // bookkeeping HandlePetitionSignOpcode itself performs, just without the account check that
    // doesn't apply here -- rather than trying to work around it through the opcode.
    if (alreadySignedByAccount)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_PETITION_SIGNATURE);
        stmt->SetData(0, petition->ownerGuid.GetCounter());
        stmt->SetData(1, petition->petitionId);
        stmt->SetData(2, bot->GetGUID().GetCounter());
        stmt->SetData(3, bot->GetSession()->GetAccountId());
        CharacterDatabase.Execute(stmt);
        sPetitionMgr->AddSignature(petition->petitionGuid, bot->GetSession()->GetAccountId(), bot->GetGUID());

        LOG_INFO("module.coa-playerbots",
            "BotAI: bot '{}' signed '{}'s guild charter for '{}' directly -- shares a hosting account with an already-signed bot.",
            bot->GetName(), leader->GetName(), petition->petitionName);
        return;
    }

    LOG_ERROR("module.coa-playerbots",
        "BotAI: bot '{}' FAILED to sign '{}'s guild charter for '{}' -- diagnostic dump: "
        "botTeam={} ownerTeamViaCache={} allowTwoSideGuild={} trialRestrictionGuild={} isTrialAccount={} "
        "guildId={} guildIdInvited={} signCount={} maxSigns={} alreadySignedByThisAccount={} accountId={}.",
        bot->GetName(), leader->GetName(), petition->petitionName,
        uint32(bot->GetTeamId()), sCharacterCache->GetCharacterTeamByGuid(petition->ownerGuid),
        sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD),
        sWorld->getBoolConfig(CONFIG_TRIAL_RESTRICTION_GUILD), bot->GetSession()->IsTrialAccount(),
        bot->GetGuildId(), bot->GetGuildIdInvited(), signCount, uint32(petition->petitionType),
        alreadySignedByAccount, bot->GetSession()->GetAccountId());
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
    if (!state.flightPathsGranted)
    {
        BotTaxi::GrantNodesForLevel(bot);
        state.flightPathsGranted = true;
    }
    TryUpgradeGearOnce(bot, state.role);
    TryMaintainEquipment(bot);
    TryAutoSignLeaderPetition(bot);
}

// "Is this known spell shaped like an offensive/taunt/heal/interrupt/dispel ability" and the
// generic spellbook scanners built on them now live in engine/SpellPredicates.h -- pulled out
// of this anonymous namespace so engine/CombatUtility.cpp's Global Combat Utility Layer can
// reuse the exact same taunt/interrupt/dispel shape checks instead of re-deriving its own copy.
// See that header's own comment. Brought into scope here via `using` so every call site below
// (SelectSpell, SelectTauntSpell, IsUsableOffensiveSpell, etc.) keeps working unqualified.
using BotAI::BURST_SPELL_MIN_COOLDOWN_MS;
using BotAI::IsBossOrEliteTarget;
using BotAI::IsTargetCastingInterruptibleSpell;
using BotAI::IsUsableAoeSpell;
using BotAI::IsUsableBuffSpell;
using BotAI::IsUsableBurstSpell;
using BotAI::IsUsableHealSpell;
using BotAI::IsUsableInterruptSpell;
using BotAI::IsUsableOffensiveSpell;
using BotAI::IsUsableSingleTargetOffensiveSpell;
using BotAI::IsUsableTauntSpell;
using BotAI::IsUnitUnderBreakableCrowdControl;
using BotAI::SelectAoeSpell;
using BotAI::SelectBuffSpell;
using BotAI::SelectBurstSpell;
using BotAI::SelectHealSpell;
using BotAI::SelectInterruptSpell;
using BotAI::SelectKnownSpell;
using BotAI::SelectSingleTargetSpell;
using BotAI::SelectSpell;
using BotAI::SelectTauntSpell;
using BotAI::CountNearbyEnemies;
using BotAI::CombatContext;
using BotAI::CombatReservations;
using BotAI::CombatUtility;
using BotAI::HealEvaluator;
using BotAI::TargetEvaluator;
using BotAI::ThreatEvaluator;
using BotAI::ThreatDecision;

uint32 FindKnownAutoRepeatRangedSpell(Player const* bot)
{
    if (!bot)
        return 0;

    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (spellInfo && spellInfo->IsAutoRepeatRangedSpell() && bot->HasItemFitToSpellRequirements(spellInfo))
            return spellId;
    }
    return 0;
}

float GetBotPreferredEngageDistance(Player* bot, BotRole role)
{
    if (!bot)
        return MELEE_ENGAGE_RANGE;

    if (role == BotRole::Tank)
        return MELEE_ENGAGE_RANGE;

    if (role == BotRole::Healer)
        return RANGED_ENGAGE_DISTANCE;

    uint32 rangedCount = 0;
    uint32 meleeCount = 0;

    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo || spellInfo->IsPassive() || spellInfo->IsPositive() || !spellInfo->CanBeUsedInCombat())
            continue;

        if (spellInfo->DmgClass == SPELL_DAMAGE_CLASS_MELEE)
        {
            ++meleeCount;
            continue;
        }

        if (!IsUsableOffensiveSpell(spellInfo))
            continue;

        float maxRange = spellInfo->GetMaxRange(false, bot);
        if (maxRange >= 15.0f || spellInfo->DmgClass == SPELL_DAMAGE_CLASS_RANGED)
            ++rangedCount;
        else if (maxRange <= 5.0f)
            ++meleeCount;
    }

    if (FindKnownAutoRepeatRangedSpell(bot))
        ++rangedCount;

    if (rangedCount > meleeCount)
        return RANGED_ENGAGE_DISTANCE;

    return MELEE_ENGAGE_RANGE;
}

class HostileEnemyCheck
{
public:
    HostileEnemyCheck(Player const* bot, Unit const* center, float range)
        : _bot(bot), _center(center), _range(range) { }

    bool operator()(Unit* u) const
    {
        if (!u || !u->IsAlive() || u == _bot)
            return false;
        if (!_center->IsWithinDistInMap(u, _range))
            return false;
        if (!_bot->IsValidAttackTarget(u))
            return false;
        return true;
    }

private:
    Player const* _bot;
    Unit const* _center;
    float _range;
};

// CountNearbyEnemies, SelectKnownSpell, and the Select*Spell wrappers now live in
// engine/SpellPredicates.h (brought into scope by the `using` block above) -- HostileEnemyCheck
// stays here since FindAllyThreatenedTarget below needs the actual unit list, not just a count.

// Same shrinking-radius nearest-hostile shape as BotMgr::AttackNearestHostile's own
// NearestHostileUnitInObjectRangeCheck, plus a level cap that check doesn't need: a directed
// `.botcmd attack` trusts whatever the caller aimed it at, but a bot picking its own fights
// while nobody's around to notice it dying must not pick something far above its own level.
// Quest targets are not this check's business any more: quest objectives are executed by the
// open-world layer (world/QuestExecutor), which searches for its own live targets around the
// objective area. Grinding is only ever grinding.
class GrindHostileUnitCheck
{
public:
    GrindHostileUnitCheck(Unit const* me, float range, uint32 maxLevel)
        : _me(me), _range(range), _maxLevel(maxLevel) { }
    bool operator()(Unit* u)
    {
        if (!_me->IsWithinDistInMap(u, _range, true, false, false))
            return false;
        if (!_me->IsValidAttackTarget(u))
            return false;
        if (u->GetLevel() > _maxLevel)
            return false;
        // Training/practice dummies -- every creature using the `npc_training_dummy` AI script
        // (confirmed via direct DB query: 16 rows, not just the 13 "*Training Dummy"-named ones
        // -- also catches "Highlord's Nemesis Trainer", "Love Fool", "Theramore Combat Dummy",
        // none of which have "Training Dummy" literally in their name) -- pass every other check
        // here just fine: they're real, right-click-attackable (friendly faction 7/35, but still
        // a valid attack target per Unit::IsValidAttackTarget's own special-case for this
        // creature shape), hostile-flagged-for-combat-purposes units. Without this a solo/idle
        // bot standing anywhere near a town's dummy would happily "grind" it forever (confirmed
        // live: bots stood in town whacking dummies instead of actually questing/leveling).
        //
        // History of getting this check right, kept because both wrong attempts looked
        // plausible and passed a compile with no warning:
        // 1. `ct->type == CREATURE_TYPE_TOTEM` (11) -- wrong, confirmed live to filter nothing:
        //    every dummy variant's real `type` column is 9 (CREATURE_TYPE_MECHANICAL, not
        //    TOTEM -- a dummy is inanimate machinery, not a shaman totem). Matching on `type` at
        //    all would also have excluded every other real mechanical creature in the game from
        //    ever being a legitimate grind target (mechanical dragonkin, gnomish constructs).
        // 2. `ct->Name.find("Training Dummy")` -- an improvement (confirmed live: stopped the 13
        //    literally-named variants), but still wrong: missed "Highlord's Nemesis Trainer" and
        //    others that use the identical dummy AI/script but don't share that name substring
        //    (confirmed live again: bots kept "successfully" -- SPELL_CAST_OK, not a failure --
        //    endlessly casting at one). The AI script, not the display name, is what actually
        //    defines "this is a stationary practice target," so it's the only signal that
        //    generalizes to every current and future dummy-shaped NPC regardless of name.
        if (Creature const* creature = u->ToCreature())
        {
            // Confirmed live -- reported again after this realm's own custom test dummies
            // ("CoA Test Attacking Dummy", "CoA Test Demon Dummy", etc.) turned out to use
            // entirely different AI scripts (npc_coa_test_attacking_dummy, npc_coa_test_
            // enemy_dummy, npc_coa_test_evasion_dummy, npc_coa_test_friendly_dummy -- confirmed
            // via direct DB query) than the stock npc_training_dummy this check already
            // excluded, so a solo/idle bot near the class-template spawn point (where every one
            // of these lives) piled onto them exactly like the original town-dummy problem: an
            // un-killable target these can never actually finish off, endlessly re-attempting
            // every tick. Matched by prefix, not each name individually, so any future
            // npc_coa_test_* variant is excluded automatically without needing another report.
            // Also excludes critters (rabbits, squirrels, etc. -- CREATURE_TYPE_CRITTER): valid
            // attack targets per Unit::IsValidAttackTarget, but not something a real player
            // grinds for XP/loot, and killing them en masse across a large solo-bot population
            // serves no purpose worth the extra unit updates.
            std::string const& scriptName = creature->GetScriptName();
            if (scriptName == "npc_training_dummy" || scriptName.rfind("npc_coa_test_", 0) == 0)
                return false;
            if (creature->GetCreatureTemplate() && creature->GetCreatureTemplate()->type == CREATURE_TYPE_CRITTER)
                return false;
        }
        _range = _me->GetDistance(u); // shrink the search radius to the closest hit so far
        return true;
    }

private:
    Unit const* _me;
    float _range;
    uint32 _maxLevel;
};

// Every caller names the subsystem the walk belongs to, so a "stop walking" from one subsystem
// can no longer cancel another's walk in flight -- see BotMovement.h for the bug that cost.
// Returns false when a higher-priority owner holds the movement slot, which a caller that has
// alternatives can act on instead of assuming its walk started.
bool MoveBotToPoint(Player* bot, MoveOwner owner, float x, float y, float z)
{
    return BotMovement::MoveTo(bot, owner, x, y, z);
}

// Real "open, take everything, release" loot flow draining the bot's pending loot queue.
// Allows bots to systematically path to and loot all trash and boss kills in an encounter.
bool TryProcessPendingLoot(Player* bot, uint32 /*diff*/, BotAIState& state)
{
    // If we have a single lastCombatTargetGuid from combat, push it into pendingLootGuids
    if (!state.lastCombatTargetGuid.IsEmpty())
    {
        bool alreadyInQueue = false;
        for (auto const& g : state.pendingLootGuids)
        {
            if (g == state.lastCombatTargetGuid)
            {
                alreadyInQueue = true;
                break;
            }
        }
        if (!alreadyInQueue)
        {
            if (state.pendingLootGuids.size() >= 15)
                state.pendingLootGuids.pop_front();
            state.pendingLootGuids.push_back(state.lastCombatTargetGuid);
        }
        state.lastCombatTargetGuid = ObjectGuid::Empty;
    }

    if (state.pendingLootGuids.empty())
        return false;

    // If leader is far away or fighting, abort looting to stay with group
    if (Group* group = bot->GetGroup())
    {
        if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
        {
            if (leader->IsInCombat() || bot->GetDistance(leader) > 40.0f)
                return false;
        }
    }

    while (!state.pendingLootGuids.empty())
    {
        ObjectGuid corpseGuid = state.pendingLootGuids.front();
        Creature* creature = ObjectAccessor::GetCreature(*bot, corpseGuid);
        if (!creature || creature->IsAlive() || creature->loot.isLooted() || creature->GetMap() != bot->GetMap())
        {
            state.pendingLootGuids.pop_front();
            continue;
        }

        float dist = bot->GetDistance(creature);
        if (dist > 30.0f)
        {
            state.pendingLootGuids.pop_front();
            continue;
        }

        if (dist > INTERACTION_DISTANCE)
        {
            MoveBotToPoint(bot, MoveOwner::Loot, creature->GetPositionX(), creature->GetPositionY(),
                creature->GetPositionZ());
            return true;
        }

        BotMovement::Release(bot, MoveOwner::Loot);

        bool isRecipient = creature->GetLootRecipientGUID() == bot->GetGUID();
        if (!isRecipient && bot->GetGroup())
            isRecipient = creature->GetLootRecipientGroup() == bot->GetGroup();

        if (isRecipient)
        {
            WorldPacket openPacket;
            openPacket << creature->GetGUID();
            bot->GetSession()->HandleLootOpcode(openPacket);

            if (bot->GetLootGUID() == creature->GetGUID())
            {
                Loot& loot = creature->loot;
                BotAI::TakeAllLoot(bot, loot);
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
        }

        state.pendingLootGuids.pop_front();
        return true;
    }

    return false;
}

// Out-of-combat rest behavior: sits and consumes food/drink when HP or mana are depleted between pulls
bool TryRestIfNeeded(Player* bot, uint32 /*diff*/, BotRole role, BotAIState& state)
{
    if (bot->IsInCombat())
    {
        if (state.isResting)
        {
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            state.isResting = false;
        }
        return false;
    }

    if (bot->IsMounted() || state.pendingMountSpellId)
    {
        if (state.isResting)
        {
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            state.isResting = false;
        }
        return false;
    }

    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (Player* member = itr->GetSource())
            {
                if (member->IsInCombat())
                {
                    if (state.isResting)
                    {
                        bot->SetStandState(UNIT_STAND_STATE_STAND);
                        state.isResting = false;
                    }
                    return false;
                }
            }
        }

        if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
        {
            if (leader->IsMounted() || bot->GetDistance(leader) > 35.0f)
            {
                if (state.isResting)
                {
                    bot->SetStandState(UNIT_STAND_STATE_STAND);
                    state.isResting = false;
                }
                return false;
            }
        }
    }

    float hpPct = bot->GetHealthPct();
    bool usesMana = (bot->getPowerType() == POWER_MANA);
    float manaPct = usesMana ? bot->GetPowerPct(POWER_MANA) : 100.0f;

    float startHpPct = 45.0f;
    float startManaPct = 25.0f;
    float endHpPct = 75.0f;
    float endManaPct = 70.0f;

    if (role == BotRole::Healer)
    {
        startHpPct = 60.0f;
        startManaPct = 50.0f;
        endHpPct = 80.0f;
        endManaPct = 85.0f;
    }
    else if (role == BotRole::Tank)
    {
        startHpPct = 60.0f;
        startManaPct = 30.0f;
        endHpPct = 85.0f;
        endManaPct = 60.0f;
    }

    if (!state.isResting)
    {
        bool needsHp = (hpPct < startHpPct);
        bool needsMana = (usesMana && manaPct < startManaPct);
        if (!needsHp && !needsMana)
            return false;

        bot->StopMoving();
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();
        bot->SetStandState(UNIT_STAND_STATE_SIT);
        state.isResting = true;

        if (needsHp && !bot->HasAura(433))
            bot->CastSpell(bot, 433, true);
        if (needsMana && !bot->HasAura(431))
            bot->CastSpell(bot, 431, true);

        return true;
    }

    bool doneHp = (hpPct >= endHpPct);
    bool doneMana = (!usesMana || manaPct >= endManaPct);

    if (doneHp && doneMana)
    {
        bot->SetStandState(UNIT_STAND_STATE_STAND);
        bot->RemoveAurasDueToSpell(433);
        bot->RemoveAurasDueToSpell(431);
        state.isResting = false;
        return false;
    }

    if (hpPct < endHpPct && !bot->HasAura(433))
        bot->CastSpell(bot, 433, true);
    if (usesMana && manaPct < endManaPct && !bot->HasAura(431))
        bot->CastSpell(bot, 431, true);

    return true;
}

bool IsCityOrSanctuary(Player const* bot)
{
    if (bot->IsInSanctuary())
        return true;

    uint32 zoneId = bot->GetZoneId();
    switch (zoneId)
    {
        case 1519: // Stormwind
        case 1537: // Ironforge
        case 1657: // Darnassus
        case 3557: // Exodar
        case 1637: // Orgrimmar
        case 1638: // Thunder Bluff
        case 1497: // Undercity
        case 3487: // Silvermoon
        case 4395: // Dalaran
        case 3703: // Shattrath
            return true;
        default:
            break;
    }
    return false;
}

// Called from UpdateOffensive's idle branch, only for a bot with no group at all (a grouped
// bot follows/assists its leader instead -- see ResumeFollowingLeader). Mirrors a real solo
// player: look for a nearby fight, and don't wander far from one spot doing it. Manual Stay
// already excludes this at the call site, same as it excludes inheriting the leader's target.
// Returns true when it started something this tick (an attack or a walk), so the ambient layer can
// tell a bot that is busy grinding from one whose scans keep coming up empty.
bool TryGrindWhenSolo(Player* bot, uint32 diff, BotAIState& state)
{
    if (IsCityOrSanctuary(bot))
    {
        BotMovement::Release(bot, MoveOwner::Grind);
        return false;
    }

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
        return false;
    }

    if (bot->GetDistance(state.grindAnchorX, state.grindAnchorY, state.grindAnchorZ) > GRIND_LEASH_RADIUS)
    {
        // A grind-chase can drag a bot well past its anchor by the time the fight ends --
        // walk back before looking for another one, same idea as ResumeFollowingLeader walking
        // a grouped bot back to its leader.
        return MoveBotToPoint(bot, MoveOwner::Grind, state.grindAnchorX, state.grindAnchorY, state.grindAnchorZ);
    }

    BotMovement::Release(bot, MoveOwner::Grind);

    if (state.nextGrindScanMs > diff)
    {
        state.nextGrindScanMs -= diff;
        return false;
    }
    state.nextGrindScanMs = GRIND_SCAN_INTERVAL_MS;

    Unit* target = nullptr;
    GrindHostileUnitCheck check(bot, GrindSearchRadius(), bot->GetLevel() + GrindMaxLevelAbove());
    Acore::UnitLastSearcher<GrindHostileUnitCheck> searcher(bot, target, check);
    Cell::VisitObjects(bot, searcher, GrindSearchRadius());

    if (target)
    {
        bot->Attack(target, true);
        return true;
    }
    return false;
}

// Opt-in "Auto Dungeon Mode" (BotMgr::SetAutoDungeonMode, toggled via .botcmd autodungeon on/off
// or the addon's AUTODUNGEON verb) lets a Tank bot run point through an instance on its own.
// It engages nearby hostile packs and pathfinds sequentially to uncleared bosses using real
// creature spawn data and CREATURE_FLAG_EXTRA_DUNGEON_BOSS flags.
void TryAutoPullInInstance(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return;

    Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID());
    if (!leader || leader->IsInCombat())
        return; // already fighting something -- let normal target-inheritance handle it

    Map* map = bot->GetMap();
    if (!map || (!map->IsDungeon() && !map->IsRaid()))
        return;

    if (!sBotMgr->IsAutoDungeonModeEnabled(leader->GetGUID()))
        return;

    // Point runner Tank check
    bool isPointRunner = (BotAI::GetRole(bot->GetGUID()) == BotRole::Tank);
    if (!isPointRunner)
        return;

    // 1. Pull nearby hostile creature
    Unit* target = nullptr;
    GrindHostileUnitCheck check(bot, GrindSearchRadius(), 255);
    Acore::UnitLastSearcher<GrindHostileUnitCheck> searcher(bot, target, check);
    Cell::VisitObjects(bot, searcher, GrindSearchRadius());

    if (target)
    {
        BotMovement::Release(bot, MoveOwner::AutoDungeon);
        bot->Attack(target, true);
        return;
    }

    // 2. Head toward the nearest uncleared dungeon encounter boss
    CreatureDataContainer const& allCreatures = sObjectMgr->GetAllCreatureData();
    float bestDistSq = 99999999.0f;
    Position bestBossPos;
    bool foundBoss = false;

    for (auto const& [spawnId, data] : allCreatures)
    {
        if (data.mapid != map->GetId())
            continue;

        if (sBotMgr->IsBossCleared(leader->GetGUID(), data.id))
            continue;

        CreatureTemplate const* cinfo = sObjectMgr->GetCreatureTemplate(data.id);
        if (!cinfo)
            continue;

        bool isBoss = cinfo->HasFlagsExtra(CREATURE_FLAG_EXTRA_DUNGEON_BOSS) || (cinfo->rank == CREATURE_ELITE_WORLDBOSS);
        if (!isBoss)
            continue;

        float dx = data.posX - bot->GetPositionX();
        float dy = data.posY - bot->GetPositionY();
        float dz = data.posZ - bot->GetPositionZ();
        float distSq = dx * dx + dy * dy + dz * dz;

        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            bestBossPos.Relocate(data.posX, data.posY, data.posZ);
            foundBoss = true;
        }
    }

    if (foundBoss)
    {
        float dist = std::sqrt(bestDistSq);
        if (dist > 8.0f)
        {
            MoveBotToPoint(bot, MoveOwner::AutoDungeon, bestBossPos.GetPositionX(), bestBossPos.GetPositionY(),
                bestBossPos.GetPositionZ());
        }
        else
        {
            BotMovement::Release(bot, MoveOwner::AutoDungeon);
        }
    }
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

// The only correct "can I cast at this node from here" test is the one the spell itself will
// run: Spell::CheckRange hands the GameObject to GameObject::IsAtInteractDistance along with the
// spell being cast, and that accounts for the node's own display bounds. The gathering code used
// to approximate it with GetDistance(node) <= INTERACTION_DISTANCE, which measures on a different
// basis -- Object::GetDistance subtracts both objects' bounding radii -- so a node with a
// sizeable model reported five yards while the spell still saw the bot as out of range. Confirmed
// live: 8040 of 8042 casts failed with SPELL_FAILED_OUT_OF_RANGE, almost all against Wild Mustard
// (GameObject size 1.5). Asking the engine removes the mismatch instead of tuning a magic number.
bool IsInGatherRange(Player* bot, GameObject* node, uint32 gatherSpellId)
{
    return node->IsAtInteractDistance(bot, sSpellMgr->GetSpellInfo(gatherSpellId));
}

bool IsGatherNodeOnRetryCooldown(BotAIState const& state, ObjectGuid nodeGuid)
{
    auto itr = state.gatherNodeRetryAtMs.find(nodeGuid);
    return itr != state.gatherNodeRetryAtMs.end() && itr->second > uint32(GameTime::GetGameTimeMS().count());
}

// Expired entries are dropped here rather than on a timer: the gather code is the only reader of
// the map, so pruning on write keeps it bounded without any per-frame work.
void SetGatherNodeRetryCooldown(BotAIState& state, ObjectGuid nodeGuid)
{
    uint32 nowMs = uint32(GameTime::GetGameTimeMS().count());
    for (auto itr = state.gatherNodeRetryAtMs.begin(); itr != state.gatherNodeRetryAtMs.end();)
        itr = (itr->second <= nowMs) ? state.gatherNodeRetryAtMs.erase(itr) : std::next(itr);

    state.gatherNodeRetryAtMs[nodeGuid] = nowMs + GATHER_NODE_RETRY_MS;
}

// Loot ids of chest-type objects whose every loot row requires a quest. Filled once at startup by
// BotAI::LoadGatherLootData -- the split is static data, so there's no reason to re-derive it.
std::unordered_set<uint32> _questOnlyGameObjectLoot;

// A node whose loot is entirely quest drops opens empty for anyone without that quest; a real
// client doesn't even highlight it for them. Wild Mustard in Dalaran is the case confirmed live:
// its only loot row is QuestRequired, level 80 bots are relocated to Dalaran, and 174 of them spent
// their gathering on the same 7 quest herbs, 984 successful casts for a single real gather.
// Mixed nodes -- ordinary herbs that also carry a quest drop -- are deliberately not excluded:
// 51 such templates with 4300 spawns exist, and hiding them would remove real gathering.
bool YieldsNothingForBot(GameObject const* go, Player const* bot)
{
    uint32 lootId = go->GetGOInfo()->GetLootId();
    return _questOnlyGameObjectLoot.count(lootId) && !LootTemplates_Gameobject.HaveQuestLootForPlayer(lootId, bot);
}

// Same shrinking-radius nearest-in-range shape as GrindHostileUnitCheck, but for a lockable
// herbalism/mining node this bot's own skill can actually open. Fills in gatherSpellId with
// whichever of the two gathering spells applies to whatever node is found. Nodes this bot has
// recently failed on are skipped, so a single unreachable node can't monopolise every scan.
class GatherableNodeCheck
{
public:
    GatherableNodeCheck(Player const* bot, float range, uint32& gatherSpellId, BotAIState const& state)
        : _bot(bot), _range(range), _gatherSpellId(gatherSpellId), _state(state) { }

    bool operator()(GameObject* go)
    {
        if (!go->isSpawned() || !_bot->IsWithinDistInMap(go, _range))
            return false;

        if (IsGatherNodeOnRetryCooldown(_state, go->GetGUID()))
            return false;

        if (YieldsNothingForBot(go, _bot))
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
    BotAIState const& _state;
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
    if (!node || bot->GetLootGUID() != node->GetGUID())
    {
        // A cast that started fine can still end without opening anything -- interrupted, or the
        // node despawned mid-cast. Without the cooldown the next scan re-picks the same node.
        SetGatherNodeRetryCooldown(state, nodeGuid);
        LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' got nothing from node {} ({}).",
            bot->GetName(), nodeGuid.ToString(), node ? "cast ended without opening the node" : "node gone");
        return;
    }

    bool wasAlreadyLooted = node->loot.isLooted();
    if (!wasAlreadyLooted)
        BotAI::TakeAllLoot(bot, node->loot);

    // Always close the loot window once it opened, empty or not, exactly as a real client does.
    // WorldSession::DoLootRelease is the only place a fully looted chest-type node is moved to
    // GO_JUST_DEACTIVATED (despawn, then respawn with fresh loot) and has its loot cleared. The
    // old early return on an already-looted node skipped that, so the node stayed open and empty
    // forever and every later bot re-opened it -- confirmed live: 2269 "already looted" results
    // across only 7 distinct nodes in four minutes, against a single real gather. Releasing here
    // also un-sticks any node some earlier opener left in that state.
    WorldPacket releasePacket;
    releasePacket << node->GetGUID();
    bot->GetSession()->HandleLootReleaseOpcode(releasePacket);

    if (wasAlreadyLooted)
    {
        SetGatherNodeRetryCooldown(state, nodeGuid);
        LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' got nothing from node {} (already looted, released).",
            bot->GetName(), nodeGuid.ToString());
        return;
    }

    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' gathered from node {} {}.", bot->GetName(), node->GetEntry(),
        nodeGuid.ToString());
}

// Shared by both entry points (already in range on the scan, or arrived after a walk) so the
// cast and its failure handling live in exactly one place. A failed cast puts the node on
// retry cooldown: before this, gatherTargetGuid was simply left unset on failure, the next scan
// found the same node again, and the bot looped scan -> walk -> fail indefinitely.
//
// Arriving is not the same as standing still. The walk's MovePoint targets the node's centre, so
// the bot crosses into interact distance while still walking, and Spell::CheckCast refuses any
// cast-time spell while Unit::isMoving() -- confirmed live: once the range mismatch was fixed, all
// 2051 failures in the next four minutes were SPELL_FAILED_MOVING. Stopping and casting in the
// same tick isn't enough either, because the movement flags only clear once the stop has been
// processed, so the cast is handed back to TryContinueGatherWalk for the following tick.
void CastGatherAt(Player* bot, BotAIState& state, GameObject* node, uint32 gatherSpellId)
{
    BotMovement::Release(bot, MoveOwner::Gather);

    if (bot->isMoving())
    {
        bot->StopMoving();
        state.gatherWalkTargetGuid = node->GetGUID();
        if (!state.gatherWalkTimeoutMs)
            state.gatherWalkTimeoutMs = GATHER_WALK_TIMEOUT_MS;
        return;
    }

    state.gatherWalkTimeoutMs = 0;

    if (bot->IsMounted())
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);

    SpellCastResult result = bot->CastSpell(node, gatherSpellId, false);
    if (result == SPELL_CAST_OK)
    {
        state.gatherTargetGuid = node->GetGUID();
        LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' cast gathering spell {} on node {} {}.",
            bot->GetName(), gatherSpellId, node->GetEntry(), node->GetGUID().ToString());
        return;
    }

    SetGatherNodeRetryCooldown(state, node->GetGUID());
    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' failed gathering spell {} on node {} (result {}), "
        "skipping it for {}s.", bot->GetName(), gatherSpellId, node->GetEntry(), uint32(result),
        GATHER_NODE_RETRY_MS / IN_MILLISECONDS);
}

void AbandonGatherWalk(Player* bot, BotAIState& state, char const* reason)
{
    LOG_DEBUG("module.coa-playerbots", "BotAI: bot '{}' abandoned gather walk ({}).", bot->GetName(), reason);
    SetGatherNodeRetryCooldown(state, state.gatherWalkTargetGuid);
    state.gatherWalkTargetGuid = ObjectGuid::Empty;
    BotMovement::Release(bot, MoveOwner::Gather);
}

// Checked every idle-solo tick while state.gatherWalkTargetGuid is set (a node TryStartGathering
// found but was too far to cast at yet). Deliberately does NOT touch movement while still out of
// range -- the MovePoint TryStartGathering already issued keeps running on its own; re-checking
// or re-issuing it here was the original bug (see gatherWalkTargetGuid's own comment in
// BotAIState). Arrival is judged by the same engine check the cast will use (IsInGatherRange),
// and a walk that never gets there within GATHER_WALK_TIMEOUT_MS is abandoned with the node put
// on retry cooldown, so an unreachable node can't hold the bot forever.
void TryContinueGatherWalk(Player* bot, uint32 diff, BotAIState& state)
{
    GameObject* node = ObjectAccessor::GetGameObject(*bot, state.gatherWalkTargetGuid);
    if (!node || !node->isSpawned())
    {
        state.gatherWalkTargetGuid = ObjectGuid::Empty;
        BotMovement::Release(bot, MoveOwner::Gather);
        return;
    }

    uint32 gatherSpellId = GatherSpellForNode(bot, node);
    if (!gatherSpellId)
    {
        AbandonGatherWalk(bot, state, "skill no longer matches node");
        return;
    }

    // Counted on every tick of the walk, including the in-range ticks spent waiting for the bot to
    // stop -- if the budget only ran while out of range, a bot whose movement flags never cleared
    // would bounce between here and CastGatherAt forever.
    bool inRange = IsInGatherRange(bot, node, gatherSpellId);
    if (state.gatherWalkTimeoutMs <= diff)
    {
        AbandonGatherWalk(bot, state, inRange ? "never stopped moving at the node" : "never reached interact distance");
        return;
    }
    state.gatherWalkTimeoutMs -= diff;

    if (!inRange)
    {
        if (bot->GetDistance(node) > 40.0f && !bot->IsMounted())
            TryMount(bot, state, false);
        return; // still walking -- nothing to do until it arrives, times out, or the caller re-decides
    }

    state.gatherWalkTargetGuid = ObjectGuid::Empty;
    CastGatherAt(bot, state, node, gatherSpellId);
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
    GatherableNodeCheck check(bot, GATHER_SEARCH_RADIUS, gatherSpellId, state);
    Acore::GameObjectLastSearcher<GatherableNodeCheck> searcher(bot, node, check);
    Cell::VisitObjects(bot, searcher, GATHER_SEARCH_RADIUS);

    if (!node || !gatherSpellId)
        return false;

    if (!IsInGatherRange(bot, node, gatherSpellId))
    {
        if (!MoveBotToPoint(bot, MoveOwner::Gather, node->GetPositionX(), node->GetPositionY(), node->GetPositionZ()))
            return false;

        state.gatherWalkTargetGuid = node->GetGUID();
        state.gatherWalkTimeoutMs = GATHER_WALK_TIMEOUT_MS;
        if (bot->GetDistance(node) > 40.0f && !bot->IsMounted())
            TryMount(bot, state, false);
        return true;
    }

    CastGatherAt(bot, state, node, gatherSpellId);
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

    // No fishing pole in bags -- auto-grant a standard Fishing Pole (item 6256)
    if (bot->AddItem(6256, 1))
    {
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        {
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot); IsFishingPole(item))
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

// Checked every idle-solo tick while channeling Fishing or while fishingBobberGuid is active.
// Fishing is a CHANNELED spell (SPELL_FISHING 7620) whose summoned bobber reaches GO_READY
// while the channel is active. When a fish bites, we apply a realistic human-like reaction delay,
// use the bobber to generate and autostore loot, and conclude the channel.
void TryFinishFishing(Player* bot, uint32 diff, BotAIState& state)
{
    if (state.fishingBobberGuid.IsEmpty())
    {
        ObjectGuid chanGuid = bot->GetGuidValue(UNIT_FIELD_CHANNEL_OBJECT);
        if (!chanGuid.IsEmpty())
            state.fishingBobberGuid = chanGuid;
    }

    Spell* chanSpell = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (!chanSpell || chanSpell->GetSpellInfo()->Id != SPELL_FISHING)
    {
        state.fishingBobberGuid = ObjectGuid::Empty;
        state.fishingCastInProgress = false;
        return;
    }

    GameObject* bobber = ObjectAccessor::GetGameObject(*bot, state.fishingBobberGuid);
    if (!bobber)
    {
        std::list<GameObject*> nearby;
        Acore::AllGameObjectsWithEntryInRange check(bot, FISHING_BOBBER_ENTRY, FISHING_MAX_DISTANCE + 5.0f);
        Acore::GameObjectListSearcher<Acore::AllGameObjectsWithEntryInRange> searcher(bot, nearby, check);
        Cell::VisitObjects(bot, searcher, FISHING_MAX_DISTANCE + 5.0f);
        for (GameObject* go : nearby)
        {
            if (go->GetOwnerGUID() == bot->GetGUID())
            {
                bobber = go;
                state.fishingBobberGuid = go->GetGUID();
                break;
            }
        }
    }

    if (!bobber || bobber->GetOwnerGUID() != bot->GetGUID())
    {
        if (state.fishingTimeoutMs <= diff)
        {
            state.fishingBobberGuid = ObjectGuid::Empty;
            bot->InterruptSpell(CURRENT_CHANNELED_SPELL);
        }
        else
            state.fishingTimeoutMs -= diff;
        return;
    }

    if (bobber->getLootState() != GO_READY)
    {
        if (state.fishingTimeoutMs <= diff)
        {
            state.fishingBobberGuid = ObjectGuid::Empty;
            bot->InterruptSpell(CURRENT_CHANNELED_SPELL);
        }
        else
            state.fishingTimeoutMs -= diff;
        return;
    }

    if (state.fishingReactionDelayMs > diff)
    {
        state.fishingReactionDelayMs -= diff;
        return;
    }

    state.fishingBobberGuid = ObjectGuid::Empty;
    bobber->Use(bot);

    if (bot->GetLootGUID() == bobber->GetGUID())
    {
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

    bot->InterruptSpell(CURRENT_CHANNELED_SPELL);
}

void TryWaitForFishingCast(Player* bot, uint32 diff, BotAIState& state)
{
    TryFinishFishing(bot, diff, state);
}

bool TryStartFishing(Player* bot, uint32 diff, BotAIState& state)
{
    if (state.nextFishingScanMs > diff)
    {
        state.nextFishingScanMs -= diff;
        return false;
    }
    state.nextFishingScanMs = FISHING_SCAN_INTERVAL_MS;

    if (bot->IsMounted())
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);

    float waterX = 0.0f, waterY = 0.0f, waterZ = 0.0f;
    if (!FindNearbyWater(bot, waterX, waterY, waterZ))
        return false;

    if (!EnsureFishingPoleEquipped(bot))
        return false;

    if (!bot->HasSpell(SPELL_FISHING))
        bot->learnSpell(SPELL_FISHING);

    bot->SetOrientation(bot->GetAngle(waterX, waterY));

    SpellCastResult result = bot->CastSpell(bot, SPELL_FISHING, false);
    LOG_INFO("module.coa-playerbots", "BotAI: bot '{}' cast Fishing (result {}).", bot->GetName(), uint32(result));
    if (result != SPELL_CAST_OK)
        return false;

    state.fishingCastInProgress = false;
    state.fishingTimeoutMs = FISHING_BITE_TIMEOUT_MS;
    state.fishingReactionDelayMs = urand(400, 800);

    ObjectGuid bobberGuid = bot->GetGuidValue(UNIT_FIELD_CHANNEL_OBJECT);
    if (!bobberGuid.IsEmpty())
        state.fishingBobberGuid = bobberGuid;

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

void ResumeFollowingLeader(Player* bot, BotAIState& state)
{
    // If the bot is already mounted, its mount cast is definitely completed
    if (bot->IsMounted())
        state.pendingMountSpellId = 0;

    // Suppress follow movement while a mount attempt is pending or the bot is actively casting
    // a non-melee spell (such as a 1.5s mount cast). Issuing movement while casting immediately
    // aborts the spell cast.
    if (state.pendingMountSpellId || bot->IsNonMeleeSpellCast(false))
        return;

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

    Map* map = bot->GetMap();
    if (map && (map->IsDungeon() || map->IsRaid()) && sBotMgr->IsAutoDungeonModeEnabled(group->GetLeaderGUID()))
    {
        // In auto-dungeon mode, followers follow the Tank (the point runner) rather than the player leader,
        // or if this bot IS the tank, it navigates toward the current boss/trash rather than following anyone.
        // Toggling auto-dungeon off cleanly restores standard following.
        Player* tank = nullptr;
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (Player* m = itr->GetSource())
            {
                if (BotAI::GetRole(m->GetGUID()) == BotRole::Tank)
                {
                    tank = m;
                    break;
                }
            }
        }

        if (bot == tank)
            return; // Tank runs point in TryAutoPullInInstance

        Player* targetToFollow = tank ? tank : leader;
        if (targetToFollow && targetToFollow != bot)
        {
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
                bot->GetMotionMaster()->MoveFollow(targetToFollow, BotAI::ComputeFollowDistance(bot), BotAI::ComputeFollowAngle(bot));
        }
        return;
    }

    // Normal follow behavior with Deadzone Slack & Reaction Latency:
    float dist = bot->GetDistance(leader);
    float followDist = BotAI::ComputeFollowDistance(bot);

    // Deadzone slack: if leader is stationary and bot is within acceptable range, stay idle
    if (!leader->isMoving() && dist >= (followDist - 1.5f) && dist <= (followDist + 2.5f))
    {
        ClearActiveFollow(bot);
        state.followWaitingReaction = false;
        return;
    }

    // Reaction latency when leader moves:
    if (leader->isMoving() && !state.followWaitingReaction && bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
    {
        state.followWaitingReaction = true;
        state.followReactionDelayMs = 150 + (bot->GetGUID().GetCounter() % 250);
        return;
    }

    if (state.followWaitingReaction)
        return;

    if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != FOLLOW_MOTION_TYPE)
        bot->GetMotionMaster()->MoveFollow(leader, followDist, BotAI::ComputeFollowAngle(bot));
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
        float dist = bot->GetDistance(candidate);
        if (dist > 45.0f)
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

// Finds whatever the bot's group is currently fighting -- prefers the designated group leader
// (usual case, and keeps prior behavior when the leader IS the one fighting), but falls through
// to ANY other online member already in combat. Confirmed live: DPS/healer bots that only ever
// checked group->GetLeaderGUID() sat there doing nothing whenever the real player (who's
// usually the group leader in this project's Quick-Fill-formed groups) stood back and let the
// Tank bot pull -- the player's own Player::IsInCombat() stays false the whole fight unless
// something personally targets/is attacked by them, so nothing ever inherited a target from the
// Tank actually fighting a few yards away. A real party's DPS reacts to whoever pulled, not
// specifically to the party leader.
Unit* FindGroupCombatTarget(Player* bot, Group* group)
{
    if (!group)
        return nullptr;

    // A member's GetVictim() (who THEY'RE swinging on) is the strongest signal, but confirmed
    // live it isn't always reliable for a real player who pulled with a ranged spell rather than
    // melee -- bots stood completely still the moment the real player attacked something,
    // because IsInCombat() went true but GetVictim() stayed null. GetSelectedUnit() (whatever
    // they currently have targeted) and getAttackers() (who's hitting them back) both cover
    // that gap without needing melee autoattack to have fired.
    auto combatTargetOf = [](Player* member) -> Unit*
    {
        if (!member->IsInCombat())
            return nullptr;
        if (Unit* victim = member->GetVictim())
            return victim;
        if (Unit* selected = member->GetSelectedUnit())
            if (selected->IsAlive() && member->IsValidAttackTarget(selected))
                return selected;
        for (Unit* attacker : member->getAttackers())
            if (attacker && attacker->IsAlive() && member->IsValidAttackTarget(attacker))
                return attacker;
        for (auto const& pair : member->GetCombatManager().GetPvECombatRefs())
        {
            if (CombatReference* ref = pair.second)
            {
                if (!ref->IsSuppressedFor(member))
                {
                    if (Unit* enemy = ref->GetOther(member))
                        if (enemy->IsAlive() && member->IsValidAttackTarget(enemy))
                            return enemy;
                }
            }
        }
        for (auto const& pair : member->GetCombatManager().GetPvPCombatRefs())
        {
            if (CombatReference* ref = pair.second)
            {
                if (!ref->IsSuppressedFor(member))
                {
                    if (Unit* enemy = ref->GetOther(member))
                        if (enemy->IsAlive() && member->IsValidAttackTarget(enemy))
                            return enemy;
                }
            }
        }
        return nullptr;
    };

    if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
        if (leader != bot)
            if (Unit* target = combatTargetOf(leader))
                return target;

    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* member = itr->GetSource();
        if (!member || member == bot)
            continue;
        if (Unit* target = combatTargetOf(member))
            return target;
    }
    return nullptr;
}

// Tank-only: scans nearby hostiles for one currently attacking a GROUPMATE other than this
// tank. Confirmed live -- explicit user feedback: "tank doesn't watch aggro on teammates, never
// reacts, even though it should throw taunt." The existing taunt-priority check below only ever
// looked at `target->GetVictim()` -- the tank's own single currently-engaged enemy -- so a
// SECOND enemy peeling off onto the healer or a DPS went completely unnoticed; nothing was ever
// even looking at any other nearby hostile. A real tank picks up a loose add immediately, even
// while already tanking something else fine, rather than let it free-hit the healer.
Unit* FindAllyThreatenedTarget(Player* tank, float range = 30.0f)
{
    Group* group = tank->GetGroup();
    if (!group)
        return nullptr;

    std::vector<Unit*> enemies;
    HostileEnemyCheck check(tank, tank, range);
    Acore::UnitListSearcher<HostileEnemyCheck> searcher(tank, enemies, check);
    Cell::VisitObjects(tank, searcher, range);

    for (Unit* enemy : enemies)
    {
        Unit* victim = enemy->GetVictim();
        if (!victim || victim == tank || !victim->IsAlive())
            continue;
        Player* victimPlayer = victim->ToPlayer();
        if (!victimPlayer || victimPlayer->GetGroup() != group)
            continue; // ignore an enemy fighting some unrelated bystander
        return enemy;
    }
    return nullptr;
}

// Dps and Tank share this whole loop -- Tank's only difference is a taunt-priority check
// spliced in right before the normal offensive-spell pick (see the `role == BotRole::Tank`
// branch below). Also used as a Healer's fallback when nobody currently needs healing (a
// real healer doesn't stand still doing nothing just because no one's low), passed
// BotRole::Dps so no taunt logic applies -- a healer without a Tank role assigned shouldn't
// be trying to hold aggro.
AmbientLean LeanFor(SoloIntent intent)
{
    switch (intent)
    {
        case SoloIntent::Quest:   return AmbientLean::Quest;
        case SoloIntent::Gather:  return AmbientLean::Gather;
        case SoloIntent::Fish:    return AmbientLean::Fish;
        case SoloIntent::Grind:   return AmbientLean::Grind;
        case SoloIntent::Explore: return AmbientLean::Explore;
        default:                  return AmbientLean::None;
    }
}

AmbientProfile MakeAmbientProfile(Player* bot, BotAIState& state)
{
    EnsurePersonality(bot, state);
    BotPersonality const& p = state.personality;
    AmbientProfile profile;
    profile.seed = p.seed;
    profile.questing = p.questing;
    profile.gathering = p.gathering;
    profile.grinding = p.grinding;
    profile.patience = p.patience;
    profile.sociability = p.sociability;
    profile.lean = LeanFor(state.soloIntent);
    return profile;
}

WorldPersona MakeWorldPersona(Player* bot, BotAIState& state)
{
    EnsurePersonality(bot, state);
    BotPersonality const& p = state.personality;
    WorldPersona persona;
    persona.seed = p.seed;
    persona.questing = p.questing;
    persona.gathering = p.gathering;
    persona.fishing = p.fishing;
    persona.grinding = p.grinding;
    persona.patience = p.patience;
    persona.sociability = p.sociability;
    persona.lean = uint8(state.soloIntent);
    return persona;
}

// The idle-solo tick of an ungrouped bot with no fight, no loot and no rest to do. Order:
//  1. an explicit guild gather order from a player;
//  2. the ambient layer's needs (repair, selling) and any errand or flight already in flight --
//     they pause the brain's task while they have the bot;
//  3. the open-world brain (world/WorldBrain.h), which either acts itself (quests, travel) or
//     names the one activity -- gather, fish, grind, ambient life -- that gets this tick.
// Before the brain existed, SoloIntent's scan order, the quest scans, the long-distance quest walk,
// the grind anchor and the ambient errands each decided on their own to move the bot; now exactly
// one of them runs per tick, the one the brain chose.
void UpdateSoloWorld(Player* bot, uint32 diff, BotAIState& state)
{
    // Active guild gather order: travel to target area and gather the requested resource
    BotMgr::GuildGatherOrder const* gatherOrder = sBotMgr->GetGuildGatherOrder(bot->GetGUID());
    if (gatherOrder && gatherOrder->remainingCount > 0)
    {
        // An explicit instruction owns the bot: the open-world task steps back with its clocks
        // stopped, exactly as for any other manual command.
        WorldBrain::Suspend(bot, SuspendReason::ManualCommand);
        if (gatherOrder->hasTargetLocation)
        {
            if (bot->GetMapId() != gatherOrder->targetMapId)
            {
                bot->TeleportTo(gatherOrder->targetMapId, gatherOrder->targetX, gatherOrder->targetY, gatherOrder->targetZ, 0.0f);
                sBotMgr->QueueTeleportAck(bot->GetSession());
                return;
            }

            float dist = bot->GetExactDist2d(gatherOrder->targetX, gatherOrder->targetY);
            if (dist > 40.0f)
            {
                if (!bot->IsMounted())
                    TryMount(bot, state, false);
                MoveBotToPoint(bot, MoveOwner::Gather, gatherOrder->targetX, gatherOrder->targetY,
                    gatherOrder->targetZ);
                return;
            }
        }
        if (!TryStartGathering(bot, diff, state))
            TryGrindWhenSolo(bot, diff, state);
        return;
    }

    // Persistent half-hour lean (personality + session variety); the brain uses it as a bias.
    UpdateSoloIntent(bot, diff, state);

    AmbientProfile ambientProfile = MakeAmbientProfile(bot, state);
    AmbientTick ambient = BotWorldBehavior::UpdateBeforeSolo(bot, ambientProfile);
    if (ambient == AmbientTick::Busy)
    {
        WorldBrain::NotifyAmbientBusy(bot);
        return;
    }
    if (ambient == AmbientTick::Relocated)
        state.hasGrindAnchor = false;

    WorldDirective directive = WorldBrain::Update(bot, diff, MakeWorldPersona(bot, state));
    bool started = false;
    switch (directive)
    {
        case WorldDirective::Busy:
            // The brain is walking the bot around for a task: whatever grind anchor existed from
            // before is somewhere else now, and must not pull the bot back later.
            state.hasGrindAnchor = false;
            return;
        case WorldDirective::Gather:
            started = TryStartGathering(bot, diff, state);
            break;
        case WorldDirective::Fish:
            started = TryStartFishing(bot, diff, state);
            break;
        case WorldDirective::Grind:
            started = TryGrindWhenSolo(bot, diff, state);
            break;
        case WorldDirective::Idle:
            return;
        default:
            break;
    }

    if (directive != WorldDirective::Ambient)
        WorldBrain::ReportActivity(bot, directive, started);

    // Cosmetic errands (town services, wandering, a trip to a gathering or grinding area) only
    // start when the chosen activity found nothing to do.
    BotWorldBehavior::UpdateAfterSolo(bot, ambientProfile, started);
}

// combatRole drives movement/positioning/taunt-eligibility behavior; profileRole is what's
// looked up in the Data-Driven profile registry (item 12, Phase 2 fixup) -- normally the same
// value, except for Support, which fights like Dps (combatRole) but must still find its own
// Support-tagged profile (profileRole), not silently fail profile lookup and fall back to the
// generic spellbook chain forever.
void UpdateOffensive(Player* bot, uint32 diff, BotRole combatRole, BotRole profileRole, BotAIState& state)
{
    Unit* target = bot->GetVictim();
    // Stay means "don't go looking for a fight" -- skip inheriting the leader's target, but
    // an existing victim (e.g. a Pull that just fired) and the attacker-retaliation fallback
    // below both still apply, since Stay is about not *initiating*, not about refusing to
    // fight at all.
    if (!target && state.manualCommand != BotManualCommand::Stay)
        target = FindGroupCombatTarget(bot, bot->GetGroup());

    if (!target && bot->InBattleground())
        target = BotBattlegroundAI::FindHostilePvPTarget(bot, 40.0f);

    // Being attacked does not, by itself, make a Player "have a victim" -- Unit::GetVictim()
    // tracks who *this* unit is attacking, not who's attacking it (this matches how a real
    // client behaves too: nothing auto-retaliates without an explicit attack action). Without
    // this, a bot with no group/leader (or whose leader isn't fighting) would just stand there
    // and take free hits from anything that aggroes it with no retaliation at all.
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

    // Comprehensive combat reference check: casters, channelers, and ranged enemies that do not
    // register in melee getAttackers() are captured directly from the unit's active CombatReferences.
    if (!target && bot->IsInCombat())
    {
        for (auto const& pair : bot->GetCombatManager().GetPvECombatRefs())
        {
            if (CombatReference* ref = pair.second)
            {
                if (!ref->IsSuppressedFor(bot))
                {
                    if (Unit* enemy = ref->GetOther(bot))
                    {
                        if (enemy->IsAlive() && bot->IsValidAttackTarget(enemy))
                        {
                            target = enemy;
                            break;
                        }
                    }
                }
            }
        }
        if (!target)
        {
            for (auto const& pair : bot->GetCombatManager().GetPvPCombatRefs())
            {
                if (CombatReference* ref = pair.second)
                {
                    if (!ref->IsSuppressedFor(bot))
                    {
                        if (Unit* enemy = ref->GetOther(bot))
                        {
                            if (enemy->IsAlive() && bot->IsValidAttackTarget(enemy))
                            {
                                target = enemy;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    // Tank-only, checked before positioning/casting settle on anything: a teammate being
    // free-hit by a loose add outranks whatever this tank was already doing, including having
    // no target at all. Scored (item 9/#3, Phase 2) instead of "first match" so a tank doesn't
    // drop a boss for a loose add that barely tapped a full-HP DPS -- see ThreatEvaluator. Old
    // FindAllyThreatenedTarget kept as-is (item 23) in case this ever needs a plain fallback.
    bool threatOverride = false;
    if (combatRole == BotRole::Tank && state.manualCommand != BotManualCommand::Stay)
    {
        // ThreatDecision distinguishes "no real candidates at all" from "found candidates, none
        // warrant switching" (item 8, Phase 2 fixup) -- the plain first-match legacy fallback
        // only makes sense for the former; falling back to it after the latter would silently
        // overrule a deliberate, already-scored decision with an unscored first match.
        ThreatDecision threatDecision = ThreatEvaluator::SelectThreatDecision(bot);
        Unit* allyThreat = threatDecision.target;
        if (!threatDecision.hadCandidates)
            allyThreat = FindAllyThreatenedTarget(bot); // plain first-match fallback, item 23
        if (allyThreat && allyThreat != target)
        {
            LOG_INFO("module.coa-playerbots", "CombatAI: bot '{}' picked up threat target '{}', reason=ally_threatened.",
                bot->GetName(), allyThreat->GetName());
        }
        if (allyThreat)
        {
            target = allyThreat;
            threatOverride = true;
        }
    }

    // General target refinement (item 1/#8, Phase 2): among nearby hostiles, prefer whichever
    // actually matters most right now (attacking the healer, mid-cast, low HP, the tank's/
    // leader's own target) over whatever was simply acquired first, with a stickiness margin so
    // the bot doesn't ping-pong between similarly-threatening enemies every tick -- see
    // TargetEvaluator. Skipped when ThreatEvaluator just picked this tick's target for a Tank:
    // that's a higher-priority, already-scored decision this shouldn't immediately second-guess.
    if (target && !threatOverride)
    {
        if (Unit* refined = TargetEvaluator::RefineTarget(bot, target))
        {
            if (refined != target)
                LOG_INFO("module.coa-playerbots", "CombatAI: bot '{}' switched target '{}' -> '{}', reason=target_score.",
                    bot->GetName(), target->GetName(), refined->GetName());
            target = refined;
        }
    }

    if (target && !target->IsAlive())
    {
        BotAI::EnqueuePendingLoot(bot, target->GetGUID());
    }
    else if (target && target->IsAlive() && bot->IsValidAttackTarget(target))
    {
        if (bot->IsMounted())
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        state.lastCombatTargetGuid = target->GetGUID();
    }

    if (!target || !target->IsAlive() || !bot->IsValidAttackTarget(target))
    {
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();

        if (!bot->IsInCombat())
        {
            if (TryProcessPendingLoot(bot, diff, state))
                return;

            if (TryRestIfNeeded(bot, diff, combatRole, state))
                return;

            if (!bot->InBattleground())
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
            // The leader of a temporary bot-only party (WorldParties) keeps living its own open-world
            // life while grouped; the other members are ordinary grouped bots that follow it.
            bool worldPartyLeader = bot->GetGroup() && WorldParties::IsLeader(bot->GetGUID());
            if ((!bot->GetGroup() || worldPartyLeader) && state.manualCommand != BotManualCommand::Stay)
            {
                // An in-flight gathering or fishing action finishes before anything else is
                // decided; everything else goes through the open-world layer (UpdateSoloWorld).
                if (!state.gatherTargetGuid.IsEmpty())
                    TryFinishGathering(bot, state);
                else if (!state.gatherWalkTargetGuid.IsEmpty())
                    TryContinueGatherWalk(bot, diff, state);
                else if (!state.fishingBobberGuid.IsEmpty() || (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL) && bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL)->GetSpellInfo()->Id == SPELL_FISHING))
                    TryFinishFishing(bot, diff, state);
                else if (state.fishingCastInProgress)
                    TryWaitForFishingCast(bot, diff, state);
                else
                    UpdateSoloWorld(bot, diff, state);
            }
            else
            {
                // A group or a manual Stay owns the bot now: the open-world layer steps back and
                // drops every claim it holds (movement, reserved mobs, area occupancy) until the
                // bot is solo and free again.
                WorldBrain::Suspend(bot, bot->GetGroup() ? SuspendReason::Grouped : SuspendReason::ManualCommand);
                if (combatRole == BotRole::Tank && state.manualCommand != BotManualCommand::Stay)
                    TryAutoPullInInstance(bot);
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
    float preferredDist = GetBotPreferredEngageDistance(bot, combatRole);
    float distance = bot->GetDistance(target);

    // Positioning is handled once, up front, independent of whatever ends up castable this
    // tick -- a real engine chase range band (MotionMaster::ChaseRange) so ranged/caster bots
    // actually retreat if the target closes in on them (kiting), not just approach when too
    // far. Previously this only ran from inside the "found a spell to cast" branch, and only
    // ever chased *closer*: (a) any bot whose chosen spell happened to target the enemy
    // directly (not itself) skipped the movement adjustment entirely -- confirmed live: a
    // melee bot with even one longer-range utility spell in its kit would cast that from afar
    // and never close in for its actual melee attacks, because only the self-cast branch
    // touched movement; and (b) nothing ever moved a ranged bot *away* once its target closed
    // the gap, so ranged bots never kited, they just stood there eating melee hits. Letting a
    // persistent ChaseMovementGenerator run does both jobs on its own from here on: it's a
    // no-op whenever already inside its band, and the engine itself decides whether to
    // approach or retreat as the target moves.
    // Ground hazard avoidance (void zones, death and decay, rain of fire, poison clouds)
    if (BotAvoidance::TryAvoidGroundHazards(bot))
        return;

    // Boss avoidance (frontal cleaves, point-blank AoE / whirlwind)
    if (BotAvoidance::TryAvoidBossTelegraphedAttacks(bot, target))
        return;

    // CC protection policy (item 6, Phase 2 fixup): damaging a target under breakable crowd
    // control -- even via auto-attack -- breaks it. TargetEvaluator's own scoring already tries
    // to steer away from a CC'd target (see ScoreTarget's penalty and RefineTarget's forced-
    // switch handling), but when there's no other engaged target at all, it has nothing better
    // to offer and returns the CC'd one anyway as "best of what's left." This is the backstop for
    // that case: never stun-lock into a groupmate's sheep/fear/sap just because it's still
    // technically "the target." A hard stun/root is deliberately NOT covered here (see
    // IsUnitUnderBreakableCrowdControl's own comment) -- only mechanics that actually break on
    // damage warrant holding off.
    if (IsUnitUnderBreakableCrowdControl(target))
    {
        if (Unit* alternative = TargetEvaluator::FindEngagedAlternative(bot, target))
        {
            LOG_INFO("module.coa-playerbots", "CombatAI: bot '{}' switched off breakable-CC'd target '{}' onto '{}'.",
                bot->GetName(), target->GetName(), alternative->GetName());
            target = alternative;
        }
        else
        {
            if (bot->GetVictim())
                bot->AttackStop();
            LOG_INFO("module.coa-playerbots", "CombatAI: bot '{}' holding damage -- target '{}' is breakable-CC'd and no other engaged target exists.",
                bot->GetName(), target->GetName());
            return;
        }
    }

    if (preferredDist > MELEE_ENGAGE_RANGE)
    {
        // A comfortable band, not a razor-thin one -- retreats once the enemy closes past
        // ~70% of preferredDist, holds anywhere between that and preferredDist itself.
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
            bot->GetMotionMaster()->MoveChase(target, ChaseRange(preferredDist * 0.7f, preferredDist));
    }
    else if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
        bot->GetMotionMaster()->MoveChase(target);

    // Auto-attack and facing run once a valid combat target is acquired, independent of
    // ability/spell selection -- a real player starts auto-attacking immediately while casting
    // abilities in parallel.
    bot->SetInFront(target);
    bot->SetFacingToObject(target);

    if (preferredDist > MELEE_ENGAGE_RANGE)
    {
        if (uint32 autoRepeatSpell = FindKnownAutoRepeatRangedSpell(bot))
        {
            if (!bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
                bot->CastSpell(target, autoRepeatSpell, false);
        }

        if (bot->GetVictim() != target)
            bot->Attack(target, false);
    }
    else if (bot->GetVictim() != target || !bot->HasUnitState(UNIT_STATE_MELEE_ATTACKING))
    {
        bot->Attack(target, true);
    }

    // Global Combat Utility Layer: emergency taunt/interrupt/cleanse ahead of role rotation --
    // see engine/CombatUtility.h's own comment and item 3/22 of the combat-engine rework. Runs
    // off the bot's real spellbook shape, so it covers every class regardless of whether its
    // profile (if any) bothers to tag Interrupt/Cleanse -- most don't yet (see ProfileRegistry
    // audit notes), and this is the generic floor under all of them, not a replacement for a
    // profile's own hand-tuned Taunt/Interrupt entries where those do exist.
    //
    // Built once and shared with RoleEngine below (item 14, Phase 2 fixup) -- Build() itself
    // does a group scan, HealUrgency pass, DamageTracker sample, and nearby-enemy scan, all of
    // which were previously repeated a second time inside DpsEngine/TankEngine::Execute.
    CombatContext ctx = CombatContext::Build(bot, target);
    if (BotAI::CombatUtility::Execute(bot, ctx, diff, state.nextCastAllowedMs))
        return;

    // Data-Driven Combat AI Framework. CombatResult::NoAction means the profile (if any) found
    // nothing castable this tick -- unlike the old bool return, that specifically falls through
    // to the legacy taunt/interrupt/AoE/burst/rotation/fallback chain below instead of eating the
    // tick silently (see item 2 of the combat-engine rework: DataDrivenAI must not swallow
    // fallback logic just because a profile happens to exist for this class/spec/role).
    // profileRole (item 12) is the profile-registry lookup role -- a Support bot fights like Dps
    // (combatRole) but must still find its own Support-tagged profile.
    BotAI::CombatResult ddResult = (combatRole == BotRole::Tank)
        ? BotAI::TankEngine::Execute(bot, ctx, profileRole, diff, state.nextCastAllowedMs)
        : BotAI::DpsEngine::Execute(bot, ctx, profileRole, diff, state.nextCastAllowedMs);
    if (ddResult == BotAI::CombatResult::Cast && profileRole != combatRole)
    {
        // Item 12/#25: makes it possible to confirm via logs alone that a Support bot's own
        // Support-tagged profile was actually used, not a silent Dps-role lookup failure that
        // happened to still produce a cast via the generic fallback further down.
        LOG_INFO("module.coa-playerbots", "CombatAI: bot '{}' used its {} profile (fighting as {}).",
            bot->GetName(), profileRole == BotRole::Support ? "Support" : "non-combat-role", combatRole == BotRole::Tank ? "Tank" : "Dps");
    }
    if (ddResult != BotAI::CombatResult::NoAction)
        return;

    uint32 spellId = 0;
    char const* castVerb = "cast";

    // 1. Tank priority: taunt if not holding aggro
    if (combatRole == BotRole::Tank && target->GetVictim() != bot)
    {
        spellId = SelectTauntSpell(bot, target);
        if (spellId)
            castVerb = "cast taunt";
    }

    // 2. Interrupt priority: target is actively casting an interruptible spell, and no other bot
    // already has a reservation on interrupting this specific cast (see CombatReservations --
    // the Utility Layer above already tried and reserves on success, so this only still fires
    // when it declined for some other reason, e.g. this bot's own interrupt spell didn't match
    // the generic shape check).
    if (!spellId && IsTargetCastingInterruptibleSpell(target) &&
        !BotAI::CombatReservations::IsInterruptReserved(target->GetGUID(), bot->GetGUID()))
    {
        spellId = SelectInterruptSpell(bot, target);
        if (spellId)
            castVerb = "cast interrupt";
    }

    // 3. AoE priority: 3+ hostile enemies around target
    uint32 nearbyEnemies = ctx.nearbyEnemyCount;
    if (!spellId && nearbyEnemies >= 3)
    {
        spellId = SelectAoeSpell(bot, target);
        if (spellId)
            castVerb = "cast aoe";
    }

    // 4. Boss / Elite Burst: target is boss or elite
    if (!spellId && IsBossOrEliteTarget(target))
    {
        spellId = SelectBurstSpell(bot, target);
        if (spellId)
            castVerb = "cast burst";
    }

    // 5. Normal class rotation
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

    // 6. Single-target offensive spell (when not in AoE condition, focus single-target without wasting AoE)
    if (!spellId && nearbyEnemies < 3)
        spellId = SelectSingleTargetSpell(bot, target);

    // 7. General offensive spell fallback
    if (!spellId)
        spellId = SelectSpell(bot, target);

    if (spellId)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        Unit* castTarget = target;
        if (spellInfo && (spellInfo->IsPositive() || !spellInfo->NeedsExplicitUnitTarget()))
            castTarget = bot;

        // Positioning (chase/kite) was already handled unconditionally above -- this only gates
        // the cast itself: a cast-time spell needs the bot actually stationary first (see
        // CombatMovement::ReadyToCast), or it's a guaranteed SPELL_FAILED_MOVING. Instant spells
        // and auto-attack/auto-repeat (already engaged up front) are untouched either way.
        if (!BotAI::CombatMovement::ReadyToCast(bot, spellInfo))
        {
            state.nextCastAllowedMs = AI_REACTION_GATE_MS;
            return;
        }

        SpellCastResult result = LogCastAttempt(bot, spellId, castTarget, castVerb);
        if (result != SPELL_CAST_OK)
            BotAI::RecordSpellCastFailure(bot->GetGUID(), spellId);
        state.nextCastAllowedMs = (result == SPELL_CAST_OK) ? AI_REACTION_GATE_MS : NO_CANDIDATE_RETRY_MS;
        return;
    }

    // Nothing usable right now (everything on cooldown/unaffordable/out of range, or a
    // pure-melee kit with no spell-based attacks at all). Auto-attack, auto-repeat, and
    // positioning were already engaged up front.
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
    if (BotAvoidance::TryAvoidGroundHazards(bot))
        return;

    // HealUrgencyScore (item 10/#4, Phase 2): missing HP, incoming-damage trend, role, whether
    // an enemy is actively on them, and other healers' already-reserved incoming heals -- not
    // just lowest HP%. See HealEvaluator. Old FindHealTarget kept as-is (item 23) as a plain
    // lowest-HP% fallback for the rare case urgency scoring finds nobody worth healing.
    Player* healTarget = HealEvaluator::SelectBestHealTarget(bot);
    if (!healTarget)
        healTarget = FindHealTarget(bot);
    Unit* threat = FindGroupCombatTarget(bot, bot->GetGroup());
    if (!threat && healTarget && healTarget->IsInCombat())
        threat = healTarget->GetVictim();

    // Real spell range (item 12, Phase 2) instead of a fixed 25/35yd -- a healer whose best known
    // heal reaches 40yd shouldn't be forced to approach any closer than that.
    float healRange = HealEvaluator::BestKnownHealRange(bot, HEAL_ENGAGE_RANGE);

    // Confirmed live -- explicit user feedback: outside any real fight, chasing a groupmate who
    // merely dipped under FindHealTarget's 95% "worth healing" threshold (incidental chip/fall
    // damage, natural regen not caught up yet) used to override normal group movement entirely,
    // since this function never called ResumeFollowingLeader as long as ANY healTarget existed.
    // That left a healer stuck well behind the group at a dungeon doorway, walking toward
    // wherever a groupmate happened to be standing instead of following through like everyone
    // else. Still reacts immediately to a real fight (threat != null) or a genuinely dangerous
    // dip (below CRITICAL_HEAL_PCT) regardless of combat state -- this is about not breaking
    // formation for a routine top-off, not about ignoring real danger.
    float criticalHealPct = sConfigMgr->GetOption<float>("CoaBots.Healer.CriticalHealPct", 50.0f);
    bool worthBreakingFormationFor = threat || (healTarget && healTarget->GetHealthPct() < criticalHealPct);
    if (!healTarget || !worthBreakingFormationFor)
    {
        // Nobody needs healing right now -- a real healer doesn't stand idle with a full
        // group, they contribute damage. Dps, not Tank: a Healer bot has no business trying
        // to hold aggro just because there's nothing to heal this tick. profileRole is also
        // Dps here (not Healer) -- a Healer-role bot generally has no separate Dps-tagged
        // profile of its own, so this correctly falls through to the generic spellbook chain.
        UpdateOffensive(bot, diff, BotRole::Dps, BotRole::Dps, state);
        return;
    }

    // Confirmed live: this used to position purely relative to the ally being healed --
    // approach until within HEAL_ENGAGE_RANGE, otherwise hold. That has no concept of "the
    // enemy is dangerous," so a healer walking up to a tank standing in melee with a boss
    // just walked itself into the boss's own melee range too, and stood there getting hit in
    // the face for the rest of the fight. A real healer's first priority is staying out of
    // danger; healing from max range is normal and expected, not a compromise. Reuses the
    // exact same ChaseRange kiting band UpdateOffensive already uses for ranged DPS, just
    // anchored on the group's current combat threat instead of the ally -- naturally keeps
    // the healer at a safe standoff distance while still close enough to the fight (the
    // threat and the allies fighting it are normally near each other) to heal most targets.
    // Only falls back to chasing the ally directly when no hostile threat is identifiable at
    // all (e.g. topping someone off between pulls, or healing a target that's run off alone).

    if (threat && threat->IsAlive() && bot->IsValidAttackTarget(threat))
    {
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
            bot->GetMotionMaster()->MoveChase(threat, ChaseRange(RANGED_ENGAGE_DISTANCE * 0.7f, RANGED_ENGAGE_DISTANCE));
    }
    else if (bot->GetDistance(healTarget) > healRange)
    {
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != CHASE_MOTION_TYPE)
            bot->GetMotionMaster()->MoveChase(healTarget, healRange - 5.0f);
        return;
    }
    else if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
        bot->GetMotionMaster()->Clear();

    // The heal cast itself always targets the ally, regardless of which anchor positioning
    // used above -- being out of heal range of healTarget after prioritizing safety from the
    // threat is the correct, honest outcome of that tradeoff (matches a real healer choosing
    // not to facetank a boss just to keep someone topped off), not a bug to paper over here.
    if (bot->GetDistance(healTarget) > healRange)
        return;

    // Global Combat Utility Layer: emergency taunt/interrupt/cleanse, ahead of role rotation --
    // see engine/CombatUtility.h. Built off the bot's real spellbook shape, so it covers every
    // class regardless of whether its profile (if any) has bothered to tag Interrupt/Cleanse.
    // Shared with HealerEngine below (item 14, Phase 2 fixup) instead of each building its own.
    CombatContext ctx = CombatContext::Build(bot, threat);
    if (BotAI::CombatUtility::Execute(bot, ctx, diff, state.nextCastAllowedMs))
        return;

    // Data-Driven Combat AI Framework
    BotAI::CombatResult ddResult = BotAI::HealerEngine::Execute(bot, ctx, diff, state.nextCastAllowedMs);
    if (ddResult != BotAI::CombatResult::NoAction)
        return;

    if (state.nextCastAllowedMs > diff)
    {
        state.nextCastAllowedMs -= diff;
        return;
    }
    state.nextCastAllowedMs = 0;

    uint32 spellId = 0;
    uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
    spellId = BotAI::SelectClassHealRotationSpell(bot, healTarget, bot->getClass(), activeSpec);
    if (!spellId)
        spellId = SelectHealSpell(bot, healTarget);

    if (spellId)
    {
        SpellInfo const* healSpellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!BotAI::CombatMovement::ReadyToCast(bot, healSpellInfo))
        {
            state.nextCastAllowedMs = AI_REACTION_GATE_MS;
            return;
        }

        // Heal reservation (items 2/11, Phase 2): a rough 20%-of-max-health estimate is all this
        // legacy fallback has to go on (no AbilityTag here to size it more precisely the way
        // HealerEngine's Data-Driven path does). Tied to the spell's real cast time (item 2) --
        // not a fixed duration -- so the reservation's lazy liveness check (see
        // CombatReservations::ReserveHeal's own comment) tracks how long this specific heal is
        // actually in flight, dropping it as soon as the cast lands/fails/is interrupted rather
        // than holding a stale reservation for a fixed several seconds regardless.
        uint32 healCastTimeMs = healSpellInfo ? healSpellInfo->CalcCastTime(bot) : 0;
        uint32 expectedLegacyHeal = uint32(healTarget->GetMaxHealth() * 0.20f);
        CombatReservations::ReserveHeal(bot->GetGUID(), healTarget->GetGUID(), spellId, expectedLegacyHeal, healCastTimeMs);
        LOG_INFO("module.coa-playerbots", "CombatAI: bot '{}' reserved ~{} heal on '{}' (spell {}, cast {}ms).",
            bot->GetName(), expectedLegacyHeal, healTarget->GetName(), spellId, healCastTimeMs);

        SpellCastResult result = LogCastAttempt(bot, spellId, healTarget, "cast heal");
        if (result != SPELL_CAST_OK)
        {
            // Didn't actually go out -- no real heal is coming, so don't hold the reservation.
            CombatReservations::ClearHealReservation(bot->GetGUID());
            BotAI::RecordSpellCastFailure(bot->GetGUID(), spellId);
        }
        state.nextCastAllowedMs = (result == SPELL_CAST_OK) ? AI_REACTION_GATE_MS : NO_CANDIDATE_RETRY_MS;
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
    // Item 12, Phase 2 fixup: combatRole is Dps (fights like one -- taunt/threat logic must not
    // apply), but profileRole stays Support so a registered "*_Support" Data-Driven profile is
    // actually found instead of silently failing lookup under BotRole::Dps forever and falling
    // back to the generic spellbook chain for every Support-role class/spec.
    UpdateOffensive(bot, diff, BotRole::Dps, BotRole::Support, state);
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
    // The open-world task survives death: the brain drops its live target and area claims and
    // re-evaluates the area once the bot is back on its feet.
    WorldBrain::OnDeath(bot);

    if (bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
    {
        state.awaitingRezGrace = false;
        state.ghostDurationMs += diff;

        // Failsafe: if a ghost has been unable to reach its corpse for > 3 minutes (e.g. unreachable terrain),
        // accept spirit resurrection instead of being permanently stuck.
        if (state.ghostDurationMs > 180000)
        {
            bot->ResurrectPlayer(0.5f, true);
            bot->SpawnCorpseBones();
            state.ghostDurationMs = 0;
            return;
        }

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
            MoveBotToPoint(bot, MoveOwner::Corpse, corpse->GetPositionX(), corpse->GetPositionY(),
                corpse->GetPositionZ());
            return;
        }

        BotMovement::Release(bot, MoveOwner::Corpse);

        // The handler itself enforces the post-release reclaim delay and the exact-range
        // recheck -- harmless (and expected) to call this every tick until it actually lands.
        WorldPacket reclaimPacket;
        reclaimPacket << bot->GetGUID();
        bot->GetSession()->HandleReclaimCorpseOpcode(reclaimPacket);
        return;
    }

    // Confirmed live user report: bots accepted a pending resurrection the instant one landed,
    // regardless of whether the fight that killed them was still going -- a dead bot could get
    // battle-rezzed, immediately die to the same ongoing AoE/boss mechanic, get rezzed again, and
    // so on, effectively giving the group infinite lives during a single pull (trivializing
    // enrage timers and attrition mechanics a real, mortal group has to respect). A real player
    // can technically accept an in-combat rez too, but doing so while the fight that just killed
    // them is still raging is a judgment call a bot shouldn't make automatically -- hold the
    // request (it isn't consumed or cleared, just not acted on yet) until no one left in the
    // bot's own group is still in combat, then accept it exactly as before. If the request
    // expires first, that's the same risk a real player who waits too long runs.
    if (bot->isResurrectRequested())
    {
        bool groupInCombat = false;
        if (Group* group = bot->GetGroup())
        {
            for (GroupReference const* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* member = itr->GetSource();
                if (member && member != bot && member->IsInCombat())
                {
                    groupInCombat = true;
                    break;
                }
            }
        }

        if (!groupInCombat)
        {
            bot->ResurectUsingRequestData();
            state.awaitingRezGrace = false;
            return;
        }
        // Still fighting -- leave the ghost waiting exactly where the grace-period path below
        // already parks it (released at the graveyard or standing over the corpse); re-checked
        // every tick until either combat ends or the request expires on its own.
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

    // HandleRepopRequestOpcode -> Player::RepopAtGraveyard() calls TeleportTo() internally to
    // move the released ghost to its graveyard -- exactly like the module's own explicit
    // teleports elsewhere (DoAcceptInvite, TryFollowLeaderAcrossMaps, TryReturnGhostToCorpseMap),
    // this needs its ack synthesized next tick or the bot is stuck with IsBeingTeleportedNear/
    // Far() == true forever (nothing else ever clears it for a null-socket session). Confirmed
    // live: bots that died stopped cross-map-following their leader afterward, permanently,
    // while a bot that never died kept working -- this was the missing piece.
    if (bot->IsBeingTeleportedNear() || bot->IsBeingTeleportedFar())
        sBotMgr->QueueTeleportAck(bot->GetSession());
}
}

void AddSC_coa_bot_petition_script()
{
    new coa_bot_petition_offer_script();
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

    // A taxi flight is a server-driven spline; anything that touches the MotionMaster now -- a
    // follow, a grind walk-back, an ambient leg -- would pull the bot off its gryphon mid-air.
    if (bot->IsInFlight())
        return;

    uint8 currentLevel = bot->GetLevel();
    if (state.lastLevel == 0)
    {
        state.lastLevel = currentLevel;
        if (!BotZoneProgression::IsZoneAppropriateForLevel(bot->GetZoneId(), currentLevel))
            BotZoneProgression::QueueRelocation(bot);
    }
    else if (currentLevel > state.lastLevel)
    {
        state.lastLevel = currentLevel;
        // Abilities first: this realm hands them out through the Book of Ascension rather than on
        // level-up, and a talent pick can depend on an ability the book only just taught.
        BotProgression::OnLevelUp(bot, currentLevel);
        BotTalentBuilds::ApplyBuildForLevel(bot, currentLevel);
        SpellResolver::Invalidate(bot->GetGUID());
        if (!BotZoneProgression::IsZoneAppropriateForLevel(bot->GetZoneId(), currentLevel))
            BotZoneProgression::QueueRelocation(bot);
    }

    if (!bot->IsAlive())
    {
        UpdateDeathHandling(bot, diff, state);
        return;
    }
    state.ghostDurationMs = 0;

    // Resolve any completed or interrupted mount cast
    ResolvePendingMountCast(bot, state);

    if (state.followWaitingReaction)
    {
        if (state.followReactionDelayMs > diff)
            state.followReactionDelayMs -= diff;
        else
        {
            state.followReactionDelayMs = 0;
            state.followWaitingReaction = false;
        }
    }

    // Mount ownership and gear upgrades matter regardless of role/combat/group state -- same
    // reasoning as TryMaintainBuff running independent of combat state.
    TryMaintainProgression(bot, diff, state);

    // Checked every tick, not throttled -- mounting is something the player expects to see
    // react immediately, and this is cheap (a couple of aura/state lookups, no scans). Safe to
    // call unconditionally: CastSpell's own real checks (in combat, indoors, etc.) already
    // silently no-op a mount attempt exactly like a real player's would fail client-side.
    TryMatchLeaderMountState(bot, state);

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

    // Battleground AI: handles objectives, gates, mount travel, and proactive targeting
    if (bot->InBattleground())
    {
        if (BotBattlegroundAI::Update(bot, diff))
            return;
    }

    if (state.role == BotRole::Healer)
        UpdateHealer(bot, diff, state);
    else if (state.role == BotRole::Support)
        UpdateSupport(bot, diff, state);
    else
        UpdateOffensive(bot, diff, state.role, state.role, state);
}

void LoadGatherLootData()
{
    _questOnlyGameObjectLoot.clear();

    // A reference row stands for loot defined in another template, so it counts as non-quest:
    // letting a bot occasionally open an empty node is better than hiding a real one.
    QueryResult result = WorldDatabase.Query("SELECT Entry FROM gameobject_loot_template GROUP BY Entry "
        "HAVING SUM(QuestRequired = 0 OR Reference <> 0) = 0");
    if (result)
    {
        do
            _questOnlyGameObjectLoot.insert(result->Fetch()[0].Get<uint32>());
        while (result->NextRow());
    }

    LOG_INFO("module.coa-playerbots", ">> Loaded {} quest-only gameobject loot templates for bot gathering.",
        _questOnlyGameObjectLoot.size());
}

void Forget(ObjectGuid botGuid)
{
    states.erase(botGuid);
    BotAI::ForgetRotationState(botGuid);
    SpellResolver::Invalidate(botGuid);
    ActionEvaluator::ClearThrottles(botGuid);
    CombatReservations::ForgetBot(botGuid);
    DamageTracker::Forget(botGuid);
    HealEvaluator::ForgetBot(botGuid);
    TargetEvaluator::ForgetBot(botGuid);
    DpsEngine::ForgetBot(botGuid);
    TankEngine::ForgetBot(botGuid);
    HealerEngine::ForgetBot(botGuid);
    BotBattlegroundAI::Forget(botGuid);
    BotMovement::Forget(botGuid);
    BotWorldBehavior::Forget(botGuid);
    WorldBrain::Forget(botGuid);
}

bool IsQuestOnlyGameObjectLoot(uint32 lootId)
{
    return _questOnlyGameObjectLoot.count(lootId) != 0;
}

bool NeedsRepair(Player* bot)
{
    return HasGearBelowRepairThreshold(bot);
}

bool NeedsVendor(Player* bot)
{
    return bot->GetFreeInventorySpace() <= BAG_CLEANUP_FREE_SLOT_THRESHOLD && HasSellableJunk(bot);
}

void MaintainEquipmentNow(Player* bot)
{
    TryMaintainEquipment(bot);
}

bool IsInCity(Player const* bot)
{
    return IsCityOrSanctuary(bot);
}

void TryMountForTravel(Player* bot)
{
    TryMount(bot, states[bot->GetGUID()], false);
}

bool IsProfessionTool(ItemTemplate const* proto)
{
    if (!proto)
        return false;
    return proto->TotemCategory || (proto->Class == ITEM_CLASS_WEAPON &&
        (proto->SubClass == ITEM_SUBCLASS_WEAPON_MISC || proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE));
}

bool MoveEquippedToolToBags(Player* bot, uint8 slot)
{
    Item* tool = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!tool)
        return true;

    ItemPosCountVec dest;
    if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, tool, false) != EQUIP_ERR_OK)
        return false;

    bot->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
    bot->StoreItem(dest, tool, true);
    return true;
}

void GetProfessionLeans(Player* bot, uint8& gathering, uint8& fishing)
{
    BotAIState& state = states[bot->GetGUID()];
    EnsurePersonality(bot, state);
    gathering = state.personality.gathering;
    fishing = state.personality.fishing;
}

void ReportProfile(Player* bot, ChatHandler* handler)
{
    if (!bot || !handler)
        return;
    BotAIState& state = states[bot->GetGUID()];
    EnsurePersonality(bot, state);
    BotPersonality const& p = state.personality;
    handler->PSendSysMessage("Bot profile for '{}': {} (seed {}).", bot->GetName(), ArchetypeName(p.archetype), p.seed);
    handler->PSendSysMessage("  quest {}, gather {}, fish {}, grind {}, patience {}, social {}.", uint32(p.questing),
        uint32(p.gathering), uint32(p.fishing), uint32(p.grinding), uint32(p.patience), uint32(p.sociability));
    handler->PSendSysMessage("  current solo intent: {} ({} sec remaining).", IntentName(state.soloIntent),
        state.soloIntentRemainingMs / IN_MILLISECONDS);
    handler->PSendSysMessage("  {}.", BotWorldBehavior::Describe(bot->GetGUID()));
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

    if (command == BotManualCommand::Follow)
    {
        state.followWaitingReaction = false;
        state.followReactionDelayMs = 0;

        if (Player* bot = ObjectAccessor::FindPlayer(botGuid))
        {
            if (bot->IsMounted())
                state.pendingMountSpellId = 0;

            if (!bot->IsNonMeleeSpellCast(false))
            {
                if (Group* group = bot->GetGroup())
                {
                    if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
                    {
                        if (leader != bot && leader->GetMap() == bot->GetMap())
                        {
                            float followDist = BotAI::ComputeFollowDistance(bot);
                            float followAngle = BotAI::ComputeFollowAngle(bot);
                            bot->GetMotionMaster()->MoveFollow(leader, followDist, followAngle);
                        }
                    }
                }
            }
        }
    }
    else if (command == BotManualCommand::Stay)
    {
        if (Player* bot = ObjectAccessor::FindPlayer(botGuid))
            ClearActiveFollow(bot);
    }
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
        {
            bot->AttackStop();
            // The open-world task lets go of everything and stops its clocks, like for any other
            // manual command; it resumes (or re-plans, after a long hold) on the next idle tick.
            WorldBrain::Suspend(bot, SuspendReason::ManualCommand);
        }
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
    uint32 interrupt = 0, aoe = 0, burst = 0;
    uint32 exampleInterrupt = 0, exampleAoe = 0, exampleBurst = 0;

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
        if (IsUsableInterruptSpell(spellInfo))
        {
            ++interrupt;
            if (!exampleInterrupt)
                exampleInterrupt = spellId;
        }
        if (IsUsableAoeSpell(spellInfo))
        {
            ++aoe;
            if (!exampleAoe)
                exampleAoe = spellId;
        }
        if (IsUsableBurstSpell(spellInfo))
        {
            ++burst;
            if (!exampleBurst)
                exampleBurst = spellId;
        }
    }

    float preferredDist = GetBotPreferredEngageDistance(bot, GetRole(bot->GetGUID()));

    if (handler)
        handler->PSendSysMessage(
            "BotAI: '{}' role signals -- off={} (e.g. {}), taunt={} (e.g. {}), heal={} (e.g. {}), buff={} (e.g. {}), int={} (e.g. {}), aoe={} (e.g. {}), burst={} (e.g. {}), dist={:.1f}yd.",
            bot->GetName(), offensive, exampleOffensive, taunt, exampleTaunt, heal, exampleHeal, buff, exampleBuff,
            interrupt, exampleInterrupt, aoe, exampleAoe, burst, exampleBurst, preferredDist);
    LOG_INFO("module.coa-playerbots",
        "BotAI: '{}' (class {}) spellbook role signals -- off={} taunt={} heal={} buff={} int={} aoe={} burst={} dist={:.1f}yd.",
        bot->GetName(), uint32(bot->getClass()), offensive, taunt, heal, buff, interrupt, aoe, burst, preferredDist);
}

// Spreads followers around the leader instead of every bot converging on the exact same spot
// -- Unit::GetFollowAngle() defaults to a single fixed angle (M_PI/2) for everyone, so a
// leader with multiple bots got them all trying to stand in the same relative position at
// once, confirmed live as bots visibly walking into/through each other. Stable per bot (keyed
// off guid, not bot count or join order), so a given bot always claims the same slot instead
// of the whole group's angles reshuffling whenever someone else joins or leaves. Exported (not
// anonymous-namespace-local) so both BotAI.cpp's own resume-following and BotMgr.cpp's
// post-teleport re-follow use the same slot assignment.
float ComputeFollowAngle(Player* bot)
{
    Group const* group = bot->GetGroup();
    if (!group)
        return 0.0f;

    BotGroupFormation formation = sBotMgr->GetGroupFormation(group->GetLeaderGUID());

    uint32 memberIndex = 0;
    uint32 totalFollowers = 0;
    for (GroupReference const* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player const* member = itr->GetSource();
        if (!member || member->GetGUID() == group->GetLeaderGUID())
            continue;
        if (member == bot)
            memberIndex = totalFollowers;
        ++totalFollowers;
    }

    float baseAngle = 0.0f;

    switch (formation)
    {
        case BotGroupFormation::Shieldwall:
        {
            BotRole role = GetRole(bot->GetGUID());
            if (role == BotRole::Tank)
                baseAngle = 0.0f;
            else if (role == BotRole::Healer)
                baseAngle = static_cast<float>(M_PI);
            else
            {
                bool left = (memberIndex % 2 == 0);
                baseAngle = left ? (static_cast<float>(M_PI) * 0.35f) : (-static_cast<float>(M_PI) * 0.35f);
            }
            break;
        }
        case BotGroupFormation::Arrow:
        {
            bool left = (memberIndex % 2 == 0);
            baseAngle = left ? (static_cast<float>(M_PI) * 0.75f) : (-static_cast<float>(M_PI) * 0.75f);
            break;
        }
        case BotGroupFormation::Circle:
        {
            if (totalFollowers > 0)
                baseAngle = (2.0f * static_cast<float>(M_PI) * memberIndex) / float(totalFollowers);
            break;
        }
        case BotGroupFormation::Line:
        {
            bool left = (memberIndex % 2 == 0);
            baseAngle = left ? (static_cast<float>(M_PI) * 0.5f) : (-static_cast<float>(M_PI) * 0.5f);
            break;
        }
        case BotGroupFormation::Chaos:
        {
            baseAngle = (float(bot->GetGUID().GetCounter() % 360)) * (static_cast<float>(M_PI) / 180.0f);
            break;
        }
        case BotGroupFormation::RoleBased:
        default:
        {
            BotRole role = GetRole(bot->GetGUID());
            uint32 sameRoleIndex = 0;
            uint32 sameRoleSeen = 0;
            for (GroupReference const* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player const* member = itr->GetSource();
                if (!member || member->GetGUID() == group->GetLeaderGUID())
                    continue;
                BotRole memberRole = sBotMgr->FindBotPlayer(member->GetGUID().GetCounter()) ? GetRole(member->GetGUID()) : BotRole::Dps;
                if (memberRole != role)
                    continue;
                if (member == bot)
                    sameRoleIndex = sameRoleSeen;
                ++sameRoleSeen;
            }

            constexpr float FRONT = 0.0f;
            constexpr float BEHIND = static_cast<float>(M_PI);
            constexpr float SIDE_START = static_cast<float>(M_PI) / 2.0f;
            constexpr float SIDE_STEP = static_cast<float>(M_PI) / 6.0f;  // 30° instead of 60° to avoid repeats with large DPS groups

            switch (role)
            {
                case BotRole::Tank:
                    baseAngle = FRONT;
                    break;
                case BotRole::Healer:
                    baseAngle = BEHIND;
                    break;
                default:
                    baseAngle = SIDE_START + float(sameRoleIndex) * SIDE_STEP;
                    break;
            }
            break;
        }
    }

    float jitter = (float(bot->GetGUID().GetCounter() % 21) - 10.0f) * (static_cast<float>(M_PI) / 180.0f);
    return baseAngle + jitter;
}

float ComputeFollowDistance(Player* bot)
{
    Group const* group = bot->GetGroup();
    if (!group)
        return BOT_FOLLOW_DIST;

    BotGroupFormation formation = sBotMgr->GetGroupFormation(group->GetLeaderGUID());
    float baseDist = BOT_FOLLOW_DIST;

    switch (formation)
    {
        case BotGroupFormation::Shieldwall:
        {
            BotRole role = GetRole(bot->GetGUID());
            if (role == BotRole::Tank)
                baseDist = 2.5f;
            else if (role == BotRole::Healer)
                baseDist = 6.0f;
            else
                baseDist = 4.0f;
            break;
        }
        case BotGroupFormation::Arrow:
        {
            uint32 rank = 1;
            for (GroupReference const* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                if (itr->GetSource() == bot)
                    break;
                if (itr->GetSource()->GetGUID() != group->GetLeaderGUID())
                    ++rank;
            }
            baseDist = 2.0f + float((rank + 1) / 2) * 2.0f;
            break;
        }
        case BotGroupFormation::Line:
        {
            uint32 rank = 1;
            for (GroupReference const* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                if (itr->GetSource() == bot)
                    break;
                if (itr->GetSource()->GetGUID() != group->GetLeaderGUID())
                    ++rank;
            }
            baseDist = float((rank + 1) / 2) * 2.5f;
            break;
        }
        case BotGroupFormation::Chaos:
            baseDist = 3.0f + float(bot->GetGUID().GetCounter() % 8);
            break;
        default:
            baseDist = BOT_FOLLOW_DIST;
            break;
    }

    float jitter = (float(bot->GetGUID().GetCounter() % 15) - 7.0f) * 0.15f;
    return std::max(1.5f, baseDist + jitter);
}

void TakeAllLoot(Player* bot, Loot& loot)
{
    // Quest-only drops are not in loot.items: Loot::AddItem files them under quest_items, and a
    // client reaches them through slots numbered after the regular items, one per entry of this
    // player's own quest item list (Loot::LootItemInSlot). Looting only loot.items -- what every
    // loot path here did before -- silently left every "collect N" quest drop on the corpse.
    uint32 slots = uint32(loot.items.size());
    QuestItemMap const& questItems = loot.GetPlayerQuestItems();
    auto own = questItems.find(bot->GetGUID());
    if (own != questItems.end() && own->second)
        slots += uint32(own->second->size());

    for (uint32 slot = 0; slot < slots; ++slot)
    {
        WorldPacket storePacket;
        storePacket << uint8(slot);
        bot->GetSession()->HandleAutostoreLootItemOpcode(storePacket);
    }
}

void EnqueuePendingLoot(Player* player, ObjectGuid creatureGuid)
{
    if (!player || creatureGuid.IsEmpty())
        return;

    auto enqueueForBot = [](Player* b, ObjectGuid guid)
    {
        auto itr = states.find(b->GetGUID());
        if (itr == states.end())
            return;
        BotAIState& st = itr->second;
        for (auto const& g : st.pendingLootGuids)
            if (g == guid)
                return;
        if (st.pendingLootGuids.size() >= 15)
            st.pendingLootGuids.pop_front();
        st.pendingLootGuids.push_back(guid);
    };

    if (sBotMgr->FindBotPlayer(player->GetGUID().GetCounter()))
        enqueueForBot(player, creatureGuid);

    if (Group* group = player->GetGroup())
    {
        for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            if (Player* member = itr->GetSource())
            {
                if (member != player && sBotMgr->FindBotPlayer(member->GetGUID().GetCounter()))
                    enqueueForBot(member, creatureGuid);
            }
        }
    }
}
}
