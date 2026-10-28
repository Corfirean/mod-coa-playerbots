#include "EncounterConfig.h"
#include "Config.h"
#include <algorithm>
#include <string>

namespace
{
    EncounterConfig _cfg;

    bool ReadBoolOption(char const* suffix, bool def)
    {
        std::string keyA = std::string("BotAI.DynamicEncounters.") + suffix;
        std::string keyB = std::string("CoaBots.DynamicEncounters.") + suffix;
        return sConfigMgr->GetOption<bool>(keyA, sConfigMgr->GetOption<bool>(keyB, def));
    }

    uint32 ReadUIntOption(char const* suffix, uint32 def)
    {
        std::string keyA = std::string("BotAI.DynamicEncounters.") + suffix;
        std::string keyB = std::string("CoaBots.DynamicEncounters.") + suffix;
        return sConfigMgr->GetOption<uint32>(keyA, sConfigMgr->GetOption<uint32>(keyB, def));
    }

    float ReadFloatOption(char const* suffix, float def)
    {
        std::string keyA = std::string("BotAI.DynamicEncounters.") + suffix;
        std::string keyB = std::string("CoaBots.DynamicEncounters.") + suffix;
        return sConfigMgr->GetOption<float>(keyA, sConfigMgr->GetOption<float>(keyB, def));
    }
}

namespace EncounterSettings
{
    void LoadFromConfig()
    {
        EncounterConfig c;

        c.enabled = ReadBoolOption("Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::AssassinContract)]      = ReadBoolOption("AssassinContract.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::TheWatcher)]            = ReadBoolOption("TheWatcher.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::FactionSkirmish)]       = ReadBoolOption("FactionSkirmish.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::PatrolAmbush)]          = ReadBoolOption("PatrolAmbush.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::BountyHunter)]          = ReadBoolOption("BountyHunter.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::Nemesis)]               = ReadBoolOption("Nemesis.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::RoadDuel)]              = ReadBoolOption("RoadDuel.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::Caravan)]               = ReadBoolOption("Caravan.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::Deserter)]              = ReadBoolOption("Deserter.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::HunterAndPrey)]         = ReadBoolOption("HunterAndPrey.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::RivalAdventuringParty)] = ReadBoolOption("RivalAdventuringParty.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::RareHunter)]            = ReadBoolOption("RareHunter.Enable", true);
        c.typeEnabled[static_cast<size_t>(EncounterType::MysteryEncounter)]      = ReadBoolOption("MysteryEncounter.Enable", true);

        c.thinkIntervalMs = std::max<uint32>(1000u, ReadUIntOption("ThinkIntervalMs", c.thinkIntervalMs));
        c.rareScanIntervalMs = std::max<uint32>(5000u, ReadUIntOption("RareHunter.ScanIntervalMs", c.rareScanIntervalMs));

        c.maxGlobalActive = ReadUIntOption("MaxGlobalActive", c.maxGlobalActive);
        c.maxMapActive = ReadUIntOption("MaxMapActive", c.maxMapActive);
        c.maxPerPlayerActive = ReadUIntOption("MaxPerPlayerActive", c.maxPerPlayerActive);

        c.playerPersonalCooldownMs = ReadUIntOption("PersonalCooldownSec", c.playerPersonalCooldownMs / 1000u) * 1000u;
        c.botCooldownMs = ReadUIntOption("BotCooldownSec", c.botCooldownMs / 1000u) * 1000u;
        c.globalTypeCooldownMs = ReadUIntOption("TypeCooldownSec", c.globalTypeCooldownMs / 1000u) * 1000u;
        c.bountyCooldownMs = ReadUIntOption("BountyHunter.CooldownSec", c.bountyCooldownMs / 1000u) * 1000u;
        c.mysteryCooldownMs = ReadUIntOption("MysteryEncounter.CooldownSec", c.mysteryCooldownMs / 1000u) * 1000u;

        c.maxLevelDiff = ReadUIntOption("MaxLevelDiff", c.maxLevelDiff);
        c.maxPlayerPartySizeForSoloAssassin = ReadUIntOption("AssassinContract.MaxPlayerPartySize", c.maxPlayerPartySizeForSoloAssassin);
        c.searchRadiusYards = std::clamp(ReadFloatOption("SearchRadiusYards", c.searchRadiusYards), 40.0f, 600.0f);
        c.maxChaseDistanceYards = std::clamp(ReadFloatOption("MaxChaseDistanceYards", c.maxChaseDistanceYards), 60.0f, 800.0f);
        c.minCityDistanceYards = std::clamp(ReadFloatOption("MinCityDistanceYards", c.minCityDistanceYards), 50.0f, 600.0f);

        c.skirmishMinPerSide = std::clamp<uint32>(ReadUIntOption("FactionSkirmish.MinPerSide", c.skirmishMinPerSide), 1u, 10u);
        c.skirmishMaxPerSide = std::clamp<uint32>(ReadUIntOption("FactionSkirmish.MaxPerSide", c.skirmishMaxPerSide), c.skirmishMinPerSide, 10u);
        c.caravanMinMembers = std::clamp<uint32>(ReadUIntOption("Caravan.MinMembers", c.caravanMinMembers), 2u, 10u);
        c.caravanMaxMembers = std::clamp<uint32>(ReadUIntOption("Caravan.MaxMembers", c.caravanMaxMembers), c.caravanMinMembers, 10u);
        c.rivalPartyMinMembers = std::clamp<uint32>(ReadUIntOption("RivalAdventuringParty.MinMembers", c.rivalPartyMinMembers), 2u, 8u);
        c.rivalPartyMaxMembers = std::clamp<uint32>(ReadUIntOption("RivalAdventuringParty.MaxMembers", c.rivalPartyMaxMembers), c.rivalPartyMinMembers, 8u);
        c.rareHunterMaxBots = std::clamp<uint32>(ReadUIntOption("RareHunter.MaxBots", c.rareHunterMaxBots), 1u, 5u);

        c.prepareTimeoutMs = std::max<uint32>(10000u, ReadUIntOption("PrepareTimeoutSec", c.prepareTimeoutMs / 1000u) * 1000u);
        c.activeTimeoutMs = std::max<uint32>(15000u, ReadUIntOption("ActiveTimeoutSec", c.activeTimeoutMs / 1000u) * 1000u);
        c.resolveTimeoutMs = std::max<uint32>(3000u, ReadUIntOption("ResolveTimeoutSec", c.resolveTimeoutMs / 1000u) * 1000u);
        c.caravanActiveTimeoutMs = std::max<uint32>(30000u, ReadUIntOption("Caravan.TimeoutSec", c.caravanActiveTimeoutMs / 1000u) * 1000u);

        c.narratorEnabled = ReadBoolOption("Narrator.Enable", c.narratorEnabled);
        c.narratorPlayerCooldownMs = ReadUIntOption("Narrator.PlayerCooldownSec", c.narratorPlayerCooldownMs / 1000u) * 1000u;
        c.narratorBotCooldownMs = ReadUIntOption("Narrator.BotCooldownSec", c.narratorBotCooldownMs / 1000u) * 1000u;
        c.narratorMaxDistanceYards = std::clamp(ReadFloatOption("Narrator.MaxDistanceYards", c.narratorMaxDistanceYards), 15.0f, 120.0f);

        c.bountyKillScore = ReadUIntOption("BountyHunter.KillScore", c.bountyKillScore);
        c.bountyThreshold = std::max<uint32>(10u, ReadUIntOption("BountyHunter.Threshold", c.bountyThreshold));
        c.bountyDecayPerMinute = ReadUIntOption("BountyHunter.DecayPerMinute", c.bountyDecayPerMinute);
        c.nemesisTtlMs = ReadUIntOption("Nemesis.TtlSec", c.nemesisTtlMs / 1000u) * 1000u;
        c.nemesisDecayIntervalMs = std::max<uint32>(60000u, ReadUIntOption("Nemesis.DecayIntervalSec", c.nemesisDecayIntervalMs / 1000u) * 1000u);
        c.nemesisMinGrudgeScore = std::max<uint32>(1u, ReadUIntOption("Nemesis.MinGrudgeScore", c.nemesisMinGrudgeScore));

        c.mysteryChancePermil = std::min<uint32>(1000u, ReadUIntOption("MysteryEncounter.ChancePermil", c.mysteryChancePermil));
        c.mysterySpellVisualId = ReadUIntOption("MysteryEncounter.SpellVisualId", c.mysterySpellVisualId);
        c.mysteryRuneGameObjectEntry = ReadUIntOption("MysteryEncounter.RuneGameObjectEntry", c.mysteryRuneGameObjectEntry);
        c.mysteryMorphDisplayId = ReadUIntOption("MysteryEncounter.MorphDisplayId", c.mysteryMorphDisplayId);

        _cfg = c;
    }

    EncounterConfig const& Get()
    {
        return _cfg;
    }

    EncounterConfig& MutableForTest()
    {
        return _cfg;
    }
}
