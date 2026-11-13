#ifndef COA_BOTS_SCHEDULING_H
#define COA_BOTS_SCHEDULING_H

#include "ObjectGuid.h"
#include "Define.h"
#include <functional>
#include <vector>

class Player;
class WorldSession;

namespace CoaBotScheduling
{
    void Configure();
    void AddSyntheticObserver(ObjectGuid guid);
    bool Enabled();
    bool Far(Player const* bot);
    bool Protected(Player const* bot);
    uint32 ScanInterval(Player const* bot);
    uint32 MaintenanceInterval(Player const* bot);
    void Notify(ObjectGuid guid, bool maintenance = false);
    bool ConsumeMaintenance(ObjectGuid guid);
    void Forget(ObjectGuid guid);
    void Clear();
    void Update(std::vector<WorldSession*> const& sessions, std::function<void(Player*, uint32)> const& dispatch);
}
#endif
