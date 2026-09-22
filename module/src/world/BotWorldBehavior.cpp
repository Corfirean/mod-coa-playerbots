#include "BotWorldBehavior.h"
#include "BotAI.h"
#include "BotMovement.h"
#include "BotWorldPoi.h"
#include "Config.h"
#include "DBCStores.h"
#include "GameTime.h"
#include "Log.h"
#include "Player.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    enum class WorldIntent : uint8
    {
        None,
        Repair,
        Vendor,
        Errand,
        GatherArea,
        GrindArea,
        Explore,
        Count,
    };

    enum class Phase : uint8
    {
        Travel,
        Linger,
    };

    struct Config
    {
        bool enabled = true;
        bool verbose = true;
        uint32 thinkIntervalMs = 2000;
        uint32 idleBeforeErrandMs = 10000;
        float serviceRadius = 200.0f;
        float areaRadius = 350.0f;
    } _config;

    // How long an errand may take end to end before it is abandoned as expired. Travel is the
    // part that can blow up (a long detour, a blocked path), so these are generous travel budgets
    // rather than activity durations; lingering has its own, much shorter, timer.
    constexpr uint32 LIFETIME_NEED_MS = 180000;
    constexpr uint32 LIFETIME_ERRAND_MS = 120000;
    constexpr uint32 LIFETIME_AREA_MS = 150000;
    constexpr uint32 LIFETIME_EXPLORE_MS = 90000;

    // A travelling bot that hasn't closed 2 yards on its destination for this long is stuck.
    constexpr uint32 STUCK_MS = 15000;
    constexpr float PROGRESS_STEP = 2.0f;

    // MovePoint path generation over very long distances can truncate, so long trips are issued as
    // a chain of legs. Each leg is re-issued on a think tick once the previous one has stopped.
    constexpr float TRAVEL_LEG = 120.0f;

    // Beyond this the bot mounts, same threshold idea the gather and quest walks already use.
    constexpr float MOUNT_DISTANCE = 80.0f;

    // Area destinations must be past what the bot's own local scans could already see, or the
    // trip would be pointless: grinding scans ~30 yards, gathering 30.
    constexpr float MIN_AREA_TRAVEL = 45.0f;

    // After a failed errand the same kind is not retried for a while, so one unreachable vendor or
    // an empty zone can't turn into a retry loop -- the lesson from the gathering loops.
    constexpr uint32 FAIL_COOLDOWN_MS = 90000;
    constexpr uint32 NO_CANDIDATE_COOLDOWN_MS = 60000;

    struct WorldState
    {
        WorldIntent intent = WorldIntent::None;
        Phase phase = Phase::Travel;
        uint32 mapId = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float arriveRadius = 4.0f;
        PoiKind poiKind = PoiKind::Count;
        uint32 poiSpawnId = 0;
        uint32 lastPoiSpawnId = 0;
        float poiX = 0.0f;
        float poiY = 0.0f;
        uint32 deadlineMs = 0;
        uint32 lingerUntilMs = 0;
        uint32 lingerMs = 0;
        uint32 nextThinkMs = 0;
        uint32 idleSinceMs = 0;
        uint32 lastSeenMs = 0;
        float bestDistance = 0.0f;
        uint32 lastProgressMs = 0;
        uint32 generation = 0;
        bool sat = false;
        std::array<uint32, size_t(WorldIntent::Count)> cooldownUntilMs{};
    };

    std::unordered_map<ObjectGuid, WorldState> _states;

    uint32 NowMs()
    {
        return uint32(GameTime::GetGameTimeMS().count());
    }

    uint32 Mix(uint32 x)
    {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    // Deterministic per bot and per decision: the personality seed keeps a bot's choices stable
    // and different from its neighbours', the generation makes successive decisions differ, so
    // thousands of bots in one city neither move in lockstep nor all pick the same vendor.
    uint32 Roll(WorldState& state, AmbientProfile const& profile, uint32 salt)
    {
        return Mix(profile.seed ^ Mix(++state.generation * 2654435761U) ^ salt);
    }

    uint32 RollRange(WorldState& state, AmbientProfile const& profile, uint32 salt, uint32 lo, uint32 hi)
    {
        return lo + Roll(state, profile, salt) % (hi - lo + 1);
    }

    char const* IntentName(WorldIntent intent)
    {
        switch (intent)
        {
            case WorldIntent::Repair:     return "Repair";
            case WorldIntent::Vendor:     return "Vendor";
            case WorldIntent::Errand:     return "Errand";
            case WorldIntent::GatherArea: return "GatherArea";
            case WorldIntent::GrindArea:  return "GrindArea";
            case WorldIntent::Explore:    return "Explore";
            default:                      return "None";
        }
    }

    template <typename... Args>
    void Trace(Player const* bot, std::string_view fmt, Args&&... args)
    {
        if (!_config.verbose)
            return;
        LOG_INFO("module.coa-playerbots.world", "Bot '{}' {}", bot->GetName(),
            Acore::StringFormat(fmt, std::forward<Args>(args)...));
    }

    float Dist2d(float ax, float ay, float bx, float by)
    {
        return std::hypot(ax - bx, ay - by);
    }

    FactionTemplateEntry const* FactionOf(uint32 faction)
    {
        return faction ? sFactionTemplateStore.LookupEntry(faction) : nullptr;
    }

    // Services a real player of this faction could actually use: an Alliance bot has no business
    // walking up to a Horde banker in a neutral town.
    bool IsUsableService(Player const* bot, Poi const& poi)
    {
        if (!poi.faction)
            return true;
        FactionTemplateEntry const* mine = bot->GetFactionTemplateEntry();
        FactionTemplateEntry const* theirs = FactionOf(poi.faction);
        return mine && theirs && !mine->IsHostileTo(*theirs) && !theirs->IsHostileTo(*mine);
    }

    // Anything the bot could attack and that is in a sane solo level band. Neutral mobs count,
    // exactly as they do for a real player and for GrindHostileUnitCheck.
    bool IsGrindableFor(Player const* bot, Poi const& poi)
    {
        FactionTemplateEntry const* mine = bot->GetFactionTemplateEntry();
        FactionTemplateEntry const* theirs = FactionOf(poi.faction);
        if (!mine || !theirs || theirs->IsFriendlyTo(*mine))
            return false;
        uint8 level = bot->GetLevel();
        return poi.maxLevel + 4 >= level && poi.minLevel <= level + 2;
    }

    // Weighted pick that favours nearer candidates without always taking the nearest one; always
    // taking the nearest is exactly how dozens of bots ended up casting on the same herb.
    Poi const* PickWeighted(std::vector<Poi const*> const& candidates, Player const* bot, WorldState& state,
        AmbientProfile const& profile, float falloff)
    {
        if (candidates.empty())
            return nullptr;

        std::vector<float> weights;
        weights.reserve(candidates.size());
        float total = 0.0f;
        for (Poi const* poi : candidates)
        {
            float d = Dist2d(poi->x, poi->y, bot->GetPositionX(), bot->GetPositionY());
            float w = 1.0f / (1.0f + d / falloff);
            if (poi->spawnId == state.lastPoiSpawnId)
                w *= 0.05f;
            weights.push_back(w);
            total += w;
        }

        float roll = (Roll(state, profile, 0x5eed) % 100000) / 100000.0f * total;
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            roll -= weights[i];
            if (roll <= 0.0f)
                return candidates[i];
        }
        return candidates.back();
    }

    bool OnCooldown(WorldState const& state, WorldIntent intent, uint32 now)
    {
        return state.cooldownUntilMs[size_t(intent)] > now;
    }

    void SetCooldown(WorldState& state, WorldIntent intent, uint32 now, uint32 ms)
    {
        state.cooldownUntilMs[size_t(intent)] = now + ms;
    }

    void StandUp(Player* bot, WorldState& state)
    {
        if (state.sat)
        {
            bot->SetStandState(UNIT_STAND_STATE_STAND);
            state.sat = false;
        }
    }

    void MoveLeg(Player* bot, WorldState& state)
    {
        float bx = bot->GetPositionX();
        float by = bot->GetPositionY();
        float dist = Dist2d(bx, by, state.x, state.y);

        float tx = state.x;
        float ty = state.y;
        float tz = state.z;
        if (dist > TRAVEL_LEG)
        {
            float t = TRAVEL_LEG / dist;
            tx = bx + (state.x - bx) * t;
            ty = by + (state.y - by) * t;
            tz = bot->GetPositionZ();
        }

        if (dist > MOUNT_DISTANCE && !bot->IsMounted())
            BotAI::TryMountForTravel(bot);

        BotMovement::MoveTo(bot, MoveOwner::Ambient, tx, ty, tz);
    }

    void Begin(Player* bot, WorldState& state, WorldIntent intent, float x, float y, float z, float arriveRadius,
        uint32 lifetimeMs, uint32 now)
    {
        state.intent = intent;
        state.phase = Phase::Travel;
        state.mapId = bot->GetMapId();
        state.x = x;
        state.y = y;
        state.z = z;
        state.arriveRadius = arriveRadius;
        state.deadlineMs = now + lifetimeMs;
        state.bestDistance = Dist2d(bot->GetPositionX(), bot->GetPositionY(), x, y);
        state.lastProgressMs = now;
        StandUp(bot, state);
        MoveLeg(bot, state);
    }

    // Stops a little short of the NPC on the side the bot approaches from, so it ends up
    // standing in front of the counter rather than inside the NPC's model.
    void BeginAtPoi(Player* bot, WorldState& state, WorldIntent intent, Poi const& poi, uint32 lingerMs,
        uint32 lifetimeMs, uint32 now)
    {
        float dx = bot->GetPositionX() - poi.x;
        float dy = bot->GetPositionY() - poi.y;
        float len = std::max(0.001f, std::hypot(dx, dy));
        float standOff = std::min(2.5f, len);

        state.poiKind = poi.kind;
        state.poiSpawnId = poi.spawnId;
        state.poiX = poi.x;
        state.poiY = poi.y;
        state.lingerMs = lingerMs;
        Begin(bot, state, intent, poi.x + dx / len * standOff, poi.y + dy / len * standOff, poi.z, 3.0f,
            lifetimeMs, now);
    }

    void Finish(Player* bot, WorldState& state, char const* outcome, uint32 cooldownMs, uint32 now)
    {
        Trace(bot, "{} WorldIntent::{}.", outcome, IntentName(state.intent));
        if (cooldownMs)
            SetCooldown(state, state.intent, now, cooldownMs);

        BotMovement::Release(bot, MoveOwner::Ambient);
        StandUp(bot, state);
        if (state.poiSpawnId)
            state.lastPoiSpawnId = state.poiSpawnId;
        state.intent = WorldIntent::None;
        state.poiKind = PoiKind::Count;
        state.poiSpawnId = 0;
        state.idleSinceMs = now;
    }

    uint32 LingerFor(PoiKind kind, WorldState& state, AmbientProfile const& profile)
    {
        uint32 lo = 4000;
        uint32 hi = 12000;
        switch (kind)
        {
            case PoiKind::Innkeeper:
                lo = 20000;
                hi = 60000;
                break;
            case PoiKind::Banker:
            case PoiKind::Auctioneer:
                lo = 8000;
                hi = 25000;
                break;
            case PoiKind::Mailbox:
                lo = 4000;
                hi = 12000;
                break;
            case PoiKind::ProfessionTrainer:
                lo = 6000;
                hi = 15000;
                break;
            case PoiKind::FlightMaster:
                lo = 3000;
                hi = 8000;
                break;
            default:
                break;
        }
        uint32 base = RollRange(state, profile, 0x11a6, lo, hi);
        return base * (50 + profile.patience) / 100;
    }

    // The small human touches on arriving somewhere: face what you walked up to, and now and then
    // sit down or gesture. Cosmetic only -- nothing here ever runs in combat.
    void ArriveAtPoi(Player* bot, WorldState& state, AmbientProfile const& profile)
    {
        if (bot->IsMounted())
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);

        bot->SetFacingTo(std::atan2(state.poiY - bot->GetPositionY(), state.poiX - bot->GetPositionX()));

        uint32 roll = Roll(state, profile, 0xa771) % 100;
        uint32 sitChance = state.poiKind == PoiKind::Innkeeper ? 55 : 8;
        uint32 emoteChance = 15 + profile.sociability / 4;
        if (roll < sitChance)
        {
            bot->SetStandState(UNIT_STAND_STATE_SIT);
            state.sat = true;
        }
        else if (roll < sitChance + emoteChance)
        {
            static constexpr uint32 emotes[] =
            {
                EMOTE_ONESHOT_TALK, EMOTE_ONESHOT_BOW, EMOTE_ONESHOT_WAVE, EMOTE_ONESHOT_POINT,
                EMOTE_ONESHOT_QUESTION, EMOTE_ONESHOT_YES, EMOTE_ONESHOT_LAUGH,
            };
            bot->HandleEmoteCommand(emotes[Roll(state, profile, 0xe307) % std::size(emotes)]);
        }
    }

    bool TryStartNeed(Player* bot, WorldState& state, uint32 now)
    {
        static constexpr std::pair<WorldIntent, PoiKind> needs[] =
        {
            { WorldIntent::Repair, PoiKind::Repair },
            { WorldIntent::Vendor, PoiKind::Vendor },
        };

        for (auto const& [intent, kind] : needs)
        {
            if (OnCooldown(state, intent, now))
                continue;

            bool needed = intent == WorldIntent::Repair ? BotAI::NeedsRepair(bot) : BotAI::NeedsVendor(bot);
            if (!needed)
                continue;

            std::vector<Poi const*> candidates;
            BotWorldPoi::Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), _config.serviceRadius, kind,
                candidates);

            Poi const* nearest = nullptr;
            float nearestDist = 0.0f;
            for (Poi const* poi : candidates)
            {
                if (!IsUsableService(bot, *poi))
                    continue;
                float d = Dist2d(poi->x, poi->y, bot->GetPositionX(), bot->GetPositionY());
                if (!nearest || d < nearestDist)
                {
                    nearest = poi;
                    nearestDist = d;
                }
            }

            if (!nearest)
            {
                SetCooldown(state, intent, now, NO_CANDIDATE_COOLDOWN_MS);
                continue;
            }

            BeginAtPoi(bot, state, intent, *nearest, 3000, LIFETIME_NEED_MS, now);
            Trace(bot, "chose WorldIntent::{} (need), travelling to {} {} ({:.0f} yd).", IntentName(intent),
                BotWorldPoi::KindName(kind), nearest->entry, nearestDist);
            return true;
        }
        return false;
    }

    bool StartErrand(Player* bot, WorldState& state, AmbientProfile const& profile, bool inCity, uint32 now)
    {
        static constexpr std::pair<PoiKind, uint32> kinds[] =
        {
            { PoiKind::Vendor, 30 },
            { PoiKind::Mailbox, 25 },
            { PoiKind::Banker, 18 },
            { PoiKind::Auctioneer, 18 },
            { PoiKind::Innkeeper, 14 },
            { PoiKind::ProfessionTrainer, 10 },
            { PoiKind::Repair, 8 },
            { PoiKind::FlightMaster, 6 },
        };

        uint32 total = 0;
        for (auto const& [kind, weight] : kinds)
            total += weight;

        // Try the rolled kind first, then the rest in order, so a town without an auction house
        // still gets a vendor visit instead of nothing.
        uint32 roll = Roll(state, profile, 0xe44a) % total;
        size_t first = 0;
        for (size_t i = 0; i < std::size(kinds); ++i)
        {
            if (roll < kinds[i].second)
            {
                first = i;
                break;
            }
            roll -= kinds[i].second;
        }

        float radius = inCity ? _config.serviceRadius : _config.serviceRadius * 0.6f;
        for (size_t n = 0; n < std::size(kinds); ++n)
        {
            PoiKind kind = kinds[(first + n) % std::size(kinds)].first;

            std::vector<Poi const*> candidates;
            BotWorldPoi::Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), radius, kind, candidates);
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                [bot](Poi const* poi) { return !IsUsableService(bot, *poi); }), candidates.end());

            Poi const* poi = PickWeighted(candidates, bot, state, profile, 60.0f);
            if (!poi)
                continue;

            BeginAtPoi(bot, state, WorldIntent::Errand, *poi, LingerFor(kind, state, profile), LIFETIME_ERRAND_MS, now);
            Trace(bot, "chose WorldIntent::Errand, travelling to {} {} ({:.0f} yd).", BotWorldPoi::KindName(kind),
                poi->entry, Dist2d(poi->x, poi->y, bot->GetPositionX(), bot->GetPositionY()));
            return true;
        }
        return false;
    }

    bool StartGatherArea(Player* bot, WorldState& state, AmbientProfile const& profile, uint32 now)
    {
        uint16 herb = bot->GetSkillValue(SKILL_HERBALISM);
        uint16 ore = bot->GetSkillValue(SKILL_MINING);
        if (!herb && !ore)
            return false;

        std::vector<Poi const*> raw;
        if (herb)
            BotWorldPoi::Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), _config.areaRadius,
                PoiKind::Herb, raw);
        if (ore)
            BotWorldPoi::Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), _config.areaRadius,
                PoiKind::Ore, raw);

        std::vector<Poi const*> candidates;
        for (Poi const* poi : raw)
        {
            uint16 skill = poi->kind == PoiKind::Herb ? herb : ore;
            if (poi->requiredSkill > skill)
                continue;
            if (Dist2d(poi->x, poi->y, bot->GetPositionX(), bot->GetPositionY()) < MIN_AREA_TRAVEL)
                continue;
            candidates.push_back(poi);
        }

        Poi const* poi = PickWeighted(candidates, bot, state, profile, 120.0f);
        if (!poi)
            return false;

        state.poiKind = poi->kind;
        state.poiSpawnId = poi->spawnId;
        Begin(bot, state, WorldIntent::GatherArea, poi->x, poi->y, poi->z, 15.0f, LIFETIME_AREA_MS, now);
        Trace(bot, "chose WorldIntent::GatherArea, travelling to {} node {} ({:.0f} yd).",
            BotWorldPoi::KindName(poi->kind), poi->entry,
            Dist2d(poi->x, poi->y, bot->GetPositionX(), bot->GetPositionY()));
        return true;
    }

    bool StartGrindArea(Player* bot, WorldState& state, AmbientProfile const& profile, uint32 now)
    {
        std::vector<Poi const*> raw;
        BotWorldPoi::Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), _config.areaRadius,
            PoiKind::Hostile, raw);

        std::vector<Poi const*> candidates;
        for (Poi const* poi : raw)
        {
            if (!IsGrindableFor(bot, *poi))
                continue;
            if (Dist2d(poi->x, poi->y, bot->GetPositionX(), bot->GetPositionY()) < MIN_AREA_TRAVEL)
                continue;
            candidates.push_back(poi);
        }

        Poi const* poi = PickWeighted(candidates, bot, state, profile, 150.0f);
        if (!poi)
            return false;

        // Arrive at the edge of the camp rather than on top of the mob, the way a player walks up
        // to a pull; the local grind scan takes over from there.
        float dx = bot->GetPositionX() - poi->x;
        float dy = bot->GetPositionY() - poi->y;
        float len = std::max(0.001f, std::hypot(dx, dy));
        state.poiKind = poi->kind;
        state.poiSpawnId = poi->spawnId;
        Begin(bot, state, WorldIntent::GrindArea, poi->x + dx / len * 15.0f, poi->y + dy / len * 15.0f, poi->z, 10.0f,
            LIFETIME_AREA_MS, now);
        Trace(bot, "chose WorldIntent::GrindArea, travelling toward creature {} (level {}-{}, {:.0f} yd).", poi->entry,
            poi->minLevel, poi->maxLevel, len);
        return true;
    }

    // Wandering to a random known spawn point rather than to a random coordinate: spawn points
    // sit on walkable, pathable ground, a random coordinate is as likely to be inside a hill.
    bool StartExplore(Player* bot, WorldState& state, AmbientProfile const& profile, bool inCity, uint32 now)
    {
        float minD = inCity ? 30.0f : 60.0f;
        float maxD = inCity ? 120.0f : 220.0f;

        std::vector<Poi const*> raw;
        static constexpr PoiKind kinds[] = { PoiKind::Vendor, PoiKind::Mailbox, PoiKind::Innkeeper, PoiKind::Herb,
            PoiKind::Ore, PoiKind::Hostile };
        for (PoiKind kind : kinds)
            BotWorldPoi::Query(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), maxD, kind, raw);

        std::vector<Poi const*> candidates;
        for (Poi const* poi : raw)
        {
            float d = Dist2d(poi->x, poi->y, bot->GetPositionX(), bot->GetPositionY());
            if (d < minD)
                continue;
            if (poi->kind == PoiKind::Hostile && poi->minLevel > bot->GetLevel() + 2)
                continue;
            if (poi->faction && poi->kind != PoiKind::Hostile && !IsUsableService(bot, *poi))
                continue;
            candidates.push_back(poi);
        }

        if (candidates.empty())
            return false;

        Poi const* poi = candidates[Roll(state, profile, 0xe8a1) % candidates.size()];
        state.poiKind = poi->kind;
        state.poiSpawnId = poi->spawnId;
        state.poiX = poi->x;
        state.poiY = poi->y;
        state.lingerMs = RollRange(state, profile, 0x1dd1, 2000, 9000);
        Begin(bot, state, WorldIntent::Explore, poi->x, poi->y, poi->z, 8.0f, LIFETIME_EXPLORE_MS, now);
        Trace(bot, "chose WorldIntent::Explore, wandering {:.0f} yd.",
            Dist2d(poi->x, poi->y, bot->GetPositionX(), bot->GetPositionY()));
        return true;
    }

    int32 Jitter(WorldState& state, AmbientProfile const& profile)
    {
        return int32(Roll(state, profile, 0x717e) % 61);
    }

    void TryStartAmbient(Player* bot, WorldState& state, AmbientProfile const& profile, bool inCity, uint32 now)
    {
        std::vector<std::pair<WorldIntent, int32>> options;
        auto offer = [&](WorldIntent intent, int32 score)
        {
            if (!OnCooldown(state, intent, now))
                options.emplace_back(intent, score + Jitter(state, profile));
        };

        if (inCity)
        {
            offer(WorldIntent::Errand, 70 + profile.sociability / 2);
            offer(WorldIntent::Explore, 30 + (100 - profile.patience) / 4);
        }
        else
        {
            offer(WorldIntent::GatherArea, profile.gathering + (profile.lean == AmbientLean::Gather ? 80 : 0));
            offer(WorldIntent::GrindArea, profile.grinding + (profile.lean == AmbientLean::Grind ? 80 : 0)
                + (profile.lean == AmbientLean::Quest ? 30 : 0));
            offer(WorldIntent::Explore, (profile.questing + profile.gathering) / 4 + 20
                + (profile.lean == AmbientLean::Explore ? 80 : 0));
            offer(WorldIntent::Errand, 15 + profile.sociability / 5);
        }

        std::sort(options.begin(), options.end(), [](auto const& a, auto const& b) { return a.second > b.second; });

        for (auto const& [intent, score] : options)
        {
            bool started = false;
            switch (intent)
            {
                case WorldIntent::Errand:     started = StartErrand(bot, state, profile, inCity, now); break;
                case WorldIntent::GatherArea: started = StartGatherArea(bot, state, profile, now); break;
                case WorldIntent::GrindArea:  started = StartGrindArea(bot, state, profile, now); break;
                case WorldIntent::Explore:    started = StartExplore(bot, state, profile, inCity, now); break;
                default: break;
            }

            if (started)
                return;

            SetCooldown(state, intent, now, NO_CANDIDATE_COOLDOWN_MS);
        }
    }

    void Arrive(Player* bot, WorldState& state, AmbientProfile const& profile, uint32 now)
    {
        BotMovement::Release(bot, MoveOwner::Ambient);
        bot->StopMoving();

        if (state.intent == WorldIntent::Repair || state.intent == WorldIntent::Vendor)
            BotAI::MaintainEquipmentNow(bot);

        if (state.intent == WorldIntent::Errand || state.intent == WorldIntent::Repair
            || state.intent == WorldIntent::Vendor)
            ArriveAtPoi(bot, state, profile);

        state.phase = Phase::Linger;
        state.lingerUntilMs = now + state.lingerMs;
        Trace(bot, "arrived for WorldIntent::{}, lingering {}s.", IntentName(state.intent),
            state.lingerMs / IN_MILLISECONDS);
    }
}

namespace BotWorldBehavior
{
    void LoadConfig()
    {
        _config.enabled = sConfigMgr->GetOption<bool>("CoaBots.World.Enable", true);
        _config.verbose = sConfigMgr->GetOption<bool>("CoaBots.World.VerboseLog", true);
        _config.thinkIntervalMs =
            std::max<uint32>(250, sConfigMgr->GetOption<uint32>("CoaBots.World.ThinkIntervalMs", 2000));
        _config.idleBeforeErrandMs = sConfigMgr->GetOption<uint32>("CoaBots.World.IdleBeforeErrandMs", 10000);
        _config.serviceRadius = sConfigMgr->GetOption<float>("CoaBots.World.ServiceRadius", 200.0f);
        _config.areaRadius = sConfigMgr->GetOption<float>("CoaBots.World.AreaRadius", 350.0f);
    }

    AmbientTick UpdateBeforeSolo(Player* bot, AmbientProfile const& profile)
    {
        if (!_config.enabled)
            return AmbientTick::Idle;

        uint32 now = NowMs();
        WorldState& state = _states[bot->GetGUID()];

        if (!state.nextThinkMs)
            state.nextThinkMs = now + bot->GetGUID().GetCounter() % _config.thinkIntervalMs;

        bool resumed = state.lastSeenMs && now - state.lastSeenMs > 3000;
        state.lastSeenMs = now;

        if (state.intent == WorldIntent::None)
        {
            if (now >= state.nextThinkMs && TryStartNeed(bot, state, now))
                return AmbientTick::Busy;
            return AmbientTick::Idle;
        }

        if (bot->GetMapId() != state.mapId)
        {
            Finish(bot, state, "dropped (map changed)", 0, now);
            return AmbientTick::Idle;
        }

        if (resumed)
        {
            Trace(bot, "resuming WorldIntent::{} after an interruption (combat or other work).",
                IntentName(state.intent));
            state.lastProgressMs = now;
            if (state.phase == Phase::Travel)
                MoveLeg(bot, state);
        }

        if (now >= state.deadlineMs)
        {
            Finish(bot, state, "expired", FAIL_COOLDOWN_MS, now);
            return AmbientTick::Relocated;
        }

        if (state.phase == Phase::Linger)
        {
            if (now < state.lingerUntilMs)
                return AmbientTick::Busy;

            Finish(bot, state, "completed", 0, now);
            // A short, varying pause before the next decision, so a bot doesn't turn on its heel
            // the instant it finishes something.
            state.nextThinkMs = now + RollRange(state, profile, 0x9a05, 1000, 6000);
            return AmbientTick::Relocated;
        }

        float dist = Dist2d(bot->GetPositionX(), bot->GetPositionY(), state.x, state.y);
        if (dist <= state.arriveRadius)
        {
            if (state.intent == WorldIntent::GatherArea || state.intent == WorldIntent::GrindArea)
            {
                // The trip was the whole errand: hand the bot straight back to its own local scans,
                // which can now see what it came here for.
                Finish(bot, state, "completed", 0, now);
                return AmbientTick::Relocated;
            }

            Arrive(bot, state, profile, now);
            return AmbientTick::Busy;
        }

        if (now < state.nextThinkMs)
            return AmbientTick::Busy;
        state.nextThinkMs = now + _config.thinkIntervalMs;

        if (dist < state.bestDistance - PROGRESS_STEP)
        {
            state.bestDistance = dist;
            state.lastProgressMs = now;
        }
        else if (now - state.lastProgressMs > STUCK_MS)
        {
            Finish(bot, state, "abandoned (stuck)", FAIL_COOLDOWN_MS, now);
            return AmbientTick::Relocated;
        }

        if (!bot->isMoving() || BotMovement::CurrentOwner(bot) != MoveOwner::Ambient)
            MoveLeg(bot, state);

        return AmbientTick::Busy;
    }

    void UpdateAfterSolo(Player* bot, AmbientProfile const& profile, bool soloStartedSomething)
    {
        if (!_config.enabled)
            return;

        WorldState& state = _states[bot->GetGUID()];
        if (state.intent != WorldIntent::None)
            return;

        uint32 now = NowMs();
        if (soloStartedSomething)
        {
            state.idleSinceMs = 0;
            return;
        }

        if (!state.idleSinceMs)
            state.idleSinceMs = now;

        if (now < state.nextThinkMs)
            return;
        state.nextThinkMs = now + _config.thinkIntervalMs;

        // In a city there's no solo activity to wait for (grinding and gathering don't happen
        // there), so errands start right away; outside, the solo scans get a fair few cycles first.
        bool inCity = BotAI::IsInCity(bot);
        if (!inCity && now - state.idleSinceMs < _config.idleBeforeErrandMs)
            return;

        TryStartAmbient(bot, state, profile, inCity, now);
    }

    std::string Describe(ObjectGuid botGuid)
    {
        auto itr = _states.find(botGuid);
        if (itr == _states.end() || itr->second.intent == WorldIntent::None)
            return "world intent: none";

        WorldState const& state = itr->second;
        uint32 now = NowMs();
        return Acore::StringFormat("world intent: {} ({}), {}, {}s left", IntentName(state.intent),
            state.poiKind == PoiKind::Count ? "-" : BotWorldPoi::KindName(state.poiKind),
            state.phase == Phase::Travel ? "travelling" : "lingering",
            state.deadlineMs > now ? (state.deadlineMs - now) / IN_MILLISECONDS : 0);
    }

    void Forget(ObjectGuid botGuid)
    {
        _states.erase(botGuid);
    }
}
