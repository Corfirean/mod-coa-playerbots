/*
 * mod-coa-playerbots
 *
 * Tactical Battleground AI for CoA Companions.
 * Implements objective-driven behaviors, base capturing, flag running,
 * proactive PvP target acquisition, and travel mounting for Eye of the Storm,
 * Warsong Gulch, Arathi Basin, and generic battlegrounds/arenas.
 */

#ifndef _BOT_BATTLEGROUND_AI_H
#define _BOT_BATTLEGROUND_AI_H

#include "Common.h"
#include "ObjectGuid.h"

class Player;
class Unit;
class Battleground;
class BattlegroundEY;
class BattlegroundWS;
class BattlegroundAB;
class GameObject;

class BotBattlegroundAI
{
public:
    // Main update loop for bots in a battleground.
    // Returns true if Battleground AI handled movement or objective actions this tick.
    static bool Update(Player* bot, uint32 diff);

    // Proactively scans for hostile enemy players within range,
    // prioritizing flag carriers, healers, and low-health targets.
    static Unit* FindHostilePvPTarget(Player* bot, float maxRange = 40.0f);

    // Cleans up any state held for this bot guid
    static void Forget(ObjectGuid botGuid);

private:
    static void HandleEyeOfTheStorm(Player* bot, BattlegroundEY* bg, uint32 diff);
    static void HandleWarsongGulch(Player* bot, BattlegroundWS* bg, uint32 diff);
    static void HandleArathiBasin(Player* bot, BattlegroundAB* bg, uint32 diff);
    static void HandleGenericBattleground(Player* bot, Battleground* bg, uint32 diff);

    static bool TryMountForTravel(Player* bot);
    static bool TryInteractWithBGObject(Player* bot, GameObject* go);
    static void MoveToPoint(Player* bot, float x, float y, float z);
};

#endif // _BOT_BATTLEGROUND_AI_H
