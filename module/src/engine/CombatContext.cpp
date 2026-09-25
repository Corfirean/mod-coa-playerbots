/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatContext implementation
 */

#include "engine/CombatContext.h"
#include "engine/CastGuard.h"
#include "engine/DamageTracker.h"
#include "engine/HealEvaluator.h"
#include "engine/SpellPredicates.h"
#include "engine/TargetEvaluator.h"
#include "Group.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Timer.h"
#include <algorithm>

namespace BotAI
{
    CombatContext CombatContext::Build(Player* bot, Unit* explicitVictim)
    {
        CombatContext ctx;
        if (!bot || !bot->IsInWorld())
            return ctx;

        ctx.bot = bot;
        ctx.currentMSTime = getMSTime();
        ctx.botHpPct = bot->GetHealthPct();

        Powers powerType = bot->getPowerType();
        uint32 maxPower = bot->GetMaxPower(powerType);
        ctx.botPowerPct = (maxPower > 0) ? (static_cast<float>(bot->GetPower(powerType)) * 100.0f / maxPower) : 100.0f;
        ctx.isCasting = CastGuard::IsCurrentlyCasting(bot);
        ctx.resources = CombatResourceEvaluator::BuildSnapshot(bot);

        ctx.classId = bot->getClass();
        ctx.activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        ctx.role = BotAI::GetRole(bot->GetGUID());

        // Target / victim resolution
        ctx.victim = explicitVictim ? explicitVictim : bot->GetVictim();

        if (ctx.victim && ctx.victim->IsAlive())
        {
            ctx.victimIsCastingInterruptible = IsTargetCastingInterruptibleSpell(
                ctx.victim, ctx.victimCastingSpellId, ctx.victimCastFinishTimeMs);

            // Check if victim is attacking someone other than our tank
            if (Unit const* victimTarget = ctx.victim->GetVictim())
            {
                if (victimTarget != bot)
                {
                    BotRole targetRole = BotAI::GetRole(victimTarget->GetGUID());
                    if (targetRole != BotRole::Tank)
                        ctx.victimTargetingNonTank = true;
                }
            }

            // Fight value: does this target justify a long-cooldown burst/defensive button --
            // see item 14/#9 of the combat-engine rework. A boss/elite, or a real pack, and not
            // already about to die (don't blow a 2-minute CD to finish off a 10%-HP trash mob).
            ctx.targetHpPct = ctx.victim->GetHealthPct();
            ctx.targetIsBossOrElite = IsBossOrEliteTarget(ctx.victim);
            ctx.nearbyEnemyCount = static_cast<uint8>(std::min<uint32>(255,
                CountNearbyEnemies(bot, ctx.victim, 10.0f)));
            ctx.engagedEnemyCount = static_cast<uint8>(std::min<uint32>(255,
                TargetEvaluator::CountEngaged(bot, ctx.victim, 10.0f)));
            bool targetIsPvP = ctx.victim->GetTypeId() == TYPEID_PLAYER;
            ctx.worthOffensiveCooldown = (ctx.targetIsBossOrElite || targetIsPvP || ctx.nearbyEnemyCount >= 3)
                && ctx.targetHpPct > 15.0f;
        }

        // Defensive prediction (item 19, Phase 2): a rolling incoming-dps estimate for the bot
        // itself, cheap (one hash-map lookup, see DamageTracker), plus a contextual "is this
        // actually dangerous right now" flag instead of a flat HP-threshold. Emergency HP floor,
        // a real incoming-damage trend implying death soon, or the current victim being both
        // dangerous (boss/elite mid-cast) and actually targeting the bot all count.
        ctx.botIncomingDps = DamageTracker::SampleIncomingDps(bot);
        float timeToDieSec = (ctx.botIncomingDps > 1.0f)
            ? (float(bot->GetHealth()) / ctx.botIncomingDps) : 999.0f;
        bool dangerousBossOnBot = ctx.targetIsBossOrElite && ctx.victim && ctx.victim->GetVictim() == bot
            && ctx.victimIsCastingInterruptible;
        ctx.worthDefensiveCooldown = ctx.botHpPct < 25.0f
            || timeToDieSec < 6.0f
            || (ctx.botHpPct < 60.0f && dangerousBossOnBot);

        // Ally triage snapshot
        Group const* group = bot->GetGroup();
        if (group)
        {
            float bestUrgency = -1.0f;
            Player* bestLowestAlly = nullptr;
            float bestLowestAllyHp = 100.0f;
            float bestTankScore = -1.0f;

            for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || !member->IsAlive() || !member->IsInWorld() || !member->IsWithinDistInMap(bot, 40.0f))
                    continue;

                float hp = member->GetHealthPct();
                if (hp < 80.0f)
                    ++ctx.injuredAllyCount;
                if (hp < 40.0f)
                    ++ctx.criticalAllyCount;

                // HealUrgencyScore (item 10/#4, Phase 2) instead of plain lowest-HP% -- a tank
                // taking heavy incoming damage at 55% can matter more than a DPS at 30% nobody's
                // still hitting. Feeds every profile ability that targets TargetType::
                // LowestHealthAlly/AnyInjuredAlly, not just the legacy healer fallback.
                float urgency = HealEvaluator::ScoreHealUrgency(bot, member);
                if (urgency > bestUrgency)
                {
                    bestUrgency = urgency;
                    bestLowestAlly = member;
                    bestLowestAllyHp = hp;
                }

                // Multi-tank selection (item 16, Phase 2 fixup): the old code just kept
                // overwriting ctx.tankAlly for every Tank-role member found, so with two tanks
                // the "chosen" one was whichever happened to iterate last -- pure GroupReference
                // order, not meaningful. Priority: holding a boss/elite's aggro outranks
                // everything else; among ties (or no boss aggro at all), the tank under the most
                // real pressure (incoming damage trend, low time-to-die) wins; the first tank
                // seen is the fallback if nothing distinguishes them.
                if (BotAI::GetRole(member->GetGUID()) == BotRole::Tank)
                {
                    float tankScore = 0.0f;
                    if (ctx.victim && ctx.targetIsBossOrElite && ctx.victim->GetVictim() == member)
                        tankScore += 1000.0f;

                    float tankIncomingDps = DamageTracker::SampleIncomingDps(member);
                    tankScore += tankIncomingDps;
                    float tankTtd = (tankIncomingDps > 1.0f) ? (float(member->GetHealth()) / tankIncomingDps) : 999.0f;
                    if (tankTtd < 10.0f)
                        tankScore += (10.0f - tankTtd) * 50.0f;

                    if (tankScore > bestTankScore)
                    {
                        bestTankScore = tankScore;
                        ctx.tankAlly = member;
                        ctx.tankAllyHpPct = hp;
                    }
                }
            }

            ctx.lowestAlly = bestLowestAlly ? bestLowestAlly : bot;
            ctx.lowestAllyHpPct = bestLowestAlly ? bestLowestAllyHp : ctx.botHpPct;
        }
        else
        {
            ctx.lowestAlly = bot;
            ctx.lowestAllyHpPct = ctx.botHpPct;
            if (ctx.botHpPct < 80.0f)
                ctx.injuredAllyCount = 1;
            if (ctx.botHpPct < 40.0f)
                ctx.criticalAllyCount = 1;
        }

        if (!ctx.tankAlly && ctx.role == BotRole::Tank)
        {
            ctx.tankAlly = bot;
            ctx.tankAllyHpPct = ctx.botHpPct;
        }

        return ctx;
    }
}
