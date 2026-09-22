#include "BotMovement.h"
// GridTerrainData.h owns INVALID_HEIGHT but is not self-contained: it uses Optional without
// including it, so it only compiles when something else has already pulled Optional.h in.
#include "Optional.h"
#include "GridTerrainData.h"
#include "MotionMaster.h"
#include "Player.h"
#include <unordered_map>

namespace
{
    std::unordered_map<ObjectGuid, MoveOwner> _claims;

    // Ordering is the enum's own declaration order, so adding an owner in the right place is all
    // that's needed to give it a priority -- there's no second table to keep in sync.
    uint8 Priority(MoveOwner owner)
    {
        return uint8(owner);
    }

    bool HasPointMovement(Player const* bot)
    {
        return bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE;
    }
}

namespace BotMovement
{
    MoveOwner CurrentOwner(Player* bot)
    {
        if (!bot)
            return MoveOwner::None;

        auto itr = _claims.find(bot->GetGUID());
        if (itr == _claims.end())
            return MoveOwner::None;

        if (!HasPointMovement(bot))
        {
            _claims.erase(itr);
            return MoveOwner::None;
        }

        return itr->second;
    }

    bool CanClaim(Player* bot, MoveOwner owner)
    {
        MoveOwner current = CurrentOwner(bot);
        return current == MoveOwner::None || current == owner || Priority(current) <= Priority(owner);
    }

    bool MoveTo(Player* bot, MoveOwner owner, float x, float y, float z)
    {
        if (!bot || bot->IsNonMeleeSpellCast(false))
            return false;

        if (!CanClaim(bot, owner))
            return false;

        float groundZ = z;
        if (!bot->CanFly())
            bot->UpdateAllowedPositionZ(x, y, groundZ);
        if (groundZ <= INVALID_HEIGHT)
            groundZ = z;

        // Redirecting an existing point movement needs the old generator gone first, same as
        // before. The CanClaim check above is what makes this safe now: at this point the slot is
        // either unowned, stale, ours, or held by someone we outrank.
        if (HasPointMovement(bot))
            bot->GetMotionMaster()->Clear();

        _claims[bot->GetGUID()] = owner;
        bot->GetMotionMaster()->MovePoint(uint32(owner), x, y, groundZ);
        return true;
    }

    void Release(Player* bot, MoveOwner owner)
    {
        if (!bot)
            return;

        auto itr = _claims.find(bot->GetGUID());
        if (itr == _claims.end())
            return;

        if (itr->second != owner)
            return;

        _claims.erase(itr);

        if (HasPointMovement(bot))
            bot->GetMotionMaster()->Clear();
    }

    void Forget(ObjectGuid botGuid)
    {
        _claims.erase(botGuid);
    }

    char const* OwnerName(MoveOwner owner)
    {
        switch (owner)
        {
            case MoveOwner::Ambient:      return "Ambient";
            case MoveOwner::Grind:        return "Grind";
            case MoveOwner::Gather:       return "Gather";
            case MoveOwner::Fish:         return "Fish";
            case MoveOwner::Quest:        return "Quest";
            case MoveOwner::QuestTracker: return "QuestTracker";
            case MoveOwner::Loot:         return "Loot";
            case MoveOwner::Corpse:       return "Corpse";
            case MoveOwner::AutoDungeon:  return "AutoDungeon";
            case MoveOwner::Battleground: return "Battleground";
            case MoveOwner::Avoidance:    return "Avoidance";
            default:                      return "None";
        }
    }
}
