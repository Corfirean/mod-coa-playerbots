#pragma once

#include "DialogueTypes.h"
#include <string>
#include <unordered_map>
#include <cstdint>

class Player;

enum class TacticalMechanicType : uint8
{
    Interrupt = 0,
    Dispel,
    MoveFromGround,
    SpreadOut,
    StackTogether,
    KillAdds,
    TankInDanger,
    TargetSwitch,
    CrowdControl,
    BurstNow
};

struct TacticalCalloutSignal
{
    TacticalMechanicType mechanicType = TacticalMechanicType::Interrupt;
    uint32 mechanicId = 0;
    uint64 sourceGuid = 0;
    uint32 castId = 0;
    uint32 spellId = 0;
    std::string spellName;
    std::string targetName;
    std::string enemyName;
    DialoguePriority priority = DialoguePriority::High;
    uint32 deduplicationTtlMs = 6000; // Suppression window for identical mechanic
};

class TacticalCalloutManager
{
public:
    static TacticalCalloutManager& Instance();

    // Core entry point: attempts to emit a tactical callout for the party.
    // Suppresses duplicates within the party using mechanicKey (type + mechanicId + sourceGuid + castId).
    bool TryEmitTacticalCallout(Player* callerBot, TacticalCalloutSignal const& signal, uint32 nowMs);

    // Convenience triggers called by Combat Intelligence / spell hook / encounter events
    bool OnInterruptibleCast(Player* callerBot, uint64 casterGuid, uint32 spellId, std::string const& spellName, uint32 nowMs);
    bool OnDispelAvailable(Player* callerBot, uint64 targetGuid, uint32 auraId, std::string const& auraName, uint32 nowMs);
    bool OnHazardousGround(Player* callerBot, uint32 areaSpellId, uint32 nowMs);
    bool OnSpreadRequired(Player* callerBot, uint32 mechanicId, uint32 nowMs);
    bool OnStackRequired(Player* callerBot, uint32 mechanicId, uint32 nowMs);
    bool OnAddsSpawned(Player* callerBot, uint64 addGuid, std::string const& addName, uint32 nowMs);
    bool OnTankInDanger(Player* callerBot, uint64 tankGuid, std::string const& tankName, uint8 healthPct, uint32 nowMs);
    bool OnTargetSwitchRequired(Player* callerBot, uint64 targetGuid, std::string const& targetName, uint32 nowMs);

    // DBM-imported & verified mechanic integration
    bool OnMechanicTrigger(Player* callerBot, uint32 spellId, uint64 casterGuid, uint32 creatureEntry, uint32 encounterId, uint32 castId, uint32 nowMs);

    void Reset();
    void CleanExpired(uint32 nowMs);

    // Map mechanic to DialogueBeat
    static DialogueBeat MechanicToBeat(TacticalMechanicType type);

private:
    TacticalCalloutManager() = default;

    std::string BuildMechanicKey(TacticalMechanicType type, uint32 mechanicId, uint64 sourceGuid, uint32 castId) const;

    // key -> expiry timestamp (ms)
    std::unordered_map<std::string, uint32> _activeCallouts;
};

#define sTacticalCalloutMgr TacticalCalloutManager::Instance()
