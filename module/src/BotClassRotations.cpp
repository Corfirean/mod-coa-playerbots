/*
 * mod-coa-playerbots
 *
 * Class-specific combat rotations for custom Ascension classes.
 */

#include "BotClassRotations.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <unordered_map>

namespace BotAI
{
namespace
{
// Per-bot temporary failure backoff map: [botGuid][spellId] -> expiryMSTime
// Prevents an ability that failed to cast (e.g. invalid target, LOS, etc.) from being retried every tick.
std::unordered_map<ObjectGuid, std::unordered_map<uint32, uint32>> failureCooldowns;
constexpr uint32 FAILURE_COOLDOWN_MS = 2000;

// Per-bot rotation ability throttle map: [botGuid][rootSpellId] -> expiryMSTime
// Used for abilities without native DBC cooldowns (e.g. totems, summons, edicts).
std::unordered_map<ObjectGuid, std::unordered_map<uint32, uint32>> abilityThrottleCooldowns;
} // anonymous namespace

bool IsSpellInFailureCooldown(ObjectGuid botGuid, uint32 spellId)
{
    auto botItr = failureCooldowns.find(botGuid);
    if (botItr == failureCooldowns.end())
        return false;

    auto spellItr = botItr->second.find(spellId);
    if (spellItr == botItr->second.end())
        return false;

    uint32 now = getMSTime();
    if (now < spellItr->second)
        return true;

    botItr->second.erase(spellItr);
    return false;
}

namespace
{
bool IsAbilityThrottled(ObjectGuid botGuid, uint32 rootSpellId)
{
    auto botItr = abilityThrottleCooldowns.find(botGuid);
    if (botItr == abilityThrottleCooldowns.end())
        return false;

    auto spellItr = botItr->second.find(rootSpellId);
    if (spellItr == botItr->second.end())
        return false;

    uint32 now = getMSTime();
    if (now < spellItr->second)
        return true;

    botItr->second.erase(spellItr);
    return false;
}

void SetAbilityThrottle(ObjectGuid botGuid, uint32 rootSpellId, uint32 durationMs)
{
    abilityThrottleCooldowns[botGuid][rootSpellId] = getMSTime() + durationMs;
}

// Finds the highest rank of the spell that this bot currently knows.
// Traverses backwards from the last spell in the chain.
uint32 GetHighestLearnedRank(Player* bot, uint32 rootSpellId)
{
    if (!rootSpellId)
        return 0;

    PlayerSpellMap const& spellMap = bot->GetSpellMap();

    uint32 lastInChain = sSpellMgr->GetLastSpellInChain(rootSpellId);
    uint32 current = lastInChain ? lastInChain : rootSpellId;

    while (current)
    {
        auto itr = spellMap.find(current);
        if (itr != spellMap.end() && itr->second && itr->second->Active && itr->second->State != PLAYERSPELL_REMOVED)
            return current;

        uint32 prev = sSpellMgr->GetPrevSpellInChain(current);
        if (!prev || prev == current)
            break;
        current = prev;
    }

    if (bot->HasSpell(rootSpellId))
    {
        auto itr = spellMap.find(rootSpellId);
        if (itr != spellMap.end() && itr->second && itr->second->Active && itr->second->State != PLAYERSPELL_REMOVED)
            return rootSpellId;
    }

    return 0;
}

bool CanCastSpell(Player* bot, Unit* target, uint32 spellId, bool positiveRange = false)
{
    if (!spellId)
        return false;

    PlayerSpellMap const& spellMap = bot->GetSpellMap();
    auto itr = spellMap.find(spellId);
    if (itr == spellMap.end() || !itr->second || !itr->second->Active || itr->second->State == PLAYERSPELL_REMOVED)
        return false;

    if (bot->HasSpellCooldown(spellId))
        return false;

    if (IsSpellInFailureCooldown(bot->GetGUID(), spellId))
        return false;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return false;

    // Item/weapon check: prevents SPELL_FAILED_EQUIPPED_ITEM_CLASS (code 29)
    if (!bot->HasItemFitToSpellRequirements(spellInfo))
        return false;

    // Aura state checks
    if (spellInfo->CasterAuraState && !bot->HasAuraState(AuraStateType(spellInfo->CasterAuraState)))
        return false;
    if (target && spellInfo->TargetAuraState && !target->HasAuraState(AuraStateType(spellInfo->TargetAuraState)))
        return false;

    // Specific caster / target aura requirements (SpellInfo::CasterAuraSpell / TargetAuraSpell)
    if (spellInfo->CasterAuraSpell && !bot->HasAura(spellInfo->CasterAuraSpell))
        return false;
    if (target && spellInfo->TargetAuraSpell && !target->HasAura(spellInfo->TargetAuraSpell))
        return false;

    // Power cost check
    if (spellInfo->PowerType == POWER_HEALTH)
    {
        int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
        if (cost > 0 && bot->GetHealth() <= (uint32)cost)
            return false;
    }
    else
    {
        int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
        if (cost > 0 && bot->GetPower(Powers(spellInfo->PowerType)) < cost)
            return false;
    }

    // Range check
    if (target && target != bot)
    {
        float dist = bot->GetDistance(target);
        float maxRange = spellInfo->GetMaxRange(positiveRange, bot);
        if (maxRange > 0.0f && dist > maxRange)
            return false;
        float minRange = spellInfo->GetMinRange(positiveRange);
        if (minRange > 0.0f && bot->IsWithinRange(target, minRange + bot->GetMeleeRange(target)))
            return false;
    }

    return true;
}

// Attempts to find the highest learned rank of a spell and verifies it can be cast right now.
uint32 TrySpell(Player* bot, Unit* target, uint32 rootSpellId, bool positiveRange = false)
{
    uint32 spellId = GetHighestLearnedRank(bot, rootSpellId);
    if (spellId && CanCastSpell(bot, target, spellId, positiveRange))
        return spellId;
    return 0;
}

// Attempts to cast a spell with an internal rotation cooldown/throttle.
// Useful for abilities without native DBC cooldowns (e.g. totems, summons, buffs).
uint32 TryThrottledSpell(Player* bot, Unit* target, uint32 rootSpellId, uint32 throttleMs, bool positiveRange = false)
{
    if (IsAbilityThrottled(bot->GetGUID(), rootSpellId))
        return 0;

    uint32 spellId = TrySpell(bot, target, rootSpellId, positiveRange);
    if (spellId)
    {
        SetAbilityThrottle(bot->GetGUID(), rootSpellId, throttleMs);
        return spellId;
    }
    return 0;
}

// -------------------------------------------------------------------------
// Class 12: Barbarian
// -------------------------------------------------------------------------
uint32 SelectBarbarianRotationSpell(Player* bot, Unit* target, uint32 /*activeSpec*/)
{
    float dist = bot->GetDistance(target);
    int32 rage = bot->GetPower(POWER_RAGE);

    // 1. Gap closer / Ranged opener
    // Whirling Advance (root 500919) has a range of ~8-25 yards
    if (dist > 8.0f && dist <= 25.0f)
    {
        if (uint32 spell = TrySpell(bot, target, 500919))
            return spell;
    }

    // Axe Throwing (root 804136) for ranged damage / pulling
    if (dist > 8.0f)
    {
        if (uint32 spell = TrySpell(bot, target, 804136))
            return spell;
    }

    // 2. Savage Rage (root 800193) - self-buff / rage generation
    // Use if rage is low (< 60) and Savage Rage is ready
    if (rage < 60)
    {
        if (uint32 spell = TrySpell(bot, bot, 800193, true))
            return spell;
    }

    // 3. Melee strikes priority (only if in melee range <= 6 yards)
    if (dist <= 6.0f)
    {
        // High damage abilities:
        // Whirlwind (root 500002)
        if (uint32 spell = TrySpell(bot, target, 500002))
            return spell;

        // Keg Smash (root 705156)
        if (uint32 spell = TrySpell(bot, target, 705156))
            return spell;

        // Maiming Spear (root 500918)
        if (uint32 spell = TrySpell(bot, target, 500918))
            return spell;

        // Crushing Slam (root 500915)
        if (uint32 spell = TrySpell(bot, target, 500915))
            return spell;

        // Ancestral Strike (root 801576) - main strike builder/spender
        if (uint32 spell = TrySpell(bot, target, 801576))
            return spell;
    }

    return 0;
}

// -------------------------------------------------------------------------
// Class 29: Venomancer
// -------------------------------------------------------------------------
uint32 SelectVenomancerRotationSpell(Player* bot, Unit* target, uint32 activeSpec)
{
    float dist = bot->GetDistance(target);

    // 1. Maintain Envenom Weapons (root 803177) on self if learned
    if (!bot->HasAura(803177))
    {
        if (uint32 spell = TrySpell(bot, bot, 803177, true))
            return spell;
    }

    // 2. Form check:
    // Spec 52 is Fortitude (Tank) -> prefer Scorpid Form (803183)
    // Other specs -> prefer Stalker Form (800841)
    if (activeSpec == 52)
    {
        if (!bot->HasAura(803183))
            if (uint32 spell = TrySpell(bot, bot, 803183, true))
                return spell;
    }
    else
    {
        if (!bot->HasAura(800841) && !bot->HasAura(803183))
            if (uint32 spell = TrySpell(bot, bot, 800841, true))
                return spell;
    }

    // 3. Maintain Shadra's Vigil (root 800870) DoT on target
    if (!target->HasAura(800870, bot->GetGUID()))
    {
        if (uint32 spell = TrySpell(bot, target, 800870))
            return spell;
    }

    // 4. Brood Spenders / Big finishers:
    // Brood Trap (root 503149)
    if (uint32 spell = TrySpell(bot, target, 503149))
        return spell;

    // Molt / Sepsis (root 502896)
    if (uint32 spell = TrySpell(bot, target, 502896))
        return spell;

    // 5. Summon pet if off cooldown: Spawn (root 805568)
    if (!bot->GetGuardianPet())
    {
        if (uint32 spell = TrySpell(bot, target, 805568))
            return spell;
    }

    // 6. In melee range:
    if (dist <= 6.0f)
    {
        // Scorpid Claw (root 803198)
        if (uint32 spell = TrySpell(bot, target, 803198))
            return spell;

        // Venom Fang (root 800880)
        if (uint32 spell = TrySpell(bot, target, 800880))
            return spell;
    }

    // 7. Ranged filler: Venom Bolt (root 800869)
    if (uint32 spell = TrySpell(bot, target, 800869))
        return spell;

    return 0;
}

// -------------------------------------------------------------------------
// Class 24: Pyromancer
// -------------------------------------------------------------------------
uint32 SelectPyromancerRotationSpell(Player* bot, Unit* target, uint32 /*activeSpec*/)
{
    float dist = bot->GetDistance(target);

    // 1. Maintain Ignite (root 800791) DoT on target
    if (!target->HasAura(800791, bot->GetGUID()))
    {
        if (uint32 spell = TrySpell(bot, target, 800791))
            return spell;
    }

    // 2. Slagstone (root 803950) - heavy damage and stun
    if (uint32 spell = TrySpell(bot, target, 803950))
        return spell;

    // 3. Destroyer's Maw (root 802107) - core Pyromancer attack
    if (uint32 spell = TrySpell(bot, target, 802107))
        return spell;

    // 4. Burst spenders / instant casts:
    // Combustion (root 802791)
    if (uint32 spell = TrySpell(bot, target, 802791))
        return spell;

    // Fire Blast (root 800792)
    if (uint32 spell = TrySpell(bot, target, 800792))
        return spell;

    // Pyroblast (root 800818) - use if Hot Streak / proc is active or when off cooldown
    if (uint32 spell = TrySpell(bot, target, 800818))
        return spell;

    // 4. Close/Mid range frontal cone: Dragon's Breath / Flame Wave (root 801915)
    if (dist <= 12.0f)
    {
        if (uint32 spell = TrySpell(bot, target, 801915))
            return spell;
    }

    // 5. Ignite spender (root 805500)
    if (uint32 spell = TrySpell(bot, target, 805500))
        return spell;

    // 6. Primary ranged filler: Scorching Ray (root 800790)
    if (uint32 spell = TrySpell(bot, target, 800790))
        return spell;

    return 0;
}

// -------------------------------------------------------------------------
// Class 21: Ranger
// -------------------------------------------------------------------------
uint32 SelectRangerRotationSpell(Player* bot, Unit* target, uint32 /*activeSpec*/)
{
    float dist = bot->GetDistance(target);

    // 1. Instant proc / burst shot: Falconstrike (root 806345)
    if (uint32 spell = TrySpell(bot, target, 806345))
        return spell;

    // 2. Melee range (<= 6.0f):
    if (dist <= 6.0f)
    {
        // Maintain Rusty Shiv debuff if learned (561315)
        if (!target->HasAura(561315, bot->GetGUID()))
        {
            if (uint32 spell = TrySpell(bot, target, 561315))
                return spell;
        }

        // Flank (root 804940) - high damage positional strike
        if (uint32 spell = TrySpell(bot, target, 804940))
            return spell;

        // Assault (root 803108) - main dagger weapon attack
        if (uint32 spell = TrySpell(bot, target, 803108))
            return spell;

        // Wild Strike (root 800083) - primary melee builder/spender
        if (uint32 spell = TrySpell(bot, target, 800083))
            return spell;

        // Talent strike (root 804027)
        if (uint32 spell = TrySpell(bot, target, 804027))
            return spell;
    }

    // 3. Ranged priority (checked by CanCastSpell for min/max range and ranged weapon fit):
    // Skullpiercer Shot (root 802036) - heavy sniper shot
    if (uint32 spell = TrySpell(bot, target, 802036))
        return spell;

    // Falcon's Focus (root 800266) - heavy ranged focus shot
    if (uint32 spell = TrySpell(bot, target, 800266))
        return spell;

    // Burst shot (root 807237)
    if (uint32 spell = TrySpell(bot, target, 807237))
        return spell;

    // Quick Shot (root 500074) - primary ranged builder / filler
    if (uint32 spell = TrySpell(bot, target, 500074))
        return spell;

    // Close-range fallback if ranged shot was inside minRange
    if (dist <= 6.0f)
    {
        if (uint32 spell = TrySpell(bot, target, 800083))
            return spell;
    }

    return 0;
}

// -------------------------------------------------------------------------
// Class 25: Cultist
// -------------------------------------------------------------------------
uint32 SelectCultistRotationSpell(Player* bot, Unit* target, uint32 /*activeSpec*/)
{
    // 1. Maintain Herald buff on self if known and missing:
    if (!bot->HasAura(520326) && !bot->HasAura(805119) && !bot->HasAura(805120) && !bot->HasAura(805121))
    {
        if (uint32 spell = TrySpell(bot, bot, 805121, true))
            return spell;
        if (uint32 spell = TrySpell(bot, bot, 805120, true))
            return spell;
        if (uint32 spell = TrySpell(bot, bot, 805119, true))
            return spell;
    }

    // 2. High-priority spenders / burst:
    // Gaze of C'Thun (root 500110) - heavy channel/DoT
    if (uint32 spell = TrySpell(bot, target, 500110))
        return spell;

    // Blade of the Empire (root 500720) - 3 charges, weapon strike
    if (uint32 spell = TrySpell(bot, target, 500720))
        return spell;

    // Insanity Spender (root 805116) - requires 40 Insanity or Madness
    if (uint32 spell = TrySpell(bot, target, 805116))
        return spell;

    // Eldritch Strike (root 801964)
    if (uint32 spell = TrySpell(bot, target, 801964))
        return spell;

    // Heavy spells: root 500715, 500711
    if (uint32 spell = TrySpell(bot, target, 500715))
        return spell;
    if (uint32 spell = TrySpell(bot, target, 500711))
        return spell;

    // 3. Primary fillers:
    if (uint32 spell = TrySpell(bot, target, 500720))
        return spell;

    // At range / Caster: Horrorbolt (root 800416)
    if (uint32 spell = TrySpell(bot, target, 800416))
        return spell;

    return 0;
}

// -------------------------------------------------------------------------
// Class 28: Tinker
// -------------------------------------------------------------------------
uint32 SelectTinkerRotationSpell(Player* bot, Unit* target, uint32 /*activeSpec*/)
{
    float dist = bot->GetDistance(target);

    // 1. Maintain Reload/ammo if known and missing: Reload (500237)
    if (!bot->HasAura(500237))
    {
        if (uint32 spell = TrySpell(bot, bot, 500237, true))
            return spell;
    }

    // 2. High-impact bombs and burst abilities:
    // Sticky Bomb (root 500232) - heavy explosive bomb
    if (uint32 spell = TrySpell(bot, target, 500232))
        return spell;

    // Gunsling (root 805351) - instant shot
    if (uint32 spell = TrySpell(bot, target, 805351))
        return spell;

    // Scrap Shot (root 500549) - only outside min range
    if (dist >= 8.0f)
    {
        if (uint32 spell = TrySpell(bot, target, 500549))
            return spell;
    }

    // 3. Close range blast: Shotgun (root 801647) if within 10yd
    if (dist <= 10.0f)
    {
        if (uint32 spell = TrySpell(bot, target, 801647))
            return spell;
    }

    // 4. Primary ranged filler: Scrap Shot (root 500549) if at range
    if (dist >= 8.0f)
    {
        if (uint32 spell = TrySpell(bot, target, 500549))
            return spell;
    }

    return 0;
}

// -------------------------------------------------------------------------
// Class 18: Guardian
// -------------------------------------------------------------------------
uint32 SelectGuardianRotationSpell(Player* bot, Unit* target, uint32 activeSpec)
{
    float dist = bot->GetDistance(target);

    // 1. Maintain Stance / Formation
    // Spec 19 is Vanguard (Tank) -> prefer Tower Formation (800317)
    // Other specs / default -> prefer Line Formation (803130) or Assault Formation (803417)
    if (activeSpec == 19)
    {
        if (!bot->HasAura(800317))
            if (uint32 spell = TrySpell(bot, bot, 800317, true))
                return spell;
    }
    else
    {
        if (!bot->HasAura(803130) && !bot->HasAura(803417) && !bot->HasAura(800317))
        {
            if (uint32 spell = TrySpell(bot, bot, 803130, true))
                return spell;
            if (uint32 spell = TrySpell(bot, bot, 803417, true))
                return spell;
            if (uint32 spell = TrySpell(bot, bot, 800317, true))
                return spell;
        }
    }

    // 2. Gap closers if target is at range (> 8.0f)
    if (dist > 8.0f)
    {
        // Grand Entrance (root 802870)
        if (uint32 spell = TrySpell(bot, target, 802870))
            return spell;

        // Battle Rush (root 802197) - requires Assault Formation
        if (uint32 spell = TrySpell(bot, target, 802197))
            return spell;
    }

    // 3. Place Standard / Banner if in combat and off cooldown
    // Standard of Valiance (root 800319)
    if (uint32 spell = TrySpell(bot, bot, 800319, true))
        return spell;

    // 4. Melee Priority Attacks:
    // Pulverize (root 800311) - heavy shield strike (requires shield)
    if (uint32 spell = TrySpell(bot, target, 800311))
        return spell;

    // Ram (root 802284) - shield ram (requires shield)
    if (uint32 spell = TrySpell(bot, target, 802284))
        return spell;

    // Heavy Blow (root 803129) - core melee builder
    if (uint32 spell = TrySpell(bot, target, 803129))
        return spell;

    // Broad Sweep (root 805150) - sweeping melee strike
    if (uint32 spell = TrySpell(bot, target, 805150))
        return spell;

    // Hammer of the Law (704418) - mace strike
    if (uint32 spell = TrySpell(bot, target, 704418))
        return spell;

    // Linebreaker (root 806220) - line formation strike
    if (uint32 spell = TrySpell(bot, target, 806220))
        return spell;

    // Hold the Line (root 803830)
    if (uint32 spell = TrySpell(bot, target, 803830))
        return spell;

    // Press the Attack (root 801219)
    if (uint32 spell = TrySpell(bot, target, 801219))
        return spell;

    // 5. Defensive in close combat if needed
    if (bot->GetHealthPct() < 70.0f)
    {
        if (uint32 spell = TrySpell(bot, bot, 800313, true)) // Brace
            return spell;
        if (uint32 spell = TrySpell(bot, bot, 500168, true)) // Raise Shield
            return spell;
    }

    return 0;
}

// -------------------------------------------------------------------------
// Class 19: Templar
// -------------------------------------------------------------------------
uint32 SelectTemplarRotationSpell(Player* bot, Unit* target, uint32 /*activeSpec*/)
{
    float dist = bot->GetDistance(target);

    // 1. Maintain Gift buff on self: Gift of Zeal (root 706634) or Gift of Fervor (572629)
    if (!bot->HasAura(706634) && !bot->HasAura(300916) && !bot->HasAura(300917) &&
        !bot->HasAura(300918) && !bot->HasAura(300919) && !bot->HasAura(300923) &&
        !bot->HasAura(572629) && !bot->HasAura(572630))
    {
        if (uint32 spell = TrySpell(bot, bot, 706634, true))
            return spell;
        if (uint32 spell = TrySpell(bot, bot, 572629, true))
            return spell;
    }

    // 2. Gap closer if target is at distance (> 8.0f): Divine Charge (527023)
    if (dist > 8.0f)
    {
        if (uint32 spell = TrySpell(bot, target, 527023))
            return spell;
    }

    // 3. Breakers / Spenders: require Oath Chain (704576), automatically gated by CasterAuraSpell
    // Blade of Faith (root 803872) - highest single-target holy spender
    if (uint32 spell = TrySpell(bot, target, 803872))
        return spell;

    // Chastise (root 803157) - holy damage and stun
    if (uint32 spell = TrySpell(bot, target, 803157))
        return spell;

    // Righteous Tempest (root 805409) - whirlwind holy spender
    if (uint32 spell = TrySpell(bot, target, 805409))
        return spell;

    // Benediction (root 801448) - holy blessing / burst
    if (uint32 spell = TrySpell(bot, target, 801448))
        return spell;

    // 4. Heavy Cooldowns:
    // Titanstrike (root 806521) - 2H holy weapon strike
    if (uint32 spell = TrySpell(bot, target, 806521))
        return spell;

    // Divine Force (root 806153) - holy force burst
    if (uint32 spell = TrySpell(bot, target, 806153))
        return spell;

    // Libram of Consecration (root 801441)
    if (dist <= 8.0f)
    {
        if (uint32 spell = TrySpell(bot, bot, 801441, true))
            return spell;
    }

    // Crusader's Brand (root 300513)
    if (uint32 spell = TrySpell(bot, target, 300513))
        return spell;

    // 5. Primary Builders (generate Oaths):
    // Righteous Lunge (root 801443) - primary builder
    if (uint32 spell = TrySpell(bot, target, 801443))
        return spell;

    // Condemn (root 804906) - primary judgment builder
    if (uint32 spell = TrySpell(bot, target, 804906))
        return spell;

    // Holy Cleave (root 801445) - cleave builder
    if (uint32 spell = TrySpell(bot, target, 801445))
        return spell;

    return 0;
}

// -------------------------------------------------------------------------
// Class 26: Starcaller
// -------------------------------------------------------------------------
uint32 SelectStarcallerRotationSpell(Player* bot, Unit* target, uint32 activeSpec)
{
    float dist = bot->GetDistance(target);

    // 1. Maintain Stance / Aspect on self:
    // Spec 45: Warden (Melee) -> Aspect of the Warden (801128)
    // Spec 43: Sentinel (Ranged) -> Aspect of the Huntress (805356)
    // Spec 100: Moon Guard (Tank) / others -> Aspect of the Stars (root 800510)
    bool hasAspect = bot->HasAura(800510) || bot->HasAura(803887) || bot->HasAura(803888) ||
                     bot->HasAura(801128) || bot->HasAura(805356) || bot->HasAura(801123);
    if (!hasAspect)
    {
        if (activeSpec == 45)
        {
            if (uint32 spell = TrySpell(bot, bot, 801128, true))
                return spell;
        }
        else if (activeSpec == 43)
        {
            if (uint32 spell = TrySpell(bot, bot, 805356, true))
                return spell;
        }
        else
        {
            if (uint32 spell = TrySpell(bot, bot, 800510, true))
                return spell;
            if (uint32 spell = TrySpell(bot, bot, 801128, true))
                return spell;
            if (uint32 spell = TrySpell(bot, bot, 801123, true))
                return spell;
        }
    }

    // 2. In Melee Range (dist <= 6.0f):
    if (dist <= 6.0f)
    {
        // Celestial Strike (root 800496) - primary astral weapon strike, generates Stars (804378)
        if (uint32 spell = TrySpell(bot, target, 800496))
            return spell;

        // Warden's Blade (root 805508) - multi-strike melee weapon attack
        if (uint32 spell = TrySpell(bot, target, 805508))
            return spell;

        // Starsunder (root 801127) - sunder strike
        if (uint32 spell = TrySpell(bot, target, 801127))
            return spell;

        // Celestial Cleave (root 801181) - cleaving astral strike
        if (uint32 spell = TrySpell(bot, target, 801181))
            return spell;

        // Starsweep (root 805550) - melee sweep (requires Scattered Stars on target)
        if (uint32 spell = TrySpell(bot, target, 805550))
            return spell;

        // Starshatter (root 801135)
        if (uint32 spell = TrySpell(bot, target, 801135))
            return spell;

        // Shooting Star (root 800505) - astral builder, can be cast in melee
        if (uint32 spell = TrySpell(bot, target, 800505))
            return spell;
    }

    // 3. At Ranged Distance:
    // Ranged weapon shots (if equipped, checked by CanCastSpell):
    // Moon Arrow (root 801972)
    if (uint32 spell = TrySpell(bot, target, 801972))
        return spell;

    // Huntress Shot (root 680220)
    if (uint32 spell = TrySpell(bot, target, 680220))
        return spell;

    // Astral Spells:
    // Lunar Lance (root 801132) - requires Scattered Stars (804378) on target (checked by TargetAuraSpell)
    if (uint32 spell = TrySpell(bot, target, 801132))
        return spell;

    // Shooting Star (root 800505) - primary astral ranged builder, applies Stars
    if (uint32 spell = TrySpell(bot, target, 800505))
        return spell;

    // Moonwell Splash (root 800370) - ranged AoE/damage
    if (uint32 spell = TrySpell(bot, target, 800370))
        return spell;

    return 0;
}

// -------------------------------------------------------------------------
// Class 13: Witch Doctor
// -------------------------------------------------------------------------
uint32 SelectWitchDoctorRotationSpell(Player* bot, Unit* target, uint32 /*activeSpec*/)
{
    // 1. Emergency Defense / Healing (< 50% health):
    if (bot->GetHealthPct() < 50.0f)
    {
        // Loa's Brew (root 801670) - healing brew
        if (uint32 spell = TryThrottledSpell(bot, bot, 801670, 8000, true))
            return spell;

        // Spirit in a Bottle (root 801696) - protective spirit potion
        if (uint32 spell = TryThrottledSpell(bot, bot, 801696, 12000, true))
            return spell;
    }

    // 2. Self Buffs / Cooldowns:
    // Shadow Avatar (root 705943) - dark power form
    if (!bot->HasAura(705943))
    {
        if (uint32 spell = TrySpell(bot, bot, 705943, true))
            return spell;
    }

    // 3. Situational Guardians, Wards, Effigies & Idols:
    // Major guardians / temporary summons:
    if (uint32 spell = TryThrottledSpell(bot, target, 802719, 45000)) // Big Voodoo (Bwonsamdi)
        return spell;
    if (uint32 spell = TryThrottledSpell(bot, target, 800330, 45000)) // War Golem
        return spell;
    if (uint32 spell = TryThrottledSpell(bot, target, 572899, 30000)) // Call Sseratus (snakes)
        return spell;
    if (uint32 spell = TryThrottledSpell(bot, target, 707162, 30000)) // Mimic
        return spell;

    // Wards (WardSlot - Healing Ward if injured, Serpent Ward if healthy):
    if (bot->GetHealthPct() < 60.0f)
    {
        if (uint32 spell = TryThrottledSpell(bot, bot, 500957, 25000)) // Healing Ward
            return spell;
    }
    else
    {
        if (uint32 spell = TryThrottledSpell(bot, target, 500960, 25000)) // Serpent Ward
            return spell;
    }

    // Effigies (EffigySlot - Hexing / Shadow / Cursed):
    if (uint32 spell = TryThrottledSpell(bot, target, 506634, 25000)) // Hexing Effigy
        return spell;
    if (uint32 spell = TryThrottledSpell(bot, target, 505339, 25000)) // Shadow Effigy
        return spell;
    if (uint32 spell = TryThrottledSpell(bot, target, 706542, 25000)) // Cursed Effigy
        return spell;

    // Idols (IdolSlot - Dark / Swift / Spirit):
    if (uint32 spell = TryThrottledSpell(bot, target, 507082, 25000)) // Dark Idol
        return spell;
    if (uint32 spell = TryThrottledSpell(bot, target, 804226, 25000)) // Swift Idol
        return spell;
    if (uint32 spell = TryThrottledSpell(bot, target, 500961, 25000)) // Spirit Idol
        return spell;

    // 4. Curses & Jinxes (Debuffs on target):
    // Hex of Malice (root 801693) - core damage over time curse
    uint32 hexMalice = GetHighestLearnedRank(bot, 801693);
    if (hexMalice && !target->HasAura(hexMalice))
    {
        if (uint32 spell = TrySpell(bot, target, 801693))
            return spell;
    }

    // Shrinking Jinx (root 806285) - weakening combat jinx
    uint32 shrinkJinx = GetHighestLearnedRank(bot, 806285);
    if (shrinkJinx && !target->HasAura(shrinkJinx))
    {
        if (uint32 spell = TrySpell(bot, target, 806285))
            return spell;
    }

    // 5. High-Impact Offensive Nukes:
    // Malefic Wrath (root 807037) - heavy shadow nuke
    if (uint32 spell = TrySpell(bot, target, 807037))
        return spell;

    // Potion Toss (root 801661) - only usable with active brew aura (checked by CanCastSpell)
    if (uint32 spell = TrySpell(bot, target, 801661))
        return spell;

    // Splash Potion (root 802710)
    if (uint32 spell = TrySpell(bot, target, 802710))
        return spell;

    // Mojo Beam (root 500950) - channeled beam
    if (uint32 spell = TrySpell(bot, target, 500950))
        return spell;

    // 6. Primary Ranged Spammer / Filler:
    // Shadowflare (root 801669) - core dark projectile
    if (uint32 spell = TrySpell(bot, target, 801669))
        return spell;

    return 0;
}

// -------------------------------------------------------------------------
// Class 15: Witch Hunter
// -------------------------------------------------------------------------
uint32 SelectWitchHunterRotationSpell(Player* bot, Unit* target, uint32 /*activeSpec*/)
{
    float dist = bot->GetDistance(target);

    // 1. Maintain a Tonic buff on self if known and missing:
    if (!bot->HasAura(802278) && !bot->HasAura(802276) && !bot->HasAura(803535) &&
        !bot->HasAura(802826) && !bot->HasAura(680491))
    {
        // Witchblood Tonic (root 802278)
        if (uint32 spell = TrySpell(bot, bot, 802278, true))
            return spell;
        // Vampiric Tonic (root 802276)
        if (uint32 spell = TrySpell(bot, bot, 802276, true))
            return spell;
        // Holy Water Tonic (root 802826)
        if (uint32 spell = TrySpell(bot, bot, 802826, true))
            return spell;
        // Dark Tonic (root 680491)
        if (uint32 spell = TrySpell(bot, bot, 680491, true))
            return spell;
    }

    // 2. Maintain Stance/Aura:
    // Dark Aura (root 680535)
    uint32 darkAura = GetHighestLearnedRank(bot, 680535);
    if (darkAura && !bot->HasAura(darkAura))
    {
        if (uint32 spell = TrySpell(bot, bot, 680535, true))
            return spell;
    }

    // 3. Emergency Defense (< 50% health):
    if (bot->GetHealthPct() < 50.0f)
    {
        // Night's Watch (root 807733)
        if (uint32 spell = TrySpell(bot, bot, 807733, true))
            return spell;
        // Dark Regeneration (root 500094)
        if (uint32 spell = TrySpell(bot, bot, 500094, true))
            return spell;
    }

    // 4. Target Brand (Apply if target doesn't have an active Witch Hunter Brand):
    uint32 brandDamned = GetHighestLearnedRank(bot, 807682);
    bool hasBrand = (brandDamned && target->HasAura(brandDamned)) ||
                    target->HasAura(501380) || target->HasAura(562390) ||
                    target->HasAura(562573) || target->HasAura(680517);
    if (!hasBrand)
    {
        // Brand of the Damned (root 807682)
        if (uint32 spell = TrySpell(bot, target, 807682))
            return spell;
        // Brand of the Condemned (root 562573)
        if (uint32 spell = TrySpell(bot, target, 562573))
            return spell;
        // Brand of the Profane (root 562390)
        if (uint32 spell = TrySpell(bot, target, 562390))
            return spell;
        // Brand of the Unworthy (root 501380)
        if (uint32 spell = TrySpell(bot, target, 501380))
            return spell;
    }

    // 5. Witchblight / Edicts:
    // Witchblight (root 680494) - debuff
    if (!target->HasAura(680494))
    {
        if (uint32 spell = TrySpell(bot, target, 680494))
            return spell;
    }

    // Witching Edict (root 707684) / Inquisitor's Edict (root 706741)
    uint32 witchingEdict = GetHighestLearnedRank(bot, 707684);
    if (witchingEdict && !bot->HasAura(witchingEdict))
    {
        if (uint32 spell = TryThrottledSpell(bot, target, 707684, 25000))
            return spell;
    }
    uint32 inqEdict = GetHighestLearnedRank(bot, 706741);
    if (inqEdict && !bot->HasAura(inqEdict))
    {
        if (uint32 spell = TryThrottledSpell(bot, target, 706741, 25000))
            return spell;
    }

    // 6. In Melee Range (<= 5.0f):
    if (dist <= 5.0f)
    {
        // Dawn Blade (root 802024) - primary holy melee blade strike
        if (uint32 spell = TrySpell(bot, target, 802024))
            return spell;

        // Pommel Smash (root 680515) - physical strike
        if (uint32 spell = TrySpell(bot, target, 680515))
            return spell;

        // Guard Strike (root 804432) - defensive strike
        if (uint32 spell = TrySpell(bot, target, 804432))
            return spell;

        // Desecrate (root 680518) - ground AoE (if aura requirements met)
        if (uint32 spell = TrySpell(bot, target, 680518))
            return spell;
    }

    // 7. Ranged Attacks (Guns / Crossbows / Ranged Spells):
    // Sixfold Shot (root 807364) - channeled burst shot
    if (uint32 spell = TrySpell(bot, target, 807364))
        return spell;

    // Shadowblast (root 804191) - primary heavy shadow/ranged attack
    if (uint32 spell = TrySpell(bot, target, 804191))
        return spell;

    // Bola Throw (root 500093) - ranged snare & damage
    if (uint32 spell = TrySpell(bot, target, 500093))
        return spell;

    // Darkslayer (root 804179) - ranged execute / heavy finisher
    if (uint32 spell = TrySpell(bot, target, 804179))
        return spell;

    // Burrow Bolt (root 802269) - special bolt attack
    if (uint32 spell = TrySpell(bot, target, 802269))
        return spell;

    // Close-range fallback if ranged failed (e.g. minimum range):
    if (dist <= 5.0f)
    {
        if (uint32 spell = TrySpell(bot, target, 802024))
            return spell;
        if (uint32 spell = TrySpell(bot, target, 680515))
            return spell;
    }

    return 0;
}

// -------------------------------------------------------------------------
// Class 27: Sun Cleric
// -------------------------------------------------------------------------
uint32 SelectSunClericRotationSpell(Player* bot, Unit* target, uint32 /*activeSpec*/)
{
    float dist = bot->GetDistance(target);

    // 1. Maintain Stance / Form:
    // Holy Form (root 805301)
    if (!bot->HasAura(805301))
    {
        if (uint32 spell = TrySpell(bot, bot, 805301, true))
            return spell;
    }

    // 2. Emergency Healing & Defense (< 50% health):
    if (bot->GetHealthPct() < 50.0f)
    {
        // Sol Invictus (root 807732) - holy emergency shield / ward
        if (uint32 spell = TrySpell(bot, bot, 807732, true))
            return spell;

        // Solar Invocation: Ascension (root 500152) - burst AoE heal
        if (uint32 spell = TrySpell(bot, bot, 500152, true))
            return spell;

        // Revivify (root 801790) - direct heal / HoT
        if (uint32 spell = TrySpell(bot, bot, 801790, true))
            return spell;

        // Daybreak (root 500147) - instant holy heal
        if (uint32 spell = TrySpell(bot, bot, 500147, true))
            return spell;

        // Illumination (root 500143) - holy heal cast
        if (uint32 spell = TrySpell(bot, bot, 500143, true))
            return spell;
    }

    // Radiance (root 800054) - burst cooldown / holy radiance aura
    if (!bot->HasAura(800054))
    {
        if (uint32 spell = TrySpell(bot, bot, 800054, true))
            return spell;
    }

    // 4. In Melee Range (<= 5.0f, with 1H weapon equipped checked by CanCastSpell):
    if (dist <= 5.0f)
    {
        // Gavel of Light (root 800611) - primary holy melee weapon strike
        if (uint32 spell = TrySpell(bot, target, 800611))
            return spell;

        // Gavel of Grace (root 800614) - secondary holy strike
        if (uint32 spell = TrySpell(bot, target, 800614))
            return spell;

        // Gavel of Wrath (root 800617) - burst holy strike
        if (uint32 spell = TrySpell(bot, target, 800617))
            return spell;
    }

    // 5. Ranged Caster Attacks:
    // Injunction (root 800624) - holy debuff on target
    if (!target->HasAura(800624))
    {
        if (uint32 spell = TrySpell(bot, target, 800624))
            return spell;
    }

    // Dawnfall (root 806118) - heavy ground holy AoE nuke
    if (uint32 spell = TrySpell(bot, target, 806118))
        return spell;

    // Horusath Blast (root 500154) - heavy ranged holy nuke
    if (uint32 spell = TrySpell(bot, target, 500154))
        return spell;

    // Glare (root 805583) - instant holy damage
    if (uint32 spell = TrySpell(bot, target, 805583))
        return spell;

    // Sunflare (root 800231) - primary holy fire ranged builder / spammer
    if (uint32 spell = TrySpell(bot, target, 800231))
        return spell;

    // Close-range fallback:
    if (dist <= 5.0f)
    {
        if (uint32 spell = TrySpell(bot, target, 800611))
            return spell;
    }

    return 0;
}

// -------------------------------------------------------------------------
// Class 23: Necromancer
// -------------------------------------------------------------------------
uint32 SelectNecromancerRotationSpell(Player* bot, Unit* target, uint32 /*activeSpec*/)
{
    float botHp = bot->GetHealthPct();

    // 1. Situational Stance Switching (Spell Group 1137):
    // 500982: Undead: Assault (Offensive DPS: minion attack speed & crit)
    // 500985: Undead: Protect (Tank / Defense: minion threat, player threat reduced)
    // 500983: Undead: Pacify  (Critical Defense / Survival: minion passive, player -20% damage taken)
    uint32 desiredStance = 500982; // Default to Assault
    if (botHp < 35.0f)
        desiredStance = 500983; // Pacify for critical survival
    else if (botHp < 60.0f || (target->GetVictim() && target->GetVictim()->GetGUID() == bot->GetGUID()))
        desiredStance = 500985; // Protect if taking damage or targeted

    if (desiredStance && !bot->HasAura(desiredStance))
    {
        if (uint32 spell = TrySpell(bot, bot, desiredStance, true))
            return spell;
    }

    // 2. Emergency Survival (< 35% HP):
    // Sacrifice Undead (root 805027, highest rank e.g. 807941)
    if (botHp < 35.0f)
    {
        if (uint32 sacrifice = GetHighestLearnedRank(bot, 805027))
        {
            if (uint32 spell = TrySpell(bot, bot, sacrifice, true))
                return spell;
        }
    }

    // 3. Situational Temporary / Cooldown Summons (0 Life Force cost):
    // Animate: Plaguefather (root 805048) - elite plague minion, throttled 30s
    if (uint32 spell = TryThrottledSpell(bot, bot, 805048, 30000, true))
        return spell;

    // Animate: Bone Wraith (root 805032) - shadow burst wraith, throttled 30s
    if (uint32 spell = TryThrottledSpell(bot, bot, 805032, 30000, true))
        return spell;

    // Animate: Skeletal Archer (root 805040) - ranged physical minion, throttled 30s
    if (uint32 spell = TryThrottledSpell(bot, bot, 805040, 30000, true))
        return spell;

    // Animate: Bone Construct (root 531130) - bone construct guardian, throttled 30s
    if (uint32 spell = TryThrottledSpell(bot, bot, 531130, 30000, true))
        return spell;

    // 4. Permanent Minions (Life Force Summons, throttled 15s so once Life Force pool is filled, DoTs/Nukes are cast):
    // Raise: Crypt Fiend (root 504859, cost 2 LF) - ranged poison/web minion
    if (uint32 spell = TryThrottledSpell(bot, bot, 504859, 15000, true))
        return spell;

    // Raise: Greater Skeletal Warrior (root 504901, cost 1 LF) - durable melee tank/dps
    if (uint32 spell = TryThrottledSpell(bot, bot, 504901, 15000, true))
        return spell;

    // Raise: Ghoul (root 500971, cost 1 LF) - aggressive melee ghoul
    if (uint32 spell = TryThrottledSpell(bot, bot, 500971, 15000, true))
        return spell;

    // Raise: Skeletal Rogue (root 500969, cost 1 LF) - burst melee rogue
    if (uint32 spell = TryThrottledSpell(bot, bot, 500969, 15000, true))
        return spell;

    // Raise: Abomination (root 500335, cost 3 LF) - giant melee brute
    if (uint32 spell = TryThrottledSpell(bot, bot, 500335, 15000, true))
        return spell;

    // Raise: Brittle Skeleton (root 500970, cost 1 LF) - basic skeleton
    if (uint32 spell = TryThrottledSpell(bot, bot, 500970, 15000, true))
        return spell;

    // 5. Afflictions & Curses (Maintain DoTs):
    // Crypt Swarm (root 500965, highest rank e.g. 501888)
    uint32 cryptSwarm = GetHighestLearnedRank(bot, 500965);
    if (cryptSwarm && !target->HasAura(cryptSwarm) && !target->HasAura(500965))
    {
        if (uint32 spell = TrySpell(bot, target, cryptSwarm))
            return spell;
    }

    // Harvest Plague (root 500968, highest rank e.g. 583256)
    uint32 harvestPlague = GetHighestLearnedRank(bot, 500968);
    if (harvestPlague && !target->HasAura(harvestPlague) && !target->HasAura(500968))
    {
        if (uint32 spell = TrySpell(bot, target, harvestPlague))
            return spell;
    }

    // Greater Chill of the Tomb (root 572173) - frost snare / AoE
    if (uint32 spell = TrySpell(bot, target, 572173))
        return spell;

    // 6. Direct Damage Fallback:
    // Return 0 so BotAI::UpdateOffensive falls through to BotAI::SelectNecromancerRotationSpell(bot, target)
    // which casts Lichfrost (501980 / etc.) or Ice Barrage!
    return 0;
}

} // anonymous namespace

uint32 SelectClassRotationSpell(Player* bot, Unit* target, uint8 classId, uint32 activeSpec)
{
    if (!bot || !target || !target->IsAlive())
        return 0;

    switch (classId)
    {
        case 12: // Barbarian
            return SelectBarbarianRotationSpell(bot, target, activeSpec);

        case 13: // Witch Doctor
            return SelectWitchDoctorRotationSpell(bot, target, activeSpec);

        case 15: // Witch Hunter
            return SelectWitchHunterRotationSpell(bot, target, activeSpec);

        case 18: // Guardian
            return SelectGuardianRotationSpell(bot, target, activeSpec);

        case 19: // Templar
            return SelectTemplarRotationSpell(bot, target, activeSpec);

        case 21: // Ranger
            return SelectRangerRotationSpell(bot, target, activeSpec);

        case 23: // Necromancer
            return SelectNecromancerRotationSpell(bot, target, activeSpec);

        case 24: // Pyromancer
            return SelectPyromancerRotationSpell(bot, target, activeSpec);

        case 25: // Cultist
            return SelectCultistRotationSpell(bot, target, activeSpec);

        case 26: // Starcaller
            return SelectStarcallerRotationSpell(bot, target, activeSpec);

        case 27: // Sun Cleric
            return SelectSunClericRotationSpell(bot, target, activeSpec);

        case 28: // Tinker
            return SelectTinkerRotationSpell(bot, target, activeSpec);

        case 29: // Venomancer
            return SelectVenomancerRotationSpell(bot, target, activeSpec);

        default:
            return 0;
    }
}

void RecordSpellCastFailure(ObjectGuid botGuid, uint32 spellId)
{
    if (!spellId)
        return;
    failureCooldowns[botGuid][spellId] = getMSTime() + FAILURE_COOLDOWN_MS;
}

void ForgetRotationState(ObjectGuid botGuid)
{
    failureCooldowns.erase(botGuid);
    abilityThrottleCooldowns.erase(botGuid);
}

} // namespace BotAI
