/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatResource -- universal resource-management engine
 *
 * CoA's 21 custom Ascension classes do not fit a "one resource per class" model: most have a
 * real custom resource (an aura-stack counter, e.g. Pyromancer's Heat/Ember or Reaper's three
 * soul-related auras plus native Runic Power) alongside or instead of their native power bar,
 * several classes track more than one simultaneously (Reaper: 3 aura channels + native; Templar:
 * 5 combo trackers; Runemaster: 3 elemental sigils), and Necromancer's real constraint is a
 * minion-capacity counter, not an aura stack or power bar at all. A single scalar "botPowerPct"
 * cannot represent any of that -- see docs/combat-engine-v2.md's "Resource management engine"
 * section for the full audit this is built from.
 *
 * This is a correctness layer, not a strategy layer (see CombatResourceEvaluator's own comment):
 * it answers "what resources does this bot have, how much of each, what does this specific
 * ability need, can it afford that right now" -- not "should it spend this resource now". That's
 * deliberate Phase 3 scope, not touched here.
 *
 * Single source of truth: every real gain/spend/cap/gate rule lives in mod-ascension-compat
 * (core), not here -- this file only reads AscensionCompatData::* query functions
 * (AscensionResourceQuery.h) and never reimplements or duplicates a class's own resource
 * mutation/gating logic, including the "which spells are X spenders" predicates (those are real
 * exported core functions like AscensionVenomancer::Spender, called from the core's own query
 * implementation -- this file never sees a class-specific spell id).
 */

#ifndef COA_PLAYERBOTS_COMBAT_RESOURCE_H
#define COA_PLAYERBOTS_COMBAT_RESOURCE_H

#include "AscensionResourceQuery.h"
#include "Define.h"
#include <array>
#include <vector>

class Player;

namespace BotAI
{
    enum class CombatResourceKind : uint8
    {
        NativePower,    // a standard Blizzard power bar (mana/rage/energy/runic power/...)
        AuraStack,      // a custom Ascension resource tracked as an aura's stack count
        MinionCapacity  // Necromancer's active-summon-count economy (see AscensionNecromancer::
                         // Capacity/Used) -- a live counter, not an aura stack or power bar
    };

    // Identifies one resource channel. Two keys are the same resource iff kind, and the
    // kind-relevant field, match. MinionCapacity needs no extra field: a bot has at most one.
    struct CombatResourceKey
    {
        CombatResourceKind kind = CombatResourceKind::NativePower;
        uint8 powerType = 0;     // valid when kind == NativePower (a Powers enum value)
        uint32 auraSpellId = 0;  // valid when kind == AuraStack

        bool operator==(CombatResourceKey const& other) const
        {
            return kind == other.kind && powerType == other.powerType && auraSpellId == other.auraSpellId;
        }
    };

    struct CombatResourceState
    {
        CombatResourceKey key;
        int32 current = 0;
        int32 maximum = 0;
        bool maximumKnown = false;  // false: maximum is meaningless (0), use `current` as an
                                     // absolute count -- never synthesize a fake percentage
        char const* name = "";
    };

    // Fixed-capacity, no heap allocation. 16 leaves real headroom above the current observed
    // maximum (Templar: 6 -- Oath chain + 5 combo trackers) without silently dropping a channel if
    // a future class adds more -- see CombatResourceSnapshot::Add's own overflow handling below,
    // which logs rather than silently discarding (a correctness layer must never do that quietly).
    constexpr size_t MAX_COMBAT_RESOURCES = 16;

    struct CombatResourceSnapshot
    {
        std::array<CombatResourceState, MAX_COMBAT_RESOURCES> resources{};
        uint8 count = 0;

        CombatResourceState const* Find(CombatResourceKey const& key) const
        {
            for (uint8 i = 0; i < count; ++i)
                if (resources[i].key == key)
                    return &resources[i];
            return nullptr;
        }

        // Returns false (and logs) on overflow instead of silently dropping the channel -- a
        // correctness layer that quietly loses a resource is worse than one that's merely loud
        // about a capacity that needs raising. See CombatResource.cpp for the actual LOG_ERROR.
        bool Add(CombatResourceState const& state);
    };

    class CombatResourceEvaluator
    {
    public:
        // Builds the full resource snapshot for `bot`: its native primary power bar (always
        // present, first entry), every custom aura-stack channel its class has (per
        // AscensionCompatData::GetResourceChannels), and Necromancer's minion-capacity channel
        // where applicable -- read live from real aura/power/minion state, never cached across
        // ticks. Called once per tick from CombatContext::Build and shared from there, not rebuilt
        // per ability (item 23).
        static CombatResourceSnapshot BuildSnapshot(Player* bot);

        // One resource requirement for a specific already-resolved spellId (0..N per ability):
        // its native DBC power cost, any custom AuraStack requirements from
        // AscensionCompatData::QueryAbilityResourceRequirements, and Necromancer's minion-capacity
        // cost where applicable. This is the DIAGNOSTIC / full-detail form (allocates a vector) --
        // CanAfford below does NOT call this; see its own comment for why.
        struct Requirement
        {
            CombatResourceKey key;
            int32 amount = 0;
            AscensionCompatData::ResourceConsumption consumption = AscensionCompatData::ResourceConsumption::Fixed;
            uint32 preserveCostAuraSpellId = 0;
            uint8 preserveCostChancePercent = 0;
            uint32 requiredAuraSpellId = 0;   // 0 = unconditional
            uint32 forbiddenAuraSpellId = 0;  // 0 = unconditional
            bool activeForBot = true;         // requiredAuraSpellId/forbiddenAuraSpellId already
                                               // evaluated against `bot`'s real aura state -- true
                                               // means this requirement genuinely applies right now
        };
        static std::vector<Requirement> ResolveRequirements(Player* bot, uint32 resolvedSpellId);

        // Metadata only -- what this ability can potentially generate (native and/or custom),
        // cached the same way as the internal custom-requirement cache. Not used for scoring yet
        // (that's Phase 3); exists now so a profile-tagging pass later doesn't need a second
        // engine change to know "this is a builder for X" (item 12).
        struct Gain
        {
            CombatResourceKey key;
            int32 amount = 0;
            AscensionCompatData::ResourceGainEvent event = AscensionCompatData::ResourceGainEvent::Cast;
            uint32 requiredAuraSpellId = 0;
            uint32 forbiddenAuraSpellId = 0;
            uint8 chancePercent = 100;
        };
        static std::vector<Gain> const& ResolveGains(uint8 classId, uint32 resolvedSpellId);

        // True only when every requirement (native, custom aura-stack, and Necromancer minion-
        // capacity) is currently satisfiable. Deliberately does NOT treat an unconfirmed preserve-
        // cost proc chance as "you have enough" -- affordability is evaluated as if the proc will
        // not trigger. A conditional requirement (requiredAuraSpellId/forbiddenAuraSpellId, e.g.
        // Cultist's Insanity spend while Madness is absent) is skipped entirely when its condition
        // isn't met for `bot` right now -- that's not a bypass, it mirrors the real gameplay rule
        // that the requirement simply doesn't apply in that state. Not a replacement for the
        // server's own Spell::CheckCast -- this exists purely so the AI doesn't select an ability
        // it obviously cannot pay for (item 19).
        //
        // Allocation-free (item 10): reads native cost straight from `bot` (never from `snapshot`
        // -- item 8, a snapshot only ever holds the bot's own primary bar plus explicitly-
        // registered extra channels, never every PowerType a spell could theoretically use) and
        // iterates a cached, non-owning view of the custom requirements rather than building a
        // fresh std::vector -- see the .cpp. This is the ONE affordability check
        // ActionEvaluator::CanCast should call; it deliberately replaces that function's own
        // former separate native-power check (item 9) so there is a single owner of "can this bot
        // afford this cast" rather than two independently-maintained ones.
        static bool CanAfford(CombatResourceSnapshot const& snapshot, Player* bot, uint32 resolvedSpellId);
    };
}

#endif // COA_PLAYERBOTS_COMBAT_RESOURCE_H
