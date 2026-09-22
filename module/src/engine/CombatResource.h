/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatResource -- universal resource-management engine
 *
 * CoA's 21 custom Ascension classes do not fit a "one resource per class" model: most have a
 * real custom resource (an aura-stack counter, e.g. Pyromancer's Heat/Ember or Reaper's three
 * soul-related auras plus native Runic Power) alongside or instead of their native power bar, and
 * several classes track more than one simultaneously (Reaper: 3 aura channels + native; Templar:
 * 5 combo trackers; Runemaster: 3 elemental sigils). A single scalar "botPowerPct" cannot
 * represent that -- see docs/combat-engine-v2.md's "Resource management engine" section for the
 * full audit this is built from.
 *
 * This is a correctness layer, not a strategy layer (see CombatResourceEvaluator's own comment):
 * it answers "what resources does this bot have, how much of each, what does this specific
 * ability need, can it afford that right now" -- not "should it spend this resource now". That's
 * deliberate Phase 3 scope, not touched here.
 *
 * Single source of truth: every real gain/spend/cap rule lives in mod-ascension-compat (core), not
 * here -- this file only reads AscensionCompatData::* query functions (AscensionResourceQuery.h)
 * and never reimplements or duplicates a class's own resource mutation logic.
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
        NativePower,  // a standard Blizzard power bar (mana/rage/energy/runic power/...)
        AuraStack     // a custom Ascension resource tracked as an aura's stack count
    };

    // Identifies one resource channel. Two keys are the same resource iff kind, and the
    // kind-relevant field (powerType or auraSpellId), match.
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

    // Fixed-capacity, no heap allocation -- no class observed so far exceeds 5 simultaneous
    // channels (Reaper: 3 aura + native Runic Power; Templar: 5 combo trackers is the current
    // maximum across all 21 classes), so 8 leaves headroom without paying for a dynamic container
    // on every CombatContext::Build call, which runs at bot-population scale every tick.
    constexpr size_t MAX_COMBAT_RESOURCES = 8;

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

        void Add(CombatResourceState const& state)
        {
            if (count < MAX_COMBAT_RESOURCES)
                resources[count++] = state;
        }
    };

    class CombatResourceEvaluator
    {
    public:
        // Builds the full resource snapshot for `bot`: its native primary power bar (always
        // present, first entry) plus every custom aura-stack channel its class has, per
        // AscensionCompatData::GetResourceChannels -- read live from real aura/power state, never
        // cached across ticks (aura stacks can change from anything: casts, hits, procs, other
        // players). Called once per tick from CombatContext::Build and shared from there, not
        // rebuilt per ability (item 23).
        static CombatResourceSnapshot BuildSnapshot(Player* bot);

        // One resource requirement for a specific already-resolved spellId (0..N per ability):
        // its native DBC power cost (if any, from SpellInfo::PowerType/CalcPowerCost) plus any
        // custom requirements from AscensionCompatData::QueryAbilityResourceRequirements. Takes
        // the real `bot` (not just its classId) because SpellInfo::CalcPowerCost needs a real
        // caster -- it dereferences it unconditionally (a handful of classes get situational 0-
        // cost procs, e.g. Tinker/Pyromancer mana-free windows) and a null caster would crash it.
        // Only the custom-requirement half is classId+spellId cacheable (pure static-table data,
        // see the .cpp) -- the native half is cheap arithmetic recomputed fresh each call, same as
        // ActionEvaluator::CanCast's own existing native-cost check.
        struct Requirement
        {
            CombatResourceKey key;
            int32 amount = 0;
            AscensionCompatData::ResourceConsumption consumption = AscensionCompatData::ResourceConsumption::Fixed;
            uint32 preserveCostAuraSpellId = 0;
            uint8 preserveCostChancePercent = 0;
        };
        static std::vector<Requirement> ResolveRequirements(Player* bot, uint32 resolvedSpellId);

        // Metadata only -- what this ability can potentially generate (native and/or custom),
        // cached the same way as ResolveRequirements. Not used for scoring yet (that's Phase 3);
        // exists now so a profile-tagging pass later doesn't need a second engine change to know
        // "this is a builder for X" (item 12).
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

        // True only when every requirement (native and custom) is currently satisfiable against
        // `snapshot`. Deliberately does NOT treat an unconfirmed preserve-cost proc chance as
        // "you have enough" (item 10) -- affordability is evaluated as if the proc will not
        // trigger, since a bot picking an ability it can only sometimes actually afford would
        // otherwise risk a real SPELL_FAILED_NO_POWER at cast time. Not a replacement for the
        // server's own Spell::CheckCast -- this exists purely so the AI doesn't select an ability
        // it obviously cannot pay for, not to re-implement cast validation (item 19).
        static bool CanAfford(CombatResourceSnapshot const& snapshot, Player* bot, uint32 resolvedSpellId);
    };
}

#endif // COA_PLAYERBOTS_COMBAT_RESOURCE_H
