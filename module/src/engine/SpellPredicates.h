/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpellPredicates
 *
 * Class-agnostic "is this known spell shaped like an offensive/taunt/heal/interrupt/dispel
 * ability" predicates, and the generic spellbook scanners built on top of them. Originally
 * lived as an anonymous-namespace block inside BotAI.cpp (see docs/architecture.md's "Beyond
 * parity" section for why this AI works off real SpellInfo shape instead of a per-class spell
 * list); pulled out here so engine/CombatUtility.cpp can reuse the exact same logic instead of
 * re-deriving its own copy of "what counts as a taunt" -- one definition, not two that can
 * silently drift apart.
 */

#ifndef COA_PLAYERBOTS_SPELL_PREDICATES_H
#define COA_PLAYERBOTS_SPELL_PREDICATES_H

#include "Define.h"
#include <vector>

class Player;
class Unit;
class SpellInfo;

namespace BotAI
{
    // A rank-agnostic cooldown floor used to tell "this is a big cooldown-gated burst button"
    // apart from an ordinary spammable ability -- see IsUsableBurstSpell.
    constexpr uint32 BURST_SPELL_MIN_COOLDOWN_MS = 45000;

    bool IsUsableOffensiveSpell(SpellInfo const* spellInfo);
    bool IsUsableTauntSpell(SpellInfo const* spellInfo);
    bool IsUsableHealSpell(SpellInfo const* spellInfo);
    bool IsUsableBuffSpell(SpellInfo const* spellInfo);
    bool IsUsableInterruptSpell(SpellInfo const* spellInfo);
    bool IsUsableAoeSpell(SpellInfo const* spellInfo);
    bool IsUsableSingleTargetOffensiveSpell(SpellInfo const* spellInfo);
    bool IsUsableBurstSpell(SpellInfo const* spellInfo);

    // A positive, explicitly-targeted spell that removes a harmful effect from an ally -- the
    // real WotLK shape is a direct SPELL_EFFECT_DISPEL (Dispel Magic/Cleanse/Remove Curse/Purify
    // all use this). Deliberately does not try to match the specific DispelType it removes
    // against what's actually on the target --
    // that needs per-spell dispel-type data this generic shape check can't see; the real cast
    // validation inside Player::CastSpell rejects a genuine mismatch on its own (SPELL_FAILED_
    // NOTHING_TO_DISPEL), so the cost of not pre-filtering is an occasional wasted GCD attempt,
    // not a bad cast landing.
    bool IsUsableDispelSpell(SpellInfo const* spellInfo);

    // True when `cleanseSpell` can actually remove `debuffSpell` -- compares cleanseSpell's own
    // SPELL_EFFECT_DISPEL effect(s) (MiscValue = the DispelType it removes, DISPEL_ALL meaning
    // "any") against debuffSpell's own DispelType (SpellInfo::Dispel), using the same
    // SpellInfo::GetDispelMask the real dispel effect handler uses -- see item 4 of the Phase 2
    // fixup pass. A bot that only knows Remove Curse must not try it on a Magic debuff.
    bool IsDispelCompatible(SpellInfo const* cleanseSpell, SpellInfo const* debuffSpell);

    // True while `target` is mid-cast on something both interruptible and worth interrupting
    // (matches the InterruptFlags/ChannelInterruptFlags the real client's own kick UI reacts to).
    bool IsTargetCastingInterruptibleSpell(Unit const* target);

    // Same check, but also reports which spell it is and the absolute getMSTime() it finishes at
    // -- CombatContext::Build uses this to populate victimCastingSpellId/victimCastFinishTimeMs
    // for interrupt-reservation priority (see engine/CombatReservations.h). Outputs are left
    // untouched (0) when this returns false.
    bool IsTargetCastingInterruptibleSpell(Unit const* target, uint32& outSpellId, uint32& outFinishTimeMs);

    // Elite/world-boss/dungeon-boss check -- the floor for "does this target justify spending a
    // long-cooldown burst/defensive button," see CombatContext's fight-value fields.
    bool IsBossOrEliteTarget(Unit const* target);

    // True when `unit` is under a hard-CC mechanic that breaks on damage (fear/polymorph/sleep/
    // banish/shackle/sap/charm/horror/disorient) -- used to avoid a target-selection pass
    // attacking straight through a groupmate's crowd control (item 1/#8's "CC target -> huge
    // penalty" and item 17's "don't instantly break another group member's CC"). Deliberately
    // narrower than the engine's own IMMUNE_TO_MOVEMENT_IMPAIRMENT_AND_LOSS_CONTROL_MASK, which
    // also covers stun/root/snare/daze -- none of those break on damage, so attacking through
    // them is normal and not penalized here.
    bool IsUnitUnderBreakableCrowdControl(Unit const* unit);

    // The per-spell eligibility core behind SelectKnownSpell (cooldown, GCD, item/aura
    // requirements, failure backoff, range, power cost) for one already-chosen, already-known
    // spellId -- exposed separately so a caller that needs to pick a *specific* spell (e.g.
    // CombatUtility's dispel-type-compatible cleanse pairing, item 4) can reuse the exact same
    // castability checks instead of scanning the whole spellbook with a predicate function.
    bool IsKnownSpellCastable(Player* bot, uint32 spellId, Unit* target, bool positiveRange);

    // Shared scan shape for all "pick a ready, affordable, in-range known spell matching this
    // predicate" lookups -- only the predicate and whether beneficial-spell ranges apply differ.
    uint32 SelectKnownSpell(Player* bot, Unit* target, bool positiveRange, bool (*predicate)(SpellInfo const*));

    // First ready/affordable/in-range candidate wins -- see SelectKnownSpell's own comment on why
    // that's fine (no per-class priority data exists for most of these 21 classes yet).
    uint32 SelectSpell(Player* bot, Unit* target);
    uint32 SelectTauntSpell(Player* bot, Unit* target);
    uint32 SelectHealSpell(Player* bot, Unit* target);
    uint32 SelectBuffSpell(Player* bot);
    uint32 SelectInterruptSpell(Player* bot, Unit* target);
    uint32 SelectAoeSpell(Player* bot, Unit* target);
    uint32 SelectSingleTargetSpell(Player* bot, Unit* target);
    uint32 SelectBurstSpell(Player* bot, Unit* target);
    uint32 SelectDispelSpell(Player* bot, Unit* target);

    // True once `bot` is clear of spellInfo's own GCD category (StartRecoveryCategory), using the
    // engine's real per-player GlobalCooldownMgr rather than an approximated fixed delay -- see
    // docs on why the old APPROXIMATE_GCD_MS constant existed and why it's gone now.
    bool IsOffGlobalCooldown(Player* bot, SpellInfo const* spellInfo);

    // Hostile units within `range` yards of `center`, valid attack targets for `bot`. Shared by
    // CombatContext::Build (per-tick nearbyEnemyCount snapshot) and the legacy AoE-threshold
    // fallback so both agree on what "nearby" means. (The predicate functor behind this scan is
    // an implementation detail of the .cpp -- BotAI.cpp keeps its own small copy for
    // FindAllyThreatenedTarget's list-returning scan rather than pulling Unit.h/Player.h's full
    // definitions into this header just for a 10-line geometry check.)
    uint32 CountNearbyEnemies(Player const* bot, Unit const* center, float range = 10.0f);

    // Same scan, but returns the actual unit list -- used by TargetEvaluator/ThreatEvaluator
    // (Phase 2) to score candidates instead of just counting them. Appends into `out` rather than
    // returning by value so a caller can reuse one vector across a tight scan loop.
    void GetNearbyEnemies(Player const* bot, Unit const* center, float range, std::vector<Unit*>& out);
}

#endif // COA_PLAYERBOTS_SPELL_PREDICATES_H
