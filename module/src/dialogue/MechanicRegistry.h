/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, cleanup balance rewrite.
 * Copyright (C) 2026 mod-coa-playerbots
 *
 * Tactical Mechanic Registry (Server-side normalized boss & dungeon mechanics)
 */

#ifndef COA_PLAYERBOTS_MECHANIC_REGISTRY_H
#define COA_PLAYERBOTS_MECHANIC_REGISTRY_H

#include "DialogueTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <cstdint>

enum class MechanicSource : uint8
{
    DBM = 0,
    Core = 1,
    Manual = 2
};

enum class MechanicConfidence : uint8
{
    Heuristic = 0,
    Imported = 1,
    Verified = 2,
    Authoritative = 3,
    Conflict = 4
};

enum class RoleRelevance : uint8
{
    None = 0,
    Tank = 1 << 0,
    Healer = 1 << 1,
    Dps = 1 << 2,
    All = Tank | Healer | Dps
};

inline RoleRelevance operator|(RoleRelevance a, RoleRelevance b)
{
    return static_cast<RoleRelevance>(static_cast<uint8>(a) | static_cast<uint8>(b));
}

inline RoleRelevance operator&(RoleRelevance a, RoleRelevance b)
{
    return static_cast<RoleRelevance>(static_cast<uint8>(a) & static_cast<uint8>(b));
}

inline bool HasRole(RoleRelevance mask, RoleRelevance role)
{
    return (static_cast<uint8>(mask) & static_cast<uint8>(role)) != 0;
}

struct MechanicProvenance
{
    std::string package;
    std::string module;
    uint32 line = 0;
    std::string construct;
};

struct MechanicRecord
{
    std::string key;
    uint32 encounterId = 0;
    uint32 mapId = 0;
    uint32 bossEntry = 0;
    uint32 spellId = 0;
    std::string spellName;
    std::string mechanicType;
    DialogueBeat dialogueBeat = DialogueBeat::None;
    RoleRelevance roleMask = RoleRelevance::All;
    DialoguePriority priority = DialoguePriority::CombatHigh;
    MechanicSource source = MechanicSource::DBM;
    MechanicConfidence confidence = MechanicConfidence::Imported;
    uint32 phase = 0;
    uint32 estimatedTimerMs = 0;
    std::string triggerEvent;
    bool disabled = false;
    std::string replacementMechanicId;
    MechanicProvenance provenance;
    std::string notes;
};

struct MechanicRegistryStats
{
    size_t totalRecords = 0;
    size_t verifiedCount = 0;
    size_t importedCount = 0;
    size_t manualOverrideCount = 0;
    size_t disabledCount = 0;
    size_t conflictCount = 0;
    size_t uniqueSpells = 0;
    size_t uniqueEncounters = 0;
};

class MechanicRegistry
{
public:
    static MechanicRegistry& Instance();

    void Initialize(std::string const& dataDir);
    void Reload();

    bool LoadGeneratedJson(std::string const& filePath, std::string& outError);
    bool LoadOverridesJson(std::string const& filePath, std::string& outError);
    bool LoadFromDirectory(std::string const& dirPath, std::string& outError);

    MechanicRecord const* FindMechanic(uint32 spellId, uint32 creatureEntry = 0, uint32 encounterId = 0) const;
    std::vector<MechanicRecord const*> FindMechanicsBySpell(uint32 spellId) const;
    std::vector<MechanicRecord const*> FindMechanicsByEncounter(uint32 encounterId) const;

    MechanicRegistryStats GetStats() const;
    void Clear();

    bool IsEnabled() const { return _enabled; }
    void SetEnabled(bool enabled) { _enabled = enabled; }

private:
    MechanicRegistry();
    ~MechanicRegistry() = default;

    void ApplyRecord(MechanicRecord const& record);
    bool ParseRecordFromJson(std::string const& jsonStr, size_t& i, MechanicRecord& outRecord);

    mutable std::shared_mutex _mutex;
    bool _enabled = true;
    std::string _dataDir;

    std::unordered_map<std::string, MechanicRecord> _recordsByKey;
    std::unordered_map<uint32, std::vector<MechanicRecord>> _recordsBySpell;
    std::unordered_map<uint32, std::vector<MechanicRecord>> _recordsByEncounter;
};

#define sMechanicRegistry MechanicRegistry::Instance()

#endif // COA_PLAYERBOTS_MECHANIC_REGISTRY_H
