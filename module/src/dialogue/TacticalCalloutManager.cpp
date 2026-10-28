#include "TacticalCalloutManager.h"
#include "BotDialogueManager.h"
#include "MechanicRegistry.h"

TacticalCalloutManager& TacticalCalloutManager::Instance()
{
    static TacticalCalloutManager instance;
    return instance;
}

DialogueBeat TacticalCalloutManager::MechanicToBeat(TacticalMechanicType type)
{
    switch (type)
    {
        case TacticalMechanicType::Interrupt:      return DialogueBeat::Interrupt;
        case TacticalMechanicType::Dispel:         return DialogueBeat::Dispel;
        case TacticalMechanicType::MoveFromGround: return DialogueBeat::MoveFromGround;
        case TacticalMechanicType::SpreadOut:      return DialogueBeat::SpreadOut;
        case TacticalMechanicType::StackTogether:  return DialogueBeat::StackTogether;
        case TacticalMechanicType::KillAdds:       return DialogueBeat::KillAdds;
        case TacticalMechanicType::TankInDanger:   return DialogueBeat::TankInDanger;
        case TacticalMechanicType::TargetSwitch:   return DialogueBeat::TargetSwitch;
        case TacticalMechanicType::CrowdControl:   return DialogueBeat::CrowdControl;
        case TacticalMechanicType::BurstNow:       return DialogueBeat::BurstNow;
        default:                                   return DialogueBeat::CombatShout;
    }
}

std::string TacticalCalloutManager::BuildMechanicKey(TacticalMechanicType type, uint32 mechanicId, uint64 sourceGuid, uint32 castId) const
{
    return "mech_" + std::to_string(static_cast<uint32>(type)) +
           "_" + std::to_string(mechanicId) +
           "_" + std::to_string(sourceGuid) +
           "_" + std::to_string(castId);
}

void TacticalCalloutManager::CleanExpired(uint32 nowMs)
{
    for (auto it = _activeCallouts.begin(); it != _activeCallouts.end();)
    {
        if (nowMs >= it->second)
            it = _activeCallouts.erase(it);
        else
            ++it;
    }
}

void TacticalCalloutManager::Reset()
{
    _activeCallouts.clear();
}

bool TacticalCalloutManager::TryEmitTacticalCallout(Player* callerBot, TacticalCalloutSignal const& signal, uint32 nowMs)
{
    CleanExpired(nowMs);

    std::string key = BuildMechanicKey(signal.mechanicType, signal.mechanicId, signal.sourceGuid, signal.castId);

    // If an identical mechanic callout was already emitted within the TTL window by any group member, suppress!
    auto it = _activeCallouts.find(key);
    if (it != _activeCallouts.end() && nowMs < it->second)
        return false;

    DialogueContext ctx;
    ctx.speaker = callerBot;
    ctx.priority = signal.priority;
    ctx.channel = DialogueChannel::Party; // Default tactical callout channel is Party
    ctx.spellName = signal.spellName;
    ctx.targetName = signal.targetName;
    ctx.enemyName = signal.enemyName;
    ctx.mechanicKey = key;

    bool emitted = sBotDialogueMgr.Emit(MechanicToBeat(signal.mechanicType), ctx, nowMs);
    if (emitted)
    {
        _activeCallouts[key] = nowMs + signal.deduplicationTtlMs;
    }

    return emitted;
}

bool TacticalCalloutManager::OnInterruptibleCast(Player* callerBot, uint64 casterGuid, uint32 spellId, std::string const& spellName, uint32 nowMs)
{
    TacticalCalloutSignal sig;
    sig.mechanicType = TacticalMechanicType::Interrupt;
    sig.mechanicId = spellId;
    sig.sourceGuid = casterGuid;
    sig.castId = spellId;
    sig.spellId = spellId;
    sig.spellName = spellName;
    sig.priority = DialoguePriority::High;
    sig.deduplicationTtlMs = 6000;
    return TryEmitTacticalCallout(callerBot, sig, nowMs);
}

bool TacticalCalloutManager::OnDispelAvailable(Player* callerBot, uint64 targetGuid, uint32 auraId, std::string const& auraName, uint32 nowMs)
{
    TacticalCalloutSignal sig;
    sig.mechanicType = TacticalMechanicType::Dispel;
    sig.mechanicId = auraId;
    sig.sourceGuid = targetGuid;
    sig.castId = auraId;
    sig.spellId = auraId;
    sig.spellName = auraName;
    sig.priority = DialoguePriority::Normal;
    sig.deduplicationTtlMs = 8000;
    return TryEmitTacticalCallout(callerBot, sig, nowMs);
}

bool TacticalCalloutManager::OnHazardousGround(Player* callerBot, uint32 areaSpellId, uint32 nowMs)
{
    TacticalCalloutSignal sig;
    sig.mechanicType = TacticalMechanicType::MoveFromGround;
    sig.mechanicId = areaSpellId;
    sig.sourceGuid = 0;
    sig.castId = areaSpellId;
    sig.spellId = areaSpellId;
    sig.priority = DialoguePriority::High;
    sig.deduplicationTtlMs = 8000;
    return TryEmitTacticalCallout(callerBot, sig, nowMs);
}

bool TacticalCalloutManager::OnSpreadRequired(Player* callerBot, uint32 mechanicId, uint32 nowMs)
{
    TacticalCalloutSignal sig;
    sig.mechanicType = TacticalMechanicType::SpreadOut;
    sig.mechanicId = mechanicId;
    sig.priority = DialoguePriority::High;
    sig.deduplicationTtlMs = 10000;
    return TryEmitTacticalCallout(callerBot, sig, nowMs);
}

bool TacticalCalloutManager::OnStackRequired(Player* callerBot, uint32 mechanicId, uint32 nowMs)
{
    TacticalCalloutSignal sig;
    sig.mechanicType = TacticalMechanicType::StackTogether;
    sig.mechanicId = mechanicId;
    sig.priority = DialoguePriority::High;
    sig.deduplicationTtlMs = 10000;
    return TryEmitTacticalCallout(callerBot, sig, nowMs);
}

bool TacticalCalloutManager::OnAddsSpawned(Player* callerBot, uint64 addGuid, std::string const& addName, uint32 nowMs)
{
    TacticalCalloutSignal sig;
    sig.mechanicType = TacticalMechanicType::KillAdds;
    sig.mechanicId = static_cast<uint32>(addGuid & 0xFFFFFFFF);
    sig.sourceGuid = addGuid;
    sig.enemyName = addName;
    sig.priority = DialoguePriority::High;
    sig.deduplicationTtlMs = 12000;
    return TryEmitTacticalCallout(callerBot, sig, nowMs);
}

bool TacticalCalloutManager::OnTankInDanger(Player* callerBot, uint64 tankGuid, std::string const& tankName, uint8 /*healthPct*/, uint32 nowMs)
{
    TacticalCalloutSignal sig;
    sig.mechanicType = TacticalMechanicType::TankInDanger;
    sig.mechanicId = static_cast<uint32>(tankGuid & 0xFFFFFFFF);
    sig.sourceGuid = tankGuid;
    sig.targetName = tankName;
    sig.priority = DialoguePriority::Critical;
    sig.deduplicationTtlMs = 6000;
    return TryEmitTacticalCallout(callerBot, sig, nowMs);
}

bool TacticalCalloutManager::OnTargetSwitchRequired(Player* callerBot, uint64 targetGuid, std::string const& targetName, uint32 nowMs)
{
    TacticalCalloutSignal sig;
    sig.mechanicType = TacticalMechanicType::TargetSwitch;
    sig.mechanicId = static_cast<uint32>(targetGuid & 0xFFFFFFFF);
    sig.sourceGuid = targetGuid;
    sig.targetName = targetName;
    sig.priority = DialoguePriority::Normal;
    sig.deduplicationTtlMs = 8000;
    return TryEmitTacticalCallout(callerBot, sig, nowMs);
}

bool TacticalCalloutManager::OnMechanicTrigger(Player* callerBot, uint32 spellId, uint64 casterGuid, uint32 creatureEntry, uint32 encounterId, uint32 castId, uint32 nowMs)
{
    if (!sMechanicRegistry.IsEnabled())
        return false;

    MechanicRecord const* rec = sMechanicRegistry.FindMechanic(spellId, creatureEntry, encounterId);
    if (!rec || rec->disabled)
        return false;

    // Suppress conflicting mechanics until resolved by override
    if (rec->confidence == MechanicConfidence::Conflict)
        return false;

    // Deduplication key: mechanicType + spellId + casterGuid + castId
    std::string key = rec->mechanicType + "_" + std::to_string(spellId) + "_" + std::to_string(casterGuid) + "_" + std::to_string(castId);
    CleanExpired(nowMs);
    auto it = _activeCallouts.find(key);
    if (it != _activeCallouts.end() && nowMs < it->second)
        return false;

    DialogueContext ctx;
    ctx.speaker = callerBot;
    ctx.channel = DialogueChannel::Party;
    ctx.priority = rec->priority;
    ctx.spellName = rec->spellName;
    ctx.mechanicKey = key;

    bool emitted = sBotDialogueMgr.Emit(rec->dialogueBeat, ctx, nowMs);
    if (emitted)
    {
        _activeCallouts[key] = nowMs + 6000;
    }
    return emitted;
}
