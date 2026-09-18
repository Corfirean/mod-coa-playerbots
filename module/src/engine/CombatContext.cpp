/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatContext implementation
 */

#include "engine/CombatContext.h"
#include "Group.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Timer.h"

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
        ctx.isCasting = bot->IsNonMeleeSpellCast(false);

        ctx.classId = bot->getClass();
        ctx.activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        ctx.role = BotAI::GetRole(bot->GetGUID());

        // Target / victim resolution
        ctx.victim = explicitVictim ? explicitVictim : bot->GetVictim();

        if (ctx.victim && ctx.victim->IsAlive())
        {
            // Check interruptible spell cast on victim
            for (uint32 i = CURRENT_FIRST_NON_MELEE_SPELL; i < CURRENT_AUTOREPEAT_SPELL; ++i)
            {
                if (Spell const* spell = ctx.victim->GetCurrentSpell(CurrentSpellTypes(i)))
                {
                    SpellInfo const* curSpellInfo = spell->GetSpellInfo();
                    if (!curSpellInfo)
                        continue;

                    if ((spell->getState() == SPELL_STATE_CASTING || (spell->getState() == SPELL_STATE_PREPARING && spell->GetCastTime() > 0.0f))
                            && spell->IsInterruptable()
                            && ((i == CURRENT_GENERIC_SPELL && (curSpellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT))
                                || (i == CURRENT_CHANNELED_SPELL && (curSpellInfo->ChannelInterruptFlags & CHANNEL_INTERRUPT_FLAG_INTERRUPT))))
                    {
                        ctx.victimIsCastingInterruptible = true;
                        break;
                    }
                }
            }

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
        }

        // Ally triage snapshot
        Group const* group = bot->GetGroup();
        if (group)
        {
            float lowestHp = 100.0f;
            Player* bestLowestAlly = nullptr;

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

                if (hp < lowestHp)
                {
                    lowestHp = hp;
                    bestLowestAlly = member;
                }

                BotRole mRole = BotAI::GetRole(member->GetGUID());
                if (mRole == BotRole::Tank)
                {
                    ctx.tankAlly = member;
                    ctx.tankAllyHpPct = hp;
                }
            }

            ctx.lowestAlly = bestLowestAlly ? bestLowestAlly : bot;
            ctx.lowestAllyHpPct = bestLowestAlly ? lowestHp : ctx.botHpPct;
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
