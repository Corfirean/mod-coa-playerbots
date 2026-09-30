/*
 * mod-coa-playerbots
 *
 * Boss Knowledge Layer: "what can this boss creature entry cast, and under what condition"
 * built from data the server already owns, not imported/authored per encounter.
 *
 * A provider chain, not a single query -- SmartAI (smart_scripts) is a real but PARTIAL source:
 * plenty of AzerothCore encounters are implemented directly in C++ BossAI/EventMap with no
 * smart_scripts rows at all, and a design assuming SmartAI covers every boss would go silent on
 * those. See GetKnownAbilities' own comment for the exact provider order.
 *
 * Consumed by BotAvoidance to let a bot pre-position or hold a cooldown a beat before a known
 * HP-threshold-triggered cast, instead of only reacting once the boss's live GetCurrentSpell()
 * already shows it in progress. A boss with no coverage from either provider is no worse off
 * than before this existed -- callers fall back to the same purely reactive behavior.
 */

#ifndef COA_PLAYERBOTS_BOT_BOSS_KNOWLEDGE_H
#define COA_PLAYERBOTS_BOT_BOSS_KNOWLEDGE_H

#include "Define.h"
#include <vector>

namespace BotBossKnowledge
{
    // One ability a boss creature entry is known to be able to cast, with whatever trigger
    // condition was recoverable. `hasHpWindow` is only meaningful when true (found via SmartAI's
    // SMART_EVENT_HEALTH_PCT, i.e. a real phase-transition trigger on the boss's OWN health);
    // an ability found via a timed action list, or with no resolvable trigger, just means "this
    // boss can cast this at some point," not gated to a specific phase.
    struct KnownAbility
    {
        uint32 spellId = 0;
        bool hasHpWindow = false;
        float hpMinPct = 0.0f;
        float hpMaxPct = 100.0f;
    };

    // Everything currently known about `creatureEntry`'s cast-capable abilities. Provider order:
    // 1. SmartAI (smart_scripts) -- direct cast actions (SMART_ACTION_CAST/SELF_CAST/CROSS_CAST/
    //    INVOKER_CAST) plus casts reached only through a SMART_ACTION_CALL_TIMED_ACTIONLIST
    //    (source_type=9 rows), with a single-hop SMART_EVENT_LINK resolution so a cast row that's
    //    only reached via another row's `link` still picks up that other row's real trigger.
    // 2. A small static fallback table (empty today -- populated per-boss as gaps are actually
    //    found live, not as an attempt at upfront coverage) for encounters implemented directly
    //    in C++ with no smart_scripts rows to read at all.
    // 3. Neither has anything: returns an empty vector.
    // Cached forever per creature entry once computed -- this is static server content, nothing
    // here needs to be invalidated at runtime.
    std::vector<KnownAbility> const& GetKnownAbilities(uint32 creatureEntry);
}

#endif // COA_PLAYERBOTS_BOT_BOSS_KNOWLEDGE_H
