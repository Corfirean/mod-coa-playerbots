#include "training/TrainingSession.h"
#include "BotMgr.h"
#include "Creature.h"
#include "CharmInfo.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ThreatManager.h"
#include "SpellAuras.h"
#include "SpellAuraEffects.h"
#include "engine/CombatResource.h"
#include "engine/CastGuard.h"
#include "MotionMaster.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include <algorithm>
#include <unordered_set>
#include <list>

namespace CoaTraining
{
void Session::Event(Json row)
{
    row["sessionId"] = id; row["arenaId"] = arenaId; row["phaseMask"] = phaseMask;
    row["timeMs"] = timeMs;
    row["sessionId"] = id;
    events << row.Dump() << '\n';
}

void Session::ResetTelemetry()
{
    timeMs = sampleMs = nextMechanic = ownedEntitySampleMs = 0;
    damage = healing = damageTaken = avoidableDamage = avoidableHits = 0;
    castingMs = movingMs = idleMs = aggroMs = oomMs = 0;
    aggroLosses = lostAt = 0;
    lastAggro = true;
    hazardActive = avoidableEvent = decisionActive = false;
    watchingInterruptCast = false;
    interruptStartMs = interruptExpectedEndMs = 0;
    multiCasts.clear();
    interruptCasts = interruptsStopped = 0;
    abilities.clear(); auras.clear(); cappedMs.clear(); starvedMs.clear();
    buffCoverageMs.clear(); debuffUptimeMs.clear();
    recoveries = Json::Array{};
    for (Member& m : members)
    {
        m.lowestHp = 100; m.criticalMs = 0; m.dead = false; m.deathMs = 0;
    }
}

void Session::Sample(uint32 diff)
{
    Player* bot = sBotMgr->FindBotPlayer(candidate.GetCounter());
    if (!bot) return;
    bool casting = bot->IsNonMeleeSpellCast(false);
    bool moving = bot->isMoving();
    if (casting) castingMs += diff;
    if (moving) movingMs += diff;
    if (!casting && !moving && !bot->GetVictim()) idleMs += diff;
    Json row;
    row["sessionId"] = id; row["arenaId"] = arenaId; row["phaseMask"] = phaseMask;
    row["timeMs"] = timeMs;
    row["hpPct"] = bot->GetHealthPct();
    row["inCombat"] = bot->IsInCombat();
    row["casting"] = casting; row["moving"] = moving;
    row["protectedCastSpell"] = BotAI::CastGuard::CurrentSpellId(bot);
    row["motionType"] = uint32(bot->GetMotionMaster()->GetCurrentMovementGeneratorType());
    row["movementBlocked"] = bot->HasUnitState(UNIT_STATE_NOT_MOVE | UNIT_STATE_NO_COMBAT_MOVEMENT);
    row["x"] = bot->GetPositionX(); row["y"] = bot->GetPositionY(); row["z"] = bot->GetPositionZ();
    auto snapshot = BotAI::CombatResourceEvaluator::BuildSnapshot(bot);
    Json resources{Json::Array{}};
    for (uint8 i = 0; i < snapshot.count; ++i)
    {
        auto const& r = snapshot.resources[i];
        std::string key = std::to_string(uint32(r.key.kind)) + ":" + std::to_string(r.key.powerType)
            + ":" + std::to_string(r.key.auraSpellId);
        Json channel;
        channel["key"] = key; channel["name"] = r.name;
        channel["current"] = r.current;
        channel["maximum"] = r.maximumKnown ? Json(r.maximum) : Json();
        resources.Push(channel);
        if (r.maximumKnown && r.maximum > 0 && r.current >= r.maximum) cappedMs[key] += diff;
        if (r.current == 0) starvedMs[key] += diff;
    }
    row["resources"] = resources;
    // Read-only, once-per-second ownership evidence for summon-cap diagnostics.
    ownedEntitySampleMs += diff;
    if (ownedEntitySampleMs >= 1000 && sampleMs + diff >= 100)
    {
        ownedEntitySampleMs = 0;
        Json entities{Json::Array{}};
        std::unordered_set<ObjectGuid> seen;
        auto consider = [&](Unit* unit)
        {
            if (!unit || !unit->IsAlive() || !bot->InSamePhase(unit) ||
                (unit->GetCharmerOrOwnerGUID() != candidate && unit->GetOwnerGUID() != candidate) ||
                !seen.insert(unit->GetGUID()).second) return;
            Json entity;
            entity["guid"] = uint64(unit->GetGUID().GetCounter());
            entity["entry"] = unit->GetEntry();
            entity["distance"] = bot->GetDistance(unit);
            entity["victim"] = uint64(unit->GetVictim() ? unit->GetVictim()->GetGUID().GetCounter() : 0);
            entity["inCombat"] = unit->IsInCombat();
            // Pet boon copies are native auras, not player cast results. Record
            // their actual caster and effect amounts alongside ownership so a
            // diagnostic can distinguish a copy from an owner-only buff.
            Json ownedAuras{Json::Array{}};
            for (auto const& [spellId, application] : unit->GetAppliedAuras())
            {
                Aura const* aura = application->GetBase();
                if (!aura) continue;
                Json observed;
                observed["spellId"] = aura->GetId();
                observed["caster"] = uint64(aura->GetCasterGUID().GetCounter());
                observed["stacks"] = uint32(aura->GetStackAmount());
                Json effects{Json::Array{}};
                for (uint8 index = 0; index < MAX_SPELL_EFFECTS; ++index)
                    if (AuraEffect const* effect = aura->GetEffect(index))
                    {
                        Json value;
                        value["index"] = uint32(index);
                        value["amount"] = effect->GetAmount();
                        effects.Push(value);
                    }
                observed["effects"] = effects;
                ownedAuras.Push(observed);
            }
            entity["auras"] = ownedAuras;
            if (Creature* creature = unit->ToCreature())
            {
                entity["reactState"] = uint32(creature->GetReactState());
                entity["aiEnabled"] = creature->IsAIEnabled;
            }
            if (CharmInfo* charm = unit->GetCharmInfo())
            {
                entity["commandState"] = uint32(charm->GetCommandState());
                entity["commandAttack"] = charm->IsCommandAttack();
                entity["commandFollow"] = charm->IsCommandFollow();
                entity["returning"] = charm->IsReturning();
                entity["following"] = charm->IsFollowing();
                entity["atStay"] = charm->IsAtStay();
            }
            entities.Push(entity);
        };
        for (Unit* unit : bot->m_Controlled) consider(unit);
        std::list<Unit*> nearby;
        Acore::AnyUnitInObjectRangeCheck check(bot, 60.0f);
        Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> search(bot, nearby, check);
        Cell::VisitObjects(bot, search, 60.0f);
        for (Unit* unit : nearby) consider(unit);
        row["ownedEntities"] = entities;
    }
    if (bot->getPowerType() == POWER_MANA && bot->GetPower(POWER_MANA) == 0) oomMs += diff;
    Json health{Json::Array{}};
    for (Member& m : members)
    {
        Player* p = sBotMgr->FindBotPlayer(m.guid.GetCounter());
        if (!p) continue;
        m.lowestHp = std::min(m.lowestHp, p->GetHealthPct());
        if (p->IsAlive() && p->GetHealthPct() < 25) m.criticalMs += diff;
        if (!p->IsAlive() && !m.dead) { m.dead = true; m.deathMs = timeMs; }
        Json member;
        member["guid"] = uint64(m.guid.GetCounter()); member["hpPct"] = p->GetHealthPct();
        member["alive"] = p->IsAlive(); member["resurrectRequested"] = p->isResurrectRequested(); health.Push(member);
    }
    row["members"] = health;
    for (uint32 spell : requiredBuffs)
        for (auto const& m : members)
            if (Player* member = sBotMgr->FindBotPlayer(m.guid.GetCounter()))
                if (member->HasAura(spell, candidate)) buffCoverageMs[spell] += diff;
    if (Creature* enemy = ObjectAccessor::GetCreature(*bot, boss))
    {
        row["targetDistance"] = bot->GetDistance(enemy);
        row["meleeRange"] = bot->GetMeleeRange(enemy);
        for (uint32 spell : requiredDebuffs)
            if (enemy->HasAura(spell, candidate)) debuffUptimeMs[spell] += diff;
        float ownThreat = enemy->GetThreatMgr().GetThreat(bot, true);
        float competitor = 0;
        for (auto const& m : members)
            if (m.guid != candidate)
                if (Player* p = sBotMgr->FindBotPlayer(m.guid.GetCounter()))
                    competitor = std::max(competitor, enemy->GetThreatMgr().GetThreat(p, true));
        row["threat"] = ownThreat;
        row["highestOtherThreat"] = competitor;
        row["threatMargin"] = ownThreat - competitor;
        bool aggro = enemy->GetVictim() == bot;
        row["hasAggro"] = aggro;
        if (aggro) aggroMs += diff;
        if (!aggro && lastAggro) { ++aggroLosses; lostAt = timeMs; }
        if (aggro && !lastAggro) recoveries.Push(timeMs - lostAt);
        lastAggro = aggro;
    }
    sampleMs += diff;
    if (sampleMs >= 100)
    {
        samples << row.Dump() << '\n'; sampleMs = 0;
    }
}
}
