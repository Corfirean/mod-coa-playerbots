/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, cleanup balance rewrite.
 * Copyright (C) 2026 mod-coa-playerbots
 *
 * Tactical Mechanic Registry Implementation
 */

#include "MechanicRegistry.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cctype>
#include <algorithm>

namespace fs = std::filesystem;

MechanicRegistry& MechanicRegistry::Instance()
{
    static MechanicRegistry instance;
    return instance;
}

MechanicRegistry::MechanicRegistry()
{
}

void MechanicRegistry::Clear()
{
    std::unique_lock lock(_mutex);
    _recordsByKey.clear();
    _recordsBySpell.clear();
    _recordsByEncounter.clear();
}

void MechanicRegistry::Initialize(std::string const& dataDir)
{
    _dataDir = dataDir;
    Reload();
}

void MechanicRegistry::Reload()
{
    Clear();
    if (_dataDir.empty())
        return;

    std::string err;
    LoadFromDirectory(_dataDir, err);
}

void MechanicRegistry::ApplyRecord(MechanicRecord const& record)
{
    // Check if key already exists
    auto it = _recordsByKey.find(record.key);
    if (it != _recordsByKey.end())
    {
        // Precedence: Manual (Authoritative) > Core (Verified) > DBM (Imported)
        if (it->second.source == MechanicSource::Manual && record.source != MechanicSource::Manual)
            return; // Existing manual override has absolute precedence
    }

    _recordsByKey[record.key] = record;

    // Index by Spell
    auto& spellList = _recordsBySpell[record.spellId];
    bool replaced = false;
    for (auto& r : spellList)
    {
        if (r.key == record.key)
        {
            r = record;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        spellList.push_back(record);

    // Index by Encounter
    if (record.encounterId > 0)
    {
        auto& encList = _recordsByEncounter[record.encounterId];
        bool encReplaced = false;
        for (auto& r : encList)
        {
            if (r.key == record.key)
            {
                r = record;
                encReplaced = true;
                break;
            }
        }
        if (!encReplaced)
            encList.push_back(record);
    }
}

MechanicRecord const* MechanicRegistry::FindMechanic(uint32 spellId, uint32 creatureEntry, uint32 encounterId) const
{
    std::shared_lock lock(_mutex);
    auto it = _recordsBySpell.find(spellId);
    if (it == _recordsBySpell.end() || it->second.empty())
        return nullptr;

    // 1. Try to find exact match by creatureEntry or encounterId
    for (auto const& rec : it->second)
    {
        if (creatureEntry > 0 && rec.bossEntry == creatureEntry)
            return &rec;
        if (encounterId > 0 && rec.encounterId == encounterId)
            return &rec;
    }

    // 2. Return first active, non-disabled mechanic for this spell
    for (auto const& rec : it->second)
    {
        if (!rec.disabled && rec.confidence != MechanicConfidence::Conflict)
            return &rec;
    }

    // 3. Fallback to first record
    return &it->second.front();
}

std::vector<MechanicRecord const*> MechanicRegistry::FindMechanicsBySpell(uint32 spellId) const
{
    std::shared_lock lock(_mutex);
    std::vector<MechanicRecord const*> results;
    auto it = _recordsBySpell.find(spellId);
    if (it != _recordsBySpell.end())
    {
        for (auto const& rec : it->second)
            results.push_back(&rec);
    }
    return results;
}

std::vector<MechanicRecord const*> MechanicRegistry::FindMechanicsByEncounter(uint32 encounterId) const
{
    std::shared_lock lock(_mutex);
    std::vector<MechanicRecord const*> results;
    auto it = _recordsByEncounter.find(encounterId);
    if (it != _recordsByEncounter.end())
    {
        for (auto const& rec : it->second)
            results.push_back(&rec);
    }
    return results;
}

MechanicRegistryStats MechanicRegistry::GetStats() const
{
    std::shared_lock lock(_mutex);
    MechanicRegistryStats stats;
    stats.totalRecords = _recordsByKey.size();
    stats.uniqueSpells = _recordsBySpell.size();
    stats.uniqueEncounters = _recordsByEncounter.size();

    for (auto const& [k, rec] : _recordsByKey)
    {
        if (rec.disabled)
            stats.disabledCount++;
        if (rec.confidence == MechanicConfidence::Verified)
            stats.verifiedCount++;
        else if (rec.confidence == MechanicConfidence::Imported)
            stats.importedCount++;
        else if (rec.confidence == MechanicConfidence::Conflict)
            stats.conflictCount++;

        if (rec.source == MechanicSource::Manual)
            stats.manualOverrideCount++;
    }

    return stats;
}

// ============================================================================
// Tolerant JSON Parser for Mechanic Records
// ============================================================================
namespace
{
    void MechSkipWhitespace(std::string const& s, size_t& i)
    {
        while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',' || s[i] == ':'))
            ++i;
    }

    std::string MechParseJsonString(std::string const& s, size_t& i)
    {
        MechSkipWhitespace(s, i);
        if (i >= s.size() || s[i] != '"')
            return "";

        ++i; // skip opening quote
        std::string result;
        while (i < s.size() && s[i] != '"')
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                ++i;
                if (s[i] == 'n') result += '\n';
                else if (s[i] == 't') result += '\t';
                else if (s[i] == '"') result += '"';
                else if (s[i] == '\\') result += '\\';
                else result += s[i];
            }
            else
            {
                result += s[i];
            }
            ++i;
        }
        if (i < s.size())
            ++i; // skip closing quote
        return result;
    }

    int64 MechParseJsonInt(std::string const& s, size_t& i)
    {
        MechSkipWhitespace(s, i);
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+'))
            ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
            ++i;

        if (start == i)
            return 0;

        try {
            return std::stoll(s.substr(start, i - start));
        } catch (...) {
            return 0;
        }
    }
}

bool MechanicRegistry::ParseRecordFromJson(std::string const& jsonStr, size_t& i, MechanicRecord& outRecord)
{
    MechSkipWhitespace(jsonStr, i);
    if (i >= jsonStr.size() || jsonStr[i] != '{')
        return false;

    ++i; // skip '{'
    outRecord = MechanicRecord();

    while (i < jsonStr.size() && jsonStr[i] != '}')
    {
        MechSkipWhitespace(jsonStr, i);
        if (i >= jsonStr.size() || jsonStr[i] == '}')
            break;

        std::string key = MechParseJsonString(jsonStr, i);
        MechSkipWhitespace(jsonStr, i);

        if (key == "key")
            outRecord.key = MechParseJsonString(jsonStr, i);
        else if (key == "encounterId")
            outRecord.encounterId = static_cast<uint32>(MechParseJsonInt(jsonStr, i));
        else if (key == "mapId")
            outRecord.mapId = static_cast<uint32>(MechParseJsonInt(jsonStr, i));
        else if (key == "bossEntry")
            outRecord.bossEntry = static_cast<uint32>(MechParseJsonInt(jsonStr, i));
        else if (key == "spellId")
            outRecord.spellId = static_cast<uint32>(MechParseJsonInt(jsonStr, i));
        else if (key == "spellName")
            outRecord.spellName = MechParseJsonString(jsonStr, i);
        else if (key == "mechanicType")
        {
            outRecord.mechanicType = MechParseJsonString(jsonStr, i);
            outRecord.dialogueBeat = DialogueUtils::StringToBeat(outRecord.mechanicType);
        }
        else if (key == "dialogueBeat")
        {
            std::string beatStr = MechParseJsonString(jsonStr, i);
            DialogueBeat b = DialogueUtils::StringToBeat(beatStr);
            if (b != DialogueBeat::None)
                outRecord.dialogueBeat = b;
        }
        else if (key == "priority")
        {
            std::string pStr = MechParseJsonString(jsonStr, i);
            if (pStr == "Critical") outRecord.priority = DialoguePriority::CombatCritical;
            else if (pStr == "High") outRecord.priority = DialoguePriority::CombatHigh;
            else if (pStr == "Normal") outRecord.priority = DialoguePriority::Downtime;
            else if (pStr == "Low") outRecord.priority = DialoguePriority::Ambient;
        }
        else if (key == "source")
        {
            std::string sStr = MechParseJsonString(jsonStr, i);
            if (sStr == "Manual") outRecord.source = MechanicSource::Manual;
            else if (sStr == "Core") outRecord.source = MechanicSource::Core;
            else outRecord.source = MechanicSource::DBM;
        }
        else if (key == "confidence")
        {
            std::string cStr = MechParseJsonString(jsonStr, i);
            if (cStr == "Authoritative") outRecord.confidence = MechanicConfidence::Authoritative;
            else if (cStr == "Verified") outRecord.confidence = MechanicConfidence::Verified;
            else if (cStr == "Conflict") outRecord.confidence = MechanicConfidence::Conflict;
            else if (cStr == "Heuristic") outRecord.confidence = MechanicConfidence::Heuristic;
            else outRecord.confidence = MechanicConfidence::Imported;
        }
        else if (key == "phase")
            outRecord.phase = static_cast<uint32>(MechParseJsonInt(jsonStr, i));
        else if (key == "estimatedTimerMs")
            outRecord.estimatedTimerMs = static_cast<uint32>(MechParseJsonInt(jsonStr, i));
        else if (key == "triggerEvent")
            outRecord.triggerEvent = MechParseJsonString(jsonStr, i);
        else if (key == "disabled")
        {
            MechSkipWhitespace(jsonStr, i);
            if (jsonStr.compare(i, 4, "true") == 0) { outRecord.disabled = true; i += 4; }
            else if (jsonStr.compare(i, 5, "false") == 0) { outRecord.disabled = false; i += 5; }
        }
        else if (key == "replacementMechanicId")
            outRecord.replacementMechanicId = MechParseJsonString(jsonStr, i);
        else if (key == "notes")
            outRecord.notes = MechParseJsonString(jsonStr, i);
        else if (key == "roleMask")
        {
            MechSkipWhitespace(jsonStr, i);
            if (i < jsonStr.size() && jsonStr[i] == '[')
            {
                ++i;
                RoleRelevance mask = RoleRelevance::None;
                while (i < jsonStr.size() && jsonStr[i] != ']')
                {
                    std::string roleStr = MechParseJsonString(jsonStr, i);
                    if (roleStr == "Tank") mask = mask | RoleRelevance::Tank;
                    else if (roleStr == "Healer") mask = mask | RoleRelevance::Healer;
                    else if (roleStr == "Dps") mask = mask | RoleRelevance::Dps;
                }
                if (i < jsonStr.size() && jsonStr[i] == ']')
                    ++i;
                if (mask != RoleRelevance::None)
                    outRecord.roleMask = mask;
            }
        }
        else
        {
            // Skip unknown value (object, array, string, number, bool)
            MechSkipWhitespace(jsonStr, i);
            if (i < jsonStr.size())
            {
                if (jsonStr[i] == '{')
                {
                    int depth = 1; ++i;
                    while (i < jsonStr.size() && depth > 0)
                    {
                        if (jsonStr[i] == '{') ++depth;
                        else if (jsonStr[i] == '}') --depth;
                        ++i;
                    }
                }
                else if (jsonStr[i] == '[')
                {
                    int depth = 1; ++i;
                    while (i < jsonStr.size() && depth > 0)
                    {
                        if (jsonStr[i] == '[') ++depth;
                        else if (jsonStr[i] == ']') --depth;
                        ++i;
                    }
                }
                else if (jsonStr[i] == '"')
                    MechParseJsonString(jsonStr, i);
                else
                {
                    while (i < jsonStr.size() && jsonStr[i] != ',' && jsonStr[i] != '}')
                        ++i;
                }
            }
        }
    }

    if (i < jsonStr.size() && jsonStr[i] == '}')
        ++i;

    if (outRecord.key.empty())
        outRecord.key = std::to_string(outRecord.spellId) + "_" + outRecord.mechanicType;

    return (outRecord.spellId > 0 || !outRecord.key.empty());
}

bool MechanicRegistry::LoadGeneratedJson(std::string const& filePath, std::string& outError)
{
    std::ifstream f(filePath);
    if (!f.is_open())
    {
        outError = "Could not open file: " + filePath;
        return false;
    }
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string jsonStr = buffer.str();

    std::unique_lock lock(_mutex);
    size_t i = 0;
    MechSkipWhitespace(jsonStr, i);
    if (i >= jsonStr.size() || jsonStr[i] != '[')
    {
        outError = "Invalid JSON array root in: " + filePath;
        return false;
    }

    ++i; // skip '['
    while (i < jsonStr.size())
    {
        MechSkipWhitespace(jsonStr, i);
        if (i >= jsonStr.size() || jsonStr[i] == ']')
            break;

        MechanicRecord rec;
        if (ParseRecordFromJson(jsonStr, i, rec))
            ApplyRecord(rec);
        else
            ++i;
    }

    return true;
}

bool MechanicRegistry::LoadOverridesJson(std::string const& filePath, std::string& outError)
{
    std::ifstream f(filePath);
    if (!f.is_open())
    {
        outError = "Could not open file: " + filePath;
        return false;
    }
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string jsonStr = buffer.str();

    std::unique_lock lock(_mutex);
    size_t i = 0;
    MechSkipWhitespace(jsonStr, i);
    if (i >= jsonStr.size() || jsonStr[i] != '[')
    {
        outError = "Invalid JSON array root in overrides: " + filePath;
        return false;
    }

    ++i; // skip '['
    while (i < jsonStr.size())
    {
        MechSkipWhitespace(jsonStr, i);
        if (i >= jsonStr.size() || jsonStr[i] == ']')
            break;

        MechanicRecord rec;
        rec.source = MechanicSource::Manual;
        rec.confidence = MechanicConfidence::Authoritative;

        if (ParseRecordFromJson(jsonStr, i, rec))
        {
            rec.source = MechanicSource::Manual;
            rec.confidence = MechanicConfidence::Authoritative;
            ApplyRecord(rec);
        }
        else
            ++i;
    }

    return true;
}

bool MechanicRegistry::LoadFromDirectory(std::string const& dirPath, std::string& outError)
{
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath))
    {
        outError = "Mechanics directory does not exist: " + dirPath;
        return false;
    }

    std::string generatedPath = dirPath + "/mechanic_registry.generated.json";
    std::string overridesPath = dirPath + "/mechanic_registry.overrides.json";

    bool genOk = false;
    if (fs::exists(generatedPath))
    {
        std::string err;
        genOk = LoadGeneratedJson(generatedPath, err);
    }

    // Overrides are always loaded AFTER generated so they take precedence
    if (fs::exists(overridesPath))
    {
        std::string err;
        LoadOverridesJson(overridesPath, err);
    }

    return genOk;
}
