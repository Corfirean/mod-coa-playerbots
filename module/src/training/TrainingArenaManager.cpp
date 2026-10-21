#include "training/TrainingSession.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "BotGear.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "BotSpawnRandom.h"
#include "BotProgression.h"
#include "BotTalentBuilds.h"
#include "Chat.h"
#include "CharacterCache.h"
#include "ClassSpecRoles.h"
#include "Config.h"
#include "DBCStores.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "Pet.h"
#include "Group.h"
#include "GroupMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "ThreatManager.h"
#include "WorldSession.h"
#include "engine/CombatContext.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/SpellPredicates.h"
#include "Spell.h"
#include "Timer.h"
#include <chrono>
#include <memory>
#include <mutex>

namespace BotAI
{
std::string DebugState(Player* bot);
}

namespace CoaTraining
{
std::atomic<bool> Enabled{false};
namespace
{
std::recursive_mutex mutex;
std::map<std::string, std::unique_ptr<Session>> sessions;
std::map<ObjectGuid, Session*> owners;
std::vector<ObjectGuid> configuredParty;
uint64 sequence = 0;
constexpr uint32 MaxArenaSlots = 4;
constexpr uint32 ArenaMap = 0;
constexpr float X = -13200.0f, Y = 275.0f, Z = 22.0f;

Session* Lookup(ObjectGuid guid)
{
    auto it = owners.find(guid);
    return it == owners.end() ? nullptr : it->second;
}

ObjectGuid Principal(Unit* unit)
{
    if (!unit) return ObjectGuid::Empty;
    if (Session* s = Lookup(unit->GetGUID()); s &&
        std::find(s->enemies.begin(), s->enemies.end(), unit->GetGUID()) != s->enemies.end()) return unit->GetGUID();
    if (Unit* owner = unit->GetCharmerOrOwner()) return owner->GetGUID();
    return unit->GetGUID();
}

std::string NewId()
{
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count()) + "-" + std::to_string(++sequence);
}

void OpenOutput(Session& s)
{
    s.id = NewId();
    s.directory = std::filesystem::path(sConfigMgr->GetOption<std::string>(
        "CoaBots.Training.OutputDir", "training/results")) / std::to_string(s.members.front().spec) / s.id;
    std::filesystem::create_directories(s.directory);
    s.events.close(); s.decisions.close(); s.samples.close();
    s.events.clear(); s.decisions.clear(); s.samples.clear();
    s.events.open(s.directory / "events.jsonl", std::ios::binary);
    s.decisions.open(s.directory / "decisions.jsonl", std::ios::binary);
    s.samples.open(s.directory / "samples.jsonl", std::ios::binary);
    if (!s.events || !s.decisions || !s.samples) throw std::runtime_error("Cannot open training output directory");
}

void Despawn(Session& s)
{
    Player* bot = sBotMgr->FindBotPlayer(s.candidate.GetCounter());
    for (ObjectGuid guid : s.enemies)
    {
        owners.erase(guid);
        if (bot)
            if (Creature* c = ObjectAccessor::GetCreature(*bot, guid)) c->DespawnOrUnsummon();
    }
    s.enemies.clear(); s.boss.Clear();
}

void Place(Session& s)
{
    for (size_t i = 0; i < s.members.size(); ++i)
    {
        Player* p = sBotMgr->FindBotPlayer(s.members[i].guid.GetCounter());
        if (!p) throw std::runtime_error("Training party member disconnected");
        if (Gear(p).Dump() != s.members[i].gear.Dump()) throw std::runtime_error("Training gear changed");
        Reset(p);
        p->SetPhaseMask(s.phaseMask, true);
        if (!p->TeleportTo(ArenaMap, X + 3 + float(i) * 2, Y, Z, 3.14f))
            throw std::runtime_error("Arena teleport failed");
    }
    s.preparationMs = 0;
}

void Release(Session& s)
{
    s.running = false;
    Despawn(s);
    if (s.createdGroup)
        if (Player* bot = sBotMgr->FindBotPlayer(s.candidate.GetCounter()))
            if (Group* group = bot->GetGroup()) group->Disband();
    for (auto const& m : s.members)
    {
        if (Player* p = sBotMgr->FindBotPlayer(m.guid.GetCounter()))
        {
            Reset(p);
            p->SetPhaseMask(m.phase, true);
            p->TeleportTo(m.home);
        }
        owners.erase(m.guid);
    }
    s.finished = true;
}

void Finish(Session& s, char const* reason, bool repeat)
{
    s.running = false;
    s.WriteReport(reason);
    LOG_INFO("module.coa-playerbots", "Training episode {} ended: {}. Report: {}", s.id, reason, s.directory.string());
    if (repeat && s.runsRemaining > 1)
    {
        --s.runsRemaining; ++s.seed;
        Despawn(s); s.ResetTelemetry(); OpenOutput(s); Place(s);
        s.startRequested = true;
    }
    else Release(s);
}

void Spawn(Session& s)
{
    Player* bot = sBotMgr->FindBotPlayer(s.candidate.GetCounter());
    for (uint32 i = 0; i < s.scenario.enemyCount; ++i)
    {
        auto* enemy = bot->SummonCreature(sConfigMgr->GetOption<uint32>("CoaBots.Training.BossEntry", 31144),
            X, Y + float(i) * 3, Z, 0, TEMPSUMMON_MANUAL_DESPAWN);
        if (!enemy) throw std::runtime_error("Training boss summon failed (check BossEntry)");
        enemy->SetPhaseMask(s.phaseMask, true);
        enemy->SetFaction(14);
        enemy->SetLevel(bot->GetLevel());
        InstallBossAI(enemy);
        if (s.scenario.targetHealthPct < 100)
            enemy->SetHealth(std::max<uint32>(1, uint32(uint64(enemy->GetMaxHealth()) * s.scenario.targetHealthPct / 100)));
        s.enemies.push_back(enemy->GetGUID()); owners[enemy->GetGUID()] = &s;
    }
    s.boss = s.enemies.front();
    if (s.scenario.hazardDamage)
    {
        uint32 spell = sConfigMgr->GetOption<uint32>("CoaBots.Training.HazardSpellId", 5740);
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spell);
        if (!info || !info->HasEffect(SPELL_EFFECT_PERSISTENT_AREA_AURA))
            throw std::runtime_error("HazardSpellId must define a real persistent area aura");
        s.hazardSpells.insert(spell);
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (info->Effects[i].TriggerSpell) s.hazardSpells.insert(info->Effects[i].TriggerSpell);
    }
    s.timeline = s.scenario.Timeline(s.seed, uint32(s.members.size()));
    for (auto const& m : s.members)
    {
        Player* p = sBotMgr->FindBotPlayer(m.guid.GetCounter());
        p->Attack(ObjectAccessor::GetCreature(*p, s.boss), true);
        p->SetInCombatWith(ObjectAccessor::GetCreature(*p, s.boss));
    }
    Creature* boss = ObjectAccessor::GetCreature(*bot, s.boss);
    Player* tank = bot;
    for (auto const& m : s.members)
        if (m.role == "Tank") tank = sBotMgr->FindBotPlayer(m.guid.GetCounter());
    for (ObjectGuid guid : s.enemies)
    {
        Creature* enemy = ObjectAccessor::GetCreature(*bot, guid);
        enemy->GetThreatMgr().AddThreat(tank, 1, nullptr, true, true);
        enemy->Attack(tank, true);
        for (auto const& member : s.members)
            sBotMgr->FindBotPlayer(member.guid.GetCounter())->SetInCombatWith(enemy);
    }
    s.running = true; s.startRequested = false;
    Json pull; pull["type"] = "episode_started"; s.Event(pull);
    for (auto const& m : s.members)
        if (Player* p = sBotMgr->FindBotPlayer(m.guid.GetCounter()))
            for (auto const& [id, application] : p->GetAppliedAuras())
            {
                auto* aura = application->GetBase();
                RecordAura(p, id, aura->GetCasterGUID(), "apply", aura->GetStackAmount());
            }
}

void MechanicEvent(Session& s, Mechanic const& m)
{
    Player* bot = sBotMgr->FindBotPlayer(s.candidate.GetCounter());
    Creature* boss = bot ? ObjectAccessor::GetCreature(*bot, s.boss) : nullptr;
    if (!boss) throw std::runtime_error("Training boss disappeared");
    Json event; event["type"] = "mechanic"; event["kind"] = m.kind; event["scheduledTimeMs"] = m.timeMs;
    event["amount"] = m.amount; event["targetSlot"] = m.targetSlot;
    s.Event(event);
    auto damage = [&](Unit* target, bool avoidable)
    {
        if (!target || !target->IsAlive()) return;
        s.avoidableEvent = avoidable;
        Unit::DealDamage(boss, target, m.amount, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
        s.avoidableEvent = false;
    };
    if (m.kind == "melee" || m.kind == "tankbuster" || m.kind == "ramp_hit")
    {
        boss->GetThreatMgr().UpdateVictimForTesting();
        if (Unit* victim = boss->GetThreatMgr().GetCurrentVictim()) boss->Attack(victim, true);
        if (m.kind == "melee")
        {
            boss->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, float(m.amount));
            boss->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, float(m.amount));
            boss->UpdateDamagePhysical(BASE_ATTACK);
            if (Unit* victim = boss->GetVictim()) boss->AttackerStateUpdate(victim);
        }
        else damage(boss->GetVictim(), false);
    }
    else if (m.kind == "dispel_debuff")
    {
        ObjectGuid allyGuid = s.members[s.members.size() > 1 ? 1 : 0].guid;
        if (Player* ally = sBotMgr->FindBotPlayer(allyGuid.GetCounter()))
        {
            SpellCastResult result = boss->CastSpell(ally, m.amount, false);
            Json cast; cast["type"] = "diagnostic_debuff_cast";
            cast["spellId"] = m.amount; cast["target"] = allyGuid.GetCounter();
            cast["result"] = uint32(result); s.Event(cast);
        }
    }
    else if (m.kind == "resurrection_death" && s.members.size() > 1)
    {
        if (Player* ally = sBotMgr->FindBotPlayer(s.members[1].guid.GetCounter()))
        {
            Unit::Kill(boss, ally, false); // Diagnostic death must not damage the gear fixture.
            Json killed; killed["type"] = "diagnostic_member_death";
            killed["target"] = ally->GetGUID().GetCounter(); s.Event(killed);
            if (s.scenario.resurrectionEndCombat)
            {
                boss->SetReactState(REACT_PASSIVE);
                boss->SetFaction(35);
                boss->CombatStop(true);
                for (auto const& member : s.members)
                    if (Player* p = sBotMgr->FindBotPlayer(member.guid.GetCounter())) p->CombatStop(true);
                Json ended; ended["type"] = "diagnostic_combat_ended"; s.Event(ended);
            }
        }
    }
    else if (m.kind == "pool_refill")
    {
        // Healer throughput test: keep every other party member at 50% health so the healer always has something to heal.
        for (auto const& member : s.members)
        {
            if (member.guid == s.candidate) continue;
            Player* p = sBotMgr->FindBotPlayer(member.guid.GetCounter());
            if (!p || !p->IsAlive() || p->GetHealth() * 2 <= p->GetMaxHealth()) continue;
            s.avoidableEvent = false;
            Unit::DealDamage(boss, p, p->GetHealth() - p->GetMaxHealth() / 2, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
        }
    }
    else if (m.kind == "party_aoe")
        for (auto const& member : s.members) damage(sBotMgr->FindBotPlayer(member.guid.GetCounter()), false);
    else if (m.kind == "targeted_damage")
        damage(sBotMgr->FindBotPlayer(s.members[m.targetSlot].guid.GetCounter()), false);
    else if (m.kind == "threat_pressure" && s.members.size() > 1)
    {
        for (auto const& member : s.members)
            if (member.role != "Tank")
            {
                boss->GetThreatMgr().AddThreat(sBotMgr->FindBotPlayer(member.guid.GetCounter()), float(m.amount),
                    nullptr, true, true);
                break;
            }
        boss->GetThreatMgr().UpdateVictimForTesting();
        if (Unit* victim = boss->GetThreatMgr().GetCurrentVictim()) boss->Attack(victim, true);
    }
    else if (m.kind == "hazard_warning")
    {
        Player* p = sBotMgr->FindBotPlayer(s.members[m.targetSlot].guid.GetCounter());
        s.hazardX = p->GetPositionX(); s.hazardY = p->GetPositionY(); s.hazardZ = p->GetPositionZ();
        s.hazardActive = true;
    }
    else if (m.kind == "hazard_explosion")
    {
        uint32 spell = sConfigMgr->GetOption<uint32>("CoaBots.Training.HazardSpellId", 5740);
        boss->CastSpell(s.hazardX, s.hazardY, s.hazardZ, spell, true);
        s.hazardActive = false;
    }
    else if (m.kind == "boss_move")
        boss->NearTeleportTo(X, Y + ((m.timeMs / 15000) % 2 ? 15.0f : -15.0f), Z, 0);
    else if (m.kind == "interruptible_cast")
    {
        uint32 spellId = sConfigMgr->GetOption<uint32>("CoaBots.Training.InterruptCastSpellId", 133);
        SpellCastResult result = boss->CastSpell(bot, spellId, false);
        Json cast; cast["type"] = "boss_cast"; cast["spellId"] = spellId; cast["result"] = uint32(result);
        uint32 observedSpell = 0, finish = 0;
        s.watchingInterruptCast = BotAI::IsTargetCastingInterruptibleSpell(boss, observedSpell, finish);
        cast["interruptible"] = s.watchingInterruptCast;
        if (s.watchingInterruptCast)
        {
            s.interruptStartMs = s.timeMs;
            uint32 duration = finish - getMSTime();
            s.interruptExpectedEndMs = s.timeMs + duration;
            cast["durationMs"] = duration;
            cast["candidateHadInterruptAvailable"] = BotAI::SelectInterruptSpell(bot, boss) != 0;
        }
        s.Event(cast);
    }
}
}

void Initialize()
{
    Enabled = sConfigMgr->GetOption<bool>("CoaBots.Training.Enable", false);
}
bool Owns(ObjectGuid guid)
{
    if (!Enabled.load(std::memory_order_relaxed)) return false;
    std::lock_guard lock(mutex);
    return Lookup(guid) != nullptr;
}
bool Running(ObjectGuid guid)
{
    if (!Enabled.load(std::memory_order_relaxed)) return false;
    std::lock_guard lock(mutex);
    Session* s = Lookup(guid); return s && s->running;
}

void Update(uint32 diff)
{
    if (!Enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    for (auto& [id, ptr] : sessions)
    {
        Session& s = *ptr;
        if (s.finished) continue;
        try
        {
            Player* bot = sBotMgr->FindBotPlayer(s.candidate.GetCounter());
            if (!bot) throw std::runtime_error("Candidate disconnected");
            if (!s.running)
            {
                s.preparationMs += diff;
                bool ready = true;
                for (auto const& m : s.members)
                {
                    Player* p = sBotMgr->FindBotPlayer(m.guid.GetCounter());
                    if (!p) throw std::runtime_error("Party member disconnected");
                    if (p->IsBeingTeleported() || !p->IsInWorld()) { ready = false; continue; }
                    auto result = BotAI::SpecStrategyRegistry::ExecutePrePullStrategy(p, p->GetGroup(), diff);
                    if (result != BotAI::PrePullResult::Ready) ready = false;
                }
                if (s.startRequested && s.preparationMs > 30000) throw std::runtime_error("Pre-pull setup timeout");
                if (s.startRequested && ready && s.preparationMs >= 2000) Spawn(s);
                continue;
            }
            uint32 elapsed = std::min(diff, s.scenario.durationMs - s.timeMs);
            s.timeMs += elapsed;
            while (s.nextMechanic < s.timeline.size() && s.timeline[s.nextMechanic].timeMs <= s.timeMs)
                MechanicEvent(s, s.timeline[s.nextMechanic++]);
            s.Sample(elapsed);
            if (s.watchingInterruptCast)
                if (Creature* boss = ObjectAccessor::GetCreature(*bot, s.boss))
                    if (!boss->IsNonMeleeSpellCast(false))
                    {
                        Json event; event["type"] = "boss_cast_ended";
                        event["latencyMs"] = s.timeMs - s.interruptStartMs;
                        event["endedBeforeExpectedFinish"] = s.timeMs + 100 < s.interruptExpectedEndMs;
                        event["successfulInterruptConfirmed"] = Json();
                        s.Event(event); s.watchingInterruptCast = false;
                    }
            bool allDead = true;
            for (auto const& m : s.members)
            {
                Player* p = sBotMgr->FindBotPlayer(m.guid.GetCounter());
                if (!p) throw std::runtime_error("Party member disconnected");
                if (p->GetMapId() != ArenaMap || p->GetPhaseMask() != s.phaseMask)
                    throw std::runtime_error("Party member left arena");
                if (p->IsAlive()) allDead = false;
            }
            if (s.timeMs >= s.scenario.durationMs || allDead) Finish(s, allDead ? "wipe" : "duration", true);
        }
        catch (std::exception const& e)
        {
            LOG_ERROR("module.coa-playerbots", "Training {} failed: {}", s.id, e.what());
            try { s.WriteReport(e.what()); } catch (...) { }
            Release(s);
        }
    }
}

void Shutdown()
{
    std::lock_guard lock(mutex);
    for (auto& [id, s] : sessions)
        if (!s->finished)
        {
            try { s->WriteReport("shutdown"); } catch (...) { }
            Release(*s);
        }
    sessions.clear(); owners.clear();
}

// ---- Dungeon probe: a group of bots led by an inert leader (a suspended bot that stands still like a player who has not touched
// the keyboard) walks into a dungeon, so the group behaviour a real player sees -- follow, tank, heal, assist -- can be measured on the
// isolated training server. Verbs: dgroup, dgo, dmove, dpull, dstat, dfree. Dedicated bot accounts only, never account 1.
Player* DungeonBot(uint32 guid)
{
    Player* p = sBotMgr->FindBotPlayer(guid);
    if (!p || p->GetSession()->GetAccountId() == 1 || !BotMgr::IsBotAccountId(p->GetSession()->GetAccountId()))
        throw std::runtime_error("dedicated online bot required: " + std::to_string(guid));
    if (Lookup(p->GetGUID())) throw std::runtime_error("bot belongs to a training session: " + std::to_string(guid));
    return p;
}

void DungeonProbe(ChatHandler* h, std::string const& verb, std::istringstream& input)
{
    uint32 leaderGuid = 0; input >> leaderGuid;
    Player* leader = DungeonBot(leaderGuid);
    if (verb == "dgroup")
    {
        std::vector<Player*> members;
        uint32 guid;
        while (input >> guid) members.push_back(DungeonBot(guid));
        if (members.empty() || members.size() > 4) throw std::runtime_error("dgroup <leader> <1..4 members>");
        std::vector<Player*> all{leader};
        all.insert(all.end(), members.begin(), members.end());
        for (Player* p : all)
            if (Group* old = p->GetGroup())
            {
                for (auto const& slot : old->GetMemberSlots())
                    if (!BotMgr::IsBotAccountId(sCharacterCache->GetCharacterAccountIdByGuid(slot.guid)))
                        throw std::runtime_error("an existing group contains a non-bot account");
                old->Disband();
            }
        Group* group = new Group();
        if (!group->Create(leader)) { delete group; throw std::runtime_error("cannot create group"); }
        sGroupMgr->AddGroup(group);
        for (Player* m : members)
            if (!group->AddMember(m)) throw std::runtime_error("cannot add " + m->GetName());
        for (Player* p : all)
            BotGear::SetProbeUnlock(p, true);
        h->PSendSysMessage("dgroup: leader {} + {} members", leader->GetName(), members.size());
        return;
    }
    if (verb == "dgo")
    {
        uint32 map = 0; float x = 0, y = 0, z = 0, o = 0; std::string all;
        if (!(input >> map >> x >> y >> z >> o)) throw std::runtime_error("dgo <leader> <map> <x> <y> <z> <o> [all]");
        input >> all;
        leader->TeleportTo(map, x, y, z, o);
        if (all == "all")
            if (Group* group = leader->GetGroup())
                for (auto const& slot : group->GetMemberSlots())
                    if (Player* m = ObjectAccessor::FindPlayer(slot.guid))
                        if (m != leader) m->TeleportTo(map, x, y, z, o);
        h->PSendSysMessage("dgo: leader to map {} ({:.1f} {:.1f} {:.1f})", map, x, y, z);
        return;
    }
    if (verb == "dmove")
    {
        float x = 0, y = 0, z = 0;
        if (!(input >> x >> y >> z)) throw std::runtime_error("dmove <leader> <x> <y> <z>");
        leader->GetMotionMaster()->MovePoint(1, x, y, z, FORCED_MOVEMENT_NONE, 0.0f, 0.0f, true, false);
        h->PSendSysMessage("dmove: leader to ({:.1f} {:.1f} {:.1f})", x, y, z);
        return;
    }
    if (verb == "dpull")
    {
        float range = 40.0f; input >> range;
        Unit* target = leader->SelectNearbyTarget(nullptr, range);
        Creature* c = target ? target->ToCreature() : nullptr;
        if (!c || !c->IsAlive()) { h->SendSysMessage("dpull: nothing to pull"); return; }
        c->SetInCombatWith(leader);
        leader->SetInCombatWith(c);
        if (c->AI()) c->AI()->AttackStart(leader);
        h->PSendSysMessage("dpull: {} (entry {}, level {}) at {:.0f} yd", c->GetName(), c->GetEntry(), c->GetLevel(), leader->GetDistance(c));
        return;
    }
    if (verb == "dstat")
    {
        Group* group = leader->GetGroup();
        Json out;
        out["map"] = leader->GetMapId();
        out["inCombat"] = leader->IsInCombat();
        std::set<Unit*> attackers;
        std::vector<Player*> members;
        if (group)
            for (auto const& slot : group->GetMemberSlots())
                if (Player* m = ObjectAccessor::FindPlayer(slot.guid)) members.push_back(m);
        if (members.empty()) members.push_back(leader);
        for (Player* m : members)
        {
            Json j;
            j["name"] = m->GetName();
            j["role"] = int(BotAI::GetRole(m->GetGUID()));
            j["alive"] = m->IsAlive();
            j["map"] = m->GetMapId();
            j["hp"] = m->GetMaxHealth() ? int(m->GetHealth() * 100 / m->GetMaxHealth()) : 0;
            uint32 maxPower = m->GetMaxPower(m->getPowerType());
            j["pw"] = maxPower ? int(m->GetPower(m->getPowerType()) * 100 / maxPower) : 0;
            j["x"] = m->GetPositionX(); j["y"] = m->GetPositionY(); j["z"] = m->GetPositionZ();
            j["d"] = m == leader ? 0.0f : (m->GetMapId() == leader->GetMapId() ? m->GetDistance(leader) : -1.0f);
            j["cmb"] = m->IsInCombat();
            j["moving"] = m->isMoving();
            if (Unit* v = m->GetVictim()) j["tgt"] = v->GetName();
            if (m != leader) j["dbg"] = BotAI::DebugState(m);
            Spell* cur = m->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (!cur) cur = m->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
            if (cur && cur->GetSpellInfo()) j["cast"] = cur->GetSpellInfo()->Id;
            out["members"].Push(j);
            for (Unit* a : m->getAttackers())
                if (a && a->IsAlive()) attackers.insert(a);
        }
        for (Unit* a : attackers)
        {
            Json j;
            j["name"] = a->GetName();
            j["hp"] = a->GetMaxHealth() ? int(a->GetHealth() * 100 / a->GetMaxHealth()) : 0;
            if (Unit* v = a->GetVictim()) j["on"] = v->GetName();
            out["mobs"].Push(j);
        }
        h->SendSysMessage(("DSTAT " + out.Dump()).c_str());
        return;
    }
    if (verb == "dfree")
    {
        if (Group* group = leader->GetGroup())
        {
            for (auto const& slot : group->GetMemberSlots())
                if (Player* m = ObjectAccessor::FindPlayer(slot.guid)) BotGear::SetProbeUnlock(m, false);
            group->Disband();
        }
        h->SendSysMessage("dfree: group disbanded");
        return;
    }
    throw std::runtime_error("unknown dungeon probe verb");
}

void Command(ChatHandler* h, std::string const& args)
{
    if (!Enabled.load(std::memory_order_relaxed)) { h->SendSysMessage("Training is disabled in server configuration."); return; }
    std::lock_guard lock(mutex);
    std::istringstream input(args);
    std::string createdSession;
    std::string verb; input >> verb;
    try
    {
        if (verb == "dgroup" || verb == "dgo" || verb == "dmove" || verb == "dpull" || verb == "dstat" || verb == "dfree")
        {
            DungeonProbe(h, verb, input);
            return;
        }
        if (verb == "ungroup")
        {
            uint32 guid = 0; input >> guid;
            Player* bot = sBotMgr->FindBotPlayer(guid);
            if (!bot || bot->GetSession()->GetAccountId() == 1 || !BotMgr::IsBotAccountId(bot->GetSession()->GetAccountId()))
                throw std::runtime_error("ungroup requires a dedicated online bot");
            if (Lookup(bot->GetGUID())) throw std::runtime_error("Cannot ungroup an active training actor");
            if (Group* group = bot->GetGroup())
            {
                for (auto const& member : group->GetMemberSlots())
                {
                    uint32 account = sCharacterCache->GetCharacterAccountIdByGuid(member.guid);
                    if (Lookup(member.guid) || account == 1 || !BotMgr::IsBotAccountId(account))
                        throw std::runtime_error("Group contains an active training actor or a non-bot account");
                }
                group->Disband();
            }
            h->PSendSysMessage("Training actor ungrouped: guid={}", guid);
            return;
        }
        if (verb == "equip" || verb == "loadout")
        {
            uint32 guid = 0; input >> guid;
            Player* bot = sBotMgr->FindBotPlayer(guid);
            if (!bot || bot->GetSession()->GetAccountId() == 1 || !BotMgr::IsBotAccountId(bot->GetSession()->GetAccountId()))
                throw std::runtime_error("equip requires a dedicated online bot");
            if (Lookup(bot->GetGUID())) throw std::runtime_error("Stop training before changing equipment");
            Reset(bot);
            std::vector<std::pair<uint8, uint32>> gear;
            uint32 slot = 0, entry = 0;
            while (input >> slot >> entry)
            {
                if (slot >= EQUIPMENT_SLOT_END) throw std::runtime_error("Invalid equipment slot");
                gear.emplace_back(uint8(slot), entry);
            }
            if (gear.empty()) throw std::runtime_error("equip needs slot/item pairs (item 0 clears a slot)");
            for (auto const& [gearSlot, itemEntry] : gear)
                if (!BotGear::PreserveSlot(bot, gearSlot)) throw std::runtime_error("Cannot preserve old gear");
            std::set<uint8> equipped;
            for (auto const& [gearSlot, itemEntry] : gear)
            {
                if (!itemEntry || equipped.count(gearSlot)) continue;
                uint16 destination = 0;
                InventoryResult result = bot->CanEquipNewItem(gearSlot, destination, itemEntry, true);
                if (result != EQUIP_ERR_OK && verb == "loadout") continue;
                if (result != EQUIP_ERR_OK)
                    throw std::runtime_error("Cannot equip slot " + std::to_string(gearSlot) + " item "
                        + std::to_string(itemEntry) + " error " + std::to_string(uint32(result)));
                if (!bot->EquipNewItem(destination, itemEntry, true)) throw std::runtime_error("EquipNewItem failed");
                equipped.insert(gearSlot);
            }
            if (verb == "loadout")
                for (uint8 requiredSlot : {uint8(0), uint8(1), uint8(2), uint8(4), uint8(5), uint8(6),
                    uint8(7), uint8(8), uint8(9), uint8(10), uint8(11), uint8(12), uint8(13), uint8(14), uint8(15)})
                    if (!equipped.count(requiredSlot)) throw std::runtime_error("No compatible candidate for slot " + std::to_string(requiredSlot));
            BotGear::LockTraining(bot);
            bot->SaveToDB(false, false);
            h->PSendSysMessage("Training equipment saved: guid={} gear={}", guid, Gear(bot).Dump());
            return;
        }
        if (verb == "livegear")
        {
            // livegear <guid> <old|new>: strip the dedicated bot and equip it by the live gear-up with the old class-only or the spec
            // stat model, so the effect of the item scoring can be compared on damage dealt (the plan loadout never uses it).
            uint32 guid = 0; std::string model; input >> guid >> model;
            Player* bot = sBotMgr->FindBotPlayer(guid);
            if (!bot || bot->GetSession()->GetAccountId() == 1 || !BotMgr::IsBotAccountId(bot->GetSession()->GetAccountId()))
                throw std::runtime_error("livegear requires a dedicated online bot");
            if (Lookup(bot->GetGUID())) throw std::runtime_error("Stop training before changing equipment");
            if (model != "old" && model != "new") throw std::runtime_error("livegear needs old or new");
            Reset(bot);
            BotAI::SetLegacyGearScoring(model == "old");
            BotSpawn::GearUpForTraining(bot);
            BotAI::SetLegacyGearScoring(false);
            BotGear::LockTraining(bot);
            bot->SaveToDB(false, false);
            h->PSendSysMessage("Training equipment saved: guid={} gear={}", guid, Gear(bot).Dump());
            return;
        }
        if (verb == "clearbags")
        {
            // Frees bag space of a dedicated training bot: earlier loadouts push every replaced item into the bags until none is left
            // for new items (totem tools). Equipped gear and items with a TotemCategory (tools) stay.
            uint32 guid = 0; input >> guid;
            Player* bot = sBotMgr->FindBotPlayer(guid);
            if (!bot || bot->GetSession()->GetAccountId() == 1 || !BotMgr::IsBotAccountId(bot->GetSession()->GetAccountId()))
                throw std::runtime_error("clearbags requires a dedicated online bot");
            if (Lookup(bot->GetGUID())) throw std::runtime_error("Stop training before clearing bags");
            uint32 removed = 0;
            auto drop = [&](uint8 bagPos, uint8 slot)
            {
                Item* item = bot->GetItemByPos(bagPos, slot);
                if (!item || item->GetTemplate()->TotemCategory) return;
                bot->DestroyItem(bagPos, slot, true); ++removed;
            };
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot) drop(INVENTORY_SLOT_BAG_0, slot);
            for (uint8 bagPos = INVENTORY_SLOT_BAG_START; bagPos < INVENTORY_SLOT_BAG_END; ++bagPos)
                if (Bag* bag = bot->GetBagByPos(bagPos))
                    for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot) drop(bagPos, uint8(slot));
            bot->SaveToDB(false, false);
            h->PSendSysMessage("Training bags cleared: guid={} removed={}", guid, removed);
            return;
        }
        if (verb == "actor")
        {
            uint32 classId = 0;
            if (!(input >> classId) || classId < 12 || classId > 32)
                throw std::runtime_error("actor needs a custom class ID (12..32)");
            uint32 level = sConfigMgr->GetOption<uint32>("CoaBots.Training.DefaultLevel", 60);
            if (!level || level > 80) throw std::runtime_error("DefaultLevel must be 1..80");
            auto guid = BotSpawn::SpawnClassBot(uint8(classId), 1, uint8(level), h);
            if (!guid) throw std::runtime_error("Cannot create training actor");
            h->PSendSysMessage("Training actor created: guid={} class={}", guid, classId);
            return;
        }
        if (verb == "status")
        {
            for (auto const& [key, s] : sessions)
                h->PSendSysMessage("{} bot={} scenario={} elapsed={}ms remaining={} state={} arena={} phase={} output={}",
                    s->id, s->candidate.GetCounter(), s->scenario.name, s->timeMs, s->runsRemaining,
                    s->finished ? "finished" : s->running ? "running" : "prepared", s->arenaId, s->phaseMask, s->directory.string());
            if (sessions.empty()) h->SendSysMessage("No training sessions.");
            return;
        }
        if (verb == "report")
        {
            std::string id; input >> id;
            for (auto const& [key, s] : sessions)
                if (s->id == id || key == id) { h->SendSysMessage(s->directory.string()); return; }
            throw std::runtime_error("Unknown session id; saved reports remain in OutputDir");
        }
        if (verb == "stop" || verb == "reset")
        {
            uint32 candidateGuid = 0;
            input >> candidateGuid;
            for (auto& [id, s] : sessions)
                if (!s->finished && (!candidateGuid || s->candidate.GetCounter() == candidateGuid))
                    Finish(*s, "stopped", false);
            h->SendSysMessage("Training stopped and participants returned."); return;
        }
        if (verb == "party")
        {
            configuredParty.clear(); uint32 guid;
            while (input >> guid)
            {
                Player* p = sBotMgr->FindBotPlayer(guid);
                if (!p) throw std::runtime_error("Party GUID is not an online bot");
                configuredParty.push_back(p->GetGUID());
            }
            if (configuredParty.size() != 4) { configuredParty.clear(); throw std::runtime_error("party needs four actor bot GUIDs"); }
            h->SendSysMessage("Four actor bots selected for the next group scenario."); return;
        }
        if (verb != "prepare" && verb != "start" && verb != "run")
            throw std::runtime_error("Usage: train status|party <four actor GUIDs>|prepare <guid> <scenario>|start <guid> <scenario> [seconds] [seed]|run <guid> <scenario> <runs> [seedBase]|stop|reset|report <id>");
        uint32 guid = 0, count = 1, seed = 1001, duration = 0;
        std::string scenarioName;
        if (!(input >> guid >> scenarioName)) throw std::runtime_error("Missing GUID or scenario");
        Scenario scenario;
        if (!Scenario::Find(scenarioName, scenario)) throw std::runtime_error("Unknown scenario");
        if (scenarioName != "RESOURCE_ENDURANCE")
        {
            uint32 seconds = sConfigMgr->GetOption<uint32>("CoaBots.Training.DefaultDurationSec", 180);
            if (!seconds || seconds > 3600) throw std::runtime_error("DefaultDurationSec must be 1..3600");
            scenario.durationMs = seconds * 1000;
        }
        if (verb == "run")
        {
            if (!(input >> count) || count == 0 || count > 1000) throw std::runtime_error("Runs must be 1..1000");
            if (input >> seed) { }
        }
        else if (verb == "start")
        {
            if (input >> duration)
            {
                if (!duration || duration > 3600) throw std::runtime_error("Duration must be 1..3600 seconds");
                scenario.durationMs = duration * 1000;
                if (input >> seed) { }
            }
            else if (scenarioName != "RESOURCE_ENDURANCE")
                scenario.durationMs = sConfigMgr->GetOption<uint32>("CoaBots.Training.DefaultDurationSec", 180) * 1000;
        }
        std::string excess;
        if (!input.eof() || input >> excess) throw std::runtime_error("Invalid optional argument");
        for (auto const& [id, s] : sessions)
            if (!s->finished)
            {
                if (verb == "start" && !s->running && s->candidate.GetCounter() == guid && s->scenario.name == scenarioName)
                {
                    s->scenario = scenario; s->seed = seed; s->startRequested = true; s->preparationMs = 0;
                    h->SendSysMessage("Prepared session queued to start."); return;
                }

            }
        uint32 limit = sConfigMgr->GetOption<uint32>("CoaBots.Training.MaxParallelArenas", 1);
        if (!limit || limit > MaxArenaSlots) throw std::runtime_error("MaxParallelArenas must be 1..4");
        std::set<uint32> occupied;
        for (auto const& [id, existing] : sessions)
            if (!existing->finished) occupied.insert(existing->arenaId);
        uint32 slot = 1;
        while (occupied.count(slot)) ++slot;
        if (slot > limit) throw std::runtime_error("All configured training arenas are occupied");
        auto s = std::make_unique<Session>();
        s->arenaId = slot; s->phaseMask = uint32(1) << (31 - slot);
        s->scenario = scenario; s->seed = seed; s->runsRemaining = count;
        s->startRequested = verb != "prepare"; s->batchId = NewId();
        Player* bot = sBotMgr->FindBotPlayer(guid);
        if (!bot) throw std::runtime_error("Candidate is not an online bot");
        s->candidate = bot->GetGUID();
        std::vector<ObjectGuid> party{s->candidate};
        if (scenario.group)
        {
            if (configuredParty.size() != 4) throw std::runtime_error("Select four actors with train party first");
            party.insert(party.end(), configuredParty.begin(), configuredParty.end());
        }
        std::set<ObjectGuid> unique(party.begin(), party.end());
        if (unique.size() != party.size()) throw std::runtime_error("Duplicate participant GUID");
        for (ObjectGuid memberGuid : party)
        {
            Player* p = sBotMgr->FindBotPlayer(memberGuid.GetCounter());
            if (Lookup(memberGuid)) throw std::runtime_error("Participant already belongs to another training session");
            if (!p || p->GetSession()->GetAccountId() == 1 || !BotMgr::IsBotAccountId(p->GetSession()->GetAccountId()))
                throw std::runtime_error("Participants must be dedicated bot accounts (never account 1)");
            if (p->InBattleground() || p->IsBeingTeleported() || p->IsInFlight() || p->GetGuildId())
                throw std::runtime_error("Participant " + std::to_string(memberGuid.GetCounter()) +
                    " must be unguilded, outside BG/taxi/teleport");
            uint32 spec = p->GetPlayerSetting("core.ascension_active_spec", 0).value;
            if (!spec || !BotTalentBuilds::GetBuild(p->getClass(), spec))
                throw std::runtime_error("Participant needs an existing curated specialization build");
            if (p->GetGroup() && (!scenario.group || p->GetGroup() != bot->GetGroup() || p->GetGroup()->GetMembersCount() != 5))
                throw std::runtime_error("Use an ungrouped candidate or an existing five-bot training party");
            s->members.push_back({memberGuid, WorldLocation(p->GetMapId(), p->GetPosition()), p->GetPhaseMask(),
                spec, p->getClass(), "", Gear(p)});
        }
        if (scenario.group)
        {
            for (ObjectGuid memberGuid : party)
                if (sBotMgr->FindBotPlayer(memberGuid.GetCounter())->GetGroup() != bot->GetGroup())
                    throw std::runtime_error("All selected actors must belong to the candidate's party");
        }
        OpenOutput(*s);
        std::string key = s->id;
        Session* ptr = s.get();
        sessions[key] = std::move(s);
        createdSession = key;
        for (auto& m : ptr->members)
        {
            owners[m.guid] = ptr;
            Player* p = sBotMgr->FindBotPlayer(m.guid.GetCounter());
            BotAI::ClearRoleOverride(m.guid);
            BotAI::SetRole(m.guid, BotAI::GetRoleForClassSpec(p->getClass(), m.spec));
            m.role = Role(p);
            p->GiveLevel(uint8(sConfigMgr->GetOption<uint32>("CoaBots.Training.DefaultLevel", 60)));
            BotProgression::LearnAbilitiesFromBook(p);
            BotTalentBuilds::ApplyBuildForLevel(p, p->GetLevel());
            p->UpdateSkillsForLevel();
            m.level = p->GetLevel();
            for (auto const& [spellId, state] : p->GetSpellMap())
                if (p->HasSpell(spellId))
                {
                    m.spellbook.Push(spellId);
                    if (state && state->Active && state->State != PLAYERSPELL_REMOVED)
                        m.activeSpellbook.Push(spellId);
                }
            for (uint32 i = 0; i < sSkillLineStore.GetNumRows(); ++i)
                if (SkillLineEntry const* skill = sSkillLineStore.LookupEntry(i))
                    if (skill->categoryId == SKILL_CATEGORY_WEAPON && p->HasSkill(skill->id))
                    {
                        uint16 maximum = p->GetMaxSkillValueForLevel();
                        p->SetSkill(uint16(skill->id), p->GetSkillStep(uint16(skill->id)), maximum, maximum);
                        Json entry; entry["skillId"] = skill->id;
                        entry["current"] = p->GetSkillValue(skill->id); entry["maximum"] = p->GetMaxSkillValue(skill->id);
                        m.weaponSkills.Push(entry);
                    }
        }
        if (scenario.group && !bot->GetGroup())
        {
            Group* group = new Group();
            if (!group->Create(bot)) { delete group; throw std::runtime_error("Cannot create training party"); }
            sGroupMgr->AddGroup(group);
            ptr->createdGroup = true;
            for (ObjectGuid memberGuid : configuredParty)
                if (!group->AddMember(sBotMgr->FindBotPlayer(memberGuid.GetCounter())))
                    throw std::runtime_error("Cannot add training party member");
        }
        auto effects = [&](char const* kind, std::vector<uint32>& target)
        {
            std::string text = sConfigMgr->GetOption<std::string>(std::string("CoaBots.Training.") + kind
                + "." + std::to_string(ptr->members.front().spec), "");
            std::replace(text.begin(), text.end(), ',', ' ');
            std::istringstream ids(text); uint32 id;
            while (ids >> id) target.push_back(id);
        };
        effects("RequiredBuffs", ptr->requiredBuffs);
        effects("RequiredDebuffs", ptr->requiredDebuffs);
        Place(*ptr);
        h->PSendSysMessage("Training queued: session={} batch={} seed={} runs={} report={}",
            ptr->id, ptr->batchId, seed, count, ptr->directory.string());
    }
    catch (std::exception const& e)
    {
        if (!createdSession.empty()) Release(*sessions.at(createdSession));
        h->SendSysMessage(e.what());
    }
}

void RecordDamage(Unit* attacker, Unit* victim, uint32 amount)
{
    if (!Enabled.load(std::memory_order_relaxed) || !victim) return;
    std::lock_guard lock(mutex);
    Session* s = Lookup(victim->GetGUID());
    ObjectGuid principal = Principal(attacker);
    if (!s || !s->running || Lookup(principal) != s) return;
    uint32 effective = std::min(amount, victim->GetHealth());
    if (principal == s->candidate && std::find(s->enemies.begin(), s->enemies.end(), victim->GetGUID()) != s->enemies.end())
        s->damage += effective;
    if (victim->GetGUID() == s->candidate)
    {
        s->damageTaken += effective;
        if (s->avoidableEvent) { s->avoidableDamage += effective; ++s->avoidableHits; }
    }
    Json row; row["type"] = "damage"; row["source"] = uint64(principal.GetCounter());
    row["target"] = uint64(victim->GetGUID().GetCounter()); row["amount"] = effective;
    row["avoidable"] = s->avoidableEvent; if (attacker && attacker->GetGUID() != principal) row["viaOwned"] = 1; s->Event(row);
}
void RecordHeal(Unit* healer, Unit* target, uint32 amount)
{
    if (!Enabled.load(std::memory_order_relaxed) || !target) return;
    std::lock_guard lock(mutex);
    Session* s = Lookup(target->GetGUID()); ObjectGuid principal = Principal(healer);
    if (!s || !s->running || Lookup(principal) != s) return;
    if (principal == s->candidate) s->healing += amount;
    Json row; row["type"] = "effective_heal"; row["source"] = uint64(principal.GetCounter());
    row["target"] = uint64(target->GetGUID().GetCounter()); row["amount"] = amount; s->Event(row);
}
void RecordPeriodicDamage(Unit* attacker, Unit* target, uint32 amount, uint32 spell)
{
    if (!Enabled.load(std::memory_order_relaxed) || !target) return;
    std::lock_guard lock(mutex);
    Session* s = Lookup(target->GetGUID());
    if (!s || !s->running || Lookup(Principal(attacker)) != s || !s->hazardSpells.count(spell)) return;
    if (target->GetGUID() == s->candidate && amount)
    {
        ++s->avoidableHits; s->avoidableDamage += amount;
    }
    Json row; row["type"] = "avoidable_periodic_damage"; row["spellId"] = spell;
    row["target"] = uint64(target->GetGUID().GetCounter()); row["amount"] = amount; s->Event(row);
}
// Damage attributed to the spell that dealt it (direct hits from the spell hit hook, ticks from the periodic hook). Only damage the
// session candidate deals to its enemies is logged; it is used to rank abilities by damage per cast, not for the episode totals.
void RecordSpellDamage(Unit* attacker, Unit* target, uint32 spell, uint32 amount, bool periodic)
{
    if (!Enabled.load(std::memory_order_relaxed) || !target || !attacker || !amount) return;
    std::lock_guard lock(mutex);
    Session* s = Lookup(target->GetGUID());
    ObjectGuid principal = Principal(attacker);
    if (!s || !s->running || principal != s->candidate || Lookup(principal) != s) return;
    if (std::find(s->enemies.begin(), s->enemies.end(), target->GetGUID()) == s->enemies.end()) return;
    Json row; row["type"] = "spell_damage"; row["spellId"] = spell; row["amount"] = std::min(amount, target->GetHealth());
    row["periodic"] = periodic; s->Event(row);
}
void RecordSpellHeal(Unit* healer, Unit* target, uint32 spell, uint32 amount)
{
    // Direct heals only (the hit-result hook has no periodic-heal counterpart): per-spell healing of the candidate on its own party.
    if (!Enabled.load(std::memory_order_relaxed) || !target || !healer || !amount) return;
    std::lock_guard lock(mutex);
    Session* s = Lookup(target->GetGUID());
    ObjectGuid principal = Principal(healer);
    if (!s || !s->running || principal != s->candidate || Lookup(principal) != s) return;
    Json row; row["type"] = "spell_heal"; row["spellId"] = spell; row["amount"] = amount;
    row["target"] = uint64(target->GetGUID().GetCounter()); s->Event(row);
}
void RecordAura(Unit* target, uint32 spell, ObjectGuid caster, char const* event, uint32 stacks)
{
    if (!Enabled.load(std::memory_order_relaxed) || !target) return;
    std::lock_guard lock(mutex);
    Session* s = Lookup(target->GetGUID());
    if (!s || !s->running || Lookup(caster) != s) return;
    auto& aura = s->auras[{spell, caster, target->GetGUID()}];
    if (std::string(event) == "remove")
    {
        aura.Remove(s->timeMs);
    }
    else
    {
        aura.Apply(s->timeMs, stacks);
    }
    Json row; row["type"] = "aura"; row["event"] = event;
    row["spellId"] = spell; row["caster"] = uint64(caster.GetCounter());
    row["target"] = uint64(target->GetGUID().GetCounter()); row["stacks"] = stacks; s->Event(row);
}
void BeginDecision(BotAI::CombatContext const& ctx)
{
    if (!Enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    Session* s = Lookup(ctx.bot->GetGUID());
    if (!s || !s->running || s->candidate != ctx.bot->GetGUID()) return;
    s->decision = Json(); s->decisionActive = true;
    s->decision["timeMs"] = s->timeMs; s->decision["botHpPct"] = ctx.botHpPct;
    s->decision["engagedEnemyCount"] = ctx.engagedEnemyCount;
    s->decision["sessionId"] = s->id; s->decision["arenaId"] = s->arenaId;
    s->decision["candidates"] = Json::Array{};
    Json resources{Json::Array{}};
    for (uint8 i = 0; i < ctx.resources.count; ++i)
    {
        auto const& r = ctx.resources.resources[i]; Json row;
        row["name"] = r.name; row["kind"] = uint32(r.key.kind);
        row["powerType"] = r.key.powerType; row["auraSpellId"] = r.key.auraSpellId;
        row["current"] = r.current; row["maximum"] = r.maximumKnown ? Json(r.maximum) : Json();
        resources.Push(row);
    }
    s->decision["resources"] = resources;
    if (Pet* pet = ctx.bot->GetPet())
    {
        s->decision["pet"] = 1;
        s->decision["petAttacking"] = pet->GetVictim() ? 1 : 0;
        s->decision["petReact"] = uint32(pet->GetReactState());
        s->decision["petDistance"] = pet->GetDistance(ctx.bot);
    }
    else
        s->decision["pet"] = 0;
    // Rage as a secondary active power (Primalist): the primary bar is mana, so the resource snapshot never showed it.
    if (ctx.bot->getPowerType() != POWER_RAGE && ctx.bot->HasActivePowerType(POWER_RAGE))
        s->decision["rage"] = ctx.bot->GetPower(POWER_RAGE);
}
void Candidate(ObjectGuid bot, uint32 spell, char const* name, bool valid, float score, char const* reason)
{
    if (!Enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    Session* s = Lookup(bot); if (!s || !s->running || !s->decisionActive || bot != s->candidate) return;
    auto& a = s->abilities[spell];
    if (valid) ++a.available; else { ++a.rejected; ++a.reasons[reason]; }
    Json row; row["spellId"] = spell; row["name"] = name; row["valid"] = valid;
    row["score"] = valid ? Json(score) : Json(); row["reason"] = reason;
    s->decision["candidates"].Push(row);
}
void EndDecision(ObjectGuid bot, uint32 spell, float score, uint32 phase)
{
    if (!Enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    Session* s = Lookup(bot); if (!s || !s->decisionActive || bot != s->candidate) return;
    if (spell) ++s->abilities[spell].selected;
    s->decision["selected"]["spellId"] = spell; s->decision["selected"]["score"] = score;
    s->decision["phase"] = phase;
    if (sConfigMgr->GetOption<bool>("CoaBots.Training.LogDecisionTrace", true))
        s->decisions << s->decision.Dump() << '\n';
    s->decisionActive = false;
}
void RecordSpellLifecycle(Unit* caster, ObjectGuid target, uint32 spell, char const* phase, uint32 result, bool bySelf)
{
    if (!Enabled.load(std::memory_order_relaxed) || !caster) return;
    std::lock_guard lock(mutex);
    Session* s = Lookup(caster->GetGUID());
    if (!s || !s->running || s->candidate != caster->GetGUID()) return;
    Json row; row["type"] = "spell_lifecycle"; row["phase"] = phase;
    row["spellId"] = spell; row["target"] = target.GetCounter();
    row["targetResolvable"] = target.IsEmpty() || ObjectAccessor::GetUnit(*caster, target) != nullptr;
    if (target.IsPlayer())
    {
        Player* targetBot = sBotMgr->FindBotPlayer(target.GetCounter());
        row["targetBotFound"] = targetBot != nullptr;
        if (targetBot)
        {
            row["targetBotInWorld"] = targetBot->IsInWorld();
            row["targetSameMap"] = targetBot->GetMap() == caster->GetMap();
            row["targetBotMoving"] = targetBot->isMoving();
        }
    }
    row["result"] = result; row["bySelf"] = bySelf; s->Event(row);
}
void CastResult(Player* bot, uint32 spell, uint32 result, Unit* target)
{
    if (!Enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex);
    Session* s = Lookup(bot->GetGUID()); if (!s || !s->running || s->candidate != bot->GetGUID()) return;
    auto& a = s->abilities[spell]; ++a.attempted;
    if (result == uint32(SPELL_CAST_OK)) ++a.accepted;
    else { ++a.failed; ++a.reasons["cast_result_" + std::to_string(result)]; }
    Json row;
    Json requirements{Json::Array{}};
    for (auto const& r : BotAI::CombatResourceEvaluator::ResolveRequirements(bot, spell))
    {
        Json item; item["kind"] = uint32(r.key.kind); item["powerType"] = r.key.powerType;
        item["auraSpellId"] = r.key.auraSpellId; item["amount"] = r.amount;
        item["activeForBot"] = r.activeForBot; item["consumption"] = uint32(r.consumption);
        requirements.Push(item);
    }
    Json gains{Json::Array{}};
    for (auto const& g : BotAI::CombatResourceEvaluator::ResolveGains(bot->getClass(), spell))
    {
        Json item; item["kind"] = uint32(g.key.kind); item["powerType"] = g.key.powerType;
        item["auraSpellId"] = g.key.auraSpellId; item["amount"] = g.amount;
        item["event"] = uint32(g.event); item["chancePercent"] = g.chancePercent; gains.Push(item);
    }
    row["requirementsAfterAttempt"] = requirements; row["potentialGainsMetadata"] = gains;
    if (result != SPELL_CAST_OK)
    {
        // A failed cast with no visible cause (e.g. CASTER_AURASTATE for a health-cost spell) cannot be diagnosed from the result code alone.
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spell))
        {
            row["failPowerType"] = int32(info->PowerType);
            row["failPowerCost"] = info->CalcPowerCost(bot, info->GetSchoolMask());
            row["failHealth"] = uint32(bot->GetHealth());
            row["failMaxHealth"] = uint32(bot->GetMaxHealth());
            row["failCreateHealth"] = uint32(bot->GetCreateHealth());
            if (target)
            {
                row["failTargetGuid"] = uint64(target->GetGUID().GetRawValue());
                row["failTargetIsSelf"] = target == bot;
                row["failTargetFriendly"] = bot->IsFriendlyTo(target);
                row["failTargetHostile"] = bot->IsHostileTo(target);
                row["failTargetAlive"] = target->IsAlive();
                row["failTargetType"] = uint32(target->GetTypeId());
            }
        }
    }
    row["type"] = "cast_result"; row["spellId"] = spell; row["result"] = result; s->Event(row);
}
}
