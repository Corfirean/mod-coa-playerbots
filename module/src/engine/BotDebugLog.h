/*
 * mod-coa-playerbots
 *
 * Per-bot debug logging for the 21-bot class-fleet debugging exercise (BotSpawnRandom.h's
 * SpawnClassFleet). Every other bot on the server keeps sharing the normal "module.coa-playerbots"
 * logger at its normal level; only the handful of GUIDs SpawnClassFleet registers here get routed
 * to their own dedicated logger/file, so this is safe to leave wired into hot call sites
 * year-round without touching production log volume.
 *
 * Pair each registered logger name with an Appender+Logger block in worldserver.conf (see
 * docs -- 21 explicit "Logger.module.coa-playerbots.fleet.botN=..." entries, one per slot).
 * An unregistered name simply falls through AzerothCore's normal dotted-hierarchy logger
 * fallback to "module.coa-playerbots", so nothing needs a config entry to keep working.
 */

#ifndef COA_PLAYERBOTS_BOT_DEBUG_LOG_H
#define COA_PLAYERBOTS_BOT_DEBUG_LOG_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>

namespace BotAI::BotDebugLog
{
    // Highest fleet slot supported by the worldserver.conf appender/logger blocks.
    constexpr uint8 MAX_FLEET_SLOTS = 21;

    // Clears every registered fleet bot -- call once at the start of SpawnClassFleet so a
    // re-run (after `.botcmd purgeall`) doesn't keep logging old, now-deleted GUIDs under a slot
    // a new bot is about to reuse.
    void Clear();

    // Registers guid under fleet slot (1-MAX_FLEET_SLOTS). Out-of-range slots are ignored.
    void Register(ObjectGuid guid, uint8 slot);

    bool IsFleetBot(ObjectGuid guid);

    // Returns "module.coa-playerbots.fleet.bot<N>" for a registered fleet bot, else the shared
    // "module.coa-playerbots" logger every other call site already uses. Safe to call
    // unconditionally at any existing LOG_* call site -- no guard needed.
    std::string const& LoggerName(ObjectGuid guid);
}

#endif // COA_PLAYERBOTS_BOT_DEBUG_LOG_H
