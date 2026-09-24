/*
 * mod-coa-playerbots
 *
 * "Speak with the wounded scout": credit from a friendly NPC, handed out by that NPC's gossip
 * script. There is no general way to know which gossip option a script wants, so the knowledge
 * base marks these objectives unsupported and bots never accept such quests. This handler exists
 * for quests that are already in a bot's log (taken before this layer existed, or from a shared
 * quest): it walks up, opens the NPC's gossip exactly as a client's right-click does
 * (CMSG_GOSSIP_HELLO), and checks whether that alone gave the credit. If it didn't, the objective
 * fails as unsupported and the quest is dropped, instead of being retried forever.
 */

#include "CreatureTargetHandler.h"
#include "Creature.h"
#include "ObjectiveCommon.h"
#include "ObjectiveHandlerList.h"
#include "Opcodes.h"
#include "Player.h"
#include "WorldPacket.h"
#include "WorldSession.h"

using namespace WorldBrainInternal;

namespace
{
    class TalkObjectiveHandler final : public CreatureTargetHandler
    {
    public:
        char const* Name() const override { return "talk"; }

        bool CanHandle(ObjectiveDef const& def) const override
        {
            return def.type == ObjectiveType::TalkTo;
        }

    protected:
        bool HostileOnly(ObjectiveContext const&) const override { return false; }

        float EngageRange(ObjectiveContext const&) const override { return INTERACTION_DISTANCE - 1.0f; }

        ObjectiveResult Execute(ObjectiveContext& ctx, Creature* target) override
        {
            if (!ObjectiveCommon::Settle(ctx.bot, ctx.state))
                return ObjectiveResult::Running;

            ctx.bot->SetFacingToObject(target);
            WorldPacket packet(CMSG_GOSSIP_HELLO, 8);
            packet << target->GetGUID();
            ctx.bot->GetSession()->HandleGossipHelloOpcode(packet);

            ++ctx.task.quest.attempts;
            ctx.task.waitUntilMs = ctx.now + RollRange(ctx.state, 0x7a1c, 1200, 2500);
            SetPhase(ctx.bot, ctx.state, TaskPhase::Loot, "talking");
            return ObjectiveResult::Running;
        }

        bool OnDryLimit(ObjectiveContext& ctx) override
        {
            ctx.task.quest.lastFailure = FailureReason::Unsupported;
            return false;
        }
    };
}

IQuestObjectiveHandler& TalkObjectiveHandlerInstance()
{
    static TalkObjectiveHandler handler;
    return handler;
}
