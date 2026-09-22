/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatReservations implementation
 */

#include "engine/CombatReservations.h"
#include "Timer.h"
#include <unordered_map>

namespace BotAI
{
    namespace
    {
        // [enemyGuid] -> live reservation. One reservation per enemy is enough for the common
        // case this solves (a group focusing one target whose cast several bots could kick) --
        // see this file's header comment on why a full per-encounter table isn't needed yet.
        std::unordered_map<ObjectGuid, InterruptReservation> s_interruptReservations;

        // [healerGuid] -> that healer's one pending reservation. Keyed by healer (not target)
        // since a healer only ever has one cast in flight at a time; summing across all entries
        // for a given targetGuid is what tells HealEvaluator how much incoming heal a candidate
        // already has reserved from other healers.
        std::unordered_map<ObjectGuid, HealReservation> s_healReservations;
    }

    bool CombatReservations::IsInterruptReserved(ObjectGuid enemyGuid, ObjectGuid askingBot)
    {
        auto itr = s_interruptReservations.find(enemyGuid);
        if (itr == s_interruptReservations.end())
            return false;

        if (itr->second.botGuid == askingBot)
            return false; // never blocks the bot that holds it

        if (getMSTime() >= itr->second.expiresAt)
        {
            s_interruptReservations.erase(itr);
            return false;
        }

        return true;
    }

    bool CombatReservations::TryReserveInterrupt(ObjectGuid enemyGuid, uint32 enemySpellId, ObjectGuid botGuid, uint32 durationMs)
    {
        auto itr = s_interruptReservations.find(enemyGuid);
        if (itr != s_interruptReservations.end() && itr->second.botGuid != botGuid && getMSTime() < itr->second.expiresAt)
            return false; // a different bot already has this one

        s_interruptReservations[enemyGuid] = InterruptReservation{ enemyGuid, enemySpellId, botGuid, getMSTime() + durationMs };
        return true;
    }

    void CombatReservations::ClearInterruptReservation(ObjectGuid enemyGuid)
    {
        s_interruptReservations.erase(enemyGuid);
    }

    void CombatReservations::ReconcileInterruptReservation(ObjectGuid enemyGuid, uint32 currentCastSpellId)
    {
        auto itr = s_interruptReservations.find(enemyGuid);
        if (itr == s_interruptReservations.end())
            return;

        // The reserved cast already resolved (interrupted, finished, or the enemy moved on to a
        // different cast) -- drop it now instead of waiting out the timeout, so a fresh cast from
        // the same enemy isn't blocked by a stale reservation.
        if (currentCastSpellId == 0 || currentCastSpellId != itr->second.spellId)
            s_interruptReservations.erase(itr);
    }

    uint32 CombatReservations::GetReservedIncomingHeal(ObjectGuid targetGuid, ObjectGuid excludingHealer)
    {
        uint32 now = getMSTime();
        uint32 total = 0;
        for (auto const& [healerGuid, reservation] : s_healReservations)
        {
            if (healerGuid == excludingHealer)
                continue;
            if (reservation.targetGuid != targetGuid)
                continue;
            if (now >= reservation.expiresAt)
                continue;
            total += reservation.expectedHeal;
        }
        return total;
    }

    void CombatReservations::ReserveHeal(ObjectGuid healerGuid, ObjectGuid targetGuid, uint32 spellId, uint32 expectedHeal, uint32 durationMs)
    {
        s_healReservations[healerGuid] = HealReservation{ healerGuid, targetGuid, spellId, expectedHeal, getMSTime() + durationMs };
    }

    void CombatReservations::ClearHealReservation(ObjectGuid healerGuid)
    {
        s_healReservations.erase(healerGuid);
    }

    void CombatReservations::ForgetBot(ObjectGuid botGuid)
    {
        for (auto itr = s_interruptReservations.begin(); itr != s_interruptReservations.end();)
        {
            if (itr->second.botGuid == botGuid)
                itr = s_interruptReservations.erase(itr);
            else
                ++itr;
        }
        s_healReservations.erase(botGuid);
    }
}
