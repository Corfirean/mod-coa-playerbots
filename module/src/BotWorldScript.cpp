#include "training/TrainingArenaManager.h"
#include "BotAI.h"
#include "BotMgr.h"
#include "BotTalentBuilds.h"
#include "BotWorldBehavior.h"
#include "WorldBrain.h"
#include "Config.h"
#include "Creature.h"
#include "KillRewarder.h"
#include "Log.h"
#include "Group.h"
#include "Player.h"
#include "PlayerScript.h"
#include "ScriptMgr.h"
#include "WorldScript.h"
#include "engine/SpecStrategyRegistry.h"

class coa_playerbots_worldscript : public WorldScript
{
public:
    coa_playerbots_worldscript() : WorldScript("coa_playerbots_worldscript", { WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_SHUTDOWN }) { }

    void OnUpdate(uint32 diff) override
    {
        CoaTraining::Update(diff);
        sBotMgr->Update(diff);
    }

    // Called by the world server's main function as soon as the world loop has ended, while the databases are still
    // open and the maps still exist: the one moment every companion can be saved and taken out of the world. (The
    // core also has a "log the bots out" call in its kick-all-players pass, but that one is compiled in only for the
    // upstream playerbots module, which brings a database of its own that this server does not use.)
    void OnShutdown() override
    {
        CoaTraining::Shutdown();
        if (!sBotMgr->BotsEnabled())
            return;
        sBotMgr->LogoutAllBots();
    }

    // User-requested standing behavior: every known bot character logs itself back in on
    // worldserver startup instead of needing a manual `.botcmd spawnbot`/`spawnrandom`/
    // `spawnleveled` after every restart. Gated behind CoaBots.AutoLoginOnStartup so a
    // dev/test session can still boot clean (no bots) when that's what's actually wanted.
    void OnStartup() override
    {
        CoaTraining::Initialize();
        if (!sBotMgr->BotsEnabled())
            return;

        BotTalentBuilds::Initialize();
        BotAI::SpecStrategyRegistry::Validate();
        sBotMgr->LoadGuildGatherOrders();
        BotAI::LoadGatherLootData();
        BotWorldBehavior::LoadConfig();
        // Quest knowledge base, objective handlers' config: built once, after the world loaded.
        WorldBrain::Initialize();

        if (sConfigMgr->GetOption<bool>("CoaBots.AutoLoginOnStartup", false))
            sBotMgr->QueueAllBotsForAutoLogin();
    }
};

class coa_playerbots_playerscript : public PlayerScript
{
public:
    coa_playerbots_playerscript() : PlayerScript("coa_playerbots_playerscript", { PLAYERHOOK_ON_REWARD_KILL_REWARDER, PLAYERHOOK_ON_PLAYER_KILLED_BY_CREATURE, PLAYERHOOK_ON_PLAYER_JUST_DIED }) { }

    // One line per bot death, so what kills the open-world bots can be counted instead of guessed: the killer, its level
    // against the bot's, how many creatures were on the bot, where. Runs on a map thread and only reads and logs.
    void OnPlayerKilledByCreature(Creature* killer, Player* killed) override
    {
        if (!killer || !killed || !BotMgr::IsBotAccountId(killed->GetSession()->GetAccountId()))
            return;

        CreatureTemplate const* cinfo = killer->GetCreatureTemplate();
        Group const* group = killed->GetGroup();
        LOG_INFO("module.coa-playerbots", "BotDeath: '{}' (class {}, level {}, {} max hp) killed by '{}' (entry {}, level {}, rank {}, {} attackers) on map {} zone {} at ({:.0f} {:.0f} {:.0f}), group of {}, in combat {}.",
            killed->GetName(), uint32(killed->getClass()), uint32(killed->GetLevel()), killed->GetMaxHealth(), killer->GetName(), killer->GetEntry(),
            uint32(killer->GetLevel()), cinfo ? uint32(cinfo->rank) : 0u, killed->getAttackers().size(), killed->GetMapId(), killed->GetZoneId(),
            killed->GetPositionX(), killed->GetPositionY(), killed->GetPositionZ(), group ? group->GetMembersCount() : 1u, killed->IsInCombat());
    }

    void OnPlayerJustDied(Player* player) override
    {
        if (!player || !BotMgr::IsBotAccountId(player->GetSession()->GetAccountId()))
            return;

        LOG_INFO("module.coa-playerbots", "BotDeath: '{}' (class {}, level {}) died on map {} zone {} at ({:.0f} {:.0f} {:.0f}), underwater {}, attackers {}, in combat {}.",
            player->GetName(), uint32(player->getClass()), uint32(player->GetLevel()), player->GetMapId(), player->GetZoneId(),
            player->GetPositionX(), player->GetPositionY(), player->GetPositionZ(), player->IsUnderWater(), player->getAttackers().size(), player->IsInCombat());
    }

    void OnPlayerRewardKillRewarder(Player* player, KillRewarder* rewarder, bool /*isDungeon*/, float& /*rate*/) override
    {
        if (!sBotMgr->BotsEnabled() || !player || !rewarder)
            return;

        Unit* victim = rewarder->GetVictim();
        if (!victim)
            return;

        Creature* creature = victim->ToCreature();
        if (!creature)
            return;

        // This hook runs on a map thread. Only read what belongs to this creature's own map here and
        // hand the rest to the world thread through the bot manager's locked queue.
        BotMgr::KillEvent event;
        event.killer = player->GetGUID();
        event.victim = creature->GetGUID();

        // In auto-dungeon mode, if this creature was a dungeon boss, record it as cleared
        if (creature->GetMap() && (creature->GetMap()->IsDungeon() || creature->GetMap()->IsRaid()))
        {
            if (CreatureTemplate const* cinfo = creature->GetCreatureTemplate())
            {
                if (cinfo->HasFlagsExtra(CREATURE_FLAG_EXTRA_DUNGEON_BOSS) || cinfo->rank == CREATURE_ELITE_WORLDBOSS)
                {
                    Group* group = player->GetGroup();
                    event.bossLeader = group ? group->GetLeaderGUID() : player->GetGUID();
                    event.bossEntry = cinfo->Entry;
                }
            }
        }

        sBotMgr->PostKillEvent(event);
    }
};

void AddSC_coa_playerbots_worldscript()
{
    new coa_playerbots_worldscript();
    new coa_playerbots_playerscript();
}
