/*
 * mod-coa-playerbots
 *
 * Quest items held by chest-type objects (a crate of supplies, a pile of bones). GameObject::Use()
 * does nothing for chests -- a client opens them by casting the generic Opening spell for the
 * object's lock -- so the handler casts that spell (QuestKB::OpeningSpellFor picked it from the
 * spell store for the lock type), Spell::EffectOpenLock opens the loot window exactly as it does
 * for a player, and the handler takes the items and closes the window the same way the gathering
 * code does. Closing it matters: DoLootRelease is what despawns an emptied chest for respawn.
 */

#include "BotAI.h"
#include "GameObject.h"
#include "LootMgr.h"
#include "ObjectTargetHandler.h"
#include "ObjectiveCommon.h"
#include "ObjectiveHandlerList.h"
#include "Opcodes.h"
#include "Player.h"
#include "QuestKnowledgeBase.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

using namespace WorldBrainInternal;

namespace
{
    class LootGameObjectObjectiveHandler final : public ObjectTargetHandler
    {
    public:
        char const* Name() const override { return "open-and-loot"; }

        bool CanHandle(ObjectiveDef const& def) const override
        {
            return def.type == ObjectiveType::LootGameObject && def.supported;
        }

    protected:
        bool IsUsable(ObjectiveContext const& ctx, GameObject* go) const override
        {
            return go->GetGoType() == GAMEOBJECT_TYPE_CHEST && ObjectTargetHandler::IsUsable(ctx, go);
        }

        bool InUseRange(ObjectiveContext const& ctx, GameObject* go) const override
        {
            uint32 spellId = QuestKB::OpeningSpellFor(go->GetGOInfo());
            return go->IsAtInteractDistance(ctx.bot, spellId ? sSpellMgr->GetSpellInfo(spellId) : nullptr);
        }

        ObjectiveResult Act(ObjectiveContext& ctx, GameObject* go) override
        {
            uint32 spellId = QuestKB::OpeningSpellFor(go->GetGOInfo());
            uint32 castMs = 0;
            if (spellId)
            {
                SpellCastResult result = ctx.bot->CastSpell(go, spellId, false);
                LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' opening object {} with spell {} (result {}).", ctx.bot->GetName(),
                    go->GetEntry(), spellId, uint32(result));
                if (result != SPELL_CAST_OK)
                {
                    ObjectiveCommon::ForgetTarget(ctx, FailureReason::CastFailed);
                    ObjectiveCommon::BeginSearch(ctx, "could not open the object");
                    return ObjectiveResult::Running;
                }
                if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId))
                    castMs = info->CalcCastTime(ctx.bot);
            }
            else
            {
                // No lock a bot can open by spell: a click is the only other way a client has.
                WorldPacket packet(CMSG_GAMEOBJ_USE, 8);
                packet << go->GetGUID();
                ctx.bot->GetSession()->HandleGameObjectUseOpcode(packet);
            }

            ++ctx.task.quest.attempts;
            ctx.task.waitUntilMs = ctx.now + castMs + RollRange(ctx.state, 0x100b, 400, 1000);
            SetPhase(ctx.bot, ctx.state, TaskPhase::Loot, "opening");
            return ObjectiveResult::Running;
        }

        void Collect(ObjectiveContext& ctx, GameObject* go) override
        {
            Player* bot = ctx.bot;
            if (bot->GetLootGUID() != go->GetGUID())
                return;

            BotAI::TakeAllLoot(bot, go->loot);

            WorldPacket releasePacket;
            releasePacket << go->GetGUID();
            bot->GetSession()->HandleLootReleaseOpcode(releasePacket);
        }
    };
}

IQuestObjectiveHandler& LootGameObjectObjectiveHandlerInstance()
{
    static LootGameObjectObjectiveHandler handler;
    return handler;
}
