/*
 * mod-coa-playerbots -- bot strength scaling
 *
 * Bots play dungeons and raids with the gear and the rotation of a bot, which is weaker than the same character played by a person. Instead
 * of weakening every creature of the game (the content scaling module does that for group size and difficulty), the bots themselves get a
 * small, configurable help while they are in dungeons and raids:
 *
 *   CoaBots.Scaling.DamageDone      damage a bot (and its pet, totem, guardian) deals to creatures
 *   CoaBots.Scaling.HealingDone     healing a bot casts
 *   CoaBots.Scaling.DamageTaken     damage a creature deals to a bot (below 1 = bots take less)
 *   CoaBots.Scaling.TankThreat      threat a bot tank generates (extra threat is added on top of the damage-based one)
 *
 * The multipliers apply on top of the group scaling of the content scaling module. Scripted lethal mechanics (instant kills, percentage
 * damage, huge scripted hits) are left alone. Each map kind has its own switch.
 */

#include "BotScaling.h"

#include "BotAI.h"
#include "Config.h"
#include "Creature.h"
#include "Map.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "ThreatManager.h"
#include "Timer.h"
#include "Unit.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
    // The values a server gets without touching the configuration. Tuned on the soak runs of the dungeon passes: a group of five bots with
    // them gets through the dungeons of all three expansions without the bosses dying in a few seconds.
    constexpr float DEFAULT_DAMAGE_DONE = 1.6f;
    constexpr float DEFAULT_HEALING_DONE = 1.4f;
    constexpr float DEFAULT_DAMAGE_TAKEN = 0.7f;
    constexpr float DEFAULT_TANK_THREAT = 1.5f;

    // Bosses whose mechanic hurts every bot of the group wherever it stands, so the fight has to be short: Loken's Pulsing Shockwave (spell
    // 52942) reaches the whole instance and grows with the distance from him; a member that died (or trails the tank) is hit for 5000 every two
    // seconds at the entrance, 400 yd away, for the whole fight. Extra multipliers on top of the ordinary ones, for this boss only.
    struct BossTuning
    {
        uint32 entry;
        float damageDone;
        float damageTaken;
    };
    constexpr BossTuning BOSS_TUNING[] =
    {
        { 28923, 2.0f, 0.4f },  // Halls of Lightning: Loken
        { 29573, 1.6f, 0.5f },  // Gundrak: Drakkari Elemental (trash that wiped every level 60 group)
        { 29307, 1.5f, 0.6f },  // Gundrak: Drakkari Colossus
        { 29306, 1.6f, 0.6f },  // Gundrak: Gal'darah (last boss; groups reached him with 5 deaths and never won)
    };

    BossTuning const* TuningFor(Unit* creature)
    {
        if (!creature || !creature->IsCreature())
            return nullptr;
        for (BossTuning const& row : BOSS_TUNING)
            if (row.entry == creature->GetEntry())
                return &row;
        return nullptr;
    }

    struct ScalingConfig
    {
        bool enabled = true;
        bool dungeons = true;
        bool raids = true;
        bool battlegrounds = false;
        bool world = false;
        float damageDone = DEFAULT_DAMAGE_DONE;
        float healingDone = DEFAULT_HEALING_DONE;
        float damageTaken = DEFAULT_DAMAGE_TAKEN;
        float tankThreat = DEFAULT_TANK_THREAT;
    };

    float ClampMultiplier(float value, float low, float high)
    {
        return std::max(low, std::min(high, value));
    }

    // Read again every few seconds so that `.reload config` takes effect without a restart and a hit does not pay for a config lookup.
    ScalingConfig const& Config()
    {
        static ScalingConfig cached;
        static uint32 loadedAtMs = 0;
        uint32 const now = getMSTime();
        if (loadedAtMs && getMSTimeDiff(loadedAtMs, now) < 5000)
            return cached;
        loadedAtMs = now;
        cached.enabled = sConfigMgr->GetOption<bool>("CoaBots.Scaling.Enable", true);
        cached.dungeons = sConfigMgr->GetOption<bool>("CoaBots.Scaling.Dungeons", true);
        cached.raids = sConfigMgr->GetOption<bool>("CoaBots.Scaling.Raids", true);
        cached.battlegrounds = sConfigMgr->GetOption<bool>("CoaBots.Scaling.Battlegrounds", false);
        cached.world = sConfigMgr->GetOption<bool>("CoaBots.Scaling.World", false);
        cached.damageDone = ClampMultiplier(sConfigMgr->GetOption<float>("CoaBots.Scaling.DamageDone", DEFAULT_DAMAGE_DONE), 0.1f, 10.0f);
        cached.healingDone = ClampMultiplier(sConfigMgr->GetOption<float>("CoaBots.Scaling.HealingDone", DEFAULT_HEALING_DONE), 0.1f, 10.0f);
        cached.damageTaken = ClampMultiplier(sConfigMgr->GetOption<float>("CoaBots.Scaling.DamageTaken", DEFAULT_DAMAGE_TAKEN), 0.1f, 10.0f);
        cached.tankThreat = ClampMultiplier(sConfigMgr->GetOption<float>("CoaBots.Scaling.TankThreat", DEFAULT_TANK_THREAT), 1.0f, 10.0f);
        return cached;
    }

    // The player behind a unit: the player itself or the owner of its pet, totem or guardian.
    Player* PlayerBehind(Unit* unit)
    {
        return unit ? unit->GetCharmerOrOwnerPlayerOrPlayerItself() : nullptr;
    }

    Player* BotBehind(Unit* unit)
    {
        Player* player = PlayerBehind(unit);
        return player && player->IsBot() ? player : nullptr;
    }

    bool MapIsScaled(Map const* map, ScalingConfig const& cfg)
    {
        if (!map)
            return false;
        if (map->IsBattleground() || map->IsBattleArena())
            return cfg.battlegrounds;
        if (map->IsRaid())
            return cfg.raids;
        if (map->IsDungeon())
            return cfg.dungeons;
        return cfg.world;
    }

    bool IsScriptedLethal(uint32 damage, SpellInfo const* spellInfo)
    {
        if (damage >= 10000000)
            return true;
        if (!spellInfo)
            return false;
        for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
            if (spellInfo->Effects[i].Effect == SPELL_EFFECT_INSTAKILL || spellInfo->Effects[i].ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
                return true;
        return false;
    }

    struct Hit
    {
        uint32 spellId;
        uint32 damage;
        uint32 attackerEntry;
        uint32 atMs;
    };
    std::mutex g_hitMutex;
    std::unordered_map<ObjectGuid, std::deque<Hit>> g_hits;

    void NoteHit(Unit* target, Unit* attacker, uint32 damage, SpellInfo const* spellInfo)
    {
        Player* player = target ? target->ToPlayer() : nullptr;
        if (!player || !attacker || !attacker->IsCreature() || !player->GetMap() || !player->GetMap()->IsDungeon())
            return;
        std::lock_guard<std::mutex> lock(g_hitMutex);
        std::deque<Hit>& hits = g_hits[player->GetGUID()];
        hits.push_back({ spellInfo ? spellInfo->Id : 0u, damage, attacker->GetEntry(), getMSTime() });
        while (hits.size() > 6)
            hits.pop_front();
    }

    float Scaled(float value, float multiplier)
    {
        return std::ceil(value * multiplier);
    }

    // What the hit does to the numbers: how much a bot deals, how much a bot takes, and the extra threat of a tank.
    void Apply(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo)
    {
        if (!damage || !target || !attacker || target == attacker)
            return;
        ScalingConfig const& cfg = Config();
        if (!cfg.enabled || !MapIsScaled(target->GetMap(), cfg) || IsScriptedLethal(damage, spellInfo))
            return;

        // A bot (or its pet) hitting a creature.
        if (Player* bot = BotBehind(attacker); bot && !PlayerBehind(target))
        {
            uint32 const before = damage;
            if (cfg.damageDone != 1.0f)
                damage = uint32(Scaled(float(damage), cfg.damageDone));
            if (BossTuning const* tuning = TuningFor(target))
                damage = uint32(Scaled(float(damage), tuning->damageDone));
            if (cfg.tankThreat > 1.0f && attacker == bot && BotAI::GetRole(bot->GetGUID()) == BotRole::Tank && target->IsCreature())
                target->GetThreatMgr().AddThreat(bot, float(std::max(before, damage)) * (cfg.tankThreat - 1.0f), spellInfo, /*ignoreModifiers*/ true);
            return;
        }

        // A creature hitting a bot (or its pet).
        if (cfg.damageTaken != 1.0f && attacker->IsCreature() && !PlayerBehind(attacker) && BotBehind(target))
            damage = std::max<uint32>(1, uint32(Scaled(float(damage), cfg.damageTaken)));
        if (BossTuning const* tuning = TuningFor(attacker); tuning && attacker->IsCreature() && !PlayerBehind(attacker) && BotBehind(target))
            damage = std::max<uint32>(1, uint32(Scaled(float(damage), tuning->damageTaken)));
        NoteHit(target, attacker, damage, spellInfo);
    }

    class coa_bot_scaling_unit : public UnitScript
    {
    public:
        coa_bot_scaling_unit() : UnitScript("coa_bot_scaling_unit") { }

        void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
        {
            Apply(target, attacker, damage, nullptr);
        }

        void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
        {
            if (damage <= 0)
                return;
            uint32 value = uint32(damage);
            Apply(target, attacker, value, spellInfo);
            damage = int32(std::min<uint32>(value, 0x7fffffff));
        }

        void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo) override
        {
            Apply(target, attacker, damage, spellInfo);
        }

        void ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellInfo const* /*spellInfo*/) override
        {
            if (!heal || !target || !healer)
                return;
            ScalingConfig const& cfg = Config();
            if (!cfg.enabled || cfg.healingDone == 1.0f || !MapIsScaled(target->GetMap(), cfg))
                return;
            if (BotBehind(healer))
                heal = uint32(Scaled(float(heal), cfg.healingDone));
        }
    };
}

std::string BotScaling::RecentHits(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(g_hitMutex);
    auto it = g_hits.find(guid);
    if (it == g_hits.end())
        return {};
    std::string out;
    uint32 const now = getMSTime();
    for (Hit const& h : it->second)
        out += "spell " + std::to_string(h.spellId) + " for " + std::to_string(h.damage) + " by " + std::to_string(h.attackerEntry) + " " + std::to_string((now - h.atMs) / 100) + " ds ago; ";
    return out;
}

void AddSC_coa_bot_scaling_script()
{
    new coa_bot_scaling_unit();
}
