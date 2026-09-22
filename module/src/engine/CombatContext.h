/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatContext (Per-tick snapshot)
 */

#ifndef COA_PLAYERBOTS_COMBAT_CONTEXT_H
#define COA_PLAYERBOTS_COMBAT_CONTEXT_H

#include "Define.h"
#include "BotAI.h"

class Player;
class Unit;

namespace BotAI
{
    struct CombatContext
    {
        Player* bot = nullptr;
        Unit* victim = nullptr;

        // Ally triage snapshot
        Player* lowestAlly = nullptr;
        float lowestAllyHpPct = 100.0f;
        Player* tankAlly = nullptr;
        float tankAllyHpPct = 100.0f;

        uint8 injuredAllyCount = 0;   // Count of alive groupmates with HP < 80%
        uint8 criticalAllyCount = 0;  // Count of alive groupmates with HP < 40%

        // Self snapshot
        float botHpPct = 100.0f;
        float botPowerPct = 100.0f;
        bool isCasting = false;

        // Threat & hostile environment
        bool victimIsCastingInterruptible = false;
        uint32 victimCastingSpellId = 0;    // spell id behind victimIsCastingInterruptible, 0 if none
        uint32 victimCastFinishTimeMs = 0;  // absolute getMSTime() the cast above finishes at
        bool victimTargetingNonTank = false;
        uint8 nearbyEnemyCount = 0;   // Hostile units within 10 yards of the victim

        // Fight value -- see item 14/#9 of the combat-engine rework: whether this fight actually
        // justifies spending a long-cooldown offensive/defensive button, not just "is one ready."
        bool targetIsBossOrElite = false;
        float targetHpPct = 100.0f;
        // Boss/elite/PvP target, or a real pack (nearbyEnemyCount>=3), and not already about to
        // die -- see ActionEvaluator::ScoreAbility's OffensiveCD gating.
        bool worthOffensiveCooldown = false;

        // Defensive prediction (item 19, Phase 2) -- a cheap rolling incoming-damage-per-second
        // estimate for the bot itself (see DamageTracker), and whether the bot's actual
        // situation right now (not just "HP below a fixed threshold") justifies a defensive
        // cooldown: taking real incoming damage, or the victim is mid-cast on something
        // dangerous while the bot is its target.
        float botIncomingDps = 0.0f;
        bool worthDefensiveCooldown = false;

        // Metadata
        uint8 classId = 0;
        uint32 activeSpec = 0;
        BotRole role = BotRole::Dps;
        uint32 currentMSTime = 0;

        // Builds a fresh CombatContext snapshot for this AI tick
        static CombatContext Build(Player* bot, Unit* explicitVictim = nullptr);
    };
}

#endif // COA_PLAYERBOTS_COMBAT_CONTEXT_H
