/*
 * mod-coa-playerbots
 *
 * Tactical Battleground AI for CoA Companions.
 * Implements objective-driven behaviors, base capturing, flag running,
 * proactive PvP target acquisition, and travel mounting for Eye of the Storm,
 * Warsong Gulch, Arathi Basin, and generic battlegrounds/arenas.
 */

#include "BotBattlegroundAI.h"
#include "Battleground.h"
#include "BattlegroundAB.h"
#include "BattlegroundEY.h"
#include "BattlegroundWS.h"
#include "CellImpl.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "GameObject.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Log.h"
#include <cmath>
#include <unordered_map>

namespace
{
struct BotBGState
{
    uint32 leaveTimerMs = 0;
    uint32 targetObjectiveNode = 0;
    uint32 objectiveSwitchCooldownMs = 0;
    uint32 scanCooldownMs = 0;
};

static std::unordered_map<ObjectGuid, BotBGState> s_bgStates;

uint32 GetRacialGroundMount(Player* bot)
{
    switch (bot->getRace())
    {
        case RACE_HUMAN:         return 470;   // Pinto Horse
        case RACE_ORC:           return 6653;  // Brown Wolf
        case RACE_DWARF:         return 6898;  // Brown Ram
        case RACE_NIGHTELF:      return 8394;  // Striped Frostsaber
        case RACE_UNDEAD_PLAYER: return 64977; // Black Skeletal Horse
        case RACE_TAUREN:        return 18989; // Gray Kodo
        case RACE_GNOME:         return 10969; // Blue Mechanostrider
        case RACE_TROLL:         return 10796; // Turquoise Raptor
        case RACE_BLOODELF:      return 35018; // Red Hawkstrider
        case RACE_DRAENEI:       return 34406; // Brown Elekk
        default:                 return 0;
    }
}
}

void BotBattlegroundAI::Forget(ObjectGuid botGuid)
{
    s_bgStates.erase(botGuid);
}

void BotBattlegroundAI::MoveToPoint(Player* bot, float x, float y, float z)
{
    if (!bot || bot->IsNonMeleeSpellCast(false))
        return;

    float groundZ = z;
    if (!bot->CanFly())
        bot->UpdateAllowedPositionZ(x, y, groundZ);
    if (groundZ <= INVALID_HEIGHT)
        groundZ = z;

    float dist2d = bot->GetDistance2d(x, y);
    if (dist2d > 3.0f)
    {
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() != POINT_MOTION_TYPE ||
            bot->GetExactDist2d(x, y) > 6.0f)
        {
            bot->GetMotionMaster()->MovePoint(0, x, y, groundZ);
        }
    }
    else
    {
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();
    }
}

bool BotBattlegroundAI::TryMountForTravel(Player* bot)
{
    if (!bot || !bot->IsInWorld() || !bot->IsAlive())
        return false;

    if (bot->IsMounted())
        return true;

    if (bot->IsInCombat() || bot->IsNonMeleeSpellCast(false) || !bot->IsOutdoors() || bot->GetLevel() < 20)
        return false;

    uint32 racialMount = GetRacialGroundMount(bot);
    if (racialMount)
    {
        if (!bot->HasSpell(racialMount))
            bot->learnSpell(racialMount);

        bot->CastSpell(bot, racialMount, false);
        return true;
    }

    for (auto const& [spellId, playerSpell] : bot->GetSpellMap())
    {
        if (!playerSpell || playerSpell->State == PLAYERSPELL_REMOVED || !playerSpell->Active)
            continue;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (!spellInfo || spellInfo->IsPassive() || spellInfo->GetMaxDuration() != -1)
            continue;
        if (!spellInfo->HasAura(SPELL_AURA_MOUNTED))
            continue;
        if (spellInfo->HasAura(SPELL_AURA_FLY) || spellInfo->HasAura(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED))
            continue;

        bot->CastSpell(bot, spellId, false);
        return true;
    }

    return false;
}

bool BotBattlegroundAI::TryInteractWithBGObject(Player* bot, GameObject* go)
{
    if (!bot || !go || !go->isSpawned())
        return false;

    if (bot->GetDistance(go) > 10.0f)
        return false;

    if (bot->IsMounted())
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);

    go->Use(bot);
    return true;
}

Unit* BotBattlegroundAI::FindHostilePvPTarget(Player* bot, float maxRange)
{
    if (!bot || !bot->IsInWorld() || !bot->IsAlive())
        return nullptr;

    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return nullptr;

    Unit* bestTarget = nullptr;
    float bestScore = -99999.0f;

    Battleground::BattlegroundPlayerMap const& players = bg->GetPlayers();
    for (auto const& [guid, player] : players)
    {
        if (!player || player == bot || !player->IsAlive() || !player->IsInWorld())
            continue;

        if (player->GetTeamId() == bot->GetTeamId())
            continue;

        if (!player->IsInMap(bot) || !player->InSamePhase(bot))
            continue;

        float dist = bot->GetDistance(player);
        if (dist > maxRange)
            continue;

        if (!bot->IsValidAttackTarget(player))
            continue;

        if (!bot->IsWithinLOSInMap(player))
            continue;

        float score = (maxRange - dist) * 2.0f;

        // Flag carriers are highest priority
        if (player->HasAura(BG_WS_SPELL_WARSONG_FLAG) ||
            player->HasAura(BG_WS_SPELL_SILVERWING_FLAG) ||
            player->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL))
        {
            score += 250.0f;
        }

        // Low health execute
        if (player->GetHealthPct() < 35.0f)
            score += 70.0f;

        // Spell casting targets (interruptible)
        if (player->IsNonMeleeSpellCast(false))
            score += 35.0f;

        // Target currently attacking self
        if (player->GetVictim() == bot)
            score += 45.0f;

        if (score > bestScore)
        {
            bestScore = score;
            bestTarget = player;
        }
    }

    return bestTarget;
}

bool BotBattlegroundAI::Update(Player* bot, uint32 diff)
{
    if (!bot || !bot->IsInWorld() || !bot->IsAlive())
        return false;

    Battleground* bg = bot->GetBattleground();
    if (!bg)
        return false;

    BotBGState& state = s_bgStates[bot->GetGUID()];
    BattlegroundStatus status = bg->GetStatus();

    if (status == STATUS_WAIT_JOIN)
    {
        // Match preparation phase: gates are closed! Stay in spawn area and clear movement
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();
        return true;
    }

    if (status == STATUS_WAIT_LEAVE)
    {
        // Match ended: leave battleground after a short delay
        state.leaveTimerMs += diff;
        if (state.leaveTimerMs > 6000)
        {
            bot->LeaveBattleground();
            state.leaveTimerMs = 0;
        }
        return true;
    }

    if (status != STATUS_IN_PROGRESS)
        return false;

    // 1. Proactive Enemy Combat Acquisition
    Unit* pvpTarget = FindHostilePvPTarget(bot, 40.0f);
    if (pvpTarget)
    {
        if (bot->IsMounted())
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);

        if (bot->GetVictim() != pvpTarget)
            bot->Attack(pvpTarget, true);

        return false; // let normal combat rotation handle the fight
    }

    // If bot already has a valid living victim, let combat run
    if (bot->GetVictim() && bot->GetVictim()->IsAlive() && bot->IsValidAttackTarget(bot->GetVictim()))
    {
        if (bot->IsMounted())
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        return false;
    }

    // 2. Out of Combat / No Target -> Execute Battleground Objectives
    switch (bg->GetBgTypeID(true))
    {
        case BATTLEGROUND_EY:
            if (BattlegroundEY* ey = bg->ToBattlegroundEY())
                HandleEyeOfTheStorm(bot, ey, diff);
            break;
        case BATTLEGROUND_WS:
            if (BattlegroundWS* ws = bg->ToBattlegroundWS())
                HandleWarsongGulch(bot, ws, diff);
            break;
        case BATTLEGROUND_AB:
            if (BattlegroundAB* ab = bg->ToBattlegroundAB())
                HandleArathiBasin(bot, ab, diff);
            break;
        default:
            HandleGenericBattleground(bot, bg, diff);
            break;
    }

    return true;
}

void BotBattlegroundAI::HandleEyeOfTheStorm(Player* bot, BattlegroundEY* bg, uint32 diff)
{
    BotBGState& state = s_bgStates[bot->GetGUID()];
    if (state.objectiveSwitchCooldownMs > diff)
        state.objectiveSwitchCooldownMs -= diff;
    else
        state.objectiveSwitchCooldownMs = 0;

    bool hasFlag = (bg->GetFlagPickerGUID() == bot->GetGUID()) || bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL);
    if (hasFlag)
    {
        // Flag carrier: run to the nearest friendly-controlled tower to score!
        int bestTower = -1;
        float bestTowerDist = 99999.0f;

        for (uint8 p = 0; p < EY_POINTS_MAX; ++p)
        {
            if (bg->GetCapturePointInfo(p).IsUnderControl(bot->GetTeamId()))
            {
                float d = bot->GetDistance2d(BG_EY_TriggerPositions[p][0], BG_EY_TriggerPositions[p][1]);
                if (d < bestTowerDist)
                {
                    bestTowerDist = d;
                    bestTower = p;
                }
            }
        }

        if (bestTower >= 0)
        {
            float targetX = BG_EY_TriggerPositions[bestTower][0];
            float targetY = BG_EY_TriggerPositions[bestTower][1];
            float targetZ = BG_EY_TriggerPositions[bestTower][2];

            if (bestTowerDist > 30.0f)
                TryMountForTravel(bot);

            MoveToPoint(bot, targetX, targetY, targetZ);
            return;
        }
        else
        {
            // No tower controlled yet: push natural base to assist team capture
            uint8 fallbackTower = (bot->GetTeamId() == TEAM_ALLIANCE) ? POINT_MAGE_TOWER : POINT_FEL_REAVER;
            float targetX = BG_EY_TriggerPositions[fallbackTower][0];
            float targetY = BG_EY_TriggerPositions[fallbackTower][1];
            float targetZ = BG_EY_TriggerPositions[fallbackTower][2];

            MoveToPoint(bot, targetX, targetY, targetZ);
            return;
        }
    }

    // Netherstorm Flag at center (x=2174.0f, y=1569.0f, z=1160.0f)
    if (bg->GetFlagPickerGUID().IsEmpty())
    {
        bool contestFlag = (bot->GetGUID().GetCounter() % 3 == 0);
        float distToCenter = bot->GetDistance2d(2174.0f, 1569.0f);

        if (contestFlag || distToCenter < 50.0f)
        {
            if (distToCenter > 30.0f)
                TryMountForTravel(bot);

            if (distToCenter <= 10.0f)
            {
                GameObject* flagGO = bg->GetBGObject(BG_EY_OBJECT_FLAG_NETHERSTORM);
                if (flagGO && flagGO->isSpawned())
                {
                    TryInteractWithBGObject(bot, flagGO);
                    return;
                }
            }

            MoveToPoint(bot, 2174.0f, 1569.0f, 1160.0f);
            return;
        }
    }

    // Tower assignment
    if (state.objectiveSwitchCooldownMs == 0)
    {
        state.objectiveSwitchCooldownMs = 12000;
        uint32 seed = bot->GetGUID().GetCounter();
        if (bot->GetTeamId() == TEAM_ALLIANCE)
        {
            static const uint8 allyNodes[] = { POINT_MAGE_TOWER, POINT_DRAENEI_RUINS, POINT_BLOOD_ELF, POINT_FEL_REAVER };
            state.targetObjectiveNode = allyNodes[seed % 4];
        }
        else
        {
            static const uint8 hordeNodes[] = { POINT_FEL_REAVER, POINT_BLOOD_ELF, POINT_DRAENEI_RUINS, POINT_MAGE_TOWER };
            state.targetObjectiveNode = hordeNodes[seed % 4];
        }
    }

    uint8 targetPoint = state.targetObjectiveNode % EY_POINTS_MAX;
    float posX = BG_EY_TriggerPositions[targetPoint][0];
    float posY = BG_EY_TriggerPositions[targetPoint][1];
    float posZ = BG_EY_TriggerPositions[targetPoint][2];

    float distToPoint = bot->GetDistance2d(posX, posY);
    if (distToPoint > 30.0f)
        TryMountForTravel(bot);

    if (distToPoint > 15.0f)
    {
        MoveToPoint(bot, posX, posY, posZ);
    }
    else
    {
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();
    }
}

void BotBattlegroundAI::HandleWarsongGulch(Player* bot, BattlegroundWS* bg, uint32 /*diff*/)
{
    TeamId myTeam = bot->GetTeamId();
    TeamId enemyTeam = (myTeam == TEAM_ALLIANCE) ? TEAM_HORDE : TEAM_ALLIANCE;

    constexpr float A_FLAG_X = 1515.0f, A_FLAG_Y = 1473.0f, A_FLAG_Z = 352.0f;
    constexpr float H_FLAG_X = 933.0f,  H_FLAG_Y = 1433.0f, H_FLAG_Z = 345.0f;

    float homeX = (myTeam == TEAM_ALLIANCE) ? A_FLAG_X : H_FLAG_X;
    float homeY = (myTeam == TEAM_ALLIANCE) ? A_FLAG_Y : H_FLAG_Y;
    float homeZ = (myTeam == TEAM_ALLIANCE) ? A_FLAG_Z : H_FLAG_Z;

    float enemyX = (myTeam == TEAM_ALLIANCE) ? H_FLAG_X : A_FLAG_X;
    float enemyY = (myTeam == TEAM_ALLIANCE) ? H_FLAG_Y : A_FLAG_Y;
    float enemyZ = (myTeam == TEAM_ALLIANCE) ? H_FLAG_Z : A_FLAG_Z;

    bool isCarrier = (bg->GetFlagPickerGUID(TEAM_ALLIANCE) == bot->GetGUID()) ||
                     (bg->GetFlagPickerGUID(TEAM_HORDE) == bot->GetGUID()) ||
                     bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) ||
                     bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG);

    // 1. Bot is flag carrier: run to friendly flag room
    if (isCarrier)
    {
        float distHome = bot->GetDistance2d(homeX, homeY);
        if (distHome > 5.0f)
            MoveToPoint(bot, homeX, homeY, homeZ);
        else
        {
            if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
                bot->GetMotionMaster()->Clear();
        }
        return;
    }

    // 2. Friendly flag dropped on ground nearby: return it!
    if (bg->GetFlagState(myTeam) == BG_WS_FLAG_STATE_ON_GROUND)
    {
        uint32 flagObjType = (myTeam == TEAM_ALLIANCE) ? BG_WS_OBJECT_A_FLAG : BG_WS_OBJECT_H_FLAG;
        if (GameObject* droppedFlag = bg->GetBGObject(flagObjType))
        {
            float dist = bot->GetDistance(droppedFlag);
            if (dist < 50.0f)
            {
                if (dist > 25.0f)
                    TryMountForTravel(bot);

                if (dist <= 8.0f)
                {
                    TryInteractWithBGObject(bot, droppedFlag);
                    return;
                }

                MoveToPoint(bot, droppedFlag->GetPositionX(), droppedFlag->GetPositionY(), droppedFlag->GetPositionZ());
                return;
            }
        }
    }

    // 3. Enemy carrier active: hunt them down
    if (bg->GetFlagState(myTeam) == BG_WS_FLAG_STATE_ON_PLAYER)
    {
        ObjectGuid enemyCarrierGuid = bg->GetFlagPickerGUID(myTeam);
        if (Player* carrier = ObjectAccessor::FindPlayer(enemyCarrierGuid))
        {
            float distToCarrier = bot->GetDistance(carrier);
            if (distToCarrier > 30.0f)
                TryMountForTravel(bot);

            MoveToPoint(bot, carrier->GetPositionX(), carrier->GetPositionY(), carrier->GetPositionZ());
            return;
        }
    }

    // 4. Enemy flag at base: assault enemy flag room and grab it
    if (bg->GetFlagState(enemyTeam) == BG_WS_FLAG_STATE_ON_BASE)
    {
        float distToEnemyFlag = bot->GetDistance2d(enemyX, enemyY);
        if (distToEnemyFlag > 30.0f)
            TryMountForTravel(bot);

        if (distToEnemyFlag <= 8.0f)
        {
            uint32 enemyFlagObjType = (enemyTeam == TEAM_ALLIANCE) ? BG_WS_OBJECT_A_FLAG : BG_WS_OBJECT_H_FLAG;
            if (GameObject* flagGO = bg->GetBGObject(enemyFlagObjType))
            {
                TryInteractWithBGObject(bot, flagGO);
                return;
            }
        }

        MoveToPoint(bot, enemyX, enemyY, enemyZ);
        return;
    }

    // 5. Friendly carrier active: escort friendly carrier
    if (bg->GetFlagState(enemyTeam) == BG_WS_FLAG_STATE_ON_PLAYER)
    {
        ObjectGuid friendlyCarrierGuid = bg->GetFlagPickerGUID(enemyTeam);
        if (Player* carrier = ObjectAccessor::FindPlayer(friendlyCarrierGuid))
        {
            float distToCarrier = bot->GetDistance(carrier);
            if (distToCarrier > 30.0f)
                TryMountForTravel(bot);

            MoveToPoint(bot, carrier->GetPositionX(), carrier->GetPositionY(), carrier->GetPositionZ());
            return;
        }
    }

    // Default: patrol mid field
    float distMid = bot->GetDistance2d(1224.0f, 1453.0f);
    if (distMid > 25.0f)
    {
        TryMountForTravel(bot);
        MoveToPoint(bot, 1224.0f, 1453.0f, 335.0f);
    }
}

void BotBattlegroundAI::HandleArathiBasin(Player* bot, BattlegroundAB* bg, uint32 diff)
{
    BotBGState& state = s_bgStates[bot->GetGUID()];
    if (state.objectiveSwitchCooldownMs > diff)
        state.objectiveSwitchCooldownMs -= diff;
    else
        state.objectiveSwitchCooldownMs = 0;

    TeamId myTeam = bot->GetTeamId();

    if (state.objectiveSwitchCooldownMs == 0)
    {
        state.objectiveSwitchCooldownMs = 15000;

        // Preferred order per team
        static const uint8 allyOrder[] = { BG_AB_NODE_STABLES, BG_AB_NODE_BLACKSMITH, BG_AB_NODE_LUMBER_MILL, BG_AB_NODE_GOLD_MINE, BG_AB_NODE_FARM };
        static const uint8 hordeOrder[] = { BG_AB_NODE_FARM, BG_AB_NODE_BLACKSMITH, BG_AB_NODE_GOLD_MINE, BG_AB_NODE_LUMBER_MILL, BG_AB_NODE_STABLES };

        uint8 const* prefOrder = (myTeam == TEAM_ALLIANCE) ? allyOrder : hordeOrder;
        uint32 seed = bot->GetGUID().GetCounter();

        // Select a base that is not currently secured by our team
        uint8 chosen = prefOrder[seed % BG_AB_DYNAMIC_NODES_COUNT];
        for (uint8 i = 0; i < BG_AB_DYNAMIC_NODES_COUNT; ++i)
        {
            uint8 candidate = prefOrder[(seed + i) % BG_AB_DYNAMIC_NODES_COUNT];
            if (bg->GetCapturePointInfo(candidate)._ownerTeamId != myTeam)
            {
                chosen = candidate;
                break;
            }
        }
        state.targetObjectiveNode = chosen;
    }

    uint8 node = state.targetObjectiveNode % BG_AB_DYNAMIC_NODES_COUNT;
    float posX = BG_AB_NodePositions[node][0];
    float posY = BG_AB_NodePositions[node][1];
    float posZ = BG_AB_NodePositions[node][2];

    float dist = bot->GetDistance2d(posX, posY);
    if (dist > 30.0f)
        TryMountForTravel(bot);

    if (dist <= 8.0f)
    {
        // Try interacting with banner if neutral or enemy contested
        if (bg->GetCapturePointInfo(node)._ownerTeamId != myTeam)
        {
            for (uint32 obj = BG_AB_OBJECT_BANNER_NEUTRAL; obj < static_cast<uint8>(BG_AB_DYNAMIC_NODES_COUNT) * BG_AB_OBJECTS_PER_NODE; ++obj)
            {
                if (GameObject* banner = bg->GetBGObject(obj))
                {
                    if (banner->isSpawned() && bot->GetDistance(banner) <= 8.0f)
                    {
                        TryInteractWithBGObject(bot, banner);
                        break;
                    }
                }
            }
        }

        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
            bot->GetMotionMaster()->Clear();
        return;
    }

    MoveToPoint(bot, posX, posY, posZ);
}

void BotBattlegroundAI::HandleGenericBattleground(Player* bot, Battleground* bg, uint32 /*diff*/)
{
    // Search for closest enemy player anywhere on the map
    Player* closestEnemy = nullptr;
    float closestDist = 99999.0f;

    Battleground::BattlegroundPlayerMap const& players = bg->GetPlayers();
    for (auto const& [guid, player] : players)
    {
        if (!player || player == bot || !player->IsAlive() || !player->IsInWorld())
            continue;

        if (player->GetTeamId() == bot->GetTeamId())
            continue;

        if (!player->IsInMap(bot) || !player->InSamePhase(bot))
            continue;

        float dist = bot->GetDistance(player);
        if (dist < closestDist)
        {
            closestDist = dist;
            closestEnemy = player;
        }
    }

    if (closestEnemy)
    {
        if (closestDist > 30.0f)
            TryMountForTravel(bot);

        MoveToPoint(bot, closestEnemy->GetPositionX(), closestEnemy->GetPositionY(), closestEnemy->GetPositionZ());
    }
}
