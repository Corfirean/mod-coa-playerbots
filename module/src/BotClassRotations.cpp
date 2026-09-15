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

} // anonymous namespace

uint32 SelectClassRotationSpell(Player* bot, Unit* target, uint8 classId, uint32 activeSpec)
{
    if (!bot || !target || !target->IsAlive())
        return 0;

    switch (classId)
    {
        case 12: // Barbarian
            return SelectBarbarianRotationSpell(bot, target, activeSpec);

        case 18: // Guardian
            return SelectGuardianRotationSpell(bot, target, activeSpec);

        case 19: // Templar
            return SelectTemplarRotationSpell(bot, target, activeSpec);

        case 21: // Ranger
            return SelectRangerRotationSpell(bot, target, activeSpec);

        case 24: // Pyromancer
            return SelectPyromancerRotationSpell(bot, target, activeSpec);

        case 25: // Cultist
            return SelectCultistRotationSpell(bot, target, activeSpec);

        case 26: // Starcaller
            return SelectStarcallerRotationSpell(bot, target, activeSpec);

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
}

} // namespace BotAI
