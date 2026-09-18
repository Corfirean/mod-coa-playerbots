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
        bool victimTargetingNonTank = false;
        uint8 nearbyEnemyCount = 0;   // Hostile units within 8 yards

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
