/*
 * mod-coa-playerbots
 *
 * Tactical Battleground AI for CoA Companions.
 * Implements objective-driven behaviors, base capturing, flag running,
 * proactive PvP target acquisition, and travel mounting for Eye of the Storm,
 * Warsong Gulch, Arathi Basin, and generic battlegrounds/arenas.
 */

#include "BotBattlegroundAI.h"
#include "BotAI.h"
#include "BotBattlegroundRoutes.h"
#include "BotSpecStats.h"
#include "BotMovement.h"
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
#include "Opcodes.h"
#include "WorldSession.h"
#include "WorldPacket.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Config.h"
#include "Log.h"
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>

namespace
{
struct BotBGState
{
    uint32 leaveTimerMs = 0;
    uint32 targetObjectiveNode = 0;
    uint32 objectiveSwitchCooldownMs = 0;
    uint32 scanCooldownMs = 0;
    uint32 engageHoldMs = 0;        // stays in combat mode for a moment after the last enemy was seen
    uint32 buffCooldownMs = 0;      // no buff detours while this runs
    int8 assignedNode = -1;         // objective this bot is counted on (spreads the team over the nodes)
    uint32 assignedInstance = 0;
    uint8 assignedTeam = 0;
    bool defender = false;          // holds a node its team already owns
    std::string lastLogged;
    uint32 snapshotMs = 0;
    int32 routeIndex = -1;          // waypoint of a long march (Alterac Valley road)
};

static std::unordered_map<ObjectGuid, BotBGState> s_bgStates;

// (battleground instance, team, node) -> bots sent there, and the bot that stays to hold it. Without these every bot of a team
// picked the same "next unowned base" and left a captured base the moment it was won.
using NodeKey = std::tuple<uint32, uint8, uint8>;
static std::map<NodeKey, uint16> s_nodeCount;
static std::map<NodeKey, ObjectGuid> s_nodeDefender;

void ReleaseAssignment(Player* bot, BotBGState& state)
{
    if (state.assignedNode < 0)
        return;
    NodeKey const key(state.assignedInstance, state.assignedTeam, uint8(state.assignedNode));
    if (auto it = s_nodeCount.find(key); it != s_nodeCount.end() && it->second > 0)
        --it->second;
    if (auto it = s_nodeDefender.find(key); it != s_nodeDefender.end() && it->second == bot->GetGUID())
        s_nodeDefender.erase(it);
    state.assignedNode = -1;
    state.defender = false;
}

void AssignNode(Player* bot, BotBGState& state, Battleground* bg, uint8 node)
{
    if (state.assignedNode == int8(node) && state.assignedInstance == bg->GetInstanceID())
        return;
    ReleaseAssignment(bot, state);
    state.assignedNode = int8(node);
    state.assignedInstance = bg->GetInstanceID();
    state.assignedTeam = uint8(bot->GetTeamId());
    ++s_nodeCount[NodeKey(state.assignedInstance, state.assignedTeam, node)];
}

uint16 NodeLoad(Battleground* bg, TeamId team, uint8 node, BotBGState const& self)
{
    auto it = s_nodeCount.find(NodeKey(bg->GetInstanceID(), uint8(team), node));
    uint16 count = it == s_nodeCount.end() ? 0 : it->second;
    if (self.assignedNode == int8(node) && self.assignedInstance == bg->GetInstanceID() && count > 0)
        --count; // do not count myself against my own node
    return count;
}

bool NodeHasDefender(Battleground* bg, TeamId team, uint8 node, Player* self)
{
    auto it = s_nodeDefender.find(NodeKey(bg->GetInstanceID(), uint8(team), node));
    if (it == s_nodeDefender.end() || it->second == self->GetGUID())
        return false;
    Player* holder = ObjectAccessor::FindPlayer(it->second);
    return holder && holder->IsAlive() && holder->GetBattleground() == bg;
}

// CoaBots.BattlegroundAI.Log = 1 prints one line whenever a bot changes what it is doing (fight / objective / defender), for
// watching a match without a debugger. Off by default.
bool BgLogEnabled()
{
    static int cached = -1;
    if (cached < 0)
        cached = sConfigMgr->GetOption<bool>("CoaBots.BattlegroundAI.Log", false) ? 1 : 0;
    return cached == 1;
}

void BgLog(Player* bot, BotBGState& state, char const* what)
{
    if (!BgLogEnabled() || state.lastLogged == what)
        return;
    state.lastLogged = what;
    LOG_INFO("module.coa-playerbots.bg", "BG bot {} (class {} role {}) {}", bot->GetName(), uint32(bot->getClass()),
        uint32(BotAI::GetRole(bot->GetGUID())), what);
}

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
    auto it = s_bgStates.find(botGuid);
    if (it == s_bgStates.end())
        return;
    // the assignment tables are keyed by guid, so release through a lookup that only needs the guid
    BotBGState& state = it->second;
    if (state.assignedNode >= 0)
    {
        NodeKey const key(state.assignedInstance, state.assignedTeam, uint8(state.assignedNode));
        if (auto count = s_nodeCount.find(key); count != s_nodeCount.end() && count->second > 0)
            --count->second;
        if (auto def = s_nodeDefender.find(key); def != s_nodeDefender.end() && def->second == botGuid)
            s_nodeDefender.erase(def);
    }
    s_bgStates.erase(it);
}

void BotBattlegroundAI::MoveToPoint(Player* bot, float x, float y, float z)
{
    if (!bot || bot->IsNonMeleeSpellCast(false) || BotMovement::GetMountState(bot) == MountState::MountCasting)
        return;

    float groundZ = z;
    if (!bot->CanFly())
        bot->UpdateAllowedPositionZ(x, y, groundZ);
    if (groundZ <= INVALID_HEIGHT)
        groundZ = z;

    float dist2d = bot->GetDistance2d(x, y);
    if (dist2d > 3.0f)
    {
        BotMovement::MoveTo(bot, MoveOwner::Battleground, x, y, groundZ);
    }
    else
    {
        BotMovement::ForceReleaseOwner(bot, MoveOwner::Battleground);
    }
}

// Banner bases (Arathi Basin, Alterac Valley, Isle of Conquest) are taken by casting "Opening" (21651) on the banner; a plain
// GameObject::Use() on these goober-type banners does nothing, which is why the bots stood on them for minutes. Mounts and forms are dropped first,
// and the cast is not restarted while the bot is already channelling it. Stock playerbots work the same way.
static bool CaptureBanner(Player* bot, GameObject* banner)
{
    constexpr uint32 SPELL_OPENING_BANNER = 21651;
    if (!bot || !banner || !banner->isSpawned() || banner->GetGoState() != GO_STATE_READY || !bot->CanUseBattlegroundObject(banner))
        return false;
    for (CurrentSpellTypes type : { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL })
        if (Spell* current = bot->GetCurrentSpell(type))
            if (current->m_spellInfo && current->m_spellInfo->Id == SPELL_OPENING_BANNER)
                return true;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(SPELL_OPENING_BANNER);
    if (!info)
        return false;
    if (bot->IsMounted())
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
    if (bot->IsInDisallowedMountForm())
        bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
    if (bot->isMoving())
        bot->StopMoving();
    BotMovement::ForceReleaseOwner(bot, MoveOwner::Battleground);
    Spell* spell = new Spell(bot, info, TRIGGERED_NONE);
    spell->m_targets.SetGOTarget(banner);
    SpellCastResult const result = spell->prepare(&spell->m_targets);
    if (BgLogEnabled())
    {
        static uint32 logged = 0;
        if (logged++ < 80)
            LOG_INFO("module.coa-playerbots.bg", "BG bot {} casts Opening on banner entry {} type {} at {:.1f} yd: result {}", bot->GetName(),
                banner->GetEntry(), uint32(banner->GetGoType()), bot->GetDistance(banner), uint32(result));
    }
    return true;
}

// Scoring a flag (Warsong Gulch, Eye of the Storm) happens when the carrier enters an area trigger. A real client reports that on
// its own; a bot has no client, so the packet is built here.
static void ReportAreaTrigger(Player* bot, uint32 triggerId)
{
    WorldPacket data(CMSG_AREATRIGGER, 4);
    data << uint32(triggerId);
    bot->GetSession()->HandleAreaTriggerOpcode(data);
    if (BgLogEnabled())
    {
        static uint32 logged = 0;
        if (logged++ < 40)
            LOG_INFO("module.coa-playerbots.bg", "BG bot {} reports area trigger {}", bot->GetName(), triggerId);
    }
}

// Long hauls (a battleground is several hundred yards across): goal-directed travel with progress tracking and recovery, so a bot that
// gets wedged on a ramp reports Stuck and the caller can pick another objective instead of pushing at the same wall forever.
static NavStatus Travel(Player* bot, uint64 goalId, float x, float y, float z, float acceptRadius)
{
    if (!bot || bot->IsNonMeleeSpellCast(false) || BotMovement::GetMountState(bot) == MountState::MountCasting)
        return NavStatus::Blocked;
    return BotMovement::Navigate(bot, MoveOwner::Battleground, goalId, x, y, z, acceptRadius, nullptr, true);
}

bool BotBattlegroundAI::TryMountForTravel(Player* bot)
{
    return BotMovement::RequestMount(bot, MoveOwner::Battleground);
}

bool BotBattlegroundAI::TryInteractWithBGObject(Player* bot, GameObject* go)
{
    if (!bot || !go || !go->isSpawned())
        return false;

    if (bot->GetDistance(go) > 10.0f)
        return false;

    if (bot->IsMounted())
        BotMovement::RequestDismount(bot, MoveOwner::Battleground, DismountReason::ActionForbidden);

    if (BgLogEnabled())
    {
        static uint32 logged = 0;
        if (logged++ < 60)
            LOG_INFO("module.coa-playerbots.bg", "BG bot {} uses object entry {} type {} at {:.1f} yd (spawned {}, state {})", bot->GetName(),
                go->GetEntry(), uint32(go->GetGoType()), bot->GetDistance(go), go->isSpawned() ? 1 : 0, uint32(go->GetGoState()));
    }
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

        // Healers die first in a real fight: the enemy's healing is what keeps the rest alive
        if (BotAI::GetRole(player->GetGUID()) == BotRole::Healer)
            score += 30.0f;

        if (score > bestScore)
        {
            bestScore = score;
            bestTarget = player;
        }
    }

    if (bestTarget)
        return bestTarget;

    // No player in reach: in the objective battlegrounds (Alterac Valley guards and bosses, Isle of Conquest and Strand of the Ancients
    // defenders) the opposition is creatures. Arenas and the flag/base maps have none worth the detour.
    BattlegroundTypeId const type = bg->GetBgTypeID(true);
    if (!bg->isArena() && type != BATTLEGROUND_WS && type != BATTLEGROUND_AB && type != BATTLEGROUND_EY)
        if (Unit* creature = bot->SelectNearbyTarget(nullptr, std::min(maxRange, 30.0f)))
            if (!creature->IsPlayer() && creature->IsAlive() && bot->IsValidAttackTarget(creature))
                return creature;

    return nullptr;
}

static bool IsFlagCarrier(Player* bot)
{
    return bot->HasAura(BG_WS_SPELL_WARSONG_FLAG) || bot->HasAura(BG_WS_SPELL_SILVERWING_FLAG) ||
        bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL);
}

// True when this bot should stop travelling and let the normal combat / healing logic run this tick. Before this, a bot walking to an
// objective never got to cast at anyone unless something had already put an enemy in its target slot, and a healer never healed
// while moving, so bots crossed whole fights doing nothing.
static bool ShouldFight(Player* bot, Battleground* bg, BotBGState& state, uint32 diff)
{
    if (state.engageHoldMs > diff)
        state.engageHoldMs -= diff;
    else
        state.engageHoldMs = 0;

    // A flag carrier runs; only someone already on top of it is answered by the normal combat logic.
    bool const carrier = IsFlagCarrier(bot);

    BotRole const role = BotAI::GetRole(bot->GetGUID());
    if (role == BotRole::Healer && !carrier)
    {
        Battleground::BattlegroundPlayerMap const& players = bg->GetPlayers();
        for (auto const& [guid, ally] : players)
        {
            if (!ally || !ally->IsInWorld() || !ally->IsAlive() || ally->GetTeamId() != bot->GetTeamId())
                continue;
            if (ally->GetHealthPct() < 85.0f && bot->GetDistance(ally) < 35.0f && bot->IsWithinLOSInMap(ally))
            {
                state.engageHoldMs = 2500;
                return true;
            }
        }
    }

    for (Unit* attacker : bot->getAttackers())
    {
        if (attacker && attacker->IsAlive() && bot->IsValidAttackTarget(attacker) && (!carrier || bot->GetDistance(attacker) < 8.0f))
        {
            state.engageHoldMs = 3000;
            return true;
        }
    }

    if (carrier)
        return false;

    if (BotBattlegroundAI::FindHostilePvPTarget(bot, 30.0f))
    {
        state.engageHoldMs = 3000;
        return true;
    }
    return state.engageHoldMs > 0;
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
        BgLog(bot, state, "waits for the gates");
        // Match preparation phase: gates are closed! Stay in spawn area and clear movement
        BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
        return true;
    }

    if (status == STATUS_WAIT_LEAVE)
    {
        BgLog(bot, state, "match over");
        // Match ended: leave battleground after a short delay
        ReleaseAssignment(bot, state);
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

    if (BgLogEnabled())
    {
        state.snapshotMs += diff;
        if (state.snapshotMs >= 15000)
        {
            state.snapshotMs = 0;
            uint32 enemies = 0, allies = 0;
            for (auto const& [guid, other] : bg->GetPlayers())
                if (other && other->IsAlive() && other->IsInWorld())
                    (other->GetTeamId() == bot->GetTeamId() ? allies : enemies)++;
            Unit* victim = bot->GetVictim();
            LOG_INFO("module.coa-playerbots.bg", "BG snapshot {} team {} pos ({:.0f},{:.0f}) hp {:.0f}% moving {} mounted {} victim {} allies {} enemies {} score {}:{}",
                bot->GetName(), uint32(bot->GetTeamId()), bot->GetPositionX(), bot->GetPositionY(), bot->GetHealthPct(),
                bot->isMoving() ? 1 : 0, bot->IsMounted() ? 1 : 0, victim ? victim->GetName() : "-", allies, enemies,
                bg->GetTeamScore(TEAM_ALLIANCE), bg->GetTeamScore(TEAM_HORDE));
        }
    }

    // If bot already in combat defending itself, let combat continue
    if (bot->GetVictim() && bot->GetVictim()->IsAlive() && bot->IsValidAttackTarget(bot->GetVictim()))
    {
        if (bot->IsMounted())
            BotMovement::RequestDismount(bot, MoveOwner::Battleground, DismountReason::Combat);
        return false;
    }

    if (ShouldFight(bot, bg, state, diff))
    {
        BgLog(bot, state, "fights");
        if (bot->IsMounted())
            BotMovement::RequestDismount(bot, MoveOwner::Battleground, DismountReason::Combat);
        BotMovement::ForceReleaseOwner(bot, MoveOwner::Battleground);
        return false;
    }

    // Battleground Objectives have priority over proactive combat acquisition.
    // Flag carriers should not abandon the flag to fight a random enemy.
    // Objective handlers will defend against attackers while completing objectives.
    BattlegroundTypeId bgType = bg->GetBgTypeID(true);
    if (bg->isArena())
    {
        HandleArena(bot, bg, diff);
        return true;
    }
    switch (bgType)
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
        case BATTLEGROUND_AV:
            HandleAlteracValley(bot, bg, diff);
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

    TeamId const myTeam = bot->GetTeamId();
    bool hasFlag = (bg->GetFlagPickerGUID() == bot->GetGUID()) || bot->HasAura(BG_EY_NETHERSTORM_FLAG_SPELL);
    if (hasFlag)
    {
        // Flag carrier: run to the nearest friendly-controlled tower to score!
        int bestTower = -1;
        float bestTowerDist = 99999.0f;

        for (uint8 p = 0; p < EY_POINTS_MAX; ++p)
        {
            if (bg->GetCapturePointInfo(p).IsUnderControl(myTeam))
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
            if (bestTowerDist > 30.0f)
                TryMountForTravel(bot);
            if (bestTowerDist <= 8.0f)
            {
                // inside the tower's circle: report the trigger that scores the flag
                uint32 trigger = 0;
                switch (bestTower)
                {
                    case POINT_BLOOD_ELF:     trigger = AT_BLOOD_ELF_POINT; break;
                    case POINT_FEL_REAVER:    trigger = AT_FEL_REAVER_POINT; break;
                    case POINT_MAGE_TOWER:    trigger = AT_MAGE_TOWER_POINT; break;
                    case POINT_DRAENEI_RUINS: trigger = AT_DRAENEI_RUINS_POINT; break;
                    default: break;
                }
                if (trigger)
                    ReportAreaTrigger(bot, trigger);
                BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
                return;
            }
            Travel(bot, 0xE100 + bestTower, BG_EY_TriggerPositions[bestTower][0], BG_EY_TriggerPositions[bestTower][1],
                BG_EY_TriggerPositions[bestTower][2], 3.0f);
            return;
        }

        // No tower controlled yet: push the natural base so the team can capture it
        uint8 fallbackTower = (myTeam == TEAM_ALLIANCE) ? POINT_MAGE_TOWER : POINT_FEL_REAVER;
        Travel(bot, 0xE100 + fallbackTower, BG_EY_TriggerPositions[fallbackTower][0], BG_EY_TriggerPositions[fallbackTower][1],
            BG_EY_TriggerPositions[fallbackTower][2], 4.0f);
        return;
    }

    // Netherstorm Flag at center (x=2174.0f, y=1569.0f, z=1160.0f): a third of the team contests it, the rest take towers.
    if (bg->GetFlagPickerGUID().IsEmpty())
    {
        bool contestFlag = (bot->GetGUID().GetCounter() % 3 == 0) && BotAI::GetRole(bot->GetGUID()) != BotRole::Healer;
        float distToCenter = bot->GetDistance2d(2174.0f, 1569.0f);

        if (contestFlag || distToCenter < 40.0f)
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

            Travel(bot, 0xE1FF, 2174.0f, 1569.0f, 1160.0f, 4.0f);
            return;
        }
    }
    else if (bg->GetFlagPickerGUID() != bot->GetGUID())
    {
        // The flag is held. Our own carrier is escorted, the enemy's is hunted.
        if (Player* carrier = ObjectAccessor::FindPlayer(bg->GetFlagPickerGUID()))
        {
            bool const friendly = carrier->GetTeamId() == myTeam;
            if (!friendly || BotAI::GetRole(bot->GetGUID()) != BotRole::Dps || bot->GetGUID().GetCounter() % 2 == 0)
            {
                if (bot->GetDistance(carrier) > 30.0f)
                    TryMountForTravel(bot);
                Travel(bot, 0xE1FE, carrier->GetPositionX(), carrier->GetPositionY(), carrier->GetPositionZ(), friendly ? 6.0f : 3.0f);
                return;
            }
        }
    }

    // Tower assignment: each tower gets its share of the team, towers the team does not own come first
    if (state.objectiveSwitchCooldownMs == 0)
    {
        state.objectiveSwitchCooldownMs = 12000;
        int best = -1;
        float bestScore = 1.0e9f;
        for (uint8 p = 0; p < EY_POINTS_MAX; ++p)
        {
            float score = bot->GetDistance2d(BG_EY_TriggerPositions[p][0], BG_EY_TriggerPositions[p][1]);
            score += 160.0f * NodeLoad(bg, myTeam, p, state);
            if (bg->GetCapturePointInfo(p).IsUnderControl(myTeam))
                score += 350.0f; // held already
            else if (bg->GetCapturePointInfo(p).IsUnderControl())
                score -= 60.0f;  // enemy tower: worth the push
            if (score < bestScore)
            {
                bestScore = score;
                best = p;
            }
        }
        if (best >= 0)
        {
            state.targetObjectiveNode = uint32(best);
            AssignNode(bot, state, bg, uint8(best));
        }
    }

    uint8 targetPoint = state.targetObjectiveNode % EY_POINTS_MAX;
    float posX = BG_EY_TriggerPositions[targetPoint][0];
    float posY = BG_EY_TriggerPositions[targetPoint][1];
    float posZ = BG_EY_TriggerPositions[targetPoint][2];

    float distToPoint = bot->GetDistance2d(posX, posY);
    if (distToPoint > 30.0f)
        TryMountForTravel(bot);

    if (distToPoint > 12.0f)
    {
        NavStatus nav = Travel(bot, 0xE100 + targetPoint, posX, posY, posZ, 8.0f);
        if (nav == NavStatus::Stuck)
        {
            ReleaseAssignment(bot, state);
            state.objectiveSwitchCooldownMs = 0;
        }
    }
    else
    {
        BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
    }
}

void BotBattlegroundAI::HandleWarsongGulch(Player* bot, BattlegroundWS* bg, uint32 /*diff*/)
{
    BotBGState& state = s_bgStates[bot->GetGUID()];
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

    // The constants above are only rough (the Horde one is 17 yards and the Alliance one 25 yards from the real flag stand), so bots stood in
    // the enemy flag room beyond interaction range and never took the flag. The flag objects know where they are.
    uint32 const homeFlagObject = (myTeam == TEAM_ALLIANCE) ? BG_WS_OBJECT_A_FLAG : BG_WS_OBJECT_H_FLAG;
    uint32 const enemyFlagObject = (myTeam == TEAM_ALLIANCE) ? BG_WS_OBJECT_H_FLAG : BG_WS_OBJECT_A_FLAG;
    if (GameObject* homeFlag = bg->GetBGObject(homeFlagObject))
    {
        homeX = homeFlag->GetPositionX();
        homeY = homeFlag->GetPositionY();
        homeZ = homeFlag->GetPositionZ();
    }
    GameObject* enemyFlag = bg->GetBGObject(enemyFlagObject);
    if (enemyFlag)
    {
        enemyX = enemyFlag->GetPositionX();
        enemyY = enemyFlag->GetPositionY();
        enemyZ = enemyFlag->GetPositionZ();
    }

    bool isCarrier = (bg->GetFlagPickerGUID(TEAM_ALLIANCE) == bot->GetGUID()) ||
                     (bg->GetFlagPickerGUID(TEAM_HORDE) == bot->GetGUID()) ||
                     IsFlagCarrier(bot);

    // 1. Bot is flag carrier: run to friendly flag room
    if (isCarrier)
    {
        BgLog(bot, state, "carries the flag home");
        float distHome = bot->GetDistance2d(homeX, homeY);
        if (distHome > 5.0f)
        {
            Travel(bot, 0xA100, homeX, homeY, homeZ, 3.0f);
        }
        else
        {
            // at the flag stand: the capture counts when the carrier enters the trigger and the own flag is home
            if (bg->GetFlagState(myTeam) == BG_WS_FLAG_STATE_ON_BASE)
                ReportAreaTrigger(bot, myTeam == TEAM_HORDE ? uint32(BG_WS_TRIGGER_HORDE_FLAG_SPAWN) : uint32(BG_WS_TRIGGER_ALLIANCE_FLAG_SPAWN));
            BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
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
            if (dist < 80.0f)
            {
                if (dist > 25.0f)
                    TryMountForTravel(bot);

                if (dist <= 8.0f && TryInteractWithBGObject(bot, droppedFlag))
                    return;

                Travel(bot, 0xA101, droppedFlag->GetPositionX(), droppedFlag->GetPositionY(), droppedFlag->GetPositionZ(), 3.0f);
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

            Travel(bot, 0xA102, carrier->GetPositionX(), carrier->GetPositionY(), carrier->GetPositionZ(), 3.0f);
            return;
        }
    }

    // Defenders: a fifth of the team, healers and tanks first, holds the home flag room while the others attack. The room is where
    // the enemy carrier has to be stopped and where a returned flag has to be waiting for a capture to count.
    BotRole const role = BotAI::GetRole(bot->GetGUID());
    uint32 const seed = uint32(bot->GetGUID().GetCounter() % 10);
    bool const defender = (role == BotRole::Tank && seed < 6) || (role == BotRole::Healer && seed < 4) || (role == BotRole::Dps && seed < 2);
    if (defender)
    {
        BgLog(bot, state, "defends the flag room");
        float distHome = bot->GetDistance2d(homeX, homeY);
        if (distHome > 30.0f)
        {
            TryMountForTravel(bot);
            Travel(bot, 0xA103, homeX, homeY, homeZ, 12.0f);
        }
        else
        {
            BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
        }
        return;
    }

    // 4. Enemy flag at base: assault enemy flag room and grab it
    if (bg->GetFlagState(enemyTeam) == BG_WS_FLAG_STATE_ON_BASE)
    {
        float distToEnemyFlag = bot->GetDistance2d(enemyX, enemyY);
        BgLog(bot, state, "attacks the enemy flag room");
        if (distToEnemyFlag > 30.0f)
            TryMountForTravel(bot);

        if (enemyFlag && enemyFlag->isSpawned() && bot->GetDistance(enemyFlag) <= 8.0f && TryInteractWithBGObject(bot, enemyFlag))
            return;

        Travel(bot, 0xA104, enemyX, enemyY, enemyZ, 2.0f);
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

            if (distToCarrier > 8.0f)
                Travel(bot, 0xA105, carrier->GetPositionX(), carrier->GetPositionY(), carrier->GetPositionZ(), 6.0f);
            else
                BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
            return;
        }
    }

    // Default: patrol mid field
    float distMid = bot->GetDistance2d(1224.0f, 1453.0f);
    if (distMid > 25.0f)
    {
        TryMountForTravel(bot);
        Travel(bot, 0xA106, 1224.0f, 1453.0f, 335.0f, 10.0f);
    }
}

void BotBattlegroundAI::HandleArathiBasin(Player* bot, BattlegroundAB* bg, uint32 diff)
{
    BotBGState& state = s_bgStates[bot->GetGUID()];
    if (state.objectiveSwitchCooldownMs > diff)
        state.objectiveSwitchCooldownMs -= diff;
    else
        state.objectiveSwitchCooldownMs = 0;
    if (state.buffCooldownMs > diff)
        state.buffCooldownMs -= diff;
    else
        state.buffCooldownMs = 0;

    TeamId myTeam = bot->GetTeamId();
    BotRole const role = BotAI::GetRole(bot->GetGUID());

    // A defender stays on the base it holds while the base is ours; the fight comes to it (ShouldFight takes over when it does)
    if (state.defender && state.assignedNode >= 0)
    {
        uint8 heldNode = uint8(state.assignedNode);
        if (bg->GetCapturePointInfo(heldNode)._ownerTeamId == myTeam)
        {
            BgLog(bot, state, "defends a base");
            float distToNode = bot->GetDistance2d(BG_AB_NodePositions[heldNode][0], BG_AB_NodePositions[heldNode][1]);
            if (distToNode > 14.0f)
                Travel(bot, 0xAB00 + heldNode, BG_AB_NodePositions[heldNode][0], BG_AB_NodePositions[heldNode][1],
                    BG_AB_NodePositions[heldNode][2], 8.0f);
            else
                BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
            return;
        }
        // lost it: back to the normal selection below
        ReleaseAssignment(bot, state);
        state.objectiveSwitchCooldownMs = 0;
    }

    // Hysteresis: a bot keeps its base until the base is ours. Re-scoring every 15 seconds sent the whole team from base to base
    // (800 base changes in nine minutes) and almost nothing was captured.
    if (state.objectiveSwitchCooldownMs == 0 && state.assignedNode >= 0 && state.assignedInstance == bg->GetInstanceID() &&
        bg->GetCapturePointInfo(uint8(state.assignedNode))._ownerTeamId != myTeam)
    {
        state.targetObjectiveNode = uint32(state.assignedNode);
        state.objectiveSwitchCooldownMs = 15000;
    }

    if (state.objectiveSwitchCooldownMs == 0)
    {
        state.objectiveSwitchCooldownMs = 15000;

        // Score every base: distance, how many of the team are already sent there, and whether it is ours. Unowned and enemy bases are
        // taken first, but a base the team owns that nobody holds goes to a tank or healer so a won base is not left empty.
        int best = -1;
        float bestScore = 1.0e9f;
        for (uint8 candidate = 0; candidate < BG_AB_DYNAMIC_NODES_COUNT; ++candidate)
        {
            TeamId owner = bg->GetCapturePointInfo(candidate)._ownerTeamId;
            float score = bot->GetDistance2d(BG_AB_NodePositions[candidate][0], BG_AB_NodePositions[candidate][1]);
            score += 140.0f * NodeLoad(bg, myTeam, candidate, state);
            if (owner == myTeam)
            {
                bool const held = NodeHasDefender(bg, myTeam, candidate, bot);
                if (held)
                    score += 500.0f;
                else if (role == BotRole::Tank || role == BotRole::Healer)
                    score -= 90.0f; // wants a defender
                else
                    score += 300.0f;
            }
            else if (owner != TEAM_NEUTRAL)
            {
                score -= 40.0f;
            }
            if (score < bestScore)
            {
                bestScore = score;
                best = candidate;
            }
        }
        if (best >= 0)
        {
            state.targetObjectiveNode = uint32(best);
            AssignNode(bot, state, bg, uint8(best));
            static char const* const nodeNames[] = { "picks the Stables", "picks the Blacksmith", "picks the Farm", "picks the Lumber Mill", "picks the Gold Mine" };
            BgLog(bot, state, nodeNames[best % 5]);
        }
    }

    uint8 node = state.targetObjectiveNode % BG_AB_DYNAMIC_NODES_COUNT;
    float posX = BG_AB_NodePositions[node][0];
    float posY = BG_AB_NodePositions[node][1];
    float posZ = BG_AB_NodePositions[node][2];

    float dist = bot->GetDistance2d(posX, posY);

    // Power-ups: the buff spawns sit along the roads; a short detour picks one up (they trigger on touch)
    if (state.buffCooldownMs == 0 && dist > 40.0f)
    {
        for (uint8 b = 0; b < BG_AB_DYNAMIC_NODES_COUNT; ++b)
        {
            if (bot->GetDistance2d(BG_AB_BuffPositions[b][0], BG_AB_BuffPositions[b][1]) < 28.0f)
            {
                state.buffCooldownMs = 20000;
                Travel(bot, 0xAB80 + b, BG_AB_BuffPositions[b][0], BG_AB_BuffPositions[b][1], BG_AB_BuffPositions[b][2], 2.0f);
                return;
            }
        }
    }

    BgLog(bot, state, "goes to a base");
    if (dist > 30.0f)
        TryMountForTravel(bot);

    // Once the bot is near its base the banner decides where it goes (the banner can stand 15-20 yards from the node point, and
    // switching between "walk to the node" and "walk to the banner" every tick left the whole team thrashing on the spot).
    if (dist <= 40.0f)
    {
        if (bg->GetCapturePointInfo(node)._ownerTeamId != myTeam)
        {
            GameObject* nearest = nullptr;
            float nearestDist = 9999.0f;
            for (uint32 obj = BG_AB_OBJECT_BANNER_NEUTRAL; obj < static_cast<uint8>(BG_AB_DYNAMIC_NODES_COUNT) * BG_AB_OBJECTS_PER_NODE; ++obj)
            {
                GameObject* banner = bg->GetBGObject(obj);
                if (!banner || !banner->isSpawned() || banner->GetGoState() != GO_STATE_READY || !bot->CanUseBattlegroundObject(banner))
                    continue;
                float const d = bot->GetDistance(banner);
                if (d < nearestDist && banner->GetDistance2d(posX, posY) < 30.0f)
                {
                    nearestDist = d;
                    nearest = banner;
                }
            }
            if (nearest)
            {
                BgLog(bot, state, "captures a base");
                if (nearestDist < 0.5f)
                {
                    // standing inside the pole: step off before casting
                    Travel(bot, 0xABB1 + node, nearest->GetPositionX() + 1.5f, nearest->GetPositionY() + 1.5f, nearest->GetPositionZ(), 0.8f);
                    return;
                }
                if (nearestDist <= 3.5f && CaptureBanner(bot, nearest))
                    return;
                Travel(bot, 0xABB0 + node, nearest->GetPositionX(), nearest->GetPositionY(), nearest->GetPositionZ(), 1.5f);
                return;
            }
        }
        else if (dist <= 14.0f && !NodeHasDefender(bg, myTeam, node, bot) && state.assignedNode == int8(node))
        {
            // captured and nobody is holding it: this bot stays
            s_nodeDefender[NodeKey(bg->GetInstanceID(), uint8(myTeam), node)] = bot->GetGUID();
            state.defender = true;
            BgLog(bot, state, "becomes a base defender");
        }
        if (dist <= 14.0f)
        {
            BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
            return;
        }
    }

    NavStatus nav = Travel(bot, 0xAB00 + node, posX, posY, posZ, 4.0f);
    if (nav == NavStatus::Stuck)
    {
        ReleaseAssignment(bot, state);
        state.objectiveSwitchCooldownMs = 0;
    }
}

void BotBattlegroundAI::HandleAlteracValley(Player* bot, Battleground* /*bg*/, uint32 diff)
{
    BotBGState& state = s_bgStates[bot->GetGUID()];
    if (state.objectiveSwitchCooldownMs > diff)
        state.objectiveSwitchCooldownMs -= diff;
    else
        state.objectiveSwitchCooldownMs = 0;

    // Alterac Valley is won by killing the other side's general. The two armies meet in the middle of the valley and fight through
    // the guards and bunkers on the way (ShouldFight sends a bot into every guard and player it passes), a few bots stay home.
    constexpr float VANNDAR_X = 722.4f, VANNDAR_Y = -10.9f, VANNDAR_Z = 50.2f;        // Alliance general (Dun Baldar)
    constexpr float DREKTHAR_X = -1371.0f, DREKTHAR_Y = -219.8f, DREKTHAR_Z = 98.4f;  // Horde general (Frostwolf Keep)

    TeamId const myTeam = bot->GetTeamId();
    float const homeX = myTeam == TEAM_ALLIANCE ? VANNDAR_X : DREKTHAR_X;
    float const homeY = myTeam == TEAM_ALLIANCE ? VANNDAR_Y : DREKTHAR_Y;
    float const homeZ = myTeam == TEAM_ALLIANCE ? VANNDAR_Z : DREKTHAR_Z;
    float const goalX = myTeam == TEAM_ALLIANCE ? DREKTHAR_X : VANNDAR_X;
    float const goalY = myTeam == TEAM_ALLIANCE ? DREKTHAR_Y : VANNDAR_Y;
    float const goalZ = myTeam == TEAM_ALLIANCE ? DREKTHAR_Z : VANNDAR_Z;

    BotRole const role = BotAI::GetRole(bot->GetGUID());
    uint32 const seed = uint32(bot->GetGUID().GetCounter() % 10);
    bool const defender = (role == BotRole::Healer && seed < 2) || (role == BotRole::Tank && seed < 3);
    if (defender)
    {
        if (bot->GetDistance2d(homeX, homeY) > 40.0f)
        {
            TryMountForTravel(bot);
            Travel(bot, 0xA700, homeX, homeY, homeZ, 20.0f);
        }
        else
        {
            BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
        }
        return;
    }

    // March along the valley road, one waypoint at a time
    BgWaypoint const* route = myTeam == TEAM_ALLIANCE ? BgRoutes::AvAllianceMarch : BgRoutes::AvHordeMarch;
    int32 const routeSize = myTeam == TEAM_ALLIANCE ? int32(std::size(BgRoutes::AvAllianceMarch)) : int32(std::size(BgRoutes::AvHordeMarch));
    auto nearestWaypoint = [&]() -> int32
    {
        int32 best = 0;
        float bestDist = 1.0e9f;
        for (int32 i = 0; i < routeSize; ++i)
        {
            float const d = bot->GetDistance2d(route[i].x, route[i].y);
            if (d < bestDist)
            {
                bestDist = d;
                best = i;
            }
        }
        return best;
    };
    if (state.routeIndex < 0 || state.routeIndex >= routeSize || bot->GetDistance2d(route[state.routeIndex].x, route[state.routeIndex].y) > 200.0f)
        state.routeIndex = nearestWaypoint(); // first tick, or the bot died and came back at a graveyard

    BgLog(bot, state, "marches on the enemy general");
    if (state.routeIndex < routeSize - 1 && bot->GetDistance2d(route[state.routeIndex].x, route[state.routeIndex].y) < 12.0f)
        ++state.routeIndex;

    if (state.routeIndex < routeSize - 1 || bot->GetDistance2d(goalX, goalY) > 40.0f)
    {
        BgWaypoint const& wp = route[state.routeIndex];
        if (state.routeIndex == routeSize - 1)
            Travel(bot, 0xA701, goalX, goalY, goalZ, 10.0f);
        else
        {
            if (bot->GetDistance2d(wp.x, wp.y) > 60.0f)
                TryMountForTravel(bot);
            Travel(bot, 0xA710 + uint64(state.routeIndex), wp.x, wp.y, wp.z, 6.0f);
        }
    }
    else
    {
        BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
    }
}

void BotBattlegroundAI::HandleArena(Player* bot, Battleground* bg, uint32 /*diff*/)
{
    // Melee and tanks walk at the nearest enemy until it is in reach and ShouldFight takes over. Healers and ranged damage dealers
    // ("squishy") do not: a healer stays with the team and a caster holds a cast range from the nearest enemy, stepping back when a
    // fighter closes in. (Zyth45's CoA arena tactics place bots by role for the same reason: every CoA bot is one class id, so a
    // class-based placement sent all of them to the melee spot.)
    BotRole const role = BotAI::GetRole(bot->GetGUID());
    BotAI::SpecStatModel const* model = BotAI::FindSpecStatModel(bot->getClass(), bot->GetPlayerSetting("core.ascension_active_spec", 0).value);
    bool const squishy = role == BotRole::Healer || bot->getClass() == 21 /* Ranger */ ||
        (model && (model->primary == 5 || model->primary == 6) && role != BotRole::Tank);
    if (!squishy)
    {
        HandleGenericBattleground(bot, bg, 0);
        return;
    }

    Player* nearestEnemy = nullptr;
    float nearestEnemyDist = 99999.0f;
    Player* nearestAlly = nullptr;
    float nearestAllyDist = 99999.0f;
    for (auto const& [guid, other] : bg->GetPlayers())
    {
        if (!other || other == bot || !other->IsAlive() || !other->IsInWorld() || !other->IsInMap(bot))
            continue;
        float const d = bot->GetDistance(other);
        if (other->GetTeamId() == bot->GetTeamId())
        {
            if (d < nearestAllyDist)
            {
                nearestAllyDist = d;
                nearestAlly = other;
            }
        }
        else if (d < nearestEnemyDist)
        {
            nearestEnemyDist = d;
            nearestEnemy = other;
        }
    }

    if (nearestEnemy && nearestEnemyDist < 12.0f)
    {
        // somebody is on top of us: back off along the line away from it, toward the team if there is one
        float dx = bot->GetPositionX() - nearestEnemy->GetPositionX();
        float dy = bot->GetPositionY() - nearestEnemy->GetPositionY();
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.1f)
            len = 1.0f;
        float tx = bot->GetPositionX() + dx / len * 10.0f;
        float ty = bot->GetPositionY() + dy / len * 10.0f;
        if (nearestAlly)
        {
            tx = (tx + nearestAlly->GetPositionX()) / 2.0f;
            ty = (ty + nearestAlly->GetPositionY()) / 2.0f;
        }
        Travel(bot, 0xAE01, tx, ty, bot->GetPositionZ(), 3.0f);
        return;
    }

    if (role == BotRole::Healer && nearestAlly && (!nearestEnemy || nearestEnemyDist > 24.0f))
    {
        if (nearestAllyDist > 18.0f)
            Travel(bot, 0xAE00, nearestAlly->GetPositionX(), nearestAlly->GetPositionY(), nearestAlly->GetPositionZ(), 10.0f);
        else
            BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
        return;
    }

    if (nearestEnemy && nearestEnemyDist > 34.0f)
    {
        // close to casting range (about 28 yards) and no further
        float dx = bot->GetPositionX() - nearestEnemy->GetPositionX();
        float dy = bot->GetPositionY() - nearestEnemy->GetPositionY();
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.1f)
            len = 1.0f;
        float tx = nearestEnemy->GetPositionX() + dx / len * 28.0f;
        float ty = nearestEnemy->GetPositionY() + dy / len * 28.0f;
        Travel(bot, 0xAE02, tx, ty, nearestEnemy->GetPositionZ(), 4.0f);
        return;
    }

    BotMovement::ForceStopOwner(bot, MoveOwner::Battleground);
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

        Travel(bot, 0xAF00, closestEnemy->GetPositionX(), closestEnemy->GetPositionY(), closestEnemy->GetPositionZ(), 8.0f);
    }
}
