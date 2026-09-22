/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: CombatResource implementation
 */

#include "engine/CombatResource.h"
#include "Log.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <unordered_map>

namespace BotAI
{
    namespace
    {
        // Cache for the custom (AuraStack) half of ResolveRequirements/ResolveGains -- pure
        // static-table lookups (AscensionCompatData::ResourceCostRules/ResourceGainRules/
        // NativePowerGainRules never change at runtime), so caching by (classId, resolvedSpellId)
        // is always safe and never goes stale. The native-power half is NOT cached here -- see
        // CombatResourceEvaluator::ResolveRequirements's own comment on why it needs a real bot.
        uint64 MakeKey(uint8 classId, uint32 resolvedSpellId)
        {
            return (uint64(classId) << 32) | resolvedSpellId;
        }

        std::vector<AscensionCompatData::ResourceRequirement> const& CachedCustomRequirements(
            uint8 classId, uint32 resolvedSpellId)
        {
            static std::unordered_map<uint64, std::vector<AscensionCompatData::ResourceRequirement>> cache;
            uint64 key = MakeKey(classId, resolvedSpellId);
            auto itr = cache.find(key);
            if (itr != cache.end())
                return itr->second;
            return cache.emplace(key,
                AscensionCompatData::QueryAbilityResourceRequirements(classId, resolvedSpellId)).first->second;
        }

        std::vector<AscensionCompatData::ResourceGain> const& CachedGains(
            uint8 classId, uint32 resolvedSpellId)
        {
            static std::unordered_map<uint64, std::vector<AscensionCompatData::ResourceGain>> cache;
            uint64 key = MakeKey(classId, resolvedSpellId);
            auto itr = cache.find(key);
            if (itr != cache.end())
                return itr->second;
            return cache.emplace(key,
                AscensionCompatData::QueryAbilityResourceGains(classId, resolvedSpellId)).first->second;
        }
    }

    CombatResourceSnapshot CombatResourceEvaluator::BuildSnapshot(Player* bot)
    {
        CombatResourceSnapshot snapshot;
        if (!bot)
            return snapshot;

        // Native primary power bar -- always present, even for a class whose real economy is a
        // custom resource (some still spend native power on a subset of abilities alongside it).
        Powers powerType = bot->getPowerType();
        CombatResourceState native;
        native.key = CombatResourceKey{CombatResourceKind::NativePower, uint8(powerType), 0};
        native.current = int32(bot->GetPower(powerType));
        native.maximum = int32(bot->GetMaxPower(powerType));
        native.maximumKnown = native.maximum > 0;
        native.name = "Native power";
        snapshot.Add(native);

        uint8 classId = bot->getClass();
        for (AscensionCompatData::ResourceChannel const& channel : AscensionCompatData::GetResourceChannels(classId))
        {
            if (channel.IsNative)
            {
                // A second native channel beyond the bot's own primary bar -- currently only
                // Reaper's Runic Power (see AscensionResourceQuery.h's GetResourceChannels
                // comment for why it's listed explicitly). Skip if it happens to already be the
                // bot's own primary bar, to avoid double-reporting the same channel.
                Powers extraType = Powers(channel.NativePowerType);
                if (extraType == powerType)
                    continue;

                CombatResourceState extra;
                extra.key = CombatResourceKey{CombatResourceKind::NativePower, channel.NativePowerType, 0};
                extra.current = int32(bot->GetPower(extraType));
                extra.maximum = int32(bot->GetMaxPower(extraType));
                extra.maximumKnown = extra.maximum > 0;
                extra.name = channel.Name;
                snapshot.Add(extra);
                continue;
            }

            AscensionCompatData::ResourceState auraState =
                AscensionCompatData::QueryAuraResourceState(bot, channel.AuraSpellId);
            CombatResourceState state;
            state.key = CombatResourceKey{CombatResourceKind::AuraStack, 0, channel.AuraSpellId};
            state.current = int32(auraState.Current);
            state.maximum = int32(auraState.Maximum);
            state.maximumKnown = auraState.MaximumKnown;
            state.name = channel.Name;
            snapshot.Add(state);
        }

        return snapshot;
    }

    std::vector<CombatResourceEvaluator::Requirement> CombatResourceEvaluator::ResolveRequirements(
        Player* bot, uint32 resolvedSpellId)
    {
        std::vector<Requirement> out;
        if (!bot || !resolvedSpellId)
            return out;

        if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(resolvedSpellId))
        {
            int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
            if (cost > 0 && spellInfo->PowerType != POWER_HEALTH)
            {
                Requirement req;
                req.key = CombatResourceKey{CombatResourceKind::NativePower, uint8(spellInfo->PowerType), 0};
                req.amount = cost;
                req.consumption = AscensionCompatData::ResourceConsumption::Fixed;
                out.push_back(req);
            }
        }

        uint8 classId = bot->getClass();
        for (AscensionCompatData::ResourceRequirement const& custom :
             CachedCustomRequirements(classId, resolvedSpellId))
        {
            Requirement req;
            req.key = CombatResourceKey{CombatResourceKind::AuraStack, 0, custom.ResourceSpellId};
            req.amount = int32(custom.Amount);
            req.consumption = custom.Consumption;
            req.preserveCostAuraSpellId = custom.PreserveCostAuraSpellId;
            req.preserveCostChancePercent = custom.PreserveCostChancePercent;
            out.push_back(req);
        }

        return out;
    }

    std::vector<CombatResourceEvaluator::Gain> const& CombatResourceEvaluator::ResolveGains(
        uint8 classId, uint32 resolvedSpellId)
    {
        static std::unordered_map<uint64, std::vector<Gain>> cache;
        uint64 key = MakeKey(classId, resolvedSpellId);
        auto itr = cache.find(key);
        if (itr != cache.end())
            return itr->second;

        std::vector<Gain> out;
        for (AscensionCompatData::ResourceGain const& source : CachedGains(classId, resolvedSpellId))
        {
            Gain gain;
            gain.key = source.IsNative
                ? CombatResourceKey{CombatResourceKind::NativePower, source.NativePowerType, 0}
                : CombatResourceKey{CombatResourceKind::AuraStack, 0, source.ResourceSpellId};
            gain.amount = source.Amount;
            gain.event = source.Event;
            gain.requiredAuraSpellId = source.RequiredAuraSpellId;
            gain.forbiddenAuraSpellId = source.ForbiddenAuraSpellId;
            gain.chancePercent = source.ChancePercent;
            out.push_back(gain);
        }

        return cache.emplace(key, std::move(out)).first->second;
    }

    bool CombatResourceEvaluator::CanAfford(CombatResourceSnapshot const& snapshot, Player* bot, uint32 resolvedSpellId)
    {
        if (!bot)
            return false;

        for (Requirement const& req : ResolveRequirements(bot, resolvedSpellId))
        {
            // An unconfirmed preserve-cost proc never counts as "affordable" -- see item 10 and
            // this function's own header comment. Evaluated as if the proc will not trigger.
            CombatResourceState const* state = snapshot.Find(req.key);
            int32 have = state ? state->current : 0;
            if (have < req.amount)
            {
                // Debug-only (item 28/33): visible with a debug log level during targeted
                // testing, silent at the server's normal INFO level -- this can fire for every
                // ability on every resource-gated class's every tick otherwise.
                LOG_DEBUG("module.coa-playerbots",
                    "CombatResource: bot '{}' can't afford spell {} -- needs {} of resource {}, has {}.",
                    bot->GetName(), resolvedSpellId, req.amount,
                    req.key.kind == CombatResourceKind::NativePower ?
                        uint32(req.key.powerType) : req.key.auraSpellId,
                    have);
                return false;
            }
        }

        return true;
    }
}
