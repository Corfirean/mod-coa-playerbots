#include "BotAvoidance.h"
#include "BotMovement.h"
#include "Player.h"
#include "Creature.h"
#include "DynamicObject.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include <cmath>

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

BotAvoidance::GroundHazardSeverity BotAvoidance::GetGroundHazardSeverity(Player* bot)
{
    if (!bot || !bot->IsAlive() || bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT))
        return GroundHazardSeverity::None;

    constexpr float HAZARD_CHECK_RADIUS = 25.0f;
    WorldObject* result = nullptr;
    HostileGroundHazardCheck check(bot);
    Acore::WorldObjectSearcher<HostileGroundHazardCheck> searcher(bot, result, check, GRID_MAP_TYPE_MASK_DYNAMICOBJECT);
    Cell::VisitObjects(bot, searcher, HAZARD_CHECK_RADIUS);

    DynamicObject* hazard = check.GetFound();
    if (!hazard)
        return GroundHazardSeverity::None;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(hazard->GetSpellId());
    if (!spellInfo)
        return GroundHazardSeverity::Dangerous;

    bool percentageOrLethalDamage = spellInfo->HasEffect(SPELL_EFFECT_INSTAKILL)
        || spellInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE_PERCENT);
    bool damaging = percentageOrLethalDamage
        || spellInfo->HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE)
        || spellInfo->HasEffect(SPELL_EFFECT_ENVIRONMENTAL_DAMAGE)
        || spellInfo->HasEffect(SPELL_EFFECT_HEALTH_LEECH)
        || spellInfo->HasAura(SPELL_AURA_PERIODIC_DAMAGE)
        || spellInfo->HasAura(SPELL_AURA_PERIODIC_LEECH);

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

    Spell const* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (!spell)
        spell = boss->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (!spell || bot->GetDistance(boss) >= 12.0f)
        return false;

    SpellInfo const* spellInfo = spell->GetSpellInfo();
    if (!spellInfo || !spellInfo->IsChanneled())
        return false;

    for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
        if (spellInfo->Effects[i].TargetA.GetTarget() == TARGET_SRC_CASTER ||
            spellInfo->Effects[i].TargetA.GetTarget() == TARGET_UNIT_SRC_AREA_ENEMY)
            return true;

    return false;
}

bool BotAvoidance::TryAvoidGroundHazards(Player* bot)
{
    if (!bot || !bot->IsAlive() || bot->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT))
        return false;

    constexpr float HAZARD_CHECK_RADIUS = 25.0f;
    WorldObject* result = nullptr;
    HostileGroundHazardCheck check(bot);
    Acore::WorldObjectSearcher<HostileGroundHazardCheck> searcher(bot, result, check, GRID_MAP_TYPE_MASK_DYNAMICOBJECT);
    Cell::VisitObjects(bot, searcher, HAZARD_CHECK_RADIUS);

    DynamicObject* hazard = check.GetFound();
    if (!hazard)
        return false;

    float dx = bot->GetPositionX() - hazard->GetPositionX();
    float dy = bot->GetPositionY() - hazard->GetPositionY();
    float dist = std::sqrt(dx * dx + dy * dy);

    if (dist < 0.1f)
    {
        dx = 1.0f;
        dy = 0.0f;
        dist = 1.0f;
    }

    float escapeDist = hazard->GetRadius() + 4.0f;
    float safeX = hazard->GetPositionX() + (dx / dist) * escapeDist;
    float safeY = hazard->GetPositionY() + (dy / dist) * escapeDist;
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

    // 1. Point-blank AoE / Whirlwind Avoidance
    if (boss->IsNonMeleeSpellCast(false))
    {
        // Check both generic spells and channeled spells (channeled spells are in CURRENT_CHANNELED_SPELL slot)
        Spell const* spell = boss->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!spell)
            spell = boss->GetCurrentSpell(CURRENT_CHANNELED_SPELL);

        if (spell)
        {
            SpellInfo const* spellInfo = spell->GetSpellInfo();
            if (spellInfo && (spellInfo->IsChanneled() || spell == boss->GetCurrentSpell(CURRENT_CHANNELED_SPELL)))
            {
                for (uint8 i = 0; i < 3; ++i)
                {
                    if (spellInfo->Effects[i].TargetA.GetTarget() == TARGET_SRC_CASTER ||
                        spellInfo->Effects[i].TargetA.GetTarget() == TARGET_UNIT_SRC_AREA_ENEMY)
                    {
                        if (bot->GetDistance(boss) < 12.0f)
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
