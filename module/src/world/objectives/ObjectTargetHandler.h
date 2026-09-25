/*
 * mod-coa-playerbots
 *
 * The flow every game-object objective shares:
 *
 *   TravelToArea -> Search -> Approach -> Execute -> Loot (wait/collect) -> Verify -> Search ...
 *
 * Objects are not all used the same way, and assuming GameObject::Use() works for every type is
 * exactly how the old code stranded bots at quest chests (Use() has no chest case at all). Each
 * handler supplies the interaction its object type really takes: a goober is clicked (the same
 * CMSG_GAMEOBJ_USE handler a client click reaches), a chest is opened with the generic Opening
 * spell for its lock and then looted, an object a quest item acts on gets the item's spell.
 */

#ifndef COA_PLAYERBOTS_OBJECT_TARGET_HANDLER_H
#define COA_PLAYERBOTS_OBJECT_TARGET_HANDLER_H

#include "IQuestObjectiveHandler.h"

class GameObject;

class ObjectTargetHandler : public IQuestObjectiveHandler
{
public:
    ObjectiveResult Update(ObjectiveContext& ctx) override;

protected:
    // Ready to be used right now (spawned, not in use, has something for the bot).
    virtual bool IsUsable(ObjectiveContext const& ctx, GameObject* go) const;
    // Close enough to act on it.
    virtual bool InUseRange(ObjectiveContext const& ctx, GameObject* go) const;
    virtual float UseRange(ObjectiveContext const& ctx, GameObject* go) const;
    // Starts the interaction. On success the handler moves the task to Loot with waitUntilMs set.
    virtual ObjectiveResult Act(ObjectiveContext& ctx, GameObject* go) = 0;
    // Takes whatever a loot window opened on the object (chests).
    virtual void Collect(ObjectiveContext& ctx, GameObject* go) { (void)ctx; (void)go; }

    GameObject* CurrentObject(ObjectiveContext const& ctx) const;
    GameObject* Scan(ObjectiveContext& ctx);
    void StartApproach(ObjectiveContext& ctx, GameObject* go);
};

#endif // COA_PLAYERBOTS_OBJECT_TARGET_HANDLER_H
