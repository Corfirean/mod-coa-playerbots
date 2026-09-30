#include "BotAvoidance.h"
#include "BotBossKnowledge.h"
#include "BotMovement.h"
#include "Player.h"
#include "Creature.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include <cmath>

namespace
{
    // Does any effect of `spellInfo` actually threaten anyone besides the caster -- the same
    // point-blank/frontal-AoE target-type check the reactive live-cast path already applies, just
    // usable against a spell id we merely know about (Boss Knowledge Layer) rather than one the
    // boss is already casting.
    bool HasCasterCenteredAreaEffect(SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return false;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (spellInfo->Effects[i].TargetA.GetTarget() == TARGET_SRC_CASTER ||
                spellInfo->Effects[i].TargetA.GetTarget() == TARGET_UNIT_SRC_AREA_ENEMY)
                return true;
        return false;
    }

    // True when `boss`'s current HP sits inside (or just about to enter) a known
    // SMART_EVENT_HEALTH_PCT window for an ability that threatens bystanders -- an early-warning
    // signal from the Boss Knowledge Layer, not a live cast. LEAD_IN_PCT gives a small head start
    // rather than only reacting the instant the window is technically entered.
    bool IsBossNearKnownAreaCastTrigger(Creature* boss)
    {
        if (!boss)
            return false;

        CreatureTemplate const* cinfo = boss->GetCreatureTemplate();
        if (!cinfo)
            return false;

        constexpr float LEAD_IN_PCT = 3.0f;
        float hpPct = boss->GetHealthPct();

        for (BotBossKnowledge::KnownAbility const& ability : BotBossKnowledge::GetKnownAbilities(cinfo->Entry))
        {
            if (!ability.hasHpWindow)
                continue;
            if (hpPct > ability.hpMaxPct + LEAD_IN_PCT || hpPct < ability.hpMinPct - LEAD_IN_PCT)
                continue;
            if (HasCasterCenteredAreaEffect(sSpellMgr->GetSpellInfo(ability.spellId)))
                return true;
        }
        return false;
    }

    // A resolved ground-damage emitter, regardless of which of the three shapes the game actually
    // represents persistent ground/AoE damage as (DynamicObject, a creature's own damaging aura,
    // or an armed GameObject trap) -- callers don't need to know which one they found.
    struct HazardHit
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float radius = 0.0f;
        SpellInfo const* spellInfo = nullptr;
    };

    // Real damage, on an effect that can actually reach someone other than the caster -- the
    // check both the creature-aura-pulse and GameObject-trap hazards below require before either
    // is treated as a hazard at all. `SPELL_AURA_PERIODIC_DAMAGE` alone (a plain self-only DoT)
    // deliberately does NOT qualify: it hurts only whoever carries the aura, not bystanders.
    bool SpellThreatensBystanders(SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return false;

        bool hasAreaTarget = false;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            Targets targetA = spellInfo->Effects[i].TargetA.GetTarget();
            if (targetA == TARGET_UNIT_SRC_AREA_ENEMY || targetA == TARGET_UNIT_DEST_AREA_ENEMY ||
                targetA == TARGET_UNIT_SRC_AREA_ENTRY || targetA == TARGET_SRC_CASTER ||
                targetA == TARGET_DEST_DYNOBJ_ENEMY)
            {
                hasAreaTarget = true;
                break;
            }
        }
        if (!hasAreaTarget)
            return false;

        return spellInfo->HasEffect(SPELL_EFFECT_INSTAKILL)
            || spellInfo->HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE)
            || spellInfo->HasEffect(SPELL_EFFECT_ENVIRONMENTAL_DAMAGE)
            || spellInfo->HasEffect(SPELL_EFFECT_HEALTH_LEECH)
            || spellInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE)
            || spellInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
            || spellInfo->HasAura(SPELL_AURA_PERIODIC_LEECH);
    }

    // Finds, among `spellInfo`'s own effects, the radius of whichever effect made it qualify as
    // an area threat -- falls back to a small default when the effect has no radius entry of its
    // own (a handful of hand-tuned spells use a fixed range instead).
    float ResolveThreatRadius(SpellInfo const* spellInfo)
    {
        constexpr float DEFAULT_RADIUS = 8.0f;
        if (!spellInfo)
            return DEFAULT_RADIUS;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (spellInfo->Effects[i].RadiusEntry)
                return spellInfo->Effects[i].RadiusEntry->RadiusMax;
        return DEFAULT_RADIUS;
    }

    // A creature's own aura either already threatens bystanders directly, or -- the common
    // "self-buff periodically triggers a real AoE" pattern -- periodically triggers a spell that
    // does. Returns whichever SpellInfo would actually be the one hitting anyone standing nearby,
    // or null if this aura is not a bystander threat at all (e.g. a plain self-only DoT).
    SpellInfo const* ResolveAreaThreatSpell(Aura const* aura)
    {
        SpellInfo const* spellInfo = aura->GetSpellInfo();
        if (!spellInfo)
            return nullptr;
        if (SpellThreatensBystanders(spellInfo))
            return spellInfo;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        {
            if (spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL)
            {
                SpellInfo const* triggered = sSpellMgr->GetSpellInfo(spellInfo->Effects[i].TriggerSpell);
                if (SpellThreatensBystanders(triggered))
                    return triggered;
            }
        }
        return nullptr;
    }
}

class HostileGroundHazardCheck
{
public:
    HostileGroundHazardCheck(Player const* bot) : _bot(bot), _found(nullptr) {}

    bool operator()(WorldObject* obj)
    {
        DynamicObject* dyn = obj->ToDynObject();
        if (!dyn)
            return false;

        Unit* caster = dyn->GetCaster();
        if (!caster || !_bot->IsHostileTo(caster))
            return false;

        float dist = _bot->GetDistance(dyn);
        float radius = dyn->GetRadius();
        if (dist <= radius + 1.5f)
        {
            _found = dyn;
            return true;
        }

        return false;
    }

    DynamicObject* GetFound() const { return _found; }

private:
    Player const* _bot;
    DynamicObject* _found;
};

// A hostile creature standing nearby that carries a real bystander-threatening damage aura (see
// SpellThreatensBystanders/ResolveAreaThreatSpell) -- the "permanent pulsing damage the party must
// not loiter in, or must actively vacate" shape ground-DynamicObject detection alone never covers,
// since the game represents it as an aura on a Unit, not a DynamicObject at all.
class HostileCreaturePulseCheck
{
public:
    HostileCreaturePulseCheck(Player const* bot) : _bot(bot) {}

    bool operator()(WorldObject* obj)
    {
        Creature* creature = obj->ToCreature();
        if (!creature || !creature->IsAlive() || !_bot->IsHostileTo(creature))
            return false;

        for (auto const& [spellId, application] : creature->GetAppliedAuras())
        {
            Aura const* aura = application->GetBase();
            if (!aura || aura->GetCaster() != creature)
                continue; // only the creature's own pulse, not a debuff something else put on it

            SpellInfo const* threat = ResolveAreaThreatSpell(aura);
            if (!threat)
                continue;

            float radius = ResolveThreatRadius(threat);
            if (_bot->GetDistance(creature) <= radius + 1.5f)
            {
                _found = creature;
                _foundSpell = threat;
                _foundRadius = radius;
                return true;
            }
        }
        return false;
    }

    Creature* GetFound() const { return _found; }
    SpellInfo const* GetFoundSpell() const { return _foundSpell; }
    float GetFoundRadius() const { return _foundRadius; }

private:
    Player const* _bot;
    Creature* _found = nullptr;
    SpellInfo const* _foundSpell = nullptr;
    float _foundRadius = 0.0f;
};

// An armed GAMEOBJECT_TYPE_TRAP whose own template spell actually threatens bystanders -- the
// third ground-damage shape (a real GameObject, re-arming on its own cooldown, not a unit and not
// a DynamicObject either -- e.g. the classic Blaze-style fire trap some instance scripts drop).
class HostileTrapCheck
{
public:
    HostileTrapCheck(Player const* bot) : _bot(bot) {}

    bool operator()(WorldObject* obj)
    {
        GameObject* go = obj->ToGameObject();
        if (!go || go->GetGoType() != GAMEOBJECT_TYPE_TRAP || !go->isSpawned())
            return false;

        GameObjectTemplate const* info = go->GetGOInfo();
        if (!info || !info->trap.spellId)
            return false;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(info->trap.spellId);
        if (!SpellThreatensBystanders(spellInfo))
            return false;

        float radius = info->trap.diameter > 0 ? float(info->trap.diameter) / 2.0f : ResolveThreatRadius(spellInfo);
        if (_bot->GetDistance(go) <= radius + 1.5f)
        {
            _found = go;
            _foundSpell = spellInfo;
            _foundRadius = radius;
            return true;
        }
        return false;
    }

    GameObject* GetFound() const { return _found; }
    SpellInfo const* GetFoundSpell() const { return _foundSpell; }
    float GetFoundRadius() const { return _foundRadius; }

private:
    Player const* _bot;
    GameObject* _found = nullptr;
    SpellInfo const* _foundSpell = nullptr;
    float _foundRadius = 0.0f;
};

namespace
{
    constexpr float HAZARD_CHECK_RADIUS = 25.0f;

    // Tries all three ground-damage shapes in turn (DynamicObject first, since that's the
    // overwhelmingly common one and cheapest to rule out) and reports whichever is found first,
    // in one common shape so both public functions below can share the same search.
    bool FindGroundHazard(Player* bot, HazardHit& out)
    {
        {
            WorldObject* result = nullptr;
            HostileGroundHazardCheck check(bot);
            Acore::WorldObjectSearcher<HostileGroundHazardCheck> searcher(bot, result, check, GRID_MAP_TYPE_MASK_DYNAMICOBJECT);
            Cell::VisitObjects(bot, searcher, HAZARD_CHECK_RADIUS);
            if (DynamicObject* dyn = check.GetFound())
            {
                out.x = dyn->GetPositionX(); out.y = dyn->GetPositionY(); out.z = dyn->GetPositionZ();
                out.radius = dyn->GetRadius();
                out.spellInfo = sSpellMgr->GetSpellInfo(dyn->GetSpellId());
                return true;
            }
        }
        {
            WorldObject* result = nullptr;
            HostileCreaturePulseCheck check(bot);
            Acore::WorldObjectSearcher<HostileCreaturePulseCheck> searcher(bot, result, check, GRID_MAP_TYPE_MASK_CREATURE);
            Cell::VisitObjects(bot, searcher, HAZARD_CHECK_RADIUS);
            if (Creature* creature = check.GetFound())
            {
                out.x = creature->GetPositionX(); out.y = creature->GetPositionY(); out.z = creature->GetPositionZ();
                out.radius = check.GetFoundRadius();
                out.spellInfo = check.GetFoundSpell();
                return true;
            }
        }
        {
            WorldObject* result = nullptr;
            HostileTrapCheck check(bot);
            Acore::WorldObjectSearcher<HostileTrapCheck> searcher(bot, result, check, GRID_MAP_TYPE_MASK_GAMEOBJECT);
            Cell::VisitObjects(bot, searcher, HAZARD_CHECK_RADIUS);
            if (GameObject* go = check.GetFound())
            {
                out.x = go->GetPositionX(); out.y = go->GetPositionY(); out.z = go->GetPositionZ();
                out.radius = check.GetFoundRadius();
                out.spellInfo = check.GetFoundSpell();
                return true;
            }
        }
        return false;
    }
}

BotAvoidance::GroundHazardSeverity BotAvoidance::GetGroundHazardSeverity(Player* bot)
{
    if (!bot || !bot->IsAlive() || bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT))
        return GroundHazardSeverity::None;

    HazardHit hit;
    if (!FindGroundHazard(bot, hit))
        return GroundHazardSeverity::None;

    if (!hit.spellInfo)
        return GroundHazardSeverity::Dangerous;

    bool percentageOrLethalDamage = hit.spellInfo->HasEffect(SPELL_EFFECT_INSTAKILL)
        || hit.spellInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE_PERCENT);
    bool damaging = percentageOrLethalDamage
        || hit.spellInfo->HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE)
        || hit.spellInfo->HasEffect(SPELL_EFFECT_ENVIRONMENTAL_DAMAGE)
        || hit.spellInfo->HasEffect(SPELL_EFFECT_HEALTH_LEECH)
        || hit.spellInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE)
        || hit.spellInfo->HasAura(SPELL_AURA_PERIODIC_LEECH);

    if (percentageOrLethalDamage || (damaging && bot->GetHealthPct() < 60.0f))
        return GroundHazardSeverity::Critical;

    return GroundHazardSeverity::Dangerous;
}

bool BotAvoidance::HasCriticalBossMechanic(Player* bot, Unit* target)
{
    if (!bot || !bot->IsAlive() || !target || !target->IsAlive())
        return false;

    Creature* boss = target->ToCreature();
    if (!boss)
        return false;

    CreatureTemplate const* cinfo = boss->GetCreatureTemplate();
    if (!cinfo || (cinfo->rank < CREATURE_ELITE_ELITE && !boss->isWorldBoss()))
        return false;

    // Boss Knowledge Layer early warning: HP is already inside (or about to enter) a known
    // phase's dangerous-cast window, ahead of any live cast actually starting -- lets a bot bail
    // out of its own cast a beat sooner than waiting for GetCurrentSpell() to show it.
    if (bot->GetDistance(boss) < 12.0f && IsBossNearKnownAreaCastTrigger(boss))
        return true;

    Spell const* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!spell)
        spell = boss->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (!spell || bot->GetDistance(boss) >= 12.0f)
        return false;

    SpellInfo const* spellInfo = spell->GetSpellInfo();
    if (!spellInfo || !spellInfo->IsChanneled())
        return false;

    return HasCasterCenteredAreaEffect(spellInfo);
}

bool BotAvoidance::TryAvoidGroundHazards(Player* bot)
{
    if (!bot || !bot->IsAlive() || bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT))
        return false;

    HazardHit hit;
    if (!FindGroundHazard(bot, hit))
        return false;

    float dx = bot->GetPositionX() - hit.x;
    float dy = bot->GetPositionY() - hit.y;
    float dist = std::sqrt(dx * dx + dy * dy);

    if (dist < 0.1f)
    {
        dx = 1.0f;
        dy = 0.0f;
        dist = 1.0f;
    }

    float escapeDist = hit.radius + 4.0f;
    float safeX = hit.x + (dx / dist) * escapeDist;
    float safeY = hit.y + (dy / dist) * escapeDist;
    // safeZ is only a starting guess (the bot's own height) -- MoveTo re-grounds it against the
    // actual terrain under (safeX, safeY) before issuing the move. Without that, a hazard escape
    // point on a different floor/ledge/slope than the bot currently stands on hands the
    // pathfinder a destination whose Z doesn't match its X/Y, which routinely fails the navmesh
    // poly lookup and falls back to a straight-line "shortcut" through walls/floors.
    float safeZ = bot->GetPositionZ();

    return BotMovement::MoveTo(bot, MoveOwner::Avoidance, safeX, safeY, safeZ);
}

bool BotAvoidance::TryAvoidBossTelegraphedAttacks(Player* bot, Unit* target)
{
    if (!bot || !bot->IsAlive() || !target || !target->IsAlive() || bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT))
        return false;

    Creature* boss = target->ToCreature();
    if (!boss)
        return false;

    CreatureTemplate const* cinfo = boss->GetCreatureTemplate();
    if (!cinfo || (cinfo->rank < CREATURE_ELITE_ELITE && !boss->isWorldBoss()))
        return false;

    // 1a. Boss Knowledge Layer early warning -- HP already inside (or about to enter) a known
    // phase's dangerous-cast window. Same retreat as the live-cast case below, just triggered by
    // upcoming-phase knowledge instead of waiting for the cast to actually start.
    if (bot->GetDistance(boss) < 12.0f && IsBossNearKnownAreaCastTrigger(boss))
    {
        float dx = bot->GetPositionX() - boss->GetPositionX();
        float dy = bot->GetPositionY() - boss->GetPositionY();
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }

        float retreatX = boss->GetPositionX() + (dx / dist) * 16.0f;
        float retreatY = boss->GetPositionY() + (dy / dist) * 16.0f;
        if (BotMovement::MoveTo(bot, MoveOwner::Avoidance, retreatX, retreatY, boss->GetPositionZ()))
            return true;
    }

    // 1b. Point-blank AoE / Whirlwind Avoidance (live cast already in progress)
    if (boss->IsNonMeleeSpellCast(false))
    {
        // Check both generic spells and channeled spells (channeled spells are in CURRENT_CHANNELED_SPELL slot)
        Spell const* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell)
            spell = boss->GetCurrentSpell(CURRENT_CHANNELED_SPELL);

        if (spell)
        {
            SpellInfo const* spellInfo = spell->GetSpellInfo();
            if (spellInfo && (spellInfo->IsChanneled() || spell == boss->GetCurrentSpell(CURRENT_CHANNELED_SPELL)) &&
                HasCasterCenteredAreaEffect(spellInfo) && bot->GetDistance(boss) < 12.0f)
            {
                float dx = bot->GetPositionX() - boss->GetPositionX();
                float dy = bot->GetPositionY() - boss->GetPositionY();
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < 0.1f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }

                float retreatX = boss->GetPositionX() + (dx / dist) * 16.0f;
                float retreatY = boss->GetPositionY() + (dy / dist) * 16.0f;
                // boss->GetPositionZ() is only a seed value; MoveTo re-grounds it
                // against the real terrain 16yd away, which is routinely a different
                // floor height in raid/dungeon rooms (stairs, pillars, pits).
                if (BotMovement::MoveTo(bot, MoveOwner::Avoidance, retreatX, retreatY, boss->GetPositionZ()))
                    return true;
            }
        }
    }

    // 2. Frontal Cleave Avoidance: Non-tanks must NEVER stand in front of a boss
    if (boss->GetVictim() != bot)
    {
        if (boss->HasInArc(static_cast<float>(M_PI) * 0.5f, bot) && bot->GetDistance(boss) < 8.0f)
        {
            float behindAngle = boss->GetOrientation() + static_cast<float>(M_PI);
            float behindX = boss->GetPositionX() + 3.5f * std::cos(behindAngle);
            float behindY = boss->GetPositionY() + 3.5f * std::sin(behindAngle);
            return BotMovement::MoveTo(bot, MoveOwner::Avoidance, behindX, behindY, boss->GetPositionZ());
        }
    }

    return false;
}
