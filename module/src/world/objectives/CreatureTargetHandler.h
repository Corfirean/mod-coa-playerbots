/*
 * mod-coa-playerbots
 *
 * The flow every creature objective shares:
 *
 *   TravelToArea -> Search -> Approach -> Execute -> Combat -> Loot -> Verify -> Search ...
 *
 * Search looks for *live* creatures around the bot (never a spawn coordinate), scores them and
 * claims the best free one so no other bot goes for it. Execute is the only step that differs per
 * objective: kill and collect call Attack() and hand the fight to the combat engine. Verify reads the real quest counter / item count
 * and decides: next target, done, or -- after too many attempts that advanced nothing -- give up.
 */

#ifndef COA_PLAYERBOTS_CREATURE_TARGET_HANDLER_H
#define COA_PLAYERBOTS_CREATURE_TARGET_HANDLER_H

#include "IQuestObjectiveHandler.h"

class Creature;

class CreatureTargetHandler : public IQuestObjectiveHandler
{
public:
    ObjectiveResult Update(ObjectiveContext& ctx) override;

protected:
    // Only creatures the bot can attack (kill/collect), or any (use-item-on objectives).
    virtual bool HostileOnly(ObjectiveContext const& ctx) const = 0;
    virtual bool WantDead(ObjectiveContext const& ctx) const { (void)ctx; return false; }
    // How close to get before Execute.
    virtual float EngageRange(ObjectiveContext const& ctx) const;
    // Starts the action on the claimed target: Attack(), or a quest item cast.
    virtual ObjectiveResult Execute(ObjectiveContext& ctx, Creature* target) = 0;
    // The bot is out of combat again (or the action ended): what happened to the target?
    virtual ObjectiveResult AfterExecute(ObjectiveContext& ctx);
    // Called by Verify when an attempt limit is hit; return true to keep going another way.
    virtual bool OnDryLimit(ObjectiveContext& ctx) { (void)ctx; return false; }

    Creature* Scan(ObjectiveContext& ctx, uint32& corpses);
    void StartApproach(ObjectiveContext& ctx, Creature* target);
    Creature* CurrentTarget(ObjectiveContext const& ctx) const;
};

#endif // COA_PLAYERBOTS_CREATURE_TARGET_HANDLER_H
