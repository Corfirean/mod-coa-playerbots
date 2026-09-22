/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: HealEvaluator (item 10/12, Phase 2)
 *
 * Healing leaned too hard on lowest-HP%. A tank at 55% taking heavy incoming damage can matter
 * more than a DPS at 30% nobody's still hitting -- this scores every candidate on missing HP,
 * incoming-damage trend (see DamageTracker), role, whether an enemy is actively on them, and
 * already-reserved incoming heals from other healers (see CombatReservations), instead of just
 * picking the lowest percentage. Also answers item 12's "don't force a fixed 25yd approach
 * distance" via BestKnownHealRange, so positioning can use the healer's own actual spell range.
 */

#ifndef COA_PLAYERBOTS_HEAL_EVALUATOR_H
#define COA_PLAYERBOTS_HEAL_EVALUATOR_H

#include "Define.h"
#include "ObjectGuid.h"

class Player;

namespace BotAI
{
    class HealEvaluator
    {
    public:
        // Higher is better; -1 means "not a valid heal candidate at all" (dead, out of world,
        // absurdly far away).
        static float ScoreHealUrgency(Player* healer, Player* candidate);

        // Best heal target among the healer and its group within maxRange, or nullptr if nobody
        // is meaningfully hurt (mirrors the old FindHealTarget's implicit "must be below ~95%"
        // floor).
        static Player* SelectBestHealTarget(Player* healer, float maxRange = 60.0f);

        // Max range among the healer's own currently-known, usable heal-shaped spells (falls back
        // to `fallback` if it knows none) -- item 12: a healer with a 40yd heal shouldn't be
        // forced to approach to a fixed 25yd. Cached per-bot for a few seconds since this walks
        // the whole spellbook; cheap to call once per healer decision tick.
        static float BestKnownHealRange(Player* healer, float fallback = 35.0f);

        // Drops this bot's cached heal-range result -- called on despawn (see BotAI::Forget) so
        // the cache doesn't grow across repeated spawn/despawn cycles.
        static void ForgetBot(ObjectGuid botGuid);
    };
}

#endif // COA_PLAYERBOTS_HEAL_EVALUATOR_H
