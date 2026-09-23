#include "BotAI.h"
#include "BotMgr.h"
#include "BotTalentBuilds.h"
#include "BotWorldBehavior.h"
#include "Config.h"
#include "Creature.h"
#include "KillRewarder.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "WorldScript.h"
#include "engine/SpecStrategyRegistry.h"

class coa_playerbots_worldscript : public WorldScript
{
public:
    coa_playerbots_worldscript() : WorldScript("coa_playerbots_worldscript", { WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_STARTUP }) { }

    void OnUpdate(uint32 diff) override
    {
        sBotMgr->Update(diff);
    }

    // User-requested standing behavior: every known bot character logs itself back in on
    // worldserver startup instead of needing a manual `.botcmd spawnbot`/`spawnrandom`/
    // `spawnleveled` after every restart. Gated behind CoaBots.AutoLoginOnStartup so a
    // dev/test session can still boot clean (no bots) when that's what's actually wanted.
    void OnStartup() override
    {
        BotTalentBuilds::Initialize();
        BotAI::SpecStrategyRegistry::Validate();
        sBotMgr->LoadGuildGatherOrders();
        BotAI::LoadGatherLootData();
        BotWorldBehavior::LoadConfig();

        if (sConfigMgr->GetOption<bool>("CoaBots.AutoLoginOnStartup", false))
            sBotMgr->QueueAllBotsForAutoLogin();
    }
};

class coa_playerbots_playerscript : public PlayerScript
{
public:
    coa_playerbots_playerscript() : PlayerScript("coa_playerbots_playerscript", { PLAYERHOOK_ON_REWARD_KILL_REWARDER }) { }

    void OnPlayerRewardKillRewarder(Player* player, KillRewarder* rewarder, bool /*isDungeon*/, float& /*rate*/) override
    {
        if (!player || !rewarder)
            return;

        Unit* victim = rewarder->GetVictim();
        if (!victim)
            return;

        Creature* creature = victim->ToCreature();
        if (!creature)
            return;

        // In auto-dungeon mode, if this creature was a dungeon boss, record it as cleared
        if (creature->GetMap() && (creature->GetMap()->IsDungeon() || creature->GetMap()->IsRaid()))
        {
            if (CreatureTemplate const* cinfo = creature->GetCreatureTemplate())
            {
                if (cinfo->HasFlagsExtra(CREATURE_FLAG_EXTRA_DUNGEON_BOSS) || cinfo->rank == CREATURE_ELITE_WORLDBOSS)
                {
                    Group* group = player->GetGroup();
                    ObjectGuid leaderGuid = group ? group->GetLeaderGUID() : player->GetGUID();
                    sBotMgr->MarkBossCleared(leaderGuid, cinfo->Entry);
                }
            }
        }

        BotAI::EnqueuePendingLoot(player, creature->GetGUID());
    }
};

void AddSC_coa_playerbots_worldscript()
{
    new coa_playerbots_worldscript();
    new coa_playerbots_playerscript();
}
