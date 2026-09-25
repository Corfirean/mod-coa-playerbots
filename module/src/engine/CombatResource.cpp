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
#include <array>
#include <unordered_map>

namespace BotAI
{
    namespace
    {
        // Up to this many custom (AuraStack) requirements per (classId, resolvedSpellId) --
        // ResourceCostRules itself never yields more than one (confirmed by reading
        // AscensionResourceService::CheckCast's own first-match-then-break), and even the busiest
        // hardcoded special case found by the full audit (Knight of Xoroth: a Demonfire spender
        // check PLUS a separate Blood-gated-spell check can both match the same spell id in
        // principle) never exceeds 2-3 in practice. 4 leaves headroom without needing a heap
        // container on the hot CanAfford path (item 10).
        constexpr size_t MAX_CACHED_REQUIREMENTS = 4;

        struct CachedRequirement
        {
            CombatResourceKey key;
            int32 amount = 0;
            AscensionCompatData::ResourceConsumption consumption = AscensionCompatData::ResourceConsumption::Fixed;
            uint32 requiredAuraSpellId = 0;
            uint32 forbiddenAuraSpellId = 0;
        };

        struct CachedRequirementSet
        {
            std::array<CachedRequirement, MAX_CACHED_REQUIREMENTS> items{};
            uint8 count = 0;
        };

        uint64 MakeKey(uint8 classId, uint32 resolvedSpellId)
        {
            return (uint64(classId) << 32) | resolvedSpellId;
        }

        // Cache model (item 11): this module is only ever driven from the world-update loop, and
        // this deployment runs MapUpdate.Threads = 1 (a single world thread updates every map) --
        // the same unsynchronized-static-cache pattern already used throughout this engine
        // (DamageTracker, CombatReservations, TargetEvaluator's target-lock map, etc.). If that
        // threading assumption ever changes, every one of those caches needs the same fix
        // together, not just this one. The underlying core tables these caches are built from
        // never change at runtime, so a cached entry never goes stale -- there's no invalidation
        // to get wrong, only the (non-)concurrency of the first insert to reason about.
        CachedRequirementSet const& CachedCustomRequirements(uint8 classId, uint32 resolvedSpellId)
        {
            static std::unordered_map<uint64, CachedRequirementSet> cache;
            uint64 key = MakeKey(classId, resolvedSpellId);
            auto itr = cache.find(key);
            if (itr != cache.end())
                return itr->second;

            CachedRequirementSet set;
            for (AscensionCompatData::ResourceRequirement const& custom :
                 AscensionCompatData::QueryAbilityResourceRequirements(classId, resolvedSpellId))
            {
                if (set.count >= MAX_CACHED_REQUIREMENTS)
                {
                    LOG_ERROR("module.coa-playerbots",
                        "CombatResource: class {} spell {} has more than {} custom resource "
                        "requirements -- one was dropped, raise MAX_CACHED_REQUIREMENTS.",
                        uint32(classId), resolvedSpellId, MAX_CACHED_REQUIREMENTS);
                    break;
                }
                CachedRequirement& req = set.items[set.count++];
                req.key = CombatResourceKey{CombatResourceKind::AuraStack, 0, custom.ResourceSpellId};
                req.amount = int32(custom.Amount);
                req.consumption = custom.Consumption;
                req.requiredAuraSpellId = custom.RequiredAuraSpellId;
                req.forbiddenAuraSpellId = custom.ForbiddenAuraSpellId;
            }

            return cache.emplace(key, set).first->second;
        }

        std::vector<AscensionCompatData::AuraGate> const& CachedAuraGates(uint8 classId, uint32 resolvedSpellId)
        {
            static std::unordered_map<uint64, std::vector<AscensionCompatData::AuraGate>> cache;
            uint64 key = MakeKey(classId, resolvedSpellId);
            auto itr = cache.find(key);
            if (itr != cache.end())
                return itr->second;
            return cache.emplace(key,
                AscensionCompatData::QueryAbilityAuraGates(classId, resolvedSpellId)).first->second;
        }

        std::vector<AscensionCompatData::ResourceGain> const& CachedGains(uint8 classId, uint32 resolvedSpellId)
        {
            static std::unordered_map<uint64, std::vector<AscensionCompatData::ResourceGain>> cache;
            uint64 key = MakeKey(classId, resolvedSpellId);
            auto itr = cache.find(key);
            if (itr != cache.end())
                return itr->second;
            return cache.emplace(key,
                AscensionCompatData::QueryAbilityResourceGains(classId, resolvedSpellId)).first->second;
        }

        // True while `req`'s condition (if any) holds for `bot` right now -- no condition means
        // always active. Mirrors exactly what the real gameplay gate checks (e.g. Cultist's
        // `!player->HasAura(Madness)`), never an approximation of it.
        bool IsRequirementActive(Player* bot, uint32 requiredAuraSpellId, uint32 forbiddenAuraSpellId)
        {
            if (requiredAuraSpellId && !bot->HasAura(requiredAuraSpellId))
                return false;
            if (forbiddenAuraSpellId && bot->HasAura(forbiddenAuraSpellId))
                return false;
            return true;
        }

        // Native affordability, read straight from `bot` -- never from a CombatResourceSnapshot
        // (item 8). A snapshot only ever holds the bot's own primary power bar plus explicitly-
        // registered extra native channels (currently just Reaper's Runic Power); a spell can use
        // any PowerType via its own SpellInfo::PowerType, so checking against the snapshot instead
        // of the bot directly could produce a false "can't afford" for a perfectly affordable
        // spell whose PowerType simply isn't one of the snapshot's channels.
        bool CanAffordNative(Player* bot, SpellInfo const* spellInfo)
        {
            int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
            if (cost <= 0)
                return true;
            if (spellInfo->PowerType == POWER_HEALTH)
                return bot->GetHealth() > uint32(cost);
            return bot->GetPower(Powers(spellInfo->PowerType)) >= cost;
        }
    }

    bool CombatResourceSnapshot::Add(CombatResourceState const& state)
    {
        if (count >= MAX_COMBAT_RESOURCES)
        {
            LOG_ERROR("module.coa-playerbots",
                "CombatResource: snapshot overflow (> {} channels) -- resource '{}' dropped, "
                "raise MAX_COMBAT_RESOURCES.", MAX_COMBAT_RESOURCES, state.name);
            return false;
        }
        resources[count++] = state;
        return true;
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
                // Reaper's Runic Power. Skip if it happens to already be the primary bar, to
                // avoid double-reporting the same channel.
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

        // Necromancer's minion-capacity economy -- a different resource shape entirely (a live
        // active-summon counter, not an aura stack or power bar), so it isn't part of
        // GetResourceChannels above; queried directly from its own small core API instead.
        if (classId == CLASS_NECROMANCER)
        {
            AscensionCompatData::MinionCapacityState capacity = AscensionCompatData::QueryMinionCapacityState(bot);
            CombatResourceState state;
            state.key = CombatResourceKey{CombatResourceKind::MinionCapacity, 0, 0};
            state.current = int32(capacity.Current);
            state.maximum = int32(capacity.Maximum);
            state.maximumKnown = true;
            state.name = "Minion capacity";
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
                req.activeForBot = true;
                out.push_back(req);
            }
        }

        uint8 classId = bot->getClass();
        CachedRequirementSet const& customReqs = CachedCustomRequirements(classId, resolvedSpellId);
        for (uint8 i = 0; i < customReqs.count; ++i)
        {
            CachedRequirement const& cached = customReqs.items[i];
            Requirement req;
            req.key = cached.key;
            req.amount = cached.amount;
            req.consumption = cached.consumption;
            req.requiredAuraSpellId = cached.requiredAuraSpellId;
            req.forbiddenAuraSpellId = cached.forbiddenAuraSpellId;
            req.activeForBot = IsRequirementActive(bot, cached.requiredAuraSpellId, cached.forbiddenAuraSpellId);
            out.push_back(req);
        }

        if (uint32 capacityCost = AscensionCompatData::QueryMinionCapacityCost(bot, resolvedSpellId))
        {
            Requirement req;
            req.key = CombatResourceKey{CombatResourceKind::MinionCapacity, 0, 0};
            req.amount = int32(capacityCost);
            req.consumption = AscensionCompatData::ResourceConsumption::None;
            req.activeForBot = true;
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
        if (!bot || !resolvedSpellId)
            return false;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(resolvedSpellId);
        if (!spellInfo)
            return false;

        // Native -- the sole owner of this check now (item 9); ActionEvaluator::CanCast no longer
        // has its own separate native-power check.
        if (!CanAffordNative(bot, spellInfo))
            return false;

        // Custom (AuraStack) requirements -- allocation-free: iterates the cached fixed-capacity
        // set directly rather than building a vector (item 10).
        uint8 classId = bot->getClass();
        CachedRequirementSet const& customReqs = CachedCustomRequirements(classId, resolvedSpellId);
        for (uint8 i = 0; i < customReqs.count; ++i)
        {
            CachedRequirement const& req = customReqs.items[i];
            if (!IsRequirementActive(bot, req.requiredAuraSpellId, req.forbiddenAuraSpellId))
                continue; // condition not met -- this requirement simply doesn't apply right now

            CombatResourceState const* state = snapshot.Find(req.key);
            int32 have = state ? state->current : 0;
            if (have < req.amount)
            {
                LOG_DEBUG("module.coa-playerbots",
                    "CombatResource: bot '{}' can't afford spell {} -- needs {} of custom resource "
                    "{}, has {}.", bot->GetName(), resolvedSpellId, req.amount, req.key.auraSpellId, have);
                return false;
            }
        }

        // Pure aura-presence/absence gates (e.g. Sun Cleric's DawnCast blocked while Dawn is up) --
        // not a numeric resource, so not part of the loop above.
        for (AscensionCompatData::AuraGate const& gate : CachedAuraGates(classId, resolvedSpellId))
        {
            bool hasAura = bot->HasAura(gate.AuraSpellId);
            if (gate.RequireAbsent ? hasAura : !hasAura)
            {
                LOG_DEBUG("module.coa-playerbots",
                    "CombatResource: bot '{}' can't cast spell {} -- aura gate on {} ({}).",
                    bot->GetName(), resolvedSpellId, gate.AuraSpellId,
                    gate.RequireAbsent ? "must be absent" : "must be present");
                return false;
            }
        }

        // Necromancer minion capacity -- not classId+spellId cacheable (Cost() depends on live
        // talent auras for at least one spell), so queried fresh; cheap (a small switch), not a
        // table scan, so no caching penalty in practice.
        if (classId == CLASS_NECROMANCER)
        {
            uint32 capacityCost = AscensionCompatData::QueryMinionCapacityCost(bot, resolvedSpellId);
            if (capacityCost > 0)
            {
                AscensionCompatData::MinionCapacityState capacity =
                    AscensionCompatData::QueryMinionCapacityState(bot);
                if (capacity.Maximum < capacity.Current + capacityCost)
                {
                    LOG_DEBUG("module.coa-playerbots",
                        "CombatResource: bot '{}' can't afford spell {} -- needs {} minion "
                        "capacity, has {}/{} used.", bot->GetName(), resolvedSpellId, capacityCost,
                        capacity.Current, capacity.Maximum);
                    return false;
                }
            }
        }

        return true;
    }
}
