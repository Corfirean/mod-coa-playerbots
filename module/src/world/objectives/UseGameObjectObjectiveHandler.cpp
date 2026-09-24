/*
 * mod-coa-playerbots
 *
 * "Use the lever", "destroy 5 supply crates", "pick up the scattered notes" -- goober objects. The
 * click goes through WorldSession::HandleGameObjectUseOpcode, the very handler a client's
 * right-click reaches (distance check included), and GameObject::Use's goober case hands out the
 * credit itself (KillCreditGO) or casts the object's spell that creates the quest item. The
 * handler only has to pick an unused object, walk up to it, click, and read the counter.
 */

#include "GameObject.h"
#include "ObjectTargetHandler.h"
#include "ObjectiveHandlerList.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"

using namespace WorldBrainInternal;

namespace
{
    class UseGameObjectObjectiveHandler final : public ObjectTargetHandler
    {
    public:
        char const* Name() const override { return "use-object"; }

        bool CanHandle(ObjectiveDef const& def) const override
        {
            return def.type == ObjectiveType::UseGameObject || def.type == ObjectiveType::UseItemSource;
        }

    protected:
        bool IsUsable(ObjectiveContext const& ctx, GameObject* go) const override
        {
            if (!ObjectTargetHandler::IsUsable(ctx, go))
                return false;
            // A goober tied to a quest only reacts for players on that quest.
            GameObjectTemplate const* info = go->GetGOInfo();
            if (info->type == GAMEOBJECT_TYPE_GOOBER && info->goober.questId &&
                ctx.bot->GetQuestStatus(info->goober.questId) != QUEST_STATUS_INCOMPLETE)
                return false;
            return true;
        }

        ObjectiveResult Act(ObjectiveContext& ctx, GameObject* go) override
        {
            WorldPacket packet(CMSG_GAMEOBJ_USE, 8);
            packet << go->GetGUID();
            ctx.bot->GetSession()->HandleGameObjectUseOpcode(packet);

            ++ctx.task.quest.attempts;
            ctx.task.waitUntilMs = ctx.now + RollRange(ctx.state, 0x05e0, 500, 1300);
            LOG_DEBUG("module.coa-playerbots.quest", "Bot '{}' used object {} for quest {}.", ctx.bot->GetName(), go->GetEntry(),
                ctx.task.quest.questId);
            SetPhase(ctx.bot, ctx.state, TaskPhase::Loot, "clicked the object");
            return ObjectiveResult::Running;
        }
    };
}

IQuestObjectiveHandler& UseGameObjectObjectiveHandlerInstance()
{
    static UseGameObjectObjectiveHandler handler;
    return handler;
}
