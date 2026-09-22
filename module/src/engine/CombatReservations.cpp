/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatReservations implementation
 */

#include "engine/CombatReservations.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "Timer.h"
#include <algorithm>
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

    namespace
    {
        // Grace window (item 2, Phase 2 fixup) absorbing the latency between "cast finishes" and
        // "the target's health has actually updated," plus a floor so even an instant heal's
        // reservation survives long enough for a sibling healer evaluated moments later this same
        // tick to see it. Scales a little with the cast itself so a long cast-time heal isn't
        // held to the same tight grace as an instant one.
        constexpr uint32 HEAL_RESERVATION_MIN_GRACE_MS = 300;
        constexpr uint32 HEAL_RESERVATION_HARD_CEILING_MS = 10000;

        // True while `reservation` is still genuinely in flight -- the primary liveness check is
        // "is this healer still actually mid-cast on this exact spell," not just a fixed TTL (see
        // this file's header comment). Falls back to the hard ceiling if the healer can't be
        // resolved at all (shouldn't normally happen, but a despawned/unloaded bot must not pin a
        // reservation alive forever).
        bool IsHealReservationLive(HealReservation const& reservation, uint32 now)
        {
            if (now >= reservation.expiresAt)
                return false;

            Player* healerPlayer = ObjectAccessor::FindPlayer(reservation.healerGuid);
            if (!healerPlayer || !healerPlayer->IsInWorld() || !healerPlayer->IsAlive())
                return false;

            // Give the tick the reservation was created on a pass -- an instant heal's CastSpell
            // call may already show no CurrentSpell by the time a sibling healer reads this in
            // the same tick, and that's not "the cast resolved," it's "it was instant."
            if (now < reservation.castStartedAt + 50)
                return true;

            Spell const* current = healerPlayer->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            return current && current->GetSpellInfo() && current->GetSpellInfo()->Id == reservation.spellId;
        }
    }

    uint32 CombatReservations::GetReservedIncomingHeal(ObjectGuid targetGuid, ObjectGuid excludingHealer)
    {
        uint32 now = getMSTime();
        uint32 total = 0;
        for (auto itr = s_healReservations.begin(); itr != s_healReservations.end();)
        {
            if (!IsHealReservationLive(itr->second, now))
            {
                itr = s_healReservations.erase(itr);
                continue;
            }

            if (itr->first != excludingHealer && itr->second.targetGuid == targetGuid)
                total += itr->second.expectedHeal;
            ++itr;
        }
        return total;
    }

    void CombatReservations::ReserveHeal(ObjectGuid healerGuid, ObjectGuid targetGuid, uint32 spellId, uint32 expectedHeal, uint32 castTimeMs)
    {
        uint32 now = getMSTime();
        uint32 landingAt = now + castTimeMs;
        uint32 grace = std::max(HEAL_RESERVATION_MIN_GRACE_MS, castTimeMs / 4);
        uint32 expiresAt = std::min(landingAt + grace, now + HEAL_RESERVATION_HARD_CEILING_MS);
        s_healReservations[healerGuid] = HealReservation{ healerGuid, targetGuid, spellId, expectedHeal, now, landingAt, expiresAt };
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
